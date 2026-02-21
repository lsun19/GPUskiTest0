// Copyright 2024 Lazy Marmot Games. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "AlphaBlend.h"
#include "SkelotWorldBase.h"

#include "SkelotInstanceComponent.generated.h"

class USkelotComponent;
class USkelotRenderParams;
class USkelotAnimCollection;

/*
Helper component that represent an Skelot instance. Transform is copied from the Component to Skelot Instance. 

#Note tick must be enabled if children are attached by socket
*/
UCLASS(BlueprintType, editinlinenew, meta = (BlueprintSpawnableComponent))
class SKELOT_API USkelotInstanceComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category="Skelot")
	FSkelotInstanceHandle Handle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skelot")
	USkelotRenderParams* RenderParams;

	USkelotInstanceComponent();


	void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
	void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	void OnRegister() override;
	void OnUnregister() override;
	void EndPlay(EEndPlayReason::Type Reason) override;
	FTransform GetSocketTransform(FName SocketName, ERelativeTransformSpace TransformSpace) const override;
	void QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const override;
	bool DoesSocketExist(FName InSocketName) const;
	bool HasAnySockets() const;
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	

	UFUNCTION(BlueprintCallable, Category="Skelot|Rendering")
	void SetRenderParams(USkelotRenderParams* Params);
	UFUNCTION(BlueprintCallable, Category="Skelot|Rendering")
	void DestroySkelotInstance();


#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void PreEditChange(FProperty* PropertyThatWillChange) override;
#endif

	

};