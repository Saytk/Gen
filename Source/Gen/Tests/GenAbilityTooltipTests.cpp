#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Counter.h"
#include "AbilitySystem/Abilities/GenGA_GroundArea.h"
#include "AbilitySystem/Abilities/GenGA_Leap.h"
#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/GenAbilityTooltipData.h"
#include "Actors/GenProjectile.h"
#include "Champions/Curffe/CurffeGA_Combustion.h"
#include "Champions/Curffe/CurffeGA_LivingFlame.h"
#include "Champions/Curffe/CurffeGA_MeteorLeap.h"
#include "Champions/Curffe/CurffeTuning.h"

/**
 * Gen.UI.AbilityTooltip : l'infobulle de chaque sort de Curffe, générée depuis son CDO (assets GA_* du dépôt, ou classe
 * native avec les valeurs de la spec). Les nombres attendus sont relus sur le CDO, par les mêmes fonctions que le jeu
 * (GetShotDamage, GetAreaRadius...) : un réglage d'asset ne casse pas le test, une infobulle qui ment, si.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenAbilityTooltipTest, "Gen.UI.AbilityTooltip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace GenAbilityTooltipTest
{
	template <typename T>
	const T* LoadCurffeAbility(const TCHAR* AssetName)
	{
		const FString Path = FString::Printf(TEXT("/Game/Gen/Champions/Curffe/Abilities/%s.%s_C"), AssetName, AssetName);
		const UClass* Class = LoadClass<UGameplayAbility>(nullptr, *Path);
		return Class ? Cast<T>(Class->GetDefaultObject()) : nullptr;
	}

	FString Build(const UGenGameplayAbility& Ability, FGenAbilityTooltipData& Out)
	{
		GenAbilityTooltip::Build(Ability, Out);
		return Out.ToString();
	}

	/** « 44 dégâts » dans la culture courante. */
	FString DamageText(float Damage)
	{
		return FString::Printf(TEXT("%s dégâts"), *GenAbilityTooltip::Number(Damage).ToString());
	}
}

