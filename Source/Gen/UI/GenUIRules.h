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

	/**
	 * Classe de format du chiffre de recharge (§2.6) : nombre de chiffres et de points ("6" = 1/0, "12" = 2/0, "0.6" = 2/1).
	 * Les chiffres de Barlow sont proportionnels : la boîte garde la largeur de la classe, pas celle de la valeur.
	 */
	inline FIntPoint CooldownFormatClass(const FString& Text)
	{
		FIntPoint Class(0, 0);
		for (const TCHAR C : Text)
		{
			Class.X += FChar::IsDigit(C) ? 1 : 0;
			Class.Y += C == TEXT('.') ? 1 : 0;
		}
		return Class;
	}

	/** Largeur de la boîte d'une classe : chiffre le plus large par chiffre, plus les points et le contour des deux côtés. */
	inline float CooldownBoxWidth(FIntPoint FormatClass, float WidestDigit, float DotWidth, float OutlineSize)
	{
		return FormatClass.X * WidestDigit + FormatClass.Y * DotWidth + 2.f * OutlineSize;
	}

	/**
	 * Toutes les valeurs que FormatCooldown affiche pour une classe : "0.1" à "0.9", puis "1" à "99".
	 * Vide au-delà (3 chiffres et plus) : la boîte retombe alors sur CooldownBoxWidth.
	 */
	inline TArray<FString> CooldownClassSamples(FIntPoint FormatClass)
	{
		TArray<FString> Samples;
		for (int32 Tenths = 1; Tenths <= 9; ++Tenths)
		{
			const FString Text = FormatCooldown(Tenths / 10.f, 100.f, 0.f);
			if (CooldownFormatClass(Text) == FormatClass)
			{
				Samples.AddUnique(Text);
			}
		}
		for (int32 Seconds = 1; Seconds <= 99; ++Seconds)
		{
			const FString Text = FormatCooldown(static_cast<float>(Seconds), 100.f, 0.f);
			if (CooldownFormatClass(Text) == FormatClass)
			{
				Samples.AddUnique(Text);
			}
		}
		return Samples;
	}

	/** Boîte du chiffre de recharge : largeur et décalage horizontal de son centre par rapport au disque (px de mise en page). */
	struct FCooldownBoxLayout
	{
		float Width = 0.f;
		/** Négatif = vers la gauche. */
		float CentreShift = 0.f;
	};

	/**
	 * Chiffre calé à droite dans une boîte à la largeur de la valeur la plus large de sa classe (§2.6) : le bord droit ne bouge
	 * pas quand le chiffre change. Boîte centrée, la valeur la plus large serait centrée et la plus étroite décalée à droite de
	 * (Widest - Narrowest) / 2 ; on décale la boîte à gauche de (Widest - Narrowest) / 4 pour partager l'écart :
	 * chaque valeur de la classe est alors à au plus (Widest - Narrowest) / 4 du centre du disque (§4.1).
	 */
	inline FCooldownBoxLayout CooldownBoxLayout(float NarrowestText, float WidestText, float OutlineSize)
	{
		FCooldownBoxLayout Layout;
		Layout.Width = WidestText + 2.f * OutlineSize;
		Layout.CentreShift = -FMath::Max(WidestText - NarrowestText, 0.f) * 0.25f;
		return Layout;
	}

	/** Épaisseur du bord en px de mise en page : jamais sous 1 px physique (§7.1), quelle que soit l'échelle DPI. */
	inline float RimLayoutWidth(float RimPx, float ViewportScale)
	{
		return ViewportScale > KINDA_SMALL_NUMBER ? FMath::Max(RimPx, 1.f / ViewportScale) : RimPx;
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
