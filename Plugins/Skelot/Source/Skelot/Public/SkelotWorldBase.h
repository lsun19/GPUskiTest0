// Copyright 2024 Lazy Marmot Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Animation/AnimNotifyQueue.h"
#include "Components/MeshComponent.h"
#include "Skelot.h"
#include "SkelotBase.h"
#include "Chaos/Real.h"
#include "SpanAllocator.h"
#include "AlphaBlend.h"
#include "AssetRegistry/AssetData.h"

#include "SkelotWorldBase.generated.h"

class USkelotAnimCollection;
class USkeletalMeshSocket;
class UAnimNotify_Skelot;
class ISkelotNotifyInterface;
class UAnimSequenceBase;
class USkelotRenderParams;
class USKelotClusterComponent;
class USkeletalMesh;
class UMaterialInterface;
class ASkelotWorld;
class USkeletalMeshComponent;

using SkelotInstanceIndex = int32;

UENUM()
enum class ESkelotValidity : uint8
{
	Valid,
	NotValid,
};


USTRUCT(BlueprintType)
struct FSkelotInstanceHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Skelot")
	int32 InstanceIndex = 0;
	UPROPERTY()
	uint32 Version = 0;

	bool IsValid() const { return Version != 0; }

	bool operator == (const FSkelotInstanceHandle Other) const { return InstanceIndex == Other.InstanceIndex && Version == Other.Version; }

	FString ToDebugString() const;

	friend inline uint32 GetTypeHash(FSkelotInstanceHandle H) { return H.InstanceIndex / 8; }
};


template<> struct TIsPODType<FSkelotInstanceHandle> { enum { Value = true }; };
template<> struct TStructOpsTypeTraits<FSkelotInstanceHandle> : public TStructOpsTypeTraitsBase2<FSkelotInstanceHandle>
{
	enum
	{
		WithZeroConstructor = true,
		WithIdenticalViaEquality = true,
	};
};


USTRUCT(BlueprintType)
struct FSkelotAnimFinishEvent
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Skelot")
	FSkelotInstanceHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category = "Skelot")
	int32 InstanceIndex = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Skelot")
	UAnimSequenceBase* AnimSequence = nullptr;
};



USTRUCT(BlueprintType)
struct FSkelotAnimNotifyEvent
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Skelot")
	FSkelotInstanceHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category = "Skelot")
	UAnimSequenceBase* AnimSequence = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Skelot")
	FName NotifyName;
};

struct FSkelotAnimNotifyObjectEvent
{
	FSkelotInstanceHandle Handle;
	UAnimSequenceBase* AnimSequence = nullptr;
	ISkelotNotifyInterface* SkelotNotify = nullptr;
};

USTRUCT(BlueprintType)
struct SKELOT_API FSkelotAnimPlayParams
{
	GENERATED_USTRUCT_BODY()

	//animation to be played, only UAnimSequence and UAnimComposite are supported  
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skelot")
	UAnimSequenceBase* Animation = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skelot")
	float StartAt = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skelot")
	float PlayScale = 1;
	//if <= 0 then dont transition
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skelot")
	float TransitionDuration = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skelot")
	EAlphaBlendOption BlendOption = EAlphaBlendOption::Linear;
	//true if should only look for transition but not generate one, typically instances far from view don't need to generate transition
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skelot")
	bool bIgnoreTransitionGeneration = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skelot")
	bool bLoop = true;
	//if true then wont replay the same animation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skelot")
	bool bUnique = false;
};



DECLARE_DYNAMIC_DELEGATE_ThreeParams(FSkelotGeneralDynamicDelegate, FSkelotInstanceHandle, Handle, FName, PayloadTag, UObject*, PayloadObject);
DECLARE_DELEGATE_ThreeParams(FSkelotGeneralDelegate, FSkelotInstanceHandle, FName, UObject*);

UCLASS(Abstract)
class USkelotBaseComponent : public UMeshComponent
{
	GENERATED_BODY()

};






