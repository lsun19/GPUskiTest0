// GPUSKI Plugin - World subsystem providing the Blueprint API

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GPUSKIWorldBase.h"

#include "GPUSKISubsystem.generated.h"

class AGPUSKIWorld;
class UGPUSKIAnimCollection;
class USkeletalMesh;

UCLASS()
class GPUSKI_API UGPUSKISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//----------------------------------------------------------------------
	// Instance Management
	//----------------------------------------------------------------------

	// Create a new GPUSKI instance with the given transform and render descriptor
	UFUNCTION(BlueprintCallable, Category = "GPUSKI")
	FGPUSKIInstanceHandle CreateInstance(const FTransform& Transform, UGPUSKIAnimCollection* AnimCollection, USkeletalMesh* Mesh);

	// Destroy an instance
	UFUNCTION(BlueprintCallable, Category = "GPUSKI")
	void DestroyInstance(UPARAM(ref) FGPUSKIInstanceHandle& Handle);

	// Check if a handle is still valid
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "GPUSKI")
	bool IsHandleValid(const FGPUSKIInstanceHandle& Handle) const;

	//----------------------------------------------------------------------
	// Animation
	//----------------------------------------------------------------------

	// Play an animation on an instance
	UFUNCTION(BlueprintCallable, Category = "GPUSKI")
	void PlayAnimation(const FGPUSKIInstanceHandle& Handle, UAnimSequenceBase* Animation, float PlayRate = 1.0f, bool bLoop = true, float StartAt = 0.0f);

	//----------------------------------------------------------------------
	// Transform
	//----------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "GPUSKI")
	void SetInstanceTransform(const FGPUSKIInstanceHandle& Handle, const FTransform& Transform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "GPUSKI")
	FTransform GetInstanceTransform(const FGPUSKIInstanceHandle& Handle) const;

private:
	AGPUSKIWorld* GetOrCreateWorld();

	UPROPERTY()
	TObjectPtr<AGPUSKIWorld> CachedWorld = nullptr;
};
