#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Projectile.h"

/**
 * Gen.Assets.BasicAttackFlag (revue finale, I-2) : contrat des assets de sort. Les tirs de base de Curffe (GA_Fireball,
 * GA_Pyroblast) déclarent bIsBasicAttack : sans lui, UGenGA_Projectile::WantsAimIndicator dessine une ligne de visée à
 * chaque tir du clic gauche (Art Bible §7.2 : un sort de base n'a pas de télégraphe).
 * Le drapeau se pose dans l'éditeur (étape d'asset) : ce test échoue tant que les deux assets ne l'ont pas.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenAssetBasicAttackFlagTest, "Gen.Assets.BasicAttackFlag",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenAssetBasicAttackFlagTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* AssetName : { TEXT("GA_Fireball"), TEXT("GA_Pyroblast") })
	{
		const FString Path = FString::Printf(TEXT("/Game/Gen/Champions/Curffe/Abilities/%s.%s_C"), AssetName, AssetName);
		const UClass* Class = LoadClass<UGameplayAbility>(nullptr, *Path);
		const UGenGA_Projectile* Ability = Class ? Cast<UGenGA_Projectile>(Class->GetDefaultObject()) : nullptr;
		if (TestNotNull(FString::Printf(TEXT("%s (UGenGA_Projectile)"), AssetName), Ability))
		{
			TestTrue(FString::Printf(TEXT("%s : bIsBasicAttack (à poser dans l'éditeur)"), AssetName), Ability->IsBasicAttack());
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
