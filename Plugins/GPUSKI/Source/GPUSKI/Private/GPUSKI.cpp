// GPUSKI Plugin - GPU-driven Instanced Skeletal Mesh Rendering

#include "GPUSKI.h"

#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"
#include "Misc/CoreDelegates.h"

DEFINE_LOG_CATEGORY(LogGPUSKI);

#define LOCTEXT_NAMESPACE "FGPUSKIModule"

void FGPUSKIModule::StartupModule()
{
	// Register shader directory so the engine can find our .ush files
	AddShaderSourceDirectoryMapping(
		TEXT("/Plugin/GPUSKI"),
		FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("GPUSKI"))->GetBaseDir(), TEXT("Shaders"))
	);

	FCoreDelegates::OnBeginFrame.AddStatic(&FGPUSKIModule::OnBeginFrame);
	FCoreDelegates::OnEndFrame.AddStatic(&FGPUSKIModule::OnEndFrame);
}

void FGPUSKIModule::ShutdownModule()
{
}

void FGPUSKIModule::OnBeginFrame()
{
}

void FGPUSKIModule::OnEndFrame()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGPUSKIModule, GPUSKI)
