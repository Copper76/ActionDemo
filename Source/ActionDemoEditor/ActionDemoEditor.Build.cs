// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ActionDemoEditor : ModuleRules
{
	public ActionDemoEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ActionDemo", "Core", "CoreUObject", "Engine", "InputCore"
		});
	}
}
