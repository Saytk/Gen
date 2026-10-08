#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "GenUIRules.generated.h"

/** État visuel d'un emplacement de sort (UI_Guidelines §4.1). */
UENUM(BlueprintType)
enum class EGenAbilitySlotState : uint8
{
	Empty,
	Ready,
	Cooldown,
	Locked
};

/** Règles pures de l'interface, testées hors monde. */
namespace GenUIRules
{
	/**
	 * Texte du chiffre de recharge (§4.1) : secondes entières arrondies au-dessus dès 1 s,
	 * une décimale sous 1 s, rien si la recharge est finie ou si sa durée totale est < HideBelowTotal.
	 */
	inline FString FormatCooldown(float Remaining, float TotalDuration, float HideBelowTotal)
	{
		if (Remaining <= 0.f || TotalDuration < HideBelowTotal)
		{
			return FString();
		}
		if (Remaining >= 1.f)
		{
			return FString::FromInt(FMath::CeilToInt32(Remaining - KINDA_SMALL_NUMBER));
		}
		const float Tenths = FMath::CeilToFloat(Remaining * 10.f - KINDA_SMALL_NUMBER) / 10.f;
		// Sous 1 s mais dixièmes arrondis à 1.0 (ex : 0.95) : afficher "1" comme la branche >= 1 s, pas "1.0" puis "1"
		if (Tenths >= 1.f)
		{
			return FString(TEXT("1"));
		}
		return FString::Printf(TEXT("%.1f"), FMath::Max(Tenths, 0.1f));
	}

	/** Priorité : vide > bloqué (étourdi) > recharge > prêt. */
	inline EGenAbilitySlotState ResolveSlotState(bool bHasAbility, bool bLocked, float CooldownRemaining)
	{
		if (!bHasAbility)
		{
			return EGenAbilitySlotState::Empty;
		}
		if (bLocked)
		{
			return EGenAbilitySlotState::Locked;
		}
		return CooldownRemaining > 0.f ? EGenAbilitySlotState::Cooldown : EGenAbilitySlotState::Ready;
	}

	/** Couleur d'un jeton hexadécimal sRGB ("#RRGGBB" ou "RRGGBB") convertie en linéaire. */
	inline FLinearColor HexToLinear(const FString& Hex, float Alpha = 1.f)
	{
		FLinearColor Linear(FColor::FromHex(Hex));
		Linear.A = Alpha;
		return Linear;
	}

	/** Libellé d'une touche : texte court du jeu de glyphes, sinon le nom court de la touche. */
	inline FText FallbackKeyLabel(const FKey& Key, const TMap<FKey, FText>& ShortTexts)
	{
		if (!Key.IsValid())
		{
			return FText::GetEmpty();
		}
		if (const FText* Short = ShortTexts.Find(Key))
		{
			return *Short;
		}
		return Key.GetDisplayName(false);
	}

	/** Segments d'énergie financés (arc de l'ultime, un segment par tranche de Max/Segments). */
	inline int32 FundedSegments(float Energy, float MaxEnergy, int32 Segments)
	{
		if (MaxEnergy <= 0.f || Segments <= 0)
		{
			return 0;
		}
		return FMath::Clamp(FMath::FloorToInt32(Energy / (MaxEnergy / Segments) + KINDA_SMALL_NUMBER), 0, Segments);
	}
}