//defines a dynamic pose enabled instance that takes bones transform from a USkeletalMeshComponent, typically used for physics simulation.
USTRUCT()
struct FSkelotFrag_DynPoseTie 
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> SKMeshComponent = nullptr;

	int32 UserData = -1;
	int32 TmpUploadIndex = -1;
	bool bCopyTransform = false;	//should SKMeshComponent's transform be copied to the Skelot instance every frame ?
};

/*
instance data as struct of arrays
*/
USTRUCT()
struct SKELOT_API FSkelotInstancesSOA
{
	GENERATED_BODY()

	struct FSlotData
	{
		uint32 Version = 1; //version zero should never exist, our handles are zero initialized
		
		uint32 bDestroyed : 1 = false; //true if instance is invalid

		//animation flags
		uint32 bAnimationLooped : 1 = false;
		uint32 bAnimationPaused : 1 = false;
		uint32 bNoSequence : 1 = true;
		uint32 bDynamicPose : 1 = false;

		uint32 bCreatedThisFrame : 1 = false;

		uint8 UserFlags = 0;

		void IncVersion()
		{
			Version++;
			if(Version == 0)
				Version = 1;
		}
	};

	struct FAnimFrame
	{
		int32 Cur = 0;
		int32 Pre = 0;
	};

	struct FAnimData
	{
		//index for AnimCollection->Sequences[]. current playing sequence index
		uint16 AnimationCurrentSequence = 0xFFff;
		//index for AnimCollection->Transitions[] if any
		uint16 AnimationTransitionIndex = 0xFFff;
		//time since start of play
		float AnimationTime = 0;
		//#Note negative not supported
		float AnimationPlayRate = 1;
		//either UAnimSequence* or UAnimComposite*
		UAnimSequenceBase* CurrentAsset = nullptr; 


		bool IsSequenceValid() const { return AnimationCurrentSequence != 0xFFff; }
		bool IsTransitionValid() const { return AnimationTransitionIndex != 0xFFff; }
	};


	union FUserData
	{
		void* Pointer;
		SIZE_T Index;
	};

	struct FClusterData
	{
		int32 DescIdx = -1;	//index for ASkelotWorld.RenderDescs 
		int32 ClusterIdx = -1; //index for FSkelotInstanceRenderDesc.Clusters 
		int32 RenderIdx = -1; //index for FSkelotCluster.Instances 

		inline FSetElementId GetDescId() const { return FSetElementId::FromInteger(DescIdx); }
		inline FSetElementId GetClusterId() const { return FSetElementId::FromInteger(ClusterIdx); }
	};

	struct FMiscData
	{
		//index for AttachParentArray if there is any relation, -1 otherwise
		int32 AttachmentIndex = -1;
	};

	//most of the following array are accessed by instance index
	TArray<FSlotData>		Slots;
	//world space transform of instances
	TArray<FTransform>		Transforms;
	//previous frame transform of instances 
	TArray<FTransform>		PrevTransforms;
	//current animation frame index, (frame index could be in sequence range, transition, or dynamic pose)
	TArray<int32>			CurAnimFrames;
	//previous frame animation frame index
	TArray<int32>			PreAnimFrames;
	//
	TArray<FAnimData>		AnimDatas;
	//
	TArray<FClusterData>	ClusterData;
	//
	TArray<FUserData>		UserData;
	//holds per instance custom data float, see GetInstanceCustomDataFloats()
	TArray<float>			PerInstanceCustomData;
	//
	int32 MaxNumCustomDataFloat = 0;
	
	
	//just like UserData but used by blueprint, array might be empty, resized on demand,see SetInstanceUserObject
	UPROPERTY()
	TArray<TObjectPtr<UObject>> UserObjects;

	TArray<FMiscData>			MiscData;
	
	
};



