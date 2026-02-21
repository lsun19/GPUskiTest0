// Copyright 2024 Lazy Marmot Games. All Rights Reserved.

#pragma once

#include "SkelotWorld.h"
#include "GameFramework/Character.h"

#include "SkelotCharacter.generated.h"

//a simple integration of Skelot into ACharacter
UCLASS()
class ACharacter_Skelot : public ACharacter
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category="Skelot")
	FSkelotInstanceHandle Handle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skelot")
	USkelotRenderParams* RenderParams;

	ACharacter_Skelot();

	UFUNCTION(BlueprintCallable, Category="Skelot|Rendering")
	void SetRenderParams(USkelotRenderParams* Params);
	UFUNCTION(BlueprintCallable, Category="Skelot|Rendering")
	void DestroySkelotInstance();

	void Tick(float DeltaSeconds) override;
	void BeginPlay() override;
};