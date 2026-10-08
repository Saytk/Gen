#include "AbilitySystem/GenAbilityTooltipData.h"

#include "AbilitySystem/Abilities/GenGameplayAbility.h"

#define LOCTEXT_NAMESPACE "GenAbilityTooltip"

FText FGenAbilityTooltipData::GetStatsText() const
{
	return GenAbilityTooltip::Join(Stats, LOCTEXT("StatsSeparator", " · "));
}

FText FGenAbilityTooltipData::GetLinesText() const
{
	return GenAbilityTooltip::Join(Lines, FText::FromString(TEXT("\n")));
}

FText FGenAbilityTooltipData::GetCompactLinesText() const
{
	const int32 Count = CoreLineCount > 0 ? FMath::Min(CoreLineCount, Lines.Num()) : FMath::Min(1, Lines.Num());
	TArray<FText> Compact(Lines.GetData(), Count);
	return GenAbilityTooltip::Join(Compact, FText::FromString(TEXT("\n")));
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
		Out.CoreLineCount = Out.Lines.Num();
		Ability.GetTooltipEffectLines(Out.Lines);
	}
}

#undef LOCTEXT_NAMESPACE
