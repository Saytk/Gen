// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Gen : ModuleRules
{
	public Gen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Permet les includes du type "AbilitySystem/GenAttributeSet.h" depuis n'importe quel fichier du module
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Niagara"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