USTRUCT(BlueprintType)
struct SKELOT_API FSkelotMeshRenderDesc
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category="Render", meta = (GetAssetFilter = MeshesShouldBeExcluded))
	TObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(EditAnywhere, Category = "Render")
	TArray<TObjectPtr<UMaterialInterface>> OverrideMaterials;
	
	UMaterialInterface* GetMaterial(int32 Index) const;

	bool operator ==(const FSkelotMeshRenderDesc& Other) const;

	inline friend uint32 GetTypeHash(const FSkelotMeshRenderDesc& Self) 
	{
		return GetTypeHash(Self.Mesh) ^ GetTypeHash(Self.OverrideMaterials);
	}

};


USTRUCT()
struct FSkelotCluster
{
	GENERATED_BODY()

	//
	FIntPoint Coord;
	//
	UPROPERTY(Transient)
	TObjectPtr<USKelotClusterComponent> Component = nullptr;
	//indices of skelot instances 
	TArray<int32> Instances;
	//
	uint32 KillCounter = 0;
	//true if any instances was add/removed to this cluster this frame
	uint32 bAnyAddRemove : 1 = true;
	//
	uint32 bTickedLastFrame : 1 = false;
	//true if this cluster is way far from main camera 
	uint32 bLowQuality : 1 = false;

	//we compare cluster by their Coordinate, used by TSet
	inline bool operator == (const FSkelotCluster& Other) const { return this->Coord == Other.Coord; }
	inline bool operator == (const FIntPoint& OtherTileCoord) const { return this->Coord == OtherTileCoord; }

	friend uint32 GetTypeHash(const FSkelotCluster& C) { return GetTypeHash(C.Coord); }
};



//render descriptor, contains properties to define how Skelot Instances should be rendered
USTRUCT(BlueprintType)
struct SKELOT_API FSkelotInstanceRenderDesc
{
	GENERATED_BODY()
	

	UPROPERTY(EditAnywhere, Category = "Rendering")
	TObjectPtr<USkelotAnimCollection> AnimCollection;
	UPROPERTY(EditAnywhere, Category = "Rendering")
	TArray<FSkelotMeshRenderDesc> Meshes;
	
	//true if the instance may have any negative scale, doesn't effect key 
	UPROPERTY(EditAnywhere, Category = "Rendering")
	uint8 bMayHaveNegativeDeterminant : 1;

	//try to pack your data into 2 floats. custom data are send as float4 and two are already taken by animation frames.
	UPROPERTY(EditAnywhere, Category = "Rendering")
	int32 NumCustomDataFloat;
	//see also GSkelot_ForcePerInstanceLocalBounds
	UPROPERTY(EditAnywhere, Category = "Rendering")
	uint8 bPerInstanceLocalBounds : 1;
	//
	uint8 bIsNegativeDeterminant : 1;
	UPROPERTY(EditAnywhere, Category = "Rendering")
	float MinDrawDistance;	
	UPROPERTY(EditAnywhere, Category = "Rendering")
	float MaxDrawDistance;
	UPROPERTY(EditAnywhere, Category = "Rendering")
	float LODScale;
	/** If true, will be rendered in the main pass (z prepass, basepass, transparency) */
	UPROPERTY(EditAnywhere, Category = Rendering)
	uint8 bRenderInMainPass:1;
	/** If true, will be rendered in the depth pass even if it's not rendered in the main pass */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (EditCondition = "!bRenderInMainPass"))
	uint8 bRenderInDepthPass:1;
	UPROPERTY(EditAnywhere, Category=Rendering)
	uint8 bReceivesDecals:1;
	UPROPERTY(EditAnywhere, Category=Rendering)
	uint8 bUseAsOccluder:1;
	UPROPERTY(EditAnywhere, Category = Rendering)
	uint8 bCastDynamicShadow:1;
	UPROPERTY(EditAnywhere, Category = Rendering)
	uint8 bRenderCustomDepth:1;
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (editcondition = "bRenderCustomDepth"))
	ERendererStencilMask CustomDepthStencilWriteMask;
	UPROPERTY(EditAnywhere, Category=Rendering,  meta=(UIMin = "0", UIMax = "255", editcondition = "bRenderCustomDepth"))
	int32 CustomDepthStencilValue;
	UPROPERTY(EditAnywhere, Category= Rendering)
	FLightingChannels LightingChannels;
	UPROPERTY(EditAnywhere, Category=Rendering, meta=(UIMin = "1", UIMax = "10.0"))
	float BoundsScale;
	
	//mutable FSkelotRenderDescKey CachedKey;
	//
	mutable TArray<int32> CachedMeshDefIndices;

	FSkelotInstanceRenderDesc();

	void CacheMeshDefIndices();

	int32 IndexOfMesh(TObjectPtr<USkeletalMesh> InMesh) const { return Meshes.IndexOfByPredicate([&](const FSkelotMeshRenderDesc& Elem) { return Elem.Mesh == InMesh; }); };

	void AddMesh(USkeletalMesh* InMesh);
	void RemoveMesh(USkeletalMesh* InMesh);

	uint32 GetPackedFlags() const;

	friend uint32 GetTypeHash(const FSkelotInstanceRenderDesc& In) { return In.ComputeHash(); }

	uint32 ComputeHash() const;

	bool Equal(const FSkelotInstanceRenderDesc& Other) const;

};

