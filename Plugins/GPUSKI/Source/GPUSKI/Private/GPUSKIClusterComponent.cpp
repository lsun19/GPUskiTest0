// GPUSKI Plugin - Cluster component and scene proxy implementation

#include "GPUSKIClusterComponent.h"
#include "GPUSKIAnimCollection.h"
#include "GPUSKIRenderResources.h"

#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SceneInterface.h"
#include "InstanceDataSceneProxy.h"

// Access FScene::PrimitiveUpdates (private) — same pattern as Skelot plugin
#define private public
#include "ScenePrivate.h"
#undef private

DECLARE_CYCLE_STAT(TEXT("GPUSKI DrawStaticElements"), STAT_GPUSKI_DrawStaticElements, STATGROUP_GPUSKI);
DECLARE_CYCLE_STAT(TEXT("GPUSKI UpdateInstanceBuffer"), STAT_GPUSKI_UpdateInstanceBuffer, STATGROUP_GPUSKI);

// Subclass to access protected Flags member (same pattern as Skelot)
struct FGPUSKIInstanceSceneDataBuffers : public FInstanceSceneDataBuffers
{
	void Init()
	{
		this->Flags.bHasPerInstanceDynamicData = true;
		this->Flags.bHasPerInstanceCustomData = true;
	}
};

//----------------------------------------------------------------------
// Scene Proxy
//----------------------------------------------------------------------
class FGPUSKIClusterProxy : public FPrimitiveSceneProxy
{
public:
	typedef FPrimitiveSceneProxy Super;

	struct FProxyMeshData
	{
		FGPUSKIMeshRenderDesc MeshDesc;
		int32 MeshDefIdx;
	};

	FMaterialRelevance MaterialRelevance;
	UGPUSKIAnimCollection* AnimCollection;
	TArray<FProxyMeshData> Meshes;
	FGPUSKIInstanceSceneDataBuffers InstanceBuffer;
	TArray<UMaterialInterface*> Materials;
	FRenderBounds ApproximateLocalBounds;

	FGPUSKIClusterProxy(const UGPUSKIClusterComponent* Component, FName ResourceName)
		: FPrimitiveSceneProxy(Component, ResourceName)
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
	{
		const FGPUSKIInstanceRenderDesc& Desc = Component->GetRenderDesc();
		AnimCollection = Desc.AnimCollection;

		const bool bUseGPUScene = UseGPUScene(GetScene().GetShaderPlatform(), GetScene().GetFeatureLevel());
		check(bUseGPUScene);

		this->bAlwaysHasVelocity = true;
		this->bHasDeformableMesh = true;
		this->bHasWorldPositionOffsetVelocity = true;
		this->bVFRequiresPrimitiveUniformBuffer = true;
		this->bDoesMeshBatchesUseSceneInstanceCount = true;
		this->bSupportsGPUScene = true;

		this->SetupInstanceSceneDataBuffers(&InstanceBuffer);
		InstanceBuffer.Init();

		for (const FGPUSKIMeshRenderDesc& MeshDesc : Desc.Meshes)
		{
			FProxyMeshData& ProxyMD = Meshes.AddDefaulted_GetRef();
			ProxyMD.MeshDesc = MeshDesc;
			ProxyMD.MeshDefIdx = AnimCollection ? AnimCollection->FindMeshDef(MeshDesc.Mesh) : INDEX_NONE;
		}

		// Gather materials and check usage flags
		for (const FGPUSKIMeshRenderDesc& MeshDesc : Desc.Meshes)
		{
			if (!MeshDesc.Mesh)
				continue;

			const int32 NumMats = MeshDesc.Mesh->GetNumMaterials();
			for (int32 i = 0; i < NumMats; i++)
			{
				UMaterialInterface* Mat = MeshDesc.GetMaterial(i);
				if (!Mat)
					Mat = UMaterial::GetDefaultMaterial(MD_Surface);

				// Ensure material has required usage flags (triggers shader recompile if needed)
				if (Mat && Mat != UMaterial::GetDefaultMaterial(MD_Surface))
				{
					if (!Mat->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh))
					{
						UE_LOG(LogGPUSKI, Warning, TEXT("Material %s missing SkeletalMesh usage flag — auto-adding"), *Mat->GetName());
					}
				}
				Materials.Add(Mat);
			}
		}

