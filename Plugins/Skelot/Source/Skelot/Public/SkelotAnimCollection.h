// Copyright 2024 Lazy Marmot Games. All Rights Reserved.


#pragma once


#include "SkelotBase.h"
#include "Engine/DataAsset.h"
#include "Chaos/Core.h"

#include "AlphaBlend.h"
#include "BoneContainer.h"
#include "RenderCommandFence.h"
#include "UnifiedBuffer.h"
#include "AssetRegistry/AssetData.h"
#include "SpanAllocator.h"


#include "SkelotAnimCollection.generated.h"

class UAnimSequenceBase;
class USkelotAnimCollection;
class USkeletalMesh;
class USkeleton;
class UPhysicsAsset;
class UAnimNotify_Skelot;
class ISkelotNotifyInterface;


class FSkelotAnimCollectionUniformParams;

typedef uint16 SkelotTransitionIndex;
typedef int SkelotFrameIndex;

class FSkelotAnimationBuffer;
class FSkelotCurveBuffer;
class FSkelotMeshDataEx;

typedef TSharedPtr<FSkelotMeshDataEx, ESPMode::ThreadSafe> FSkelotMeshDataExPtr;

//describes a notification inside a AnimSeueqnce
USTRUCT()
struct SKELOT_API FSkelotAnimNotifyDesc
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	float Time = 0;
	UPROPERTY()
	FName Name;
	UPROPERTY()
	UAnimNotify* Notify = nullptr;
	//
	ISkelotNotifyInterface* NotifyInterface = nullptr;
};

USTRUCT(BlueprintType)
struct SKELOT_API FSkelotSequenceDef
{
	GENERATED_USTRUCT_BODY()

	//animation sequence 
	UPROPERTY(EditAnywhere, Category = "Skelot|AnimCollection", meta=(NoResetToDefault, GetAssetFilter = "Sequences_ShouldBeExcluded"))
	TObjectPtr<UAnimSequenceBase> Sequence;
	//number of samples in a second (less value occupies less VRAM)
	UPROPERTY(EditAnywhere, Category = "Skelot|AnimCollection", meta=(UIMin=10, UIMax=60, NoResetToDefault))
	int SampleFrequency;
	//base frame index of this sequence in animation buffer
	UPROPERTY(VisibleAnywhere, Transient, Category = "Skelot|AnimCollection")
	int AnimationFrameIndex;
	//number of frame generated
	UPROPERTY(VisibleAnywhere, Transient, Category = "Skelot|AnimCollection")
	int AnimationFrameCount;
	//we cant use engine notifications (UAnimNotify_PlaySound, etc) so we to have our own classes + its faster :|
	UPROPERTY(Transient)
	TArray<FSkelotAnimNotifyDesc> Notifies;


	FSkelotSequenceDef();

	//@return Local Frame Index
	int CalcFrameIndex(float SequencePlayTime) const;
	//
	float GetSequenceLength() const;
	//
	int CalcFrameCount() const;

};



/*
is filled from UPhysicsAsset for raycast support
#TODO maybe we can replace it with Chaos objects ?  ChaosInterface::CreateGeometry  ?
*/
struct SKELOT_API FSkelotCompactPhysicsAsset
{
	struct FShapeSphere
	{
		FVector Center;
		float Radius;
		int BoneIndex;
	};

	struct FShapeBox
	{
		FQuat Rotation;
		FVector Center;
		FVector BoxMin, BoxMax;
		int BoneIndex;
		bool bHasTransform;
	};

	struct FShapeCapsule
	{
		FVector A;
		FVector B;
		float Radius;
		int BoneIndex;
	};

	//most of the time physics asset are made of capsules and there is only 1 capsule per bone
	TArray<FShapeCapsule> Capsules;
	TArray<FShapeSphere> Spheres;
	TArray<FShapeBox> Boxes;


	void Init(const USkeleton* Skeleton, const UPhysicsAsset* PhysAsset);
	//test if any shape overlaps the specified point
	//return reference skeleton bone index of the first overlap (not the closest necessarily)
	int Overlap(const USkelotAnimCollection* AnimCollection, int AnimFrameIndex, const FVector& Point, Chaos::FReal Thickness) const;
	
