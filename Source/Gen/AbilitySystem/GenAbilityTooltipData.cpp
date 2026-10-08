#include "AbilitySystem/GenAbilityTooltipData.h"

#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "Algo/AllOf.h"

#define LOCTEXT_NAMESPACE "GenAbilityTooltip"

FText FGenAbilityTooltipData::GetStatsText() const
{
	return GenAbilityTooltip::Join(Stats, LOCTEXT("StatsSeparator", " · "));
}

FText FGenAbilityTooltipData::GetLinesText() const
{
	return GenAbilityTooltip::Join(Lines, FText::FromString(TEXT("\n")));
}

FText FGenAbilityTooltipData::GetCompactStatsText() const
{
	return GenAbilityTooltip::Join(CompactStats, LOCTEXT("StatsSeparator", " · "));
}

FString FGenAbilityTooltipData::ToString() const
{
	return FString::Printf(TEXT("%s\n%s\n%s\n%s"), *Name.ToString(), *GetStatsText().ToString(), *Description.ToString(), *GetLinesText().ToString());
}

namespace GenAbilityTooltip
{
	FText Number(float Value)
	{
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(false);
		Options.SetMinimumFractionalDigits(0);
		Options.SetMaximumFractionalDigits(2);
		return FText::AsNumber(Value, &Options);
	}

	FText Seconds(float InSeconds)
	{
		return FText::Format(LOCTEXT("Seconds", "{0} s"), Number(InSeconds));
	}

	FText Meters(float Centimetres)
	{
		return FText::Format(LOCTEXT("Meters", "{0} m"), Number(Centimetres / 100.f));
	}

	FText Percent(float Ratio)
	{
		FNumberFormattingOptions Options;
		Options.SetMaximumFractionalDigits(0);
		return FText::AsPercent(Ratio, &Options);
	}

	FText Join(const TArray<FText>& Parts, const FText& Separator)
	{
		TArray<FText> NonEmpty;
		for (const FText& Part : Parts)
		{
			if (!Part.IsEmpty())
			{
				NonEmpty.Add(Part);
			}
		}
		return FText::Join(Separator, NonEmpty);
	}

	FText Series(TConstArrayView<float> Values)
	{
		TArray<FText> Parts;
		for (const float Value : Values)
		{
			Parts.Add(Number(Value));
		}
		const bool bAllEqual = !Values.IsEmpty() && Algo::AllOf(Values, [&Values](float Value) { return FMath::IsNearlyEqual(Value, Values[0]); });
		return bAllEqual ? Parts[0] : FText::Join(INVTEXT("/"), Parts);
	}

	FText SecondsSeries(TConstArrayView<float> InSeconds)
	{
		return FText::Format(LOCTEXT("Seconds", "{0} s"), Series(InSeconds));
	}

	FText MetersSeries(TConstArrayView<float> Centimetres)
	{
		TArray<float> InMetres;
		for (const float Value : Centimetres)
		{
			InMetres.Add(Value / 100.f);
		}
		return FText::Format(LOCTEXT("Meters", "{0} m"), Series(InMetres));
	}

	FText FromFeed(int32 FirstFed, int32 MaxFeed, const FText& Always)
	{
		if (FirstFed <= 0)
		{
			return Always;
		}
		return FirstFed >= MaxFeed ? FText::Format(LOCTEXT("AtFeed", "à {0}"), FirstFed) : FText::Format(LOCTEXT("FromFeed", "dès {0}"), FirstFed);
	}

	FText FeedSummary(int32 MaxFeed, const TArray<FText>& Parts)
	{
		TArray<FText> Counts;
		for (int32 Fed = 0; Fed <= MaxFeed; ++Fed)
		{
			Counts.Add(FText::AsNumber(Fed));
		}
		return FText::Format(LOCTEXT("FeedSummary", "{0} {1}|plural(one=flamme,other=flammes) : {2}"), FText::Join(INVTEXT("/"), Counts), MaxFeed,
			Join(Parts, LOCTEXT("StatsSeparator", " · ")));
	}

