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

	// Spawn conventional CPU skeletal meshes next to the GPU instances for comparison
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test|Comparison")
	bool bSpawnCPUComparison = false;

	// Material override for both GPU and CPU instances (leave empty to use mesh default)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GPUSKI Test")
	TObjectPtr<UMaterialInterface> MaterialOverride;

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
	void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	// Editor button to re-spawn at runtime (PIE)
#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "GPUSKI Test")
	void RespawnInstances();
#endif

private:
	void SpawnGPUInstances();
	void SpawnCPUInstances();
	void DestroyAllInstances();

	UPROPERTY()
	TArray<FGPUSKIInstanceHandle> GPUHandles;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> CPUActors;
};
