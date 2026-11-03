// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class Reprojection : ModuleRules
{
	public Reprojection(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"EnhancedInput",
			"HeadMountedDisplay", // UMotionControllerComponent, EHMDTrackingOrigin
			"XRBase",             // UHeadMountedDisplayFunctionLibrary
			"RenderCore",         // GGameThreadTime / GRenderThreadTime / GRHIThreadTime (perf stats)
			"RHI",                // RHIGetGPUFrameCycles, GPU readback (M2)
			"ProceduralMeshComponent", // M3 depth mesh
		});
	}
}