		// Compute local bounds from mesh bounding boxes
		FBox LocalBox(ForceInit);
		for (const FGPUSKIMeshRenderDesc& MeshDesc : Desc.Meshes)
		{
			if (MeshDesc.Mesh)
			{
				FBoxSphereBounds MB = MeshDesc.Mesh->GetBounds();
				LocalBox += FBox(MB.Origin - MB.BoxExtent, MB.Origin + MB.BoxExtent);
			}
		}
		if (LocalBox.IsValid)
			ApproximateLocalBounds = FRenderBounds(FVector3f(LocalBox.Min), FVector3f(LocalBox.Max));
		else
			ApproximateLocalBounds = FRenderBounds(FVector3f(-100, -100, -100), FVector3f(100, 100, 100));
	}

	~FGPUSKIClusterProxy() {}

	bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View) && View->Family->EngineShowFlags.SkeletalMeshes != 0;
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bDynamicRelevance = true;
		Result.bStaticRelevance = true;
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bRenderInDepthPass = ShouldRenderInDepthPass();
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque == 1 && Result.bRenderInMainPass == 1;
		return Result;
	}

	SIZE_T GetTypeHash() const override
	{
		static SIZE_T UniquePointer;
		return reinterpret_cast<SIZE_T>(&UniquePointer);
	}

	uint32 GetMemoryFootprint() const override { return sizeof(*this); }

	void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		// Debug bounds only in non-shipping
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}
		}
