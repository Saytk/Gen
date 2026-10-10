#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Counter.h"
#include "AbilitySystem/Abilities/GenGA_GroundArea.h"
#include "AbilitySystem/Abilities/GenGA_Dash.h"
#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/GenAbilityTooltipData.h"
#include "Actors/GenProjectile.h"
#include "Champions/Curffe/CurffeGA_Combustion.h"
#include "Champions/Curffe/CurffeGA_LivingFlame.h"
#include "Champions/Curffe/CurffeGA_MeteorLeap.h"
#include "Champions/Curffe/CurffeTuning.h"
#include "UObject/Package.h"

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

	/** Carte compacte (§4.1) : une seule ligne d'effet, et des statistiques sans portée ni nourrissage. */
	void TestCompact(FAutomationTestBase& Test, const TCHAR* What, const FGenAbilityTooltipData& Data)
	{
		Test.TestFalse(FString::Printf(TEXT("%s : ligne compacte"), What), Data.CompactLine.IsEmpty());
		Test.TestFalse(FString::Printf(TEXT("%s : ligne compacte sur une ligne"), What), Data.CompactLine.ToString().Contains(TEXT("\n")));
		Test.TestFalse(FString::Printf(TEXT("%s : statistiques compactes sans portée"), What), Data.GetCompactStatsText().ToString().Contains(TEXT("Portée")));
		Test.TestFalse(FString::Printf(TEXT("%s : statistiques compactes sans nourrissage"), What), Data.GetCompactStatsText().ToString().Contains(TEXT("Nourri")));
	}
}

