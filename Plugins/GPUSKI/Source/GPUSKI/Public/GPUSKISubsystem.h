// GPUSKI Plugin - World subsystem providing the Blueprint API

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GPUSKIWorldBase.h"

#include "GPUSKISubsystem.generated.h"

class AGPUSKIWorld;
class UGPUSKIAnimCollection;
class USkeletalMesh;

// Global animation notify delegate. Fires once per instance × notify crossing, per frame.
// Bind at the subsystem and filter by Handle if you only care about a subset of instances.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGPUSKIAnimNotify,
	FGPUSKIInstanceHandle, Handle, FName, NotifyName);

UCLASS()
class GPUSKI_API UGPUSKISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Broadcast once per instance × instant-notify crossing per tick. Handlers must not assume
	// the instance still exists on reentry; call IsHandleValid first if you might destroy others.
	UPROPERTY(BlueprintAssignable, Category = "GPUSKI|Animation")
	FOnGPUSKIAnimNotify OnAnimNotify;

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

	// Play an animation on an instance. BlendInTime > 0 smoothly transitions from the current animation.
	UFUNCTION(BlueprintCallable, Category = "GPUSKI")
	void PlayAnimation(const FGPUSKIInstanceHandle& Handle, UAnimSequenceBase* Animation, float PlayRate = 1.0f, bool bLoop = true, float StartAt = 0.0f, float BlendInTime = 0.0f);

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