	struct FRaycastParams
	{
		const USkelotAnimCollection* AnimCollection;
		int32 FrameIndex;
		TConstArrayView<int32> BonesToExclude;
		Chaos::FVec3 StartPoint;
		Chaos::FVec3 Direction;	//must be normalized
		Chaos::FReal Length; //length of ray
		Chaos::FReal Thickness;
		bool bReturnFirstHit;	//whether to just test if anything hit or get closest hit

		int32 OutBoneIndex = -1;
		Chaos::FReal OutTime;
		Chaos::FVec3 OutPosition;
		Chaos::FVec3 OutNormal;
	};

	void Raycast(FRaycastParams& InOutParams) const;
};

extern FArchive& operator <<(FArchive& Ar, FSkelotCompactPhysicsAsset& PA);
extern FArchive& operator <<(FArchive& Ar, FSkelotCompactPhysicsAsset::FShapeSphere& Shape);
extern FArchive& operator <<(FArchive& Ar, FSkelotCompactPhysicsAsset::FShapeBox& Shape);
extern FArchive& operator <<(FArchive& Ar, FSkelotCompactPhysicsAsset::FShapeCapsule& Shape);

USTRUCT(BlueprintType)
struct SKELOT_API FSkelotMeshDef
{
	GENERATED_USTRUCT_BODY()

	//SkeletalMesh to generate custom data for. skeletal meshes with same skeleton asset and equal reference pose can properly use same animation collection. in other words a short and a tall character can't be shared.
	UPROPERTY(EditAnywhere, Category = "Skelot|AnimCollection", meta=(GetAssetFilter=Meshes_ShouldBeExclude))
	TObjectPtr<USkeletalMesh> Mesh;
	//Minimum LOD Index to start. 
	UPROPERTY(EditAnywhere, Category = "Skelot|AnimCollection", meta=(DisplayName="Base LOD"))
	uint8 BaseLOD;
	//set -1 if this mesh should generate its own bounds, otherwise a valid index in Meshes array.
	//most of the time meshes are nearly identical in size so bounds can be shared.
	UPROPERTY(EditAnywhere, Category = "Skelot|AnimCollection")
	int8 OwningBoundMeshIndex;
	//
	UPROPERTY(EditAnywhere, Category = "Skelot|AnimCollection")
	FVector3f BoundExtent;
	
	//mesh data containing our Bone Indices and Vertex Factory
	FSkelotMeshDataExPtr MeshData;
	//empty if this mesh is using bounds of another FSkelotMeshDef
	//bounds generated from sequences, OwningBounds.Num() == AnimCollection->FrameCountSequences
	TArray<FBoxCenterExtentFloat> OwningBounds;
	//
	TArrayView<FBoxCenterExtentFloat> BoundsView;
	//
	FBoxMinMaxFloat MaxBBox;
	//
	FSkelotCompactPhysicsAsset CompactPhysicsAsset;
	//
	float SmallestBoundRadius = 0;

	FSkelotMeshDef();

	const FSkeletalMeshLODRenderData& GetBaseLODRenderData() const;

	void SerializeCooked(FArchive& Ar);
};


/*
* data asset that generates and keeps animation data. typically you need one per skeleton.
* #Note: animation sequences are generated at load time.

*/
UCLASS(BlueprintType)
class SKELOT_API USkelotAnimCollection : public UDataAsset
{
	GENERATED_BODY()

public:


	struct FPoseUploadData
	{
		TArray<uint32> ScatterData;		//value is animation frame index
		TArray<FMatrix3x4> PoseData;	//length is == ScatterData.Num() * RenderBoneCount
		TArray<FFloat16> CurvesValue;	//length is == ScatterData.Num() * CurveBuffer->NumCurve
	};

	struct FTransitionKey
	{
		union
		{
			struct
			{
				uint16 FromSI;	//index of sequence transition started from
				uint16 ToSI; //index of sequence transition blends to

				int FromFI;	//local frame index transition started from

				int ToFI; //local frame index transition blends to 

