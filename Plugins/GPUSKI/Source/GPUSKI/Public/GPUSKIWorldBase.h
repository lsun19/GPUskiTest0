// GPUSKI Plugin - World base types: instance handle, SOA, render descriptors, clusters

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "GPUSKI.h"
#include "GPUSKIBase.h"
#include "AlphaBlend.h"

#include "GPUSKIWorldBase.generated.h"

class UGPUSKIAnimCollection;
class UGPUSKIClusterComponent;
class USkeletalMesh;
class UMaterialInterface;
class AGPUSKIWorld;
class UAnimSequenceBase;

using GPUSKIInstanceIndex = int32;

//----------------------------------------------------------------------
// Instance Handle
//----------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FGPUSKIInstanceHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GPUSKI")
	int32 InstanceIndex = 0;

	UPROPERTY()
	uint32 Version = 0;

	bool IsValid() const { return Version != 0; }

	bool operator==(const FGPUSKIInstanceHandle Other) const
	{
		return InstanceIndex == Other.InstanceIndex && Version == Other.Version;
	}

	friend inline uint32 GetTypeHash(FGPUSKIInstanceHandle H) { return H.InstanceIndex / 8; }
};

template<> struct TIsPODType<FGPUSKIInstanceHandle> { enum { Value = true }; };
template<> struct TStructOpsTypeTraits<FGPUSKIInstanceHandle> : public TStructOpsTypeTraitsBase2<FGPUSKIInstanceHandle>
{
	enum { WithZeroConstructor = true, WithIdenticalViaEquality = true };
};

//----------------------------------------------------------------------
// Animation Play Parameters
//----------------------------------------------------------------------
USTRUCT(BlueprintType)
struct GPUSKI_API FGPUSKIAnimPlayParams
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI")
	UAnimSequenceBase* Animation = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI")
	float StartAt = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI")
	float PlayScale = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI")
	bool bLoop = true;

	// Blend duration in seconds from the previously playing animation (0 = instant snap)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI")
	float BlendInTime = 0.0f;
};

//----------------------------------------------------------------------
// Base component for GPUSKI rendering
//----------------------------------------------------------------------
UCLASS(Abstract)
class UGPUSKIBaseComponent : public UMeshComponent
{
	GENERATED_BODY()
};

//----------------------------------------------------------------------
// Instance SOA (Structure of Arrays)
//----------------------------------------------------------------------
USTRUCT()
struct GPUSKI_API FGPUSKIInstancesSOA
{
	GENERATED_BODY()

	struct FSlotData
	{
		uint32 Version = 1;
		uint32 bDestroyed : 1 = false;
		uint32 bAnimationLooped : 1 = false;
		uint32 bAnimationPaused : 1 = false;
		uint32 bNoSequence : 1 = true;

		void IncVersion()
		{
			Version++;
			if (Version == 0) Version = 1;
		}
	};

	struct FAnimData
	{
		uint16 AnimationCurrentSequence = 0xFFFF;
		float AnimationTime = 0;
		float AnimationPlayRate = 1;
		UAnimSequenceBase* CurrentAsset = nullptr;

		// Transition blend state
		int32 FromAnimFrame = 0;    // Frame index to blend away from
		float BlendAlpha = 1.0f;    // 0 = fully at FromFrame, 1 = fully at current animation
		float BlendDuration = 0.0f; // Total blend duration in seconds (0 = no active blend)

		bool IsSequenceValid() const { return AnimationCurrentSequence != 0xFFFF; }
		bool IsBlending() const { return BlendDuration > 0.0f && BlendAlpha < 1.0f; }
	};

	struct FClusterData
	{
		int32 DescIdx = -1;
		int32 ClusterIdx = -1;
		int32 RenderIdx = -1;

		inline FSetElementId GetDescId() const { return FSetElementId::FromInteger(DescIdx); }
		inline FSetElementId GetClusterId() const { return FSetElementId::FromInteger(ClusterIdx); }
	};

	TArray<FSlotData> Slots;
	TArray<FTransform> Transforms;
	TArray<FTransform> PrevTransforms;
	TArray<int32> CurAnimFrames;
	TArray<int32> PreAnimFrames;
	TArray<FAnimData> AnimDatas;
	TArray<FClusterData> ClusterData;
	TArray<float> PerInstanceCustomData;
	int32 MaxNumCustomDataFloat = 0;
};

//----------------------------------------------------------------------
// Mesh Render Descriptor
//----------------------------------------------------------------------
USTRUCT(BlueprintType)
struct GPUSKI_API FGPUSKIMeshRenderDesc
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Render")
	TObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, Category = "Render")
	TArray<TObjectPtr<UMaterialInterface>> OverrideMaterials;

	UMaterialInterface* GetMaterial(int32 Index) const;

	bool operator==(const FGPUSKIMeshRenderDesc& Other) const;

	friend uint32 GetTypeHash(const FGPUSKIMeshRenderDesc& Self)
	{
		return GetTypeHash(Self.Mesh) ^ GetTypeHash(Self.OverrideMaterials);
	}
};

//----------------------------------------------------------------------
// Cluster
//----------------------------------------------------------------------
USTRUCT()
struct FGPUSKICluster
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UGPUSKIClusterComponent> Component = nullptr;

	TArray<int32> Instances;

	uint32 bAnyAddRemove : 1 = true;

	bool operator==(const FGPUSKICluster& Other) const { return this == &Other; }
	friend uint32 GetTypeHash(const FGPUSKICluster& C) { return PointerHash(&C); }
};

//----------------------------------------------------------------------
// Instance Render Descriptor
//----------------------------------------------------------------------
USTRUCT(BlueprintType)
struct GPUSKI_API FGPUSKIInstanceRenderDesc
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Rendering")
	TObjectPtr<UGPUSKIAnimCollection> AnimCollection;

	UPROPERTY(EditAnywhere, Category = "Rendering")
	TArray<FGPUSKIMeshRenderDesc> Meshes;

	UPROPERTY(EditAnywhere, Category = "Rendering")
	int32 NumCustomDataFloat = 0;

	UPROPERTY(EditAnywhere, Category = "Rendering")
	uint8 bCastDynamicShadow : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Rendering")
	uint8 bRenderInMainPass : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Rendering")
	float MaxDrawDistance = 0;

	FGPUSKIInstanceRenderDesc();

	uint32 ComputeHash() const;
	bool Equal(const FGPUSKIInstanceRenderDesc& Other) const;

	friend uint32 GetTypeHash(const FGPUSKIInstanceRenderDesc& In) { return In.ComputeHash(); }
};

//----------------------------------------------------------------------
// Final Render Descriptor (with runtime cluster data)
//----------------------------------------------------------------------
USTRUCT()
struct GPUSKI_API FGPUSKIInstanceRenderDescFinal : public FGPUSKIInstanceRenderDesc
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<FGPUSKICluster> Clusters;

	// Maps spatial cell key (from WorldPosToCell) to index in Clusters array
	TMap<FIntVector, int32> CellToClusterIndex;

	FGPUSKIInstanceRenderDescFinal() {}
	FGPUSKIInstanceRenderDescFinal(const FGPUSKIInstanceRenderDesc& Copy)
	{
		*static_cast<FGPUSKIInstanceRenderDesc*>(this) = Copy;
	}

	bool operator==(const FGPUSKIInstanceRenderDescFinal& Other) const { return Equal(Other); }
	bool operator==(const FGPUSKIInstanceRenderDesc& Other) const { return Equal(Other); }
};