bool FGenAbilityTooltipTest::RunTest(const FString& Parameters)
{
	using namespace GenAbilityTooltipTest;

	// Nombres dans la culture courante, unités, sans zéro inutile
	TestEqual(TEXT("44 -> AsNumber"), GenAbilityTooltip::Number(44.f).ToString(), FText::AsNumber(44).ToString());
	TestEqual(TEXT("0.4 s"), GenAbilityTooltip::Seconds(0.4f).ToString(), FString::Printf(TEXT("%s s"), *FText::AsNumber(0.4f).ToString()));
	TestEqual(TEXT("350 cm -> 3.5 m"), GenAbilityTooltip::Meters(350.f).ToString(), FString::Printf(TEXT("%s m"), *FText::AsNumber(3.5f).ToString()));

	// Séries de la carte compacte : une valeur par seuil, une seule si elles sont égales ; seuil d'apparition d'un effet
	TestEqual(TEXT("Série 14/24"), GenAbilityTooltip::Series({ 14.f, 24.f }).ToString(), FString::Printf(TEXT("%s/%s"), *GenAbilityTooltip::Number(14.f).ToString(), *GenAbilityTooltip::Number(24.f).ToString()));
	TestEqual(TEXT("Série égale -> une valeur"), GenAbilityTooltip::Series({ 12.f, 12.f, 12.f }).ToString(), GenAbilityTooltip::Number(12.f).ToString());
	TestEqual(TEXT("Effet dès 0 flamme -> sa valeur"), GenAbilityTooltip::FromFeed(0, 3, INVTEXT("1,5 m")).ToString(), FString(TEXT("1,5 m")));
	TestEqual(TEXT("Effet dès 2"), GenAbilityTooltip::FromFeed(2, 3, FText::GetEmpty()).ToString(), FString::Printf(TEXT("dès %s"), *FText::AsNumber(2).ToString()));
	TestEqual(TEXT("Effet au dernier seuil"), GenAbilityTooltip::FromFeed(3, 3, FText::GetEmpty()).ToString(), FString::Printf(TEXT("à %s"), *FText::AsNumber(3).ToString()));

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
		TestCompact(*this, TEXT("Boule de feu"), Data);
		TestTrue(TEXT("Boule de feu compacte : dégâts"), Data.CompactLine.ToString().Contains(DamageText(Fireball->GetShotDamage(0))));
		TestTrue(TEXT("Boule de feu compacte : incantation"), Data.GetCompactStatsText().ToString().Contains(GenAbilityTooltip::Seconds(Fireball->GetCastTime()).ToString()));
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

		// Carte compacte : « 0/1/2/3 flammes : 14/24/34/44 dégâts · explosion dès 2 · recul à 3 », valeurs du jeu
		TestCompact(*this, TEXT("Grande boule de feu"), Data);
		TArray<float> Damage;
		int32 FirstExplosion = INDEX_NONE;
		for (int32 Fed = 0; Fed <= Great->GetTooltipMaxFeed(); ++Fed)
		{
			Damage.Add(Great->GetShotDamage(Fed));
			FirstExplosion = FirstExplosion == INDEX_NONE && Great->GetShotParams(Fed).ExplosionRadius > 0.f ? Fed : FirstExplosion;
		}
		const FString Compact = Data.CompactLine.ToString();
		AddInfo(FString::Printf(TEXT("GA_GreatFireball compacte : %s | %s"), *Data.GetCompactStatsText().ToString(), *Compact));
		TestTrue(TEXT("Compacte : seuils 0/1/2/3"), Compact.StartsWith(TEXT("0/1/2/3")));
		TestTrue(TEXT("Compacte : dégâts par flamme"), Compact.Contains(GenAbilityTooltip::Series(Damage).ToString()));
		TestTrue(TEXT("Compacte : seuil de l'explosion"), FirstExplosion != INDEX_NONE
			&& Compact.Contains(GenAbilityTooltip::FromFeed(FirstExplosion, Great->GetTooltipMaxFeed(), GenAbilityTooltip::Meters(Great->GetShotParams(Great->GetTooltipMaxFeed()).ExplosionRadius)).ToString()));
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
		TestCompact(*this, TEXT("Pilier"), Data);
		TArray<float> Radii;
		for (int32 Fed = 0; Fed <= Pillar->GetTooltipMaxFeed(); ++Fed)
		{
			Radii.Add(Pillar->GetAreaRadius(Fed));
		}
		TestTrue(TEXT("Pilier compact : rayon par flamme"), Data.CompactLine.ToString().Contains(GenAbilityTooltip::MetersSeries(Radii).ToString()));

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

	// --- Ruée de flammes (asset GA_FlameLeap, sinon la classe native) : décollage par flamme, un segment par flamme ---
	{
		const UGenGA_Dash* Dash = LoadCurffeAbility<UGenGA_Dash>(TEXT("GA_FlameLeap"));
		if (!Dash)
		{
			Dash = GetDefault<UCurffeGA_MeteorLeap>();
		}
		FGenAbilityTooltipData Data;
		const FString Text = Build(*Dash, Data);
		AddInfo(FString::Printf(TEXT("%s : %s"), *Dash->GetClass()->GetName(), *Data.GetLinesText().ToString()));
		const GenDashRules::FZigzagParams Params = Dash->GetZigzagParams();
		const int32 Top = Dash->GetTooltipMaxFeed();
		if (Top > 0)
		{
			TestTrue(TEXT("Ruée : décollage au dernier seuil"), Text.Contains(GenAbilityTooltip::Seconds(Dash->GetCastTime() + Top * Dash->GetTooltipFeedInterval()).ToString()));
			TestTrue(TEXT("Ruée : trajet au dernier seuil"), Text.Contains(GenAbilityTooltip::Meters(GenDashRules::GetPathLength(Top, Params)).ToString()));
		}
		TestTrue(TEXT("Ruée : portée = premier segment"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Meters(Params.BaseDistance).ToString()));
		// Lignes générées par le code (la description de l'asset est éditée dans l'éditeur)
		TestFalse(TEXT("Ruée : plus d'atterrissage"), Data.GetLinesText().ToString().Contains(TEXT("Atterrissage")));
		TestFalse(TEXT("Ruée : plus d'anneau"), Data.GetLinesText().ToString().Contains(TEXT("anneau")));
		TestCompact(*this, TEXT("Ruée"), Data);
		if (Top > 0)
		{
			TArray<float> TakeOff;
			TArray<float> Length;
			for (int32 Fed = 0; Fed <= Top; ++Fed)
			{
				TakeOff.Add(Dash->GetCastTime() + Fed * Dash->GetTooltipFeedInterval());
				Length.Add(GenDashRules::GetPathLength(Fed, Params));
			}
			TestTrue(TEXT("Ruée compacte : décollage par flamme"), Data.CompactLine.ToString().Contains(GenAbilityTooltip::SecondsSeries(TakeOff).ToString()));
			TestTrue(TEXT("Ruée compacte : trajet par flamme"), Data.CompactLine.ToString().Contains(GenAbilityTooltip::MetersSeries(Length).ToString()));
		}
	}

	// --- Retour de flamme (contre natif) : fenêtre ---
	{
		const UGenGA_Counter* Counter = GetDefault<UGenGA_Counter>();
		FGenAbilityTooltipData Data;
		const FString Text = Build(*Counter, Data);
		TestTrue(TEXT("Contre : fenêtre"), Text.Contains(GenAbilityTooltip::Seconds(Counter->GetCounterWindow()).ToString()));
		TestTrue(TEXT("Contre : incantation"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Seconds(Counter->GetCastTime()).ToString()));
		TestCompact(*this, TEXT("Contre"), Data);
		TestTrue(TEXT("Contre compact : fenêtre"), Data.CompactLine.ToString().Contains(GenAbilityTooltip::Seconds(Counter->GetCounterWindow()).ToString()));
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
		TestCompact(*this, TEXT("Flamme vivante"), Data);
		TestTrue(TEXT("Flamme vivante compacte : forme"), Data.CompactLine.ToString().Contains(GenAbilityTooltip::Seconds(LivingFlame->GetFormDuration()).ToString()));
		TestTrue(TEXT("Flamme vivante compacte : coût"), Data.GetCompactStatsText().ToString().Contains(GenAbilityTooltip::Number(LivingFlame->EnergyCost).ToString()));
	}
	{
		const UCurffeGA_Combustion* Combustion = GetDefault<UCurffeGA_Combustion>();
		FGenAbilityTooltipData Data;
		const FString Text = Build(*Combustion, Data);
		TestTrue(TEXT("Combustion : 100 énergie"), Data.GetStatsText().ToString().Contains(GenAbilityTooltip::Number(100.f).ToString()));
		TestTrue(TEXT("Combustion : nova"), Text.Contains(GenAbilityTooltip::Meters(Combustion->GetNovaRadius()).ToString()));
		TestTrue(TEXT("Combustion : embrasement"), Text.Contains(GenAbilityTooltip::Seconds(Combustion->GetAblazeDuration()).ToString()));
		TestCompact(*this, TEXT("Combustion"), Data);
		TestTrue(TEXT("Combustion compacte : nova"), Data.CompactLine.ToString().Contains(GenAbilityTooltip::Meters(Combustion->GetNovaRadius()).ToString()));
		TestTrue(TEXT("Combustion compacte : embrasement"), Data.CompactLine.ToString().Contains(GenAbilityTooltip::Seconds(Combustion->GetAblazeDuration()).ToString()));
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