bool FGenAbilityTooltipTest::RunTest(const FString& Parameters)
{
	using namespace GenAbilityTooltipTest;

	// Nombres dans la culture courante, unités, sans zéro inutile
	TestEqual(TEXT("44 -> AsNumber"), GenAbilityTooltip::Number(44.f).ToString(), FText::AsNumber(44).ToString());
	TestEqual(TEXT("0.4 s"), GenAbilityTooltip::Seconds(0.4f).ToString(), FString::Printf(TEXT("%s s"), *FText::AsNumber(0.4f).ToString()));
	TestEqual(TEXT("350 cm -> 3.5 m"), GenAbilityTooltip::Meters(350.f).ToString(), FString::Printf(TEXT("%s m"), *FText::AsNumber(3.5f).ToString()));

	// --- Boule de feu (asset) : incantation et dégâts de l'asset ---
	if (const UGenGA_Projectile* Fireball = LoadCurffeAbility<UGenGA_Projectile>(TEXT("GA_Fireball")); TestNotNull(TEXT("GA_Fireball"), Fireball))
	{
		FGenAbilityTooltipData Data;
		const FString Text = Build(*Fireball, Data);
		AddInfo(FString::Printf(TEXT("GA_Fireball : incantation %.2f s, dégâts %.1f"), Fireball->GetCastTime(), Fireball->GetShotDamage(0)));
		TestFalse(TEXT("Boule de feu : nom"), Data.Name.IsEmpty());
		TestTrue(TEXT("Boule de feu : incantation de l'asset"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Seconds(Fireball->GetCastTime()).ToString()));
		TestTrue(TEXT("Boule de feu : dégâts de l'asset"), Text.Contains(DamageText(Fireball->GetShotDamage(0))));
		TestTrue(TEXT("Boule de feu : portée du projectile"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Meters(Fireball->GetTooltipRange()).ToString()));
	}

	// --- Grande boule de feu (asset) : un seuil par flamme, 3 flammes = 44 dégâts + explosion + recul ---
	if (const UGenGA_Projectile* Great = LoadCurffeAbility<UGenGA_Projectile>(TEXT("GA_GreatFireball")); TestNotNull(TEXT("GA_GreatFireball"), Great))
	{
		FGenAbilityTooltipData Data;
		Build(*Great, Data);
		AddInfo(FString::Printf(TEXT("GA_GreatFireball : %s"), *Data.GetLinesText().ToString()));
		TestEqual(TEXT("Grande boule de feu : 3 flammes = 44 dégâts (spec)"), Great->GetShotDamage(3), 44.f);
		TestEqual(TEXT("seuils 0 à 3"), Great->GetTooltipMaxFeed(), CurffeTuning::MaxFeedPerSpell);
		if (TestTrue(TEXT("une ligne par seuil (0 à 3)"), Data.Lines.Num() >= 4))
		{
			const FString Three = Data.Lines[3].ToString();
			TestTrue(TEXT("3 flammes : dégâts du jeu"), Three.Contains(DamageText(Great->GetShotDamage(3))));
			TestTrue(TEXT("3 flammes : explosion"), Three.Contains(GenAbilityTooltip::Meters(Great->GetShotParams(3).ExplosionRadius).ToString()));
			TestTrue(TEXT("3 flammes : recul"), Three.Contains(GenAbilityTooltip::Meters(Great->GetShotParams(3).KnockbackDistance).ToString()));
			TestTrue(TEXT("3 flammes : temps de maintien (3 × intervalle)"), Three.Contains(GenAbilityTooltip::Seconds(3 * Great->GetTooltipFeedInterval()).ToString()));
			TestEqual(TEXT("1 flamme : pas d'explosion"), Great->GetShotParams(1).ExplosionRadius, 0.f);
			TestFalse(TEXT("1 flamme : ligne sans explosion"), Data.Lines[1].ToString().Contains(TEXT("explosion")));
		}
	}

	// --- Pilier de flammes (classe native, valeurs de la spec) : rayon 3,5 m à 3 flammes ---
	{
		// Le pilier est nourrissable (GA_FlamePillar) : la classe native l'est par réflexion, valeurs de la spec intactes
		UGenGA_GroundArea* Pillar = NewObject<UGenGA_GroundArea>(GetTransientPackage());
		if (const FBoolProperty* Feedable = FindFProperty<FBoolProperty>(UGenGA_Cast::StaticClass(), TEXT("bFeedable")))
		{
			Feedable->SetPropertyValue_InContainer(Pillar, true);
		}
		FGenAbilityTooltipData Data;
		const FString Text = Build(*Pillar, Data);
		TestEqual(TEXT("Pilier : rayon max 350 cm"), Pillar->GetAreaRadius(CurffeTuning::MaxFeedPerSpell), 350.f);
		TestTrue(TEXT("Pilier : 3,5 m dans l'infobulle"), Text.Contains(GenAbilityTooltip::Meters(350.f).ToString()));
		TestTrue(TEXT("Pilier : 2 m sans flamme"), Data.Lines.Num() > 0 && Data.Lines[0].ToString().Contains(GenAbilityTooltip::Meters(200.f).ToString()));
		TestTrue(TEXT("Pilier : portée"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Meters(Pillar->GetTooltipRange()).ToString()));

		// Revue finale, M-1 : l'infobulle donne le délai d'impact du jeu (plancher MinTelegraph relevé de la marge de latence)
		TestTrue(TEXT("Pilier : délai d'impact du jeu"), Text.Contains(GenAbilityTooltip::Seconds(Pillar->GetEffectiveImpactDelay()).ToString()));
		if (const FFloatProperty* DelayProperty = FindFProperty<FFloatProperty>(UGenGA_GroundArea::StaticClass(), TEXT("ImpactDelay"));
			TestNotNull(TEXT("ImpactDelay"), DelayProperty))
		{
			DelayProperty->SetPropertyValue_InContainer(Pillar, 0.3f);
			FGenAbilityTooltipData ShortData;
			const FString ShortText = Build(*Pillar, ShortData);
			TestEqual(TEXT("Délai court : plancher 0.6 s + marge 0.1 s"), Pillar->GetEffectiveImpactDelay(), 0.7f, KINDA_SMALL_NUMBER);
			TestTrue(TEXT("Délai court : 0.7 s dans l'infobulle"), ShortText.Contains(GenAbilityTooltip::Seconds(0.7f).ToString()));
		}
	}

	// --- Bond (asset GA_FlameLeap, sinon le bond météore natif) : décollage par flamme, anneau ---
	{
		const UGenGA_Leap* Leap = LoadCurffeAbility<UGenGA_Leap>(TEXT("GA_FlameLeap"));
		if (!Leap)
		{
			Leap = GetDefault<UCurffeGA_MeteorLeap>();
		}
		FGenAbilityTooltipData Data;
		const FString Text = Build(*Leap, Data);
		AddInfo(FString::Printf(TEXT("%s : %s"), *Leap->GetClass()->GetName(), *Data.GetLinesText().ToString()));
		const int32 Top = Leap->GetTooltipMaxFeed();
		if (Top > 0)
		{
			TestTrue(TEXT("Bond : décollage au dernier seuil"), Text.Contains(GenAbilityTooltip::Seconds(Leap->GetCastTime() + Top * Leap->GetTooltipFeedInterval()).ToString()));
		}
		TestTrue(TEXT("Bond : portée"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Meters(Leap->GetTooltipRange()).ToString()));
		TestTrue(TEXT("Bond : atterrissage"), Text.Contains(TEXT("Atterrissage")));
	}

	// --- Retour de flamme (contre natif) : fenêtre ---
	{
		const UGenGA_Counter* Counter = GetDefault<UGenGA_Counter>();
		FGenAbilityTooltipData Data;
		const FString Text = Build(*Counter, Data);
		TestTrue(TEXT("Contre : fenêtre"), Text.Contains(GenAbilityTooltip::Seconds(Counter->GetCounterWindow()).ToString()));
		TestTrue(TEXT("Contre : incantation"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Seconds(Counter->GetCastTime()).ToString()));
	}

	// --- Flamme vivante et Combustion (natives) : coûts, durées, rayons ---
	{
		const UCurffeGA_LivingFlame* LivingFlame = GetDefault<UCurffeGA_LivingFlame>();
		FGenAbilityTooltipData Data;
		const FString Text = Build(*LivingFlame, Data);
		TestTrue(TEXT("Flamme vivante : 25 énergie"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Number(LivingFlame->EnergyCost).ToString()));
		TestTrue(TEXT("Flamme vivante : forme"), Text.Contains(GenAbilityTooltip::Seconds(LivingFlame->GetFormDuration()).ToString()));
		TestTrue(TEXT("Flamme vivante : anneau"), Text.Contains(GenAbilityTooltip::Meters(LivingFlame->GetBurstRadius()).ToString()));
		TestTrue(TEXT("Flamme vivante : recharge"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Seconds(LivingFlame->CooldownDuration.GetValueAtLevel(1)).ToString()));
	}
	{
		const UCurffeGA_Combustion* Combustion = GetDefault<UCurffeGA_Combustion>();
		FGenAbilityTooltipData Data;
		const FString Text = Build(*Combustion, Data);
		TestTrue(TEXT("Combustion : 100 énergie"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Number(100.f).ToString()));
		TestTrue(TEXT("Combustion : nova"), Text.Contains(GenAbilityTooltip::Meters(Combustion->GetNovaRadius()).ToString()));
		TestTrue(TEXT("Combustion : embrasement"), Text.Contains(GenAbilityTooltip::Seconds(Combustion->GetAblazeDuration()).ToString()));
	}

	// --- Description de l'asset : arguments nommés remplis depuis les valeurs de jeu ---
	{
		UCurffeGA_LivingFlame* Authored = NewObject<UCurffeGA_LivingFlame>(GetTransientPackage());
		Authored->Description = INVTEXT("Anneau de {Radius}, {Damage} dégâts, {EnergyCost} énergie, forme {Duration}.");
		FGenAbilityTooltipData Data;
		Build(*Authored, Data);
		const FString Expected = FString::Printf(TEXT("Anneau de %s, %s dégâts, %s énergie, forme %s."),
			*GenAbilityTooltip::Meters(Authored->GetBurstRadius()).ToString(), *GenAbilityTooltip::Number(8.f).ToString(),
			*GenAbilityTooltip::Number(Authored->EnergyCost).ToString(), *GenAbilityTooltip::Seconds(Authored->GetFormDuration()).ToString());
		TestEqual(TEXT("Description : arguments nommés"), Data.Description.ToString(), Expected);
	}
	return true;
}

#endif
