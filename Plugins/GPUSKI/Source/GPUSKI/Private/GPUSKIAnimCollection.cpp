// GPUSKI Plugin - Animation Collection implementation

#include "GPUSKIAnimCollection.h"
#include "GPUSKIRenderResources.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimComposite.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "BoneContainer.h"
#include "RenderingThread.h"

//----------------------------------------------------------------------
// FGPUSKISequenceDef
//----------------------------------------------------------------------
int FGPUSKISequenceDef::CalcFrameIndex(float SequencePlayTime) const
{
	int LocalFrame = (int)(SequencePlayTime * SampleFrequency);
	return AnimationFrameIndex + FMath::Clamp(LocalFrame, 0, AnimationFrameCount - 1);
}

float FGPUSKISequenceDef::GetSequenceLength() const
{
	return Sequence ? Sequence->GetPlayLength() : 0.0f;
}

int FGPUSKISequenceDef::CalcFrameCount() const
{
	if (!Sequence)
		return 0;
	return FMath::Max(1, FMath::CeilToInt(Sequence->GetPlayLength() * SampleFrequency) + 1);
}

//----------------------------------------------------------------------
// UGPUSKIAnimCollection
//----------------------------------------------------------------------
UGPUSKIAnimCollection::UGPUSKIAnimCollection()
{
}

UGPUSKIAnimCollection::~UGPUSKIAnimCollection()
{
	// Safety: should already be released by BeginDestroy/ReleaseMeshDataResources
	delete AnimationBuffer;
	AnimationBuffer = nullptr;
}

void UGPUSKIAnimCollection::PostInitProperties()
{
	Super::PostInitProperties();
}

void UGPUSKIAnimCollection::PostLoad()
{
	Super::PostLoad();

	if (!bIsBuilt && Skeleton && Sequences.Num() > 0)
	{
		BuildData();
	}
}

void UGPUSKIAnimCollection::BeginDestroy()
{
	ReleaseMeshDataResources();
	Super::BeginDestroy();
}

bool UGPUSKIAnimCollection::IsReadyForFinishDestroy()
{
	return Super::IsReadyForFinishDestroy() && ReleaseResourcesFence.IsFenceComplete();
}

#if WITH_EDITOR
void UGPUSKIAnimCollection::Rebuild()
{
	ReleaseMeshDataResources();
	bIsBuilt = false;
	BuildData();
}
#endif

//----------------------------------------------------------------------
// Build pipeline
//----------------------------------------------------------------------
bool UGPUSKIAnimCollection::BuildData()
{
	if (bIsBuilt)
		return true;

	if (!Skeleton)
	{
		UE_LOG(LogGPUSKI, Error, TEXT("AnimCollection %s: No Skeleton assigned!"), *GetName());
		return false;
	}

	if (Sequences.Num() == 0)
	{
		UE_LOG(LogGPUSKI, Warning, TEXT("AnimCollection %s: No sequences to build."), *GetName());
		return false;
	}

	InitSkeletonData();

	if (!BuildAnimationData())
		return false;

	BuildMeshData();

	bIsBuilt = true;

	UE_LOG(LogGPUSKI, Log, TEXT("AnimCollection %s built: %d bones, %d frames, %d bytes"),
		*GetName(), RenderBoneCount, TotalFrameCount, TotalAnimationBufferSize);

	return true;
}