#endif
	}

	void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override
	{
		SCOPE_CYCLE_COUNTER(STAT_GPUSKI_DrawStaticElements);

		check(IsInParallelRenderingThread());

		if (!AnimCollection || !AnimCollection->bIsBuilt)
			return;

		int32 MatOffset = 0;

		for (int32 SubMeshIdx = 0; SubMeshIdx < Meshes.Num(); SubMeshIdx++)
		{
			const FProxyMeshData& ProxyMD = Meshes[SubMeshIdx];
			if (ProxyMD.MeshDefIdx == INDEX_NONE)
				continue;

			const FGPUSKIMeshDef& MeshDefStruct = AnimCollection->Meshes[ProxyMD.MeshDefIdx];
			if (!MeshDefStruct.Mesh || !MeshDefStruct.MeshData)
				continue;

			const FSkeletalMeshRenderData* SKMRenderData = MeshDefStruct.Mesh->GetResourceForRendering();
			const int32 CurMatOffset = MatOffset;
			MatOffset += MeshDefStruct.Mesh->GetNumMaterials();

			for (int32 LODIndex = MeshDefStruct.BaseLOD; LODIndex < SKMRenderData->LODRenderData.Num(); LODIndex++)
			{
				const FSkeletalMeshLODRenderData& SKLODData = SKMRenderData->LODRenderData[LODIndex];
				const FSkeletalMeshLODInfo* SKLODInfo = MeshDefStruct.Mesh->GetLODInfo(LODIndex);
				const float ScreenSize = SKLODInfo ? SKLODInfo->ScreenSize.GetValue() : 1.0f;

				for (int32 SectionIndex = 0; SectionIndex < SKLODData.RenderSections.Num(); SectionIndex++)
				{
					const FSkelMeshRenderSection& Section = SKLODData.RenderSections[SectionIndex];

					FMeshBatch MeshBatch;
					FMeshBatchElement& BatchElement = MeshBatch.Elements[0];

					MeshBatch.SegmentIndex = SectionIndex;
					MeshBatch.MeshIdInPrimitive = SectionIndex;
					MeshBatch.LODIndex = LODIndex;
					MeshBatch.ReverseCulling = false;
					MeshBatch.bUseAsOccluder = this->ShouldUseAsOccluder();
					MeshBatch.bUseForDepthPass = this->ShouldRenderInDepthPass();
					MeshBatch.CastShadow = bCastDynamicShadow && Section.bCastShadow;

					const int MBI = FMath::Min((int)Section.MaxBoneInfluences, GPUSKI_WITH_EXTRA_BONE ? 8 : 4);
					MeshBatch.VertexFactory = MeshDefStruct.MeshData->LODs[LODIndex - MeshDefStruct.BaseLOD].GetVertexFactory(MBI);

					BatchElement.IndexBuffer = SKLODData.MultiSizeIndexContainer.GetIndexBuffer();
					BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
					BatchElement.FirstIndex = Section.BaseIndex;
					BatchElement.NumPrimitives = Section.NumTriangles;
					BatchElement.MinVertexIndex = Section.BaseVertexIndex;
					BatchElement.MaxVertexIndex = SKLODData.GetNumVertices() - 1;

					// Pass anim collection UB via UserData
					FRHIUniformBuffer* AnimUB = AnimCollection->AnimCollectionUB.GetReference();
					BatchElement.UserData = AnimUB;
					BatchElement.VertexFactoryUserData = AnimUB;

					BatchElement.InstancedLODIndex = LODIndex;
					BatchElement.bForceInstanceCulling = true;
					BatchElement.bFetchInstanceCountFromScene = true;
					BatchElement.bPreserveInstanceOrder = true;

					// Material
					int32 MatIdx = CurMatOffset + Section.MaterialIndex;
					if (Materials.IsValidIndex(MatIdx) && Materials[MatIdx])
						MeshBatch.MaterialRenderProxy = Materials[MatIdx]->GetRenderProxy();
					else
						MeshBatch.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();

					MeshBatch.DepthPriorityGroup = GetStaticDepthPriorityGroup();

					BatchElement.MaxScreenSize = ScreenSize;
					BatchElement.MinScreenSize = 0;

					const FSkeletalMeshLODInfo* NextLODInfo = MeshDefStruct.Mesh->GetLODInfo(LODIndex + 1);
					if (NextLODInfo)
						BatchElement.MinScreenSize = NextLODInfo->ScreenSize.GetValue();

					PDI->DrawMesh(MeshBatch, ScreenSize);
				}
			}
		}
	}

	void UpdateInstanceBuffer(FGPUSKICompDynData& CompDynData)
	{
		SCOPE_CYCLE_COUNTER(STAT_GPUSKI_UpdateInstanceBuffer);

		FInstanceSceneDataBuffers::FAccessTag AT(123);
		FInstanceSceneDataBuffers::FWriteView RW = this->InstanceBuffer.BeginWriteAccess(AT);

		// Use Center-based LocalToWorld (matching Skelot's approach)
		InstanceBuffer.SetPrimitiveLocalToWorld(CompDynData.GetLocalToWorld(false), AT);
		const int32 Num = CompDynData.Transforms.Num();

		if (Num != 0)
		{
			RW.NumCustomDataFloats = (CompDynData.CustomData.Num() / Num);
			check(RW.NumCustomDataFloats % 4 == 0);
		}

		RW.InstanceLocalBounds.Reset(1);
		RW.InstanceLocalBounds.Add(this->ApproximateLocalBounds);

		RW.InstanceToPrimitiveRelative.SetNum(Num);
		RW.PrevInstanceToPrimitiveRelative.SetNum(Num);
		RW.InstanceCustomData.SetNum(Num * RW.NumCustomDataFloats);

		// Transforms are already Center-relative FRenderTransform, assign directly
		for (int32 i = 0; i < Num; i++)
		{
			RW.InstanceToPrimitiveRelative[i] = CompDynData.Transforms[i];
			RW.PrevInstanceToPrimitiveRelative[i] = CompDynData.PrevTransforms[i];
		}

		if (RW.NumCustomDataFloats > 0 && CompDynData.CustomData.Num() > 0)
		{
			FMemory::Memcpy(RW.InstanceCustomData.GetData(), CompDynData.CustomData.GetData(),
				CompDynData.CustomData.Num() * sizeof(float));
		}

		this->InstanceBuffer.EndWriteAccess(AT);

		// Notify GPUScene of updated transforms and bounds (critical for proper rendering)
		FScene* ScenePtr = this->GetScene().GetRenderScene();
		FPrimitiveSceneInfo* PSI = this->GetPrimitiveSceneInfo();
		if (ScenePtr && PSI)
		{
			ScenePtr->PrimitiveUpdates.Enqueue(PSI, FUpdateTransformCommand{
				.WorldBounds = CompDynData.WorldBounds,
				.LocalBounds = CompDynData.LocalBounds,
				.LocalToWorld = CompDynData.GetLocalToWorld(false),
				.AttachmentRootPosition = FVector::Zero()
			});
			ScenePtr->PrimitiveUpdates.Enqueue(PSI, FUpdateOverridePreviousTransformData(CompDynData.GetPrevLocalToWorld(false)));
			ScenePtr->PrimitiveUpdates.Enqueue(PSI, FUpdateInstanceCommand{
				.PrimitiveSceneProxy = this,
				.WorldBounds = CompDynData.WorldBounds,
				.LocalBounds = CompDynData.LocalBounds,
				.StaticMeshBounds = CompDynData.LocalBounds
			});
		}
	}

	bool GetInstanceDrawDistanceMinMax(FVector2f& OutDistanceMinMax) const override
	{
		OutDistanceMinMax = FVector2f(0, MaxDrawDistance);
		return MaxDrawDistance > 0;
	}

	float GetGpuLodInstanceRadius() const override
	{
		return ApproximateLocalBounds.GetExtent().Size();
	}
};

