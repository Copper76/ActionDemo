// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ActionDemo : ModuleRules
{
	public ActionDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "EnhancedInput", "CableComponent", "UMG", "OnlineSubsystem", "OnlineSubsystemUtils", "DeveloperSettings" });

		PrivateDependencyModuleNames.AddRange(new string[] { "ApplicationCore", "CableComponent", "Slate", "SlateCore" });

		SetupModulePhysicsSupport(Target);
    }
}
