// GPUSKI Plugin - World singleton actor managing all GPUSKI instances

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GPUSKIWorldBase.h"
#include "RenderTransform.h"

#include "GPUSKIWorld.generated.h"

class UGPUSKIAnimCollection;
class UGPUSKIClusterComponent;

// Simple free-list allocator for instance handles
struct FGPUSKIHandleAllocator
{
	TArray<int32> FreeList;
	int32 MaxAllocated = 0;

	int32 Allocate()
	{
		if (FreeList.Num() > 0)
			return FreeList.Pop();
		return MaxAllocated++;
	}

	void Free(int32 Index)
	{
		FreeList.Add(Index);
	}

	int32 GetMaxSize() const { return MaxAllocated; }
};

// Dynamic data passed to the scene proxy for instance buffer updates
struct FGPUSKICompDynData
{
	FBoxSphereBounds WorldBounds;
	FBoxSphereBounds LocalBounds;
	FVector Center = FVector::ZeroVector;
	FVector PrevCenter = FVector::ZeroVector;

	TArray<FRenderTransform> Transforms;
	TArray<FRenderTransform> PrevTransforms;
	TArray<float> CustomData;
	TArray<FRenderBounds> PerInstanceLocalBounds;

	FMatrix GetLocalToWorld(bool bNeg) const
	{
		FMatrix M = FMatrix::Identity;
		M.SetOrigin(Center);
		if (bNeg) M.M[0][0] = -1.0;
		return M;
	}

	FMatrix GetPrevLocalToWorld(bool bNeg) const
	{
		FMatrix M = FMatrix::Identity;
		M.SetOrigin(PrevCenter);
		if (bNeg) M.M[0][0] = -1.0;
		return M;
	}
};

UCLASS(NotBlueprintable, NotPlaceable, Transient)
class GPUSKI_API AGPUSKIWorld : public AActor
{
	GENERATED_BODY()

public:
	// Instance data (SOA layout)
	UPROPERTY()
	FGPUSKIInstancesSOA SOA;

	// Handle allocator
	FGPUSKIHandleAllocator HandleAllocator;

	// Render descriptors (deduplicated)
	UPROPERTY()
	TSet<FGPUSKIInstanceRenderDescFinal> RenderDescs;

	//----------------------------------------------------------------------
	// Lifecycle
	//----------------------------------------------------------------------

	AGPUSKIWorld();

	void BeginPlay() override;
	void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	void Tick(float DeltaSeconds) override;

	//----------------------------------------------------------------------
	// Instance Management
	//----------------------------------------------------------------------

	FGPUSKIInstanceHandle CreateInstance(const FTransform& Transform, const FGPUSKIInstanceRenderDesc& RenderDesc);
	void DestroyInstance(FGPUSKIInstanceHandle& Handle);
	bool IsHandleValid(const FGPUSKIInstanceHandle& Handle) const;

	//----------------------------------------------------------------------
	// Animation
	//----------------------------------------------------------------------

	void PlayAnimation(const FGPUSKIInstanceHandle& Handle, const FGPUSKIAnimPlayParams& Params);

	//----------------------------------------------------------------------
	// Transform
	//----------------------------------------------------------------------

	void SetInstanceTransform(const FGPUSKIInstanceHandle& Handle, const FTransform& Transform);
	FTransform GetInstanceTransform(const FGPUSKIInstanceHandle& Handle) const;

	//----------------------------------------------------------------------
	// Static singleton access
	//----------------------------------------------------------------------

	static AGPUSKIWorld* GetInstance(UWorld* World);

	// Size of each spatial partition cell in cm (~32m). Instances within one cell share a cluster.
	// Smaller = more draw calls but better culling. Larger = fewer components but weaker culling.
	static constexpr float ClusterCellSize = 3200.0f;

	// Convert a world position to a 2D integer cell key (Z ignored — suitable for ground crowds)
	static FIntVector WorldPosToCell(const FVector& WorldPos)
	{
		return FIntVector(
			FMath::FloorToInt(WorldPos.X / ClusterCellSize),
			FMath::FloorToInt(WorldPos.Y / ClusterCellSize),
			0
		);
	}

private:
	void TickAnimations(float DeltaSeconds);
	void UpdateClusterComponents();

	// Assign an instance to a cluster
	void AssignToCluster(int32 InstanceIndex, const FGPUSKIInstanceRenderDesc& RenderDesc);
	void RemoveFromCluster(int32 InstanceIndex);

	// Destroy a cluster's component if it has become empty
	void MaybeDestroyEmptyCluster(FGPUSKIInstanceRenderDescFinal& Desc, int32 ClusterIdx);

	// Ensure SOA arrays are large enough
	void GrowSOA(int32 NewSize);

	// Create cluster component for a cluster
	UGPUSKIClusterComponent* CreateClusterComponent(FGPUSKIInstanceRenderDescFinal& Desc, int32 ClusterIdx);
};
