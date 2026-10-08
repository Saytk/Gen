using UnrealBuildTool;

/**
 * Tests automatisés éditeur uniquement (CQTest), en particulier les tests réseau
 * serveur dédié + N clients dans un seul process (FPIENetworkComponent).
 * Chargé seulement par la cible GenEditor (jamais dans le jeu packagé).
 */
public class GenTests : ModuleRules
{
	public GenTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Permet les includes du type "Net/GenNetTestHelpers.h"
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Private"));

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"NetCore",
			"CQTest",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Gen"
		});

		if (Target.bBuildEditor)
		{
			// Requis par Components/PIENetworkComponent.h (Editor.h, UnrealEdGlobals.h, GameMapsSettings)
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"EngineSettings",
				"LevelEditor"
			});
		}

		// PIENetworkComponent.h inclut les en-têtes Iris (ReplicationSystem, ObjectReplicationBridge)
		SetupIrisSupport(Target);
	}
}