//////////////////////////////////////////////////////////////////////////
//data asset containing FSkelotInstanceRenderDesc, mostly used by BP
UCLASS(Blueprintable, Blueprintable)
class SKELOT_API USkelotRenderParams : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, meta=(ShowOnlyInnerProperties), Category = "Render")
	FSkelotInstanceRenderDesc Data;


	UFUNCTION()
	bool MeshesShouldBeExcluded(const FAssetData& AssetData) const;

#if WITH_EDITOR
	UFUNCTION(CallInEditor)
	void AddSelectedMeshes();
#endif

#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};


//final struct, contains runtime data too
USTRUCT()
struct SKELOT_API FSkelotInstanceRenderDescFinal : public FSkelotInstanceRenderDesc
{
	GENERATED_BODY()

	
	//
	uint32 KillCounter = 0;
	//
	UPROPERTY(Transient)
	TSet<FSkelotCluster> Clusters;

	FSkelotInstanceRenderDescFinal(){}
	FSkelotInstanceRenderDescFinal(const FSkelotInstanceRenderDesc& Copy) 
	{
		*static_cast<FSkelotInstanceRenderDesc*>(this) = Copy;
	}
	bool operator == (const FSkelotInstanceRenderDescFinal& Other) const { return Equal(Other); }
	bool operator == (const FSkelotInstanceRenderDesc& Other) const { return Equal(Other);  }
};



class ASkelotWorld_Impl;


//keeps relation data of a Skelot instance, used for attachment hierarchy. see InstanceAttachChild() DetachInstanceFromParent()
struct FSkelotAttachParentData
{
	FTransform3f RelativeTransform = FTransform3f::Identity; //most of the time not used
	SkelotInstanceIndex InstanceIndex = -1;
	SkelotInstanceIndex Parent = -1;	//instance index of the parent 
	SkelotInstanceIndex FirstChild = -1;  //-1 means it has no children
	SkelotInstanceIndex Down = -1;	//instance index of the next sibling
	int32 SocketBoneIndex = -1;	//resolved from socket name for fast access
	USkeletalMeshSocket* SocketPtr = nullptr; //not worried of GC since skeletal meshes are referenced already
};

struct FSkelotLifeSpan
{
	double DeathTime;
};



//keeps a delegate and its values
USTRUCT()
struct FSkelotInstanceGeneralDelegate
{
	GENERATED_BODY()

	using TimerDelegateT = TVariant<FSkelotGeneralDynamicDelegate, FSkelotGeneralDelegate>;
	//
	TimerDelegateT Delegate;
	//
	UPROPERTY()
	TObjectPtr<UObject> PayloadObject;
	//
	FName PayloadTag;
};

USTRUCT()
struct FSkelotInstanceTimerData 
{
	GENERATED_BODY()


	UPROPERTY()
	FSkelotInstanceGeneralDelegate Data;

	double FireTime;
	float Interval;
	bool bLoop;
	uint8 TimeIndex; //0 == GetRealTimeSeconds(), 1  == GetTimeSeconds()
};



