using UnrealBuildTool;

// Tiny module that only maps the project's Shaders/ folder to the virtual path "/Reprojection".
// It must load at PostConfigInit (see .uproject), before any material compiles, so it is kept
// separate from the main game module.
public class ReprojectionShaders : ModuleRules
{
	public ReprojectionShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore" });
	}
}
