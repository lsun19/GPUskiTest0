using UnrealBuildTool;

public class GPUSKI : ModuleRules
{
	public GPUSKI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;

		PrivateIncludePaths.AddRange(new string[] {
			System.IO.Path.Combine(GetModuleDirectory("Renderer"), "Private"),
		});

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"RenderCore",
				"Engine",
				"Renderer",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Projects",
				"RHI",
				"DeveloperSettings",
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd" });
		}

		PublicDefinitions.AddRange(new string[] {
			"GPUSKI_WITH_EXTRA_BONE=1",
			"GPUSKI_WITH_MANUAL_VERTEX_FETCH=1",
		});
	}
}