void UGPUSKIAnimCollection::InitSkeletonData()
{
	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	const int32 NumBones = RefSkel.GetNum();

	// For MVP: use all bones as render bones
	// (In production, you'd filter to only bones that affect rendering)
	RenderRequiredBones.Reset(NumBones);
	SkeletonBoneToRenderBone.SetNumZeroed(NumBones);

	for (int32 i = 0; i < NumBones; i++)
	{
		RenderRequiredBones.Add(i);
		SkeletonBoneToRenderBone[i] = i;
	}

	RenderBoneCount = RenderRequiredBones.Num();

	// Compute reference pose in component space
	RefPoseComponentSpace.SetNum(NumBones);
	RefPoseInverse.SetNum(NumBones);

	const TArray<FTransform>& RefPoseLocal = RefSkel.GetRefBonePose();

	for (int32 BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
	{
		int32 ParentIndex = RefSkel.GetParentIndex(BoneIndex);
		if (ParentIndex == INDEX_NONE)
		{
			RefPoseComponentSpace[BoneIndex] = RefPoseLocal[BoneIndex];
		}
		else
		{
			RefPoseComponentSpace[BoneIndex] = RefPoseLocal[BoneIndex] * RefPoseComponentSpace[ParentIndex];
		}

		RefPoseInverse[BoneIndex] = FMatrix44f(RefPoseComponentSpace[BoneIndex].ToMatrixWithScale().Inverse());
	}

	// Init bone container for animation evaluation
	UE::Anim::FCurveFilterSettings CurveFilter(UE::Anim::ECurveFilterMode::DisallowFiltered);
	AnimationBoneContainer.InitializeTo(RenderRequiredBones, CurveFilter, *Skeleton);
}

bool UGPUSKIAnimCollection::BuildAnimationData()
{
	// Count total frames needed (frame 0 is reserved for identity/reference pose)
	int FrameCounter = 1;
	for (FGPUSKISequenceDef& SeqDef : Sequences)
	{
		if (!SeqDef.Sequence)
			continue;

		SeqDef.AnimationFrameCount = SeqDef.CalcFrameCount();
		SeqDef.AnimationFrameIndex = FrameCounter;
		FrameCounter += SeqDef.AnimationFrameCount;
	}

	TotalFrameCount = FrameCounter;

	if (TotalFrameCount <= 1)
	{
		UE_LOG(LogGPUSKI, Warning, TEXT("AnimCollection %s: No valid sequences."), *GetName());
		return false;
	}

	// Allocate animation buffer (frame 0 filled with identity matrices via bFillIdentity=true)
	const uint32 TotalMatrices = TotalFrameCount * RenderBoneCount;
	AnimationBuffer = new FGPUSKIAnimationBuffer();
	AnimationBuffer->InitBuffer(TotalMatrices, bHighPrecision, true);

	// Set up animation evaluation structures (Skelot pattern)
	FMemMark MemMarker(FMemStack::Get());

	FCompactPose CompactPose;
	CompactPose.SetBoneContainer(&AnimationBoneContainer);
	FBlendedCurve InCurve;
	InCurve.InitFrom(AnimationBoneContainer);
	UE::Anim::FStackAttributeContainer InAttributes;
	FAnimationPoseData PoseData(CompactPose, InCurve, InAttributes);

	TArray<FTransform> ComponentSpace;
	ComponentSpace.SetNum(AnimationBoneContainer.GetCompactPoseNumBones());

	// Sample each sequence
	for (FGPUSKISequenceDef& SeqDef : Sequences)
	{
		if (!SeqDef.Sequence)
			continue;

		const double FrameTime = 1.0 / SeqDef.SampleFrequency;

		for (int FrameIdx = 0; FrameIdx < SeqDef.AnimationFrameCount; FrameIdx++)
		{
			const double SampleTime = FrameIdx * FrameTime;
			const int32 BufferFrameIndex = SeqDef.AnimationFrameIndex + FrameIdx;

			// Evaluate animation at this time - fills CompactPose with local bone transforms
			SeqDef.Sequence->GetAnimationPose(PoseData, FAnimExtractContext(SampleTime));

			// Convert local pose to component space
			ComponentSpace[0] = CompactPose[FCompactPoseBoneIndex(0)];
			for (FCompactPoseBoneIndex CompactIdx(1); CompactIdx < CompactPose.GetNumBones(); ++CompactIdx)
			{
				const FCompactPoseBoneIndex ParentIdx = CompactPose.GetParentBoneIndex(CompactIdx);
				ComponentSpace[CompactIdx.GetInt()] = CompactPose[CompactIdx] * ComponentSpace[ParentIdx.GetInt()];
			}

			// Write render matrices
			if (bHighPrecision)
			{
				CalcRenderMatrices(ComponentSpace, AnimationBuffer->GetDataPointerHP() + BufferFrameIndex * RenderBoneCount);
			}
			else
			{
				CalcRenderMatrices(ComponentSpace, AnimationBuffer->GetDataPointerLP() + BufferFrameIndex * RenderBoneCount);
			}
		}
	}

	// Compute buffer size
	TotalAnimationBufferSize = TotalMatrices * (bHighPrecision ? sizeof(FMatrix3x4) : sizeof(FGPUSKIMatrix3x4Half));

	// Init GPU resource
	ENQUEUE_RENDER_COMMAND(InitGPUSKIAnimBuffer)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			AnimationBuffer->InitResource(RHICmdList);

			// Create uniform buffer
			FGPUSKIAnimCollectionUniformParams Params;
			Params.BoneCount = RenderBoneCount;
			Params.Padding0 = 0;
			Params.Padding1 = 0;
			Params.Padding2 = 0;
			Params.AnimationBuffer = AnimationBuffer->ShaderResourceViewRHI;
			AnimCollectionUB = FGPUSKIAnimCollectionUBRef::CreateUniformBufferImmediate(Params, EUniformBufferUsage::UniformBuffer_MultiFrame);
		}
	);

	return true;
}