				uint16 FrameCount; //transition length in frame
				EAlphaBlendOption BlendOption;
				uint8 bFromLoops : 1; //true if source sequence was looping
				uint8 bToLoops : 1; //true if target sequence will loop
			};

			uint32 Packed[4];
		};

		FTransitionKey()
		{
			FMemory::Memzero(*this);
		}
		uint32 GetKeyHash() const
		{
			return static_cast<uint32>(FromSI ^ ToSI ^ FromFI ^ ToFI);
		}
		bool KeysEqual(const FTransitionKey& Other) const
		{
			return FMemory::Memcmp(this, &Other, sizeof(FTransitionKey)) == 0;
		}
	};
	static_assert(sizeof(FTransitionKey) == 16);

	struct FTransition : FTransitionKey
	{
		int RefCount = 1;
		uint32 BlockOffset = 0; //offset of block inside TransitionPoseAllocator
		int FrameIndex = 0; //animation buffer frame index
		uint16 DeferredIndex = 0xFFff; //index in DeferredTransitions
		uint16 StateIndex = 0xFFff; //index in ZeroRCTransitions or NegativeRCTransitions depending on RefCount

		bool IsDeferred() const { return DeferredIndex != 0xFFff; }
		//true if transition has no references and passed one more frame 
		bool IsUnused() const { return RefCount == -1; }
	};

