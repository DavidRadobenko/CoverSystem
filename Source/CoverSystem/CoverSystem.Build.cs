// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CoverSystem : ModuleRules
{
	public CoverSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Landscape", "Navmesh", "NavigationSystem" });
		PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "GameplayTasks"});

		PublicDefinitions.Add("DEBUG_RENDERING=!(UE_BUILD_SHIPPING || UE_BUILD_TEST) || WITH_EDITOR");
	}
}