void UGPUSKIAnimCollection::BuildMeshData()
{
	for (FGPUSKIMeshDef& MeshDef : Meshes)
	{
		if (!MeshDef.Mesh)
			continue;

		FGPUSKIMeshDataExPtr MeshData = MakeShared<FGPUSKIMeshDataEx, ESPMode::ThreadSafe>();
		MeshData->InitFromMesh(MeshDef.BaseLOD, MeshDef.Mesh, this);

		const FSkeletalMeshRenderData* RenderData = MeshDef.Mesh->GetResourceForRendering();
		MeshData->InitMeshData(RenderData, MeshDef.BaseLOD);

		MeshDef.MeshData = MeshData;
	}

	// Init GPU resources on render thread
	ENQUEUE_RENDER_COMMAND(InitGPUSKIMeshData)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			InitMeshDataResources(RHICmdList);
		}
	);
}

void UGPUSKIAnimCollection::InitMeshDataResources(FRHICommandListBase& RHICmdList)
{
	for (FGPUSKIMeshDef& MeshDef : Meshes)
	{
		if (MeshDef.MeshData)
		{
			MeshDef.MeshData->InitResources(RHICmdList);
		}
	}
}

void UGPUSKIAnimCollection::ReleaseMeshDataResources()
{
	for (FGPUSKIMeshDef& MeshDef : Meshes)
	{
		if (MeshDef.MeshData)
		{
			FGPUSKIMeshDataExPtr MeshData = MeshDef.MeshData;
			ENQUEUE_RENDER_COMMAND(ReleaseGPUSKIMeshData)(
				[MeshData](FRHICommandListImmediate& RHICmdList)
				{
					MeshData->ReleaseResources();
				}
			);
			MeshDef.MeshData = nullptr;
		}
	}

	if (AnimationBuffer)
	{
		FGPUSKIAnimationBuffer* Buffer = AnimationBuffer;
		AnimationBuffer = nullptr;
		ENQUEUE_RENDER_COMMAND(ReleaseGPUSKIAnimBuffer)(
			[Buffer](FRHICommandListImmediate& RHICmdList)
			{
				Buffer->ReleaseResource();
				delete Buffer;
			}
		);
	}

	AnimCollectionUB.SafeRelease();
	ReleaseResourcesFence.BeginFence();
}

//----------------------------------------------------------------------
// Lookups
//----------------------------------------------------------------------
int UGPUSKIAnimCollection::FindSequenceDef(const UAnimSequenceBase* Animation) const
{
	return Sequences.IndexOfByPredicate([&](const FGPUSKISequenceDef& Elem)
	{
		return Elem.Sequence == Animation;
	});
}

int UGPUSKIAnimCollection::FindMeshDef(const USkeletalMesh* Mesh) const
{
	return Meshes.IndexOfByPredicate([&](const FGPUSKIMeshDef& Elem)
	{
		return Elem.Mesh == Mesh;
	});
}

FGPUSKIMeshDataExPtr UGPUSKIAnimCollection::FindMeshData(const USkeletalMesh* Mesh) const
{
	int Idx = FindMeshDef(Mesh);
	if (Idx != INDEX_NONE)
		return Meshes[Idx].MeshData;
	return nullptr;
}

void UGPUSKIAnimCollection::CalcRenderMatrices(const TArrayView<FTransform> PoseComponentSpace, FMatrix3x4* OutMatrices) const
{
	for (int32 i = 0; i < RenderBoneCount; i++)
	{
		int32 SkelBoneIndex = RenderRequiredBones[i];
		FMatrix SkinMatrix = RefPoseComponentSpace[SkelBoneIndex].ToMatrixWithScale().Inverse() * PoseComponentSpace[SkelBoneIndex].ToMatrixWithScale();
		OutMatrices[i].SetMatrixTranspose(SkinMatrix);
	}
}

void UGPUSKIAnimCollection::CalcRenderMatrices(const TArrayView<FTransform> PoseComponentSpace, FGPUSKIMatrix3x4Half* OutMatrices) const
{
	for (int32 i = 0; i < RenderBoneCount; i++)
	{
		int32 SkelBoneIndex = RenderRequiredBones[i];
		FMatrix SkinMatrix = RefPoseComponentSpace[SkelBoneIndex].ToMatrixWithScale().Inverse() * PoseComponentSpace[SkelBoneIndex].ToMatrixWithScale();
		OutMatrices[i].SetMatrixTranspose(FMatrix44f(SkinMatrix));
	}
}

uint32 UGPUSKIAnimCollection::GetRenderMatrixSize() const
{
	return bHighPrecision ? sizeof(FMatrix3x4) : sizeof(FGPUSKIMatrix3x4Half);
}