	enum ETransitionResult
	{
		ETR_Failed_BufferFull,
		ETR_Failed_RateLimitReached,
		ETR_Success_Found,
		ETR_Success_NewlyCreated,
	};



	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<USkeleton> Skeleton;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<USkeletalMesh> RefPoseOverrideMesh;
	//
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (TitleProperty = "{Sequence}"))
	TArray<FSkelotSequenceDef> Sequences;

	//bones that we need their transform be available on memory. for attaching, sockets, ...
	UPROPERTY(EditAnywhere, Category = "Animation", meta = (GetOptions = "GetValidBones"))
	TSet<FName> BonesToCache;
	//Skeletal Meshes that need to be rendered by Skelot. (Custom Bone Index Buffer will be generated for them)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (TitleProperty = "{Mesh}"))
	TArray<FSkelotMeshDef> Meshes;


	UPROPERTY(EditAnywhere, Category = "Animation")
	bool bExtractRootMotion;
	//
	UPROPERTY(EditAnywhere, Category = "Animation", meta = (DisplayAfter = "BonesToCache"))
	bool bCachePhysicsAssetBones;
	//true if animation data should be kept as float32 instead of float16, with low precision you may see jitter in places like fingers
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	bool bHighPrecision;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	bool bDisableRetargeting;
	//generating bounding box for all animation frames may take up too much memory. if set to true uses biggest bound generated from all sequences. See also Skelot.DrawInstanceBounds 1
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	bool bDontGenerateBounds;
	//
	bool bNeedRebuild;
	//
	bool bIsBuilt;
	//
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info")
	int RenderBoneCount;
	//
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info")
	int AnimationBoneCount;
	//
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info")
	int TotalFrameCount;
	//number of pose generated for sequences
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info")
	int FrameCountSequences;
	//VRAM taken for animations
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info", meta = (Units = "Bytes"))
	int TotalAnimationBufferSize;
	//VRAM taken for custom bone indices
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info", meta = (Units = "Bytes"))
	int TotalMeshBonesBufferSize;
	//number of animation frames to be reserved for transitions. Transitions are generated and stored when requested. 
	UPROPERTY(EditAnywhere, Category = "Animation")
	int MaxTransitionPose = 2000;
	//number of animation frames to be reserved for dynamic instances
	UPROPERTY(EditAnywhere, Category = "Animation")
	int MaxDynamicPose;
	//following AnimNotifies won't be captured. (UAnimNotify_PlaySound, UAnimNotify_PlayNiagaraEffect and Name Only notification are converted to Skelot version by default)
	UPROPERTY(EditAnywhere, Category = "Animation")
	TArray<TSubclassOf<UAnimNotify>> ForbiddenNotifies;
	//animation curves to be sampled and stored in VRAM. value can be sampled by GetCurveValue() Material Function.
	UPROPERTY(EditAnywhere, Category = "Animation", meta = (GetOptions = "GetCurveNames"))
	TArray<FName> CurvesToCache;
	//
	UPROPERTY()
	FGuid LastEditId;
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info")
	int NumTransitionFrameAllocated;

	//
	//
	FRenderCommandFence ReleaseResourcesFence;
	//
	TUniquePtr<FSkelotAnimationBuffer> AnimationBuffer;
	TUniquePtr<FSkelotCurveBuffer> CurveBuffer;

	TUniformBufferRef<FSkelotAnimCollectionUniformParams> AnimCollectionUB;



	TArray<FTransform3f> CachedTransforms; //ComponentSpace cached bone transforms used for attaching
	TArray<FTransform3f*> SkeletonBoneIndexTo_CachedTransforms; //accessed by [skeleton bone index][animation frame index]
	TArray<FBoneIndexType /* Skeleton Bone Index */> BonesToCache_Indices;

	TArray<FTransform> RefPoseComponentSpace;//component space bones of our reference pose
	TArray<FMatrix44f> RefPoseInverse;//inverse of RefPoseComponentSpace

	TArray<FBoneIndexType> RenderRequiredBones;
	TArray<int> SkeletonBoneToRenderBone; //Skeleton bone index to Skelot Bone index for rendering

	FBoneContainer AnimationBoneContainer;
	//bounding box that covers maximum of all the meshes
	FBoxCenterExtentFloat MeshesBBox;


	TSparseArray<FTransition> Transitions;
	FHashTable TransitionsHashTable;
	FSpanAllocator TransitionPoseAllocator;
	TArray<SkelotTransitionIndex> ZeroRCTransitions;
	TArray<SkelotTransitionIndex> NegativeRCTransitions;
	//indices of transitions created this frame. will be generated concurrently at EndOfFrame 
	TArray<SkelotTransitionIndex> DeferredTransitions;
	uint32 DeferredTransitions_FrameCount;

	FSpanAllocator DynamicPoseAllocator;
	TBitArray<> DynamicPoseFlipFlags;

	FPoseUploadData CurrentUpload;
	FScatterUploadBuffer ScatterBuffer;	//used for uploading pose to GPU, index identifies animation frame index (can't upload single bone)






	USkelotAnimCollection();
	~USkelotAnimCollection();


	UFUNCTION()
	TArray<FName> GetValidBones() const;
	UFUNCTION()
	TArray<FName> GetCurveNames() const;
	UFUNCTION()
	bool Sequences_ShouldBeExcluded(const FAssetData& AssetData) const;
	UFUNCTION()
	bool Meshes_ShouldBeExclude(const FAssetData& AssetData) const;

	int32 FindSkeletonBoneIndex(FName BoneName) const;

	FName GetBoneName(int32 RefSkeletonBoneIndex) const;

	//
	bool HasAnyCurve() const { return CurvesToCache.Num() > 0; }

	bool IsBoneTransformCached(int SkeletonBoneIndex) const
	{
		return SkeletonBoneIndexTo_CachedTransforms.IsValidIndex(SkeletonBoneIndex) && SkeletonBoneIndexTo_CachedTransforms[SkeletonBoneIndex];
	}
	//
	FTransform3f& GetBoneTransformFast(uint32 SkeletonBoneIndex, uint32 FrameIndex)
	{
		return SkeletonBoneIndexTo_CachedTransforms[SkeletonBoneIndex][FrameIndex * static_cast<uint32>(this->BonesToCache_Indices.Num())];
	}
	//
	const FTransform3f& GetBoneTransformFast(uint32 SkeletonBoneIndex, uint32 FrameIndex) const
	{
		return const_cast<USkelotAnimCollection*>(this)->GetBoneTransformFast(SkeletonBoneIndex, FrameIndex);
	}
	//
	const FTransform3f& GetBoneTransform(uint32 SkeletonBoneIndex, uint32 FrameIndex);


	void PostInitProperties() override;
	void PostLoad() override;
