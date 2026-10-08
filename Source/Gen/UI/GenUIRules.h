#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "AbilitySystem/GenEnergy.h"
#include "GenUIRules.generated.h"

/** État visuel d'un emplacement de sort (UI_Guidelines §4.1). */
UENUM(BlueprintType)
enum class EGenAbilitySlotState : uint8
{
	Empty,
	Ready,
	Cooldown,
	Locked,
	/** Pas assez d'énergie pour le coût du sort (R, F) : voile cooldown.noEnergy, icône à 55 % (§4.1). */
	NoEnergy
};

/**
 * Jetons de UI_Guidelines §2 recopiés en C++ (valeurs par défaut des data assets) : UNE constante par jeton, que le
 * défaut C++ et les tests lisent. DA_UIPalette reste la valeur en jeu.
 */
namespace GenUITokens
{
	/** cooldown.noEnergy (§2.5) : voile « pas assez d'énergie ». */
	inline const TCHAR* const CooldownNoEnergyHex = TEXT("#2E4A78");
	inline constexpr float CooldownNoEnergyAlpha = 0.45f;

	/** bg.panelRaised (§2.1) : fond des infobulles, cartes de menu, tableau des scores. */
	inline const TCHAR* const BgPanelRaisedHex = TEXT("#2B1E16");
	inline constexpr float BgPanelRaisedAlpha = 0.88f;

	/** accent.brass (§2.2) : seul accent de marque sur fond sombre (focus, sélection, liens des menus et infobulles). */
	inline const TCHAR* const AccentBrassHex = TEXT("#D6A47C");
}

/** Règles pures de l'interface, testées hors monde. */
namespace GenUIRules
{
	/**
	 * Temps restant d'une recharge, borné à sa durée. Côté client, le GE de recharge répliqué du serveur part de
	 * l'heure serveur estimée par le GameState, en retard de la latence et de la période de réplication de
	 * cette heure : pendant quelques images il reste plus que la durée ("7" sur une recharge de 6 s).
	 */
	inline float ClampCooldownRemaining(float Remaining, float TotalDuration)
	{
		return TotalDuration > 0.f ? FMath::Min(Remaining, TotalDuration) : Remaining;
	}

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
	 * Texte affiché du chiffre de recharge : la valeur de FormatCooldown (indépendante de la culture, "0.6", "12"),
	 * mise en forme dans la culture courante (ou Culture) : "0,6" en français (§9 Localisation). Sans séparateur de milliers.
	 * FormatCooldown reste la référence pour la classe de format et les mesures de largeur.
	 */
	inline FText CooldownDisplayText(const FString& CultureInvariant, const FCulturePtr& Culture = nullptr)
	{
		if (CultureInvariant.IsEmpty())
		{
			return FText::GetEmpty();
		}

		int32 DotIndex = INDEX_NONE;
		CultureInvariant.FindChar(TEXT('.'), DotIndex);
		const int32 FractionalDigits = DotIndex == INDEX_NONE ? 0 : CultureInvariant.Len() - DotIndex - 1;

		FNumberFormattingOptions Options;
		Options.SetUseGrouping(false);
		Options.SetMinimumFractionalDigits(FractionalDigits);
		Options.SetMaximumFractionalDigits(FractionalDigits);
		return FText::AsNumber(FCString::Atod(*CultureInvariant), &Options, Culture);
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

	/** Priorité : vide > bloqué (contrôle dur) > recharge > pas assez d'énergie > prêt. */
	inline EGenAbilitySlotState ResolveSlotState(bool bHasAbility, bool bLocked, float CooldownRemaining, bool bCanAfford = true)
	{
		if (!bHasAbility)
		{
			return EGenAbilitySlotState::Empty;
		}
		if (bLocked)
		{
			return EGenAbilitySlotState::Locked;
		}
		if (CooldownRemaining > 0.f)
		{
			return EGenAbilitySlotState::Cooldown;
		}
		return bCanAfford ? EGenAbilitySlotState::Ready : EGenAbilitySlotState::NoEnergy;
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

	/**
	 * Revue P3 T8-10, M5 : segments financés de l'arc d'un sort qui coûte Cost en Segments segments. Même règle que
	 * CheckCost : tous financés si et seulement si GenEnergy::CanAfford ; sinon un segment par Cost/Segments d'énergie,
	 * au plus Segments - 1 (jamais un arc plein sur un sort qu'on ne peut pas lancer). Sans coût : FundedSegments.
	 */
	inline int32 CostFundedSegments(float Energy, float Cost, int32 Segments)
	{
		if (Segments <= 0 || Cost <= 0.f)
		{
			return 0;
		}
		if (GenEnergy::CanAfford(Energy, Cost))
		{
			return Segments;
		}
		return FMath::Clamp(FMath::FloorToInt32(Energy / (Cost / Segments)), 0, Segments - 1);
	}

	/**
	 * Revue P3 T8-10, M4 (UI_Guidelines §4.1) : le flash « prêt » part quand l'emplacement DEVIENT lançable depuis une
	 * recharge ou un manque d'énergie, jamais d'une recharge vers « pas assez d'énergie », ni à la levée d'un contrôle.
	 */
	inline bool IsReadyFlash(EGenAbilitySlotState Old, EGenAbilitySlotState New)
	{
		return New == EGenAbilitySlotState::Ready && (Old == EGenAbilitySlotState::Cooldown || Old == EGenAbilitySlotState::NoEnergy);
	}

	/** Segments d'arc d'un coût (§4.1, un segment par tranche de Max/Segments) : R (25) = 1, F (100) = 4, sort gratuit = 0. */
	inline int32 CostSegments(float EnergyCost, float MaxEnergy, int32 Segments)
	{
		if (EnergyCost <= 0.f || MaxEnergy <= 0.f || Segments <= 0)
		{
			return 0;
		}
		return FMath::Clamp(FMath::CeilToInt32(EnergyCost / (MaxEnergy / Segments) - KINDA_SMALL_NUMBER), 1, Segments);
	}
}
