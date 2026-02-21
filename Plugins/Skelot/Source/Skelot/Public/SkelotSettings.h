// Copyright 2024 Lazy Marmot Games. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettingsBackedByCVars.h"
#include "CoreMinimal.h"

#include "SkelotSettings.generated.h"

class ASkelotWorld;

UCLASS(Config = Skelot, defaultconfig, meta = (DisplayName = "Skelot"))
class SKELOT_API USkelotDeveloperSettings : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()
public:

	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	int32 MaxTransitionGenerationPerFrame;
	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	uint32 ClusterLifeTime = 600;
	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	uint32 RenderDescLifetime = 60;
	//
	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	int32 HighQualityRange = 2;

	UPROPERTY(Config, EditAnywhere, Category = "Settings", meta=(MetaClass = "SkelotWorld"))
	FSoftClassPath SkelotWorldClass;

	

	USkelotDeveloperSettings(const FObjectInitializer& Initializer);
};