#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void PreEditChange(FProperty* PropertyThatWillChange) override;
	bool IsObjectRelatedToThis(const UObject* Other) const;
	void ObjectModifiedEvent(UObject* Object);
#endif

	bool IsPostLoadThreadSafe() const override { return false; }
	void BeginDestroy() override;
	bool IsReadyForFinishDestroy() override;
	void FinishDestroy() override;
	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	bool CanBeClusterRoot() const override { return false; }
	void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform);
	void ClearAllCachedCookedPlatformData();
#endif



#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "Animation")
	void Rebuild();
	UFUNCTION(CallInEditor, Category = "Animation")
	void AddAllMeshes();
	UFUNCTION(CallInEditor, Category = "Animation")
	void AddAllAnimations();
	UFUNCTION(CallInEditor, Category = "Animation")
	void AddSelectedAssets();

	UFUNCTION(CallInEditor, Category = "Animation")
	void AddAllCurves();
	UFUNCTION(CallInEditor, Category = "Animation")
	void RemoveInvalidAssets();

	void AddMeshUnique(const FAssetData& InAssetData);
	void AddAnimationUnique(const FAssetData& InAssetData);


#endif


	FString GetSkeletonTagValue() const;
	bool IsAssetDataSkeletonCompatible(const FAssetData& AssetData) const;

	void DestroyBuildData();
	void TryBuildAll();
	void InternalBuildAll();

	int FindSequenceDef(const UAnimSequenceBase* animation) const;
	int FindSequenceDefByPath(const FSoftObjectPath& AnimationPath) const;
	int FindMeshDef(const USkeletalMesh* Mesh) const;
	int FindMeshDefByPath(const FSoftObjectPath& MeshPath) const;

	FSkelotMeshDataExPtr FindMeshData(const USkeletalMesh* mesh) const;

	UFUNCTION(BlueprintCallable, Category = "Skelot|AnimCollection")
	UAnimSequenceBase* GetRandomAnimSequenceFromStream(const FRandomStream& RandomSteam) const;
	UFUNCTION(BlueprintCallable, Category = "Skelot|AnimCollection")
	UAnimSequenceBase* GetRandomAnimSequence() const;
	UFUNCTION(BlueprintCallable, Category = "Skelot|AnimCollection")
	UAnimSequenceBase* FindAnimByName(const FString& ContainingName);
	UFUNCTION(BlueprintCallable, Category = "Skelot|AnimCollection")
	USkeletalMesh* FindMeshByName(const FString& ContainingName);
	UFUNCTION(BlueprintCallable, Category = "Skelot|AnimCollection")
	USkeletalMesh* GetRandomMesh() const;
	UFUNCTION(BlueprintCallable, Category = "Skelot|AnimCollection")
	USkeletalMesh* GetRandomMeshFromStream(const FRandomStream& RandomSteam) const;

	void InitMeshDataResources(FRHICommandListBase& RHICmdList);
	void ReleaseMeshDataResources();


	void PostDuplicate(bool bDuplicateForPIE) override;
#if WITH_EDITOR
	FString GetDDCKeyForMeshBoneIndex(const FSkelotMeshDef& MeshDef) const;
#endif
	TConstArrayView<FBoxCenterExtentFloat> GetMeshBounds(int MeshDefIndex) const
	{
		return Meshes[MeshDefIndex].BoundsView;
	}

	int CalcFrameIndex(const FSkelotSequenceDef& SequenceStruct, float SequencePlayTime) const;
	//
	void InitSkeletonData();


#pragma region Transition

	

	int ReserveUploadData(int FrameCount);

	void UploadDataSetNumUninitialized(int N);

	void RemoveUnusedTransition(SkelotTransitionIndex UnusedTI);
	void RemoveAllUnusedTransitions();

	int32 AllocTransitionPose(int32 FrameCount);

	TPair<int,ETransitionResult> FindOrCreateTransition(const FTransitionKey& Key, bool bIgonreTransitionGeneration);
	//increase transition refcount
	void IncTransitionRef(SkelotTransitionIndex TransitionIndex);
	//decrease transition refcount and fill TransitionIndex with invalid index
	void DecTransitionRef(SkelotTransitionIndex& TransitionIndex);
	//
	void ReleasePendingTransitions();
	//
	void GenerateTransition_Concurrent(uint32 TransitionIndex, uint32 ScatterIdx);
	//
	void FlushDeferredTransitions();
	//
	bool HasAnyDeferredTransitions() const { return this->DeferredTransitions.Num() > 0; }
	//flush transitions if FrameIndex is in transition range 
	void ConditionalFlushDeferredTransitions(int FrameIndex) 
	{
		if(HasAnyDeferredTransitions() && IsTransitionFrameIndex(FrameIndex))
			FlushDeferredTransitions();
	}
	bool IsAnimationFrameIndex(int FrameIndex) const	{ return FrameIndex > 0 && FrameIndex < FrameCountSequences; }
	bool IsTransitionFrameIndex(int FrameIndex) const	{ return FrameIndex >= FrameCountSequences && FrameIndex < (FrameCountSequences + MaxTransitionPose); }
	bool IsDynamicPoseFrameIndex(int FrameIndex) const	{ return FrameIndex >= (FrameCountSequences + MaxTransitionPose) && FrameIndex < TotalFrameCount; }
	bool IsFrameIndexValid(int FrameIndex) const		{ return FrameIndex >= 0 && FrameIndex < TotalFrameCount; }

	void ApplyScatterBufferRT(FRHICommandList& RHICmdList, const FPoseUploadData& UploadData);

	FMatrix3x4* RequestPoseUpload(int FrameInde);
	void RequestPoseUpload(int FrameInde, const TArrayView<FMatrix3x4> PoseTransforms);

	int32 FrameIndexToDynamicPoseIndex(int32 FrameIndex) const { return (FrameIndex - (FrameCountSequences + MaxTransitionPose)) / 2; }
	int32 DynamicPoseIndexToFrameIndex(int32 DynamicPoseIndex) { return (FrameCountSequences + MaxTransitionPose) + (DynamicPoseIndex * 2) + (DynamicPoseFlipFlags[DynamicPoseIndex] ? 1 : 0); }
	
	//flip the flag of dynamic pose and return new animation frame index
	int32 FlipDynamicPoseSign(int32 DynamicPoseIndex)
	{
		DynamicPoseFlipFlags[DynamicPoseIndex] = !DynamicPoseFlipFlags[DynamicPoseIndex];
		return DynamicPoseIndexToFrameIndex(DynamicPoseIndex);
	}
	int32 AllocDynamicPose();
	void FreeDynamicPose(int32 DynamicPoseIndex)
	{
		DynamicPoseAllocator.Free(DynamicPoseIndex);
	}

	#pragma endregion

	void CachePose(int PoseIndex, const TArrayView<FTransform>& PoseComponentSpace);
	void CachePoseBounds(int PoseIndex, const TArrayView<FTransform>& PoseComponentSpace);
	void CachePoseBones(int PoseIndex, const TArrayView<FTransform>& PoseComponentSpace);

	bool CheckCanBuild(FString& OutError) const;
	bool BuildData();
	bool BuildAnimationData();
	void BuildSequence(int SequenceIndex);
	void BuildMeshData();


	void InitBoneContainer();
	void InitRefPoseOverride();

	FBox CalcPhysicsAssetBound(const UPhysicsAsset* PhysAsset, const TArrayView<FTransform>& PoseComponentSpace, bool bConsiderAllBodiesForBounds);

	void CalcRenderMatrices(const TArrayView<FTransform> PoseComponentSpace, FMatrix3x4* OutMatrices) const;
	void CalcRenderMatrices(const TArrayView<FTransform> PoseComponentSpace, FMatrix3x4Half* OutMatrices) const;
	
	uint32 GetRenderMatrixSize() const;

	void EnqueueReleaseResources();

	void OnPreSendAllEndOfFrameUpdates(UWorld* World);
	void OnEndFrame();
	void OnBeginFrame();
	void OnEndFrameRT();
	void OnBeginFrameRT();

	void ConvertNotification(const FAnimNotifyEvent& Notify, FSkelotSequenceDef& SD);
};