	/** Nom affichable de la classe d'un sort sans DisplayName (UClass::GetDisplayNameText n'existe qu'avec l'éditeur). */
	static FText GetClassDisplayName(const UClass* Class)
	{
#if WITH_EDITOR
		return Class->GetDisplayNameText();
#else
		// Revue finale, C-1 : même rendu hors éditeur ("GA_Fireball_C" -> "GA Fireball")
		FString ClassName = Class->GetName();
		ClassName.RemoveFromEnd(TEXT("_C"));
		return FText::FromString(FName::NameToDisplayString(ClassName, false));
#endif
	}

	void Build(const UGenGameplayAbility& Ability, FGenAbilityTooltipData& Out)
	{
		Out = FGenAbilityTooltipData();
		Out.Name = Ability.DisplayName.IsEmpty() ? GetClassDisplayName(Ability.GetClass()) : Ability.DisplayName;

		// En-tête : incantation, recharge, coût, portée, nourrissage (valeurs de jeu, niveau 1)
		const float CastTime = Ability.GetTooltipCastTime();
		if (CastTime > 0.f)
		{
			Out.Stats.Add(FText::Format(LOCTEXT("CastTime", "Incantation {0}"), Seconds(CastTime)));
		}
		else if (CastTime == 0.f)
		{
			Out.Stats.Add(LOCTEXT("Instant", "Instantané"));
		}
		const float Cooldown = Ability.CooldownDuration.GetValueAtLevel(1);
		if (Cooldown > 0.f)
		{
			Out.Stats.Add(FText::Format(LOCTEXT("Cooldown", "Recharge {0}"), Seconds(Cooldown)));
		}
		if (Ability.EnergyCost > 0.f)
		{
			Out.Stats.Add(FText::Format(LOCTEXT("EnergyCost", "{0} énergie"), Number(Ability.EnergyCost)));
		}
		// Carte compacte : incantation, recharge et coût seulement (une ligne à TooltipCompactWidth)
		Out.CompactStats = Out.Stats;
		const float Range = Ability.GetTooltipRange();
		if (Range > 0.f)
		{
			Out.Stats.Add(FText::Format(LOCTEXT("Range", "Portée {0}"), Meters(Range)));
		}
		const int32 MaxFeed = Ability.GetTooltipMaxFeed();
		const float FeedInterval = Ability.GetTooltipFeedInterval();
		if (MaxFeed > 0)
		{
			Out.Stats.Add(FText::Format(LOCTEXT("Feeding", "Nourri : {0} par flamme, {1} max"), Seconds(FeedInterval), MaxFeed));
		}

		// Description de l'asset, arguments remplis depuis les valeurs de jeu
		if (!Ability.Description.IsEmpty())
		{
			FFormatNamedArguments Args;
			Ability.GetTooltipArgs(Args);
			Out.Description = FText::Format(Ability.Description, Args);
		}

		// Un seuil par ligne (temps de maintien compris), ou l'effet unique d'un sort non nourri
		if (MaxFeed > 0)
		{
			for (int32 Fed = 0; Fed <= MaxFeed; ++Fed)
			{
				const FText Effect = Ability.GetFeedTooltipLines(Fed);
				if (Effect.IsEmpty())
				{
					continue;
				}
				Out.Lines.Add(Fed == 0
					? FText::Format(LOCTEXT("FeedLineNone", "Sans flamme : {0}"), Effect)
					: FText::Format(LOCTEXT("FeedLine", "{0} {0}|plural(one=flamme,other=flammes) ({1}) : {2}"), Fed, Seconds(Fed * FeedInterval), Effect));
			}
		}
		else
		{
			const FText Effect = Ability.GetFeedTooltipLines(0);
			if (!Effect.IsEmpty())
			{
				Out.Lines.Add(Effect);
			}
		}
		Ability.GetTooltipEffectLines(Out.Lines);

		// Carte compacte : une ligne, la série par flamme ou l'effet clé du sort
		Out.CompactLine = Ability.GetCompactTooltipLine();
	}
}

#undef LOCTEXT_NAMESPACE
