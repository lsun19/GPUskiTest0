// GPUSKI Plugin - Test actor for quick drag-and-drop testing and CPU vs GPU comparison

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GPUSKIWorldBase.h"

#include "GPUSKITestActor.generated.h"

class UGPUSKIAnimCollection;
class USkeletalMesh;
class UAnimSequenceBase;
class UGPUSKISubsystem;

UENUM(BlueprintType)
enum class EGPUSKITestSpawnMode : uint8
{
	GPUOnly			UMETA(DisplayName = "GPU Only"),
	CPUOnly			UMETA(DisplayName = "CPU Only"),
	Comparison		UMETA(DisplayName = "GPU + CPU Comparison"),
};

UCLASS(BlueprintType, meta = (DisplayName = "GPUSKI Test Actor"))
class GPUSKI_API AGPUSKITestActor : public AActor
{
	GENERATED_BODY()

public:
	//----------------------------------------------------------------------
	// Configuration (set in Details panel)
	//----------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test")
	TObjectPtr<UGPUSKIAnimCollection> AnimCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test")
	TObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test")
	TObjectPtr<UAnimSequenceBase> Animation;

	// Number of instances along each axis (total = Rows * Columns)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test", meta = (UIMin = 1, UIMax = 200))
	int32 Rows = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test", meta = (UIMin = 1, UIMax = 200))
	int32 Columns = 10;

	// Distance between instances (cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test", meta = (UIMin = 50))
	float Spacing = 200.0f;

	// Randomize animation start time so instances don't animate in sync
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test")
	bool bRandomizeStartTime = true;

	// Choose which instances to spawn:
	//  GPUOnly    — GPUSKI instances only (default; transitions apply)
	//  CPUOnly    — conventional USkeletalMeshComponent actors only (no GPU, no transitions)
	//  Comparison — both, side by side (CPU group offset on -Y for visual comparison)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Comparison")
	EGPUSKITestSpawnMode SpawnMode = EGPUSKITestSpawnMode::GPUOnly;

	// Material override for both GPU and CPU instances (leave empty to use mesh default)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test")
	TObjectPtr<UMaterialInterface> MaterialOverride;

	//----------------------------------------------------------------------
	// Transition Testing
	//----------------------------------------------------------------------

	// Enable periodic blended transitions between Animation and AnimationB
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Transition")
	bool bEnableTransition = false;

	// Second animation to alternate with the primary Animation (A/B mode, used when AnimationPool is empty)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Transition",
		meta = (EditCondition = "bEnableTransition"))
	TObjectPtr<UAnimSequenceBase> AnimationB;

	// Blend duration in seconds between animations
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Transition",
		meta = (EditCondition = "bEnableTransition", UIMin = 0.0, UIMax = 2.0))
	float TransitionBlendTime = 0.3f;

	// How long to hold each animation before switching (A/B mode only)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Transition",
		meta = (EditCondition = "bEnableTransition", UIMin = 0.1))
	float TransitionInterval = 2.0f;

	// Pool mode: 2+ animations here activates per-instance independent random transitions.
	// Each instance picks a random animation from the pool and transitions on its own schedule.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Transition",
		meta = (EditCondition = "bEnableTransition"))
	TArray<TObjectPtr<UAnimSequenceBase>> AnimationPool;

	// Minimum seconds an instance holds an animation before transitioning (pool mode)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Transition",
		meta = (EditCondition = "bEnableTransition", UIMin = 0.1))
	float TransitionIntervalMin = 1.5f;

	// Maximum seconds an instance holds an animation before transitioning (pool mode)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Transition",
		meta = (EditCondition = "bEnableTransition", UIMin = 0.1))
	float TransitionIntervalMax = 3.0f;

	//----------------------------------------------------------------------
	// Animation Notifies
	//----------------------------------------------------------------------

	// Log every animation notify fired by this actor's GPU instances to the output log.
	// Useful for smoke-testing notify events on a new AnimCollection. GPU-only feature.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Notifies")
	bool bLogAnimNotifies = false;

	//----------------------------------------------------------------------
	// Runtime info (read-only)
	//----------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, Transient, Category = "GPUSKI Test|Info")
	int32 GPUInstanceCount = 0;

	UPROPERTY(VisibleAnywhere, Transient, Category = "GPUSKI Test|Info")
	int32 CPUInstanceCount = 0;

	//----------------------------------------------------------------------
	// Lifecycle
	//----------------------------------------------------------------------

	AGPUSKITestActor();

	void BeginPlay() override;
	void Tick(float DeltaSeconds) override;
	void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	// Editor button to re-spawn at runtime (PIE)
#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "GPUSKI Test")
	void RespawnInstances();
#endif

	UFUNCTION()
	void HandleAnimNotify(FGPUSKIInstanceHandle Handle, FName NotifyName);

private:
	void SpawnGPUInstances();
	void SpawnCPUInstances();
	void DestroyAllInstances();
	void OnTransitionTimer();

	// Track whether we own a binding on UGPUSKISubsystem::OnAnimNotify so we only remove ours.
	bool bNotifyHandlerBound = false;

	UPROPERTY()
	TArray<FGPUSKIInstanceHandle> GPUHandles;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> CPUActors;

	FTimerHandle TransitionTimerHandle;
	bool bTransitionOnB = false;

	// Pool mode per-instance state (parallel to GPUHandles)
	TArray<float> InstanceNextTransitionTimes;
	TArray<int32> InstanceCurrentAnimIndices;
};
