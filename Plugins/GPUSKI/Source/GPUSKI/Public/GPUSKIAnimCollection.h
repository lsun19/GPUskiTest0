// GPUSKI Plugin - Animation Collection data asset

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GPUSKIBase.h"
#include "GPUSKI.h"
#include "RenderCommandFence.h"
#include "BoneContainer.h"

#include "GPUSKIAnimCollection.generated.h"

class UAnimSequenceBase;
class USkeleton;
class USkeletalMesh;
class FGPUSKIAnimationBuffer;
class FGPUSKIMeshDataEx;
class FGPUSKIAnimCollectionUniformParams;

typedef TSharedPtr<FGPUSKIMeshDataEx, ESPMode::ThreadSafe> FGPUSKIMeshDataExPtr;

//----------------------------------------------------------------------
// Sequence Definition
//----------------------------------------------------------------------
USTRUCT(BlueprintType)
struct GPUSKI_API FGPUSKISequenceDef
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "GPUSKI")
	TObjectPtr<UAnimSequenceBase> Sequence;

	// Samples per second (10-60). Lower = less VRAM but less smooth
	UPROPERTY(EditAnywhere, Category = "GPUSKI", meta = (UIMin = 10, UIMax = 60))
	int SampleFrequency = 30;

	// Base frame index of this sequence in the animation buffer (computed during build)
	UPROPERTY(VisibleAnywhere, Transient, Category = "GPUSKI")
	int AnimationFrameIndex = 0;

	// Number of frames generated for this sequence (computed during build)
	UPROPERTY(VisibleAnywhere, Transient, Category = "GPUSKI")
	int AnimationFrameCount = 0;

	FGPUSKISequenceDef() {}

	// Calculate the animation buffer frame index for a given play time
	int CalcFrameIndex(float SequencePlayTime) const;

	// Get the total length of the animation in seconds
	float GetSequenceLength() const;

	// Calculate how many frames this sequence will produce
	int CalcFrameCount() const;
};

//----------------------------------------------------------------------
// Mesh Definition
//----------------------------------------------------------------------
USTRUCT(BlueprintType)
struct GPUSKI_API FGPUSKIMeshDef
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "GPUSKI")
	TObjectPtr<USkeletalMesh> Mesh;

	// Minimum LOD index to start from
	UPROPERTY(EditAnywhere, Category = "GPUSKI", meta = (DisplayName = "Base LOD"))
	uint8 BaseLOD = 0;

	// Runtime: custom bone index buffer + vertex factories per LOD
	FGPUSKIMeshDataExPtr MeshData;

	// Bounding boxes per animation frame (for culling)
	TArray<GPUSKI::FBoxCenterExtent> OwningBounds;

	FGPUSKIMeshDef() {}
};

//----------------------------------------------------------------------
// Animation Collection
//----------------------------------------------------------------------
UCLASS(BlueprintType)
class GPUSKI_API UGPUSKIAnimCollection : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<USkeleton> Skeleton;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (TitleProperty = "{Sequence}"))
	TArray<FGPUSKISequenceDef> Sequences;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (TitleProperty = "{Mesh}"))
	TArray<FGPUSKIMeshDef> Meshes;

	// Use float32 instead of float16 for bone matrices (more VRAM but higher precision)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	bool bHighPrecision = false;

	//----------------------------------------------------------------------
	// Computed data (transient)
	//----------------------------------------------------------------------

	// Number of render bones (subset of skeleton bones used for rendering)
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info")
	int RenderBoneCount = 0;

	// Total number of frames across all sequences
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info")
	int TotalFrameCount = 0;

	// VRAM size of animation buffer
	UPROPERTY(VisibleAnywhere, Transient, Category = "Info", meta = (Units = "Bytes"))
	int TotalAnimationBufferSize = 0;

	//----------------------------------------------------------------------
	// Runtime GPU resources
	//----------------------------------------------------------------------

	FGPUSKIAnimationBuffer* AnimationBuffer = nullptr;
	TUniformBufferRef<FGPUSKIAnimCollectionUniformParams> AnimCollectionUB;

	// Bone mapping arrays
	TArray<FBoneIndexType> RenderRequiredBones;
	TArray<int> SkeletonBoneToRenderBone; // Skeleton bone index -> render bone index

	// Reference pose
	TArray<FTransform> RefPoseComponentSpace;
	TArray<FMatrix44f> RefPoseInverse;

	FBoneContainer AnimationBoneContainer;

	FRenderCommandFence ReleaseResourcesFence;

	bool bIsBuilt = false;

	//----------------------------------------------------------------------
	// Lifecycle
	//----------------------------------------------------------------------

	UGPUSKIAnimCollection();
	~UGPUSKIAnimCollection();

	void PostInitProperties() override;
	void PostLoad() override;
	void BeginDestroy() override;
	bool IsReadyForFinishDestroy() override;

	//----------------------------------------------------------------------
	// Build pipeline
	//----------------------------------------------------------------------

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "Animation")
	void Rebuild();
#endif

	bool BuildData();
	bool BuildAnimationData();
	void BuildMeshData();
	void InitSkeletonData();

	// Find sequence/mesh by asset
	int FindSequenceDef(const UAnimSequenceBase* Animation) const;
	int FindMeshDef(const USkeletalMesh* Mesh) const;
	FGPUSKIMeshDataExPtr FindMeshData(const USkeletalMesh* Mesh) const;

	// Init/release GPU resources
	void InitMeshDataResources(FRHICommandListBase& RHICmdList);
	void ReleaseMeshDataResources();

	// Calculate render matrices for a pose
	void CalcRenderMatrices(const TArrayView<FTransform> PoseComponentSpace, FMatrix3x4* OutMatrices) const;
	void CalcRenderMatrices(const TArrayView<FTransform> PoseComponentSpace, FGPUSKIMatrix3x4Half* OutMatrices) const;

	uint32 GetRenderMatrixSize() const;
};
