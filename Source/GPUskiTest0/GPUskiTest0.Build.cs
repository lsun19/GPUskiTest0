// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GPUskiTest0 : ModuleRules
{
	public GPUskiTest0(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
	}
}