//----------------------------------------------------------------------
// UGPUSKIClusterComponent
//----------------------------------------------------------------------
UGPUSKIClusterComponent::UGPUSKIClusterComponent()
{
	bWantsInitializeComponent = false;
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	CastShadow = true;
	bUseAsOccluder = false;
}

FGPUSKIInstanceRenderDesc& UGPUSKIClusterComponent::GetRenderDesc() const
{
	AGPUSKIWorld* SKWorld = GetGPUSKIWorld();
	FSetElementId DescId = FSetElementId::FromInteger(DescIdx);
	return SKWorld->RenderDescs.Get(DescId);
}

FGPUSKICluster& UGPUSKIClusterComponent::GetClusterStruct() const
{
	AGPUSKIWorld* SKWorld = GetGPUSKIWorld();
	FSetElementId DescId = FSetElementId::FromInteger(DescIdx);
	return SKWorld->RenderDescs.Get(DescId).Clusters[ClusterIdx];
}

FBoxSphereBounds UGPUSKIClusterComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	AGPUSKIWorld* SKWorld = GetGPUSKIWorld();
	if (!SKWorld)
		return FBoxSphereBounds(FVector::ZeroVector, FVector(100), 100);

	const FGPUSKICluster& Cluster = GetClusterStruct();
	if (Cluster.Instances.Num() == 0)
		return FBoxSphereBounds(FVector::ZeroVector, FVector(100), 100);

	GPUSKI::FBoxMinMax BoundsAccum(ForceInit);
	for (int32 InstIdx : Cluster.Instances)
	{
		FVector Loc = SKWorld->SOA.Transforms[InstIdx].GetLocation();
		BoundsAccum.Add(Loc + FVector(100));
		BoundsAccum.Add(Loc - FVector(100));
	}

	return BoundsAccum.ToBoxSphereBounds();
}

void UGPUSKIClusterComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();

	if (!SceneProxy)
		return;

	// Update bounds before building data so WorldBounds is current
	this->UpdateBounds();

	FGPUSKICompDynData* DynData = new FGPUSKICompDynData();
	BuildDynamicData(*DynData);

	FGPUSKIClusterProxy* Proxy = static_cast<FGPUSKIClusterProxy*>(SceneProxy);

	ENQUEUE_RENDER_COMMAND(GPUSKIUpdateInstances)(
		[Proxy, DynData](FRHICommandListImmediate& RHICmdList)
		{
			Proxy->UpdateInstanceBuffer(*DynData);
			delete DynData;
		}
	);
}

void UGPUSKIClusterComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
}

void UGPUSKIClusterComponent::BeginDestroy()
{
	Super::BeginDestroy();
}

int32 UGPUSKIClusterComponent::GetNumMaterials() const
{
	int32 Count = 0;
	const FGPUSKIInstanceRenderDesc& Desc = GetRenderDesc();
	for (const FGPUSKIMeshRenderDesc& MeshDesc : Desc.Meshes)
	{
		if (MeshDesc.Mesh)
			Count += MeshDesc.Mesh->GetNumMaterials();
	}
	return Count;
}

