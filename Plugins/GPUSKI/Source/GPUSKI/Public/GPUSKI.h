// GPUSKI Plugin - GPU-driven Instanced Skeletal Mesh Rendering

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Stats/Stats.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGPUSKI, Log, All);

DECLARE_STATS_GROUP(TEXT("GPUSKI"), STATGROUP_GPUSKI, STATCAT_Advanced);

class FGPUSKIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void OnBeginFrame();
	static void OnEndFrame();
};

static constexpr int GPUSKI_MAX_LOD = 8;
static constexpr int GPUSKI_MAX_BONE_INFLUENCE = 8;
