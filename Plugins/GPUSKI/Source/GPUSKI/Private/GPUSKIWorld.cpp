// GPUSKI Plugin - World singleton implementation

#include "GPUSKIWorld.h"
#include "GPUSKIClusterComponent.h"
#include "GPUSKIAnimCollection.h"
#include "GPUSKIRenderResources.h"

#include "Engine/World.h"
#include "EngineUtils.h"

//----------------------------------------------------------------------
// FGPUSKIMeshRenderDesc
//----------------------------------------------------------------------
UMaterialInterface* FGPUSKIMeshRenderDesc::GetMaterial(int32 Index) const
{
	if (OverrideMaterials.IsValidIndex(Index) && OverrideMaterials[Index])
		return OverrideMaterials[Index];

	if (Mesh)
	{
		TArray<FSkeletalMaterial>& MeshMaterials = Mesh->GetMaterials();
		if (MeshMaterials.IsValidIndex(Index))
			return MeshMaterials[Index].MaterialInterface;
	}
	return nullptr;
}

bool FGPUSKIMeshRenderDesc::operator==(const FGPUSKIMeshRenderDesc& Other) const
{
	return Mesh == Other.Mesh && OverrideMaterials == Other.OverrideMaterials;
}

//----------------------------------------------------------------------
// FGPUSKIInstanceRenderDesc
//----------------------------------------------------------------------
FGPUSKIInstanceRenderDesc::FGPUSKIInstanceRenderDesc()
	: bCastDynamicShadow(true)
	, bRenderInMainPass(true)
	, MaxDrawDistance(0)
{
}

uint32 FGPUSKIInstanceRenderDesc::ComputeHash() const
{
	uint32 H = GetTypeHash(AnimCollection);
	for (const FGPUSKIMeshRenderDesc& M : Meshes)
		H ^= GetTypeHash(M);
	return H;
}

bool FGPUSKIInstanceRenderDesc::Equal(const FGPUSKIInstanceRenderDesc& Other) const
{
	return AnimCollection == Other.AnimCollection
		&& Meshes == Other.Meshes
		&& NumCustomDataFloat == Other.NumCustomDataFloat;
}

//----------------------------------------------------------------------
// AGPUSKIWorld
//----------------------------------------------------------------------
AGPUSKIWorld::AGPUSKIWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void AGPUSKIWorld::BeginPlay()
{
	Super::BeginPlay();
}

void AGPUSKIWorld::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	// Destroy all cluster components
	for (auto& Desc : RenderDescs)
	{
		for (auto& Cluster : Desc.Clusters)
		{
			if (Cluster.Component)
			{
				Cluster.Component->DestroyComponent();
				Cluster.Component = nullptr;
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AGPUSKIWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TickAnimations(DeltaSeconds);
	UpdateClusterComponents();
}

//----------------------------------------------------------------------
// Instance Management
//----------------------------------------------------------------------
void AGPUSKIWorld::GrowSOA(int32 NewSize)
{
	if (SOA.Slots.Num() < NewSize)
	{
		const int32 OldSize = SOA.Slots.Num();
		SOA.Slots.SetNum(NewSize);
		SOA.Transforms.SetNum(NewSize);
		SOA.PrevTransforms.SetNum(NewSize);
		SOA.CurAnimFrames.SetNumZeroed(NewSize);
		SOA.PreAnimFrames.SetNumZeroed(NewSize);
		SOA.AnimDatas.SetNum(NewSize);
		SOA.ClusterData.SetNum(NewSize);

		const int32 NumCDF = 2 + SOA.MaxNumCustomDataFloat; // 2 reserved for frame indices
		SOA.PerInstanceCustomData.SetNumZeroed(NewSize * NumCDF);
	}
}

FGPUSKIInstanceHandle AGPUSKIWorld::CreateInstance(const FTransform& Transform, const FGPUSKIInstanceRenderDesc& RenderDesc)
{
	// Allocate index (auto-grows)
	int32 Index = HandleAllocator.Allocate();

	GrowSOA(HandleAllocator.GetMaxSize());

	// Init slot
	FGPUSKIInstancesSOA::FSlotData& Slot = SOA.Slots[Index];
	Slot.IncVersion();
	Slot.bDestroyed = false;
	Slot.bNoSequence = true;

	SOA.Transforms[Index] = Transform;
	SOA.PrevTransforms[Index] = Transform;
	SOA.CurAnimFrames[Index] = 0;
	SOA.PreAnimFrames[Index] = 0;
	SOA.AnimDatas[Index] = FGPUSKIInstancesSOA::FAnimData();
	SOA.ClusterData[Index] = FGPUSKIInstancesSOA::FClusterData();

	// Assign to cluster
	AssignToCluster(Index, RenderDesc);

	FGPUSKIInstanceHandle Handle;
	Handle.InstanceIndex = Index;
	Handle.Version = Slot.Version;
	return Handle;
}

void AGPUSKIWorld::DestroyInstance(FGPUSKIInstanceHandle& Handle)
{
	if (!IsHandleValid(Handle))
		return;

	const int32 Index = Handle.InstanceIndex;

	RemoveFromCluster(Index);

	SOA.Slots[Index].bDestroyed = true;
	HandleAllocator.Free(Index);

	Handle.Version = 0;
}

bool AGPUSKIWorld::IsHandleValid(const FGPUSKIInstanceHandle& Handle) const
{
	if (!Handle.IsValid())
		return false;

	if (!SOA.Slots.IsValidIndex(Handle.InstanceIndex))
		return false;

	const FGPUSKIInstancesSOA::FSlotData& Slot = SOA.Slots[Handle.InstanceIndex];
	return Slot.Version == Handle.Version && !Slot.bDestroyed;
}

//----------------------------------------------------------------------
// Animation
//----------------------------------------------------------------------
void AGPUSKIWorld::PlayAnimation(const FGPUSKIInstanceHandle& Handle, const FGPUSKIAnimPlayParams& Params)
{
	if (!IsHandleValid(Handle))
		return;

	const int32 Index = Handle.InstanceIndex;
	const FGPUSKIInstancesSOA::FClusterData& CD = SOA.ClusterData[Index];

	if (CD.DescIdx == -1)
		return;

	const FGPUSKIInstanceRenderDescFinal& Desc = RenderDescs.Get(FSetElementId::FromInteger(CD.DescIdx));
	UGPUSKIAnimCollection* AnimColl = Desc.AnimCollection;

	if (!AnimColl || !Params.Animation)
		return;

	int SeqIdx = AnimColl->FindSequenceDef(Params.Animation);
	if (SeqIdx == INDEX_NONE)
	{
		UE_LOG(LogGPUSKI, Warning, TEXT("Animation %s not found in AnimCollection %s"),
			*Params.Animation->GetName(), *AnimColl->GetName());
		return;
	}

	FGPUSKIInstancesSOA::FAnimData& AnimData = SOA.AnimDatas[Index];

	// Start a blend transition if requested and a sequence is already playing
	if (Params.BlendInTime > 0.0f && AnimData.IsSequenceValid())
	{
		AnimData.FromAnimFrame = SOA.CurAnimFrames[Index];
		AnimData.BlendAlpha = 0.0f;
		AnimData.BlendDuration = Params.BlendInTime;
	}
	else
	{
		AnimData.FromAnimFrame = 0;
		AnimData.BlendAlpha = 1.0f;
		AnimData.BlendDuration = 0.0f;
	}

	AnimData.AnimationCurrentSequence = (uint16)SeqIdx;
	AnimData.AnimationTime = Params.StartAt;
	AnimData.AnimationPlayRate = Params.PlayScale;
	AnimData.CurrentAsset = Params.Animation;

	SOA.Slots[Index].bNoSequence = false;
	SOA.Slots[Index].bAnimationLooped = Params.bLoop;
	SOA.Slots[Index].bAnimationPaused = false;

	// Compute initial frame
	const FGPUSKISequenceDef& SeqDef = AnimColl->Sequences[SeqIdx];
	SOA.CurAnimFrames[Index] = SeqDef.CalcFrameIndex(Params.StartAt);
	SOA.PreAnimFrames[Index] = SOA.CurAnimFrames[Index];
}

//----------------------------------------------------------------------
// Transform
//----------------------------------------------------------------------
void AGPUSKIWorld::SetInstanceTransform(const FGPUSKIInstanceHandle& Handle, const FTransform& Transform)
{
	if (!IsHandleValid(Handle))
		return;

	const int32 Index = Handle.InstanceIndex;
	const FIntVector OldCell = WorldPosToCell(SOA.Transforms[Index].GetLocation());
	SOA.Transforms[Index] = Transform;
	const FIntVector NewCell = WorldPosToCell(Transform.GetLocation());

	if (OldCell != NewCell)
	{
		// Instance crossed a cell boundary — move it to the correct cluster
		const FGPUSKIInstancesSOA::FClusterData CD = SOA.ClusterData[Index]; // copy before RemoveFromCluster clears it
		if (CD.DescIdx != -1)
		{
			const FGPUSKIInstanceRenderDesc BaseDesc =
				static_cast<const FGPUSKIInstanceRenderDesc&>(RenderDescs.Get(FSetElementId::FromInteger(CD.DescIdx)));
			RemoveFromCluster(Index);
			AssignToCluster(Index, BaseDesc);
		}
	}
}

FTransform AGPUSKIWorld::GetInstanceTransform(const FGPUSKIInstanceHandle& Handle) const
{
	if (!IsHandleValid(Handle))
		return FTransform::Identity;

	return SOA.Transforms[Handle.InstanceIndex];
}

//----------------------------------------------------------------------
// Animation Tick
//----------------------------------------------------------------------
void AGPUSKIWorld::TickAnimations(float DeltaSeconds)
{
	for (auto& DescFinal : RenderDescs)
	{
		UGPUSKIAnimCollection* AnimColl = DescFinal.AnimCollection;
		if (!AnimColl || !AnimColl->bIsBuilt)
			continue;

		for (auto& Cluster : DescFinal.Clusters)
		{
			for (int32 InstIdx : Cluster.Instances)
			{
				FGPUSKIInstancesSOA::FSlotData& Slot = SOA.Slots[InstIdx];
				if (Slot.bDestroyed || Slot.bNoSequence || Slot.bAnimationPaused)
					continue;

				FGPUSKIInstancesSOA::FAnimData& AnimData = SOA.AnimDatas[InstIdx];
				if (!AnimData.IsSequenceValid())
					continue;

				// Store previous frame
				SOA.PrevTransforms[InstIdx] = SOA.Transforms[InstIdx];
				SOA.PreAnimFrames[InstIdx] = SOA.CurAnimFrames[InstIdx];

				// Advance time
				AnimData.AnimationTime += DeltaSeconds * AnimData.AnimationPlayRate;

				const FGPUSKISequenceDef& SeqDef = AnimColl->Sequences[AnimData.AnimationCurrentSequence];
				const float SeqLength = SeqDef.GetSequenceLength();

				// Handle looping / completion
				if (AnimData.AnimationTime >= SeqLength)
				{
					if (Slot.bAnimationLooped)
					{
						AnimData.AnimationTime = FMath::Fmod(AnimData.AnimationTime, SeqLength);
					}
					else
					{
						AnimData.AnimationTime = SeqLength;
						Slot.bAnimationPaused = true;
					}
				}

				// Compute frame index
				SOA.CurAnimFrames[InstIdx] = SeqDef.CalcFrameIndex(AnimData.AnimationTime);

				// Advance blend transition
				if (AnimData.BlendDuration > 0.0f && AnimData.BlendAlpha < 1.0f)
				{
					AnimData.BlendAlpha = FMath::Min(1.0f, AnimData.BlendAlpha + DeltaSeconds / AnimData.BlendDuration);
					if (AnimData.BlendAlpha >= 1.0f)
						AnimData.BlendDuration = 0.0f; // Transition complete
				}
			}
		}
	}
}

//----------------------------------------------------------------------
// Cluster Management
//----------------------------------------------------------------------
void AGPUSKIWorld::AssignToCluster(int32 InstanceIndex, const FGPUSKIInstanceRenderDesc& RenderDesc)
{
	// Find or create the render descriptor
	FGPUSKIInstanceRenderDescFinal FindKey(RenderDesc);
	FSetElementId DescId = RenderDescs.FindId(FindKey);
	if (!DescId.IsValidId())
		DescId = RenderDescs.Add(FindKey);

	FGPUSKIInstanceRenderDescFinal& Desc = RenderDescs.Get(DescId);

	// Determine which spatial cell this instance belongs to
	const FIntVector CellKey = WorldPosToCell(SOA.Transforms[InstanceIndex].GetLocation());

	int32 ClusterIdx;
	if (int32* ExistingIdx = Desc.CellToClusterIndex.Find(CellKey))
	{
		ClusterIdx = *ExistingIdx;
		// Re-create component if the cluster was previously emptied
		FGPUSKICluster& Cluster = Desc.Clusters[ClusterIdx];
		if (!Cluster.Component)
			Cluster.Component = CreateClusterComponent(Desc, ClusterIdx);
	}
	else
	{
		// First instance in this cell — create a new cluster
		ClusterIdx = Desc.Clusters.Num();
		FGPUSKICluster& NewCluster = Desc.Clusters.AddDefaulted_GetRef();
		NewCluster.Component = CreateClusterComponent(Desc, ClusterIdx);
		Desc.CellToClusterIndex.Add(CellKey, ClusterIdx);
	}

	FGPUSKICluster& Cluster = Desc.Clusters[ClusterIdx];
	const int32 RenderIdx = Cluster.Instances.Add(InstanceIndex);
	Cluster.bAnyAddRemove = true;

	FGPUSKIInstancesSOA::FClusterData& CD = SOA.ClusterData[InstanceIndex];
	CD.DescIdx = DescId.AsInteger();
	CD.ClusterIdx = ClusterIdx;
	CD.RenderIdx = RenderIdx;
}

void AGPUSKIWorld::RemoveFromCluster(int32 InstanceIndex)
{
	FGPUSKIInstancesSOA::FClusterData& CD = SOA.ClusterData[InstanceIndex];
	if (CD.DescIdx == -1)
		return;

	FGPUSKIInstanceRenderDescFinal& Desc = RenderDescs.Get(FSetElementId::FromInteger(CD.DescIdx));

	if (Desc.Clusters.IsValidIndex(CD.ClusterIdx))
	{
		FGPUSKICluster& Cluster = Desc.Clusters[CD.ClusterIdx];
		Cluster.Instances.RemoveAtSwap(CD.RenderIdx);
		Cluster.bAnyAddRemove = true;

		// Update swapped instance's RenderIdx
		if (CD.RenderIdx < Cluster.Instances.Num())
		{
			int32 SwappedInstanceIdx = Cluster.Instances[CD.RenderIdx];
			SOA.ClusterData[SwappedInstanceIdx].RenderIdx = CD.RenderIdx;
		}

		// Destroy the component when the cluster becomes empty to avoid empty draw calls
		MaybeDestroyEmptyCluster(Desc, CD.ClusterIdx);
	}

	CD.DescIdx = -1;
	CD.ClusterIdx = -1;
	CD.RenderIdx = -1;
}

void AGPUSKIWorld::MaybeDestroyEmptyCluster(FGPUSKIInstanceRenderDescFinal& Desc, int32 ClusterIdx)
{
	FGPUSKICluster& Cluster = Desc.Clusters[ClusterIdx];
	if (Cluster.Instances.Num() == 0 && Cluster.Component)
	{
		Cluster.Component->DestroyComponent();
		Cluster.Component = nullptr;
	}
}

UGPUSKIClusterComponent* AGPUSKIWorld::CreateClusterComponent(FGPUSKIInstanceRenderDescFinal& Desc, int32 ClusterIdx)
{
	UGPUSKIClusterComponent* Comp = NewObject<UGPUSKIClusterComponent>(this);
	Comp->DescIdx = RenderDescs.FindId(Desc).AsInteger();
	Comp->ClusterIdx = ClusterIdx;

	Comp->SetCastShadow(Desc.bCastDynamicShadow);
	Comp->SetVisibleFlag(true);
	Comp->RegisterComponent();

	return Comp;
}

void AGPUSKIWorld::UpdateClusterComponents()
{
	for (auto& Desc : RenderDescs)
	{
		for (auto& Cluster : Desc.Clusters)
		{
			if (Cluster.Component && Cluster.bAnyAddRemove)
			{
				Cluster.Component->MarkRenderStateDirty();
				Cluster.bAnyAddRemove = false;
			}
			else if (Cluster.Component)
			{
				// Always update dynamic data (transforms and frame indices change every frame)
				Cluster.Component->MarkRenderDynamicDataDirty();
			}
		}
	}
}

//----------------------------------------------------------------------
// Singleton Access
//----------------------------------------------------------------------
AGPUSKIWorld* AGPUSKIWorld::GetInstance(UWorld* World)
{
	if (!World)
		return nullptr;

	// Find existing
	for (TActorIterator<AGPUSKIWorld> It(World); It; ++It)
	{
		return *It;
	}

	// Spawn new
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGPUSKIWorld* NewWorld = World->SpawnActor<AGPUSKIWorld>(Params);
	return NewWorld;
}