UMaterialInterface* UGPUSKIClusterComponent::GetMaterial(int32 MaterialIndex) const
{
	const FGPUSKIInstanceRenderDesc& Desc = GetRenderDesc();
	int32 Offset = 0;
	for (const FGPUSKIMeshRenderDesc& MeshDesc : Desc.Meshes)
	{
		if (!MeshDesc.Mesh)
			continue;

		int32 NumMats = MeshDesc.Mesh->GetNumMaterials();
		if (MaterialIndex < Offset + NumMats)
		{
			return MeshDesc.GetMaterial(MaterialIndex - Offset);
		}
		Offset += NumMats;
	}
	return nullptr;
}

FPrimitiveSceneProxy* UGPUSKIClusterComponent::CreateSceneProxy()
{
	const FGPUSKICluster& Cluster = GetClusterStruct();
	if (Cluster.Instances.Num() == 0)
		return nullptr;

	return new FGPUSKIClusterProxy(this, GetFName());
}

void UGPUSKIClusterComponent::BuildDynamicData(FGPUSKICompDynData& OutData) const
{
	AGPUSKIWorld* SKWorld = GetGPUSKIWorld();
	if (!SKWorld)
		return;

	const FGPUSKICluster& Cluster = GetClusterStruct();
	const FGPUSKIInstanceRenderDesc& Desc = GetRenderDesc();
	const int32 NumInstances = Cluster.Instances.Num();
	const int32 NumCDF = FMath::Max(4, Align(2 + Desc.NumCustomDataFloat, 4)); // Must be aligned to 4

	// Set center to component location (instances will be relative to this)
	OutData.Center = GetComponentLocation();
	OutData.PrevCenter = OutData.Center;
	OutData.WorldBounds = Bounds; // Component's calculated world bounds

	// Compute approximate local bounds from meshes
	FBox LocalBox(ForceInit);
	if (Desc.AnimCollection)
	{
		for (const FGPUSKIMeshRenderDesc& MeshDesc : Desc.Meshes)
		{
			if (MeshDesc.Mesh)
			{
				FBoxSphereBounds MB = MeshDesc.Mesh->GetBounds();
				LocalBox += FBox(MB.Origin - MB.BoxExtent, MB.Origin + MB.BoxExtent);
			}
		}
	}
	if (!LocalBox.IsValid)
		LocalBox = FBox(FVector(-100), FVector(100));
	OutData.LocalBounds = FBoxSphereBounds(LocalBox);

	OutData.Transforms.SetNum(NumInstances);
	OutData.PrevTransforms.SetNum(NumInstances);
	OutData.CustomData.SetNumZeroed(NumInstances * NumCDF);

	for (int32 i = 0; i < NumInstances; i++)
	{
		int32 InstIdx = Cluster.Instances[i];

		// Offset instance transform by Center (matching Skelot's approach)
		FTransform InsT = SKWorld->SOA.Transforms[InstIdx];
		InsT.SetLocation(InsT.GetLocation() - OutData.Center);
		OutData.Transforms[i] = FRenderTransform(FMatrix44f(InsT.ToMatrixWithScale()));

		FTransform PrevInsT = SKWorld->SOA.PrevTransforms[InstIdx];
		PrevInsT.SetLocation(PrevInsT.GetLocation() - OutData.PrevCenter);
		OutData.PrevTransforms[i] = FRenderTransform(FMatrix44f(PrevInsT.ToMatrixWithScale()));

		// Pack animation data into custom data slots 0-3 (all system-reserved)
		float* CD = &OutData.CustomData[i * NumCDF];
		const FGPUSKIInstancesSOA::FAnimData& AnimData = SKWorld->SOA.AnimDatas[InstIdx];

		uint32 CurFrame  = (uint32)SKWorld->SOA.CurAnimFrames[InstIdx];
		uint32 PreFrame  = (uint32)SKWorld->SOA.PreAnimFrames[InstIdx];
		uint32 FromFrame = (uint32)AnimData.FromAnimFrame;

		FMemory::Memcpy(&CD[0], &CurFrame,  sizeof(uint32)); // slot 0: current frame
		FMemory::Memcpy(&CD[1], &PreFrame,  sizeof(uint32)); // slot 1: previous frame (motion vectors)
		FMemory::Memcpy(&CD[2], &FromFrame, sizeof(uint32)); // slot 2: transition source frame
		CD[3] = AnimData.BlendAlpha;                         // slot 3: blend alpha (0=from, 1=current)
	}
}
