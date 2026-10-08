#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "UI/GenUIRules.h"
#include "GenUIDataAssets.generated.h"

class UTexture2D;

/** Jetons de couleur de l'interface (UI_Guidelines §2.1–2.5). Valeurs remplies depuis les hex via HexToLinear. */
UCLASS(BlueprintType, Const)
class GEN_API UGenUIPalette : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Text") FLinearColor Text_Primary = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Text") FLinearColor Text_Secondary = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lines") FLinearColor Line_Bronze = FLinearColor::White;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lines") FLinearColor Line_Outline = FLinearColor::Black;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Surfaces") FLinearColor Bg_Panel = FLinearColor::Black;
	/** bg.panelRaised #2B1E16 α 0.88 (§2.1) : infobulles (défaut : le jeton, depuis GenUITokens). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Surfaces") FLinearColor Bg_PanelRaised = GenUIRules::HexToLinear(GenUITokens::BgPanelRaisedHex, GenUITokens::BgPanelRaisedAlpha);
	/** accent.brass #D6A47C (§2.2) : libellé de touche des infobulles (défaut : le jeton, depuis GenUITokens). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Accent") FLinearColor Accent_Brass = GenUIRules::HexToLinear(GenUITokens::AccentBrassHex, 1.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown") FLinearColor Cooldown_Overlay = FLinearColor::Black;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown") FLinearColor Cooldown_Locked = FLinearColor::Black;
	/**
	 * cooldown.noEnergy #2E4A78 α 0.45 (§2.5) : voile « pas assez d'énergie ». Valeur posée dans DA_UIPalette via HexToLinear ;
	 * le défaut est le jeton lui-même (depuis le hex), pour qu'une palette pas encore mise à jour ne voile pas le disque en noir opaque.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown") FLinearColor Cooldown_NoEnergy = GenUIRules::HexToLinear(GenUITokens::CooldownNoEnergyHex, GenUITokens::CooldownNoEnergyAlpha);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Energy") FLinearColor Energy_Charging = FLinearColor::Yellow;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Energy") FLinearColor Energy_Full = FLinearColor::Yellow;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flash") FLinearColor Flash_White = FLinearColor::White;
};

/** Tailles, seuils et durées de l'interface (UI_Guidelines §3.1, §4.1, §5.2). */
UCLASS(BlueprintType, Const)
class GEN_API UGenUIMetrics : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float SlotSize = 64.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float UltimateSlotSize = 72.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float SlotGap = 12.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float KeyLabelGap = 2.f;
	/** Bande des libellés de touche au-dessus des emplacements (§3.1 : 20 px). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float KeyLabelHeight = 20.f;
	/** Bande de l'arc de coût d'énergie sous les emplacements (§3.1 : 6 px). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float CostArcBand = 6.f;
	/** Bord des emplacements : 1 px ; anneau de l'ultime : 2 px (§4.1, §2.9). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float SlotRimWidth = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float UltimateRimWidth = 2.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Layout") float ScreenMargin = 32.f;
	/** Chiffre de recharge caché si la durée totale est inférieure (§4.1, tunable). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "s")) float CooldownHideBelowTotal = 2.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float CooldownDesaturation = 0.7f;
	/** Luminosité de l'icône quand l'énergie manque (§4.1 : 55 %). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float NoEnergyBrightness = 0.55f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion", meta = (Units = "s")) float ReadyFlashDuration = 0.2f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion", meta = (Units = "s")) float UltimatePulseDuration = 0.3f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") int32 UltimateSegments = 4;
	/** Infobulle de sort (§4.1) : délai de survol avant le fondu d'entrée. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tooltip", meta = (Units = "s")) float TooltipHoverDelay = 0.3f;
	/** motion.fast (§5.1) : fondu des infobulles (entrée ease-out, sortie ease-in). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion", meta = (Units = "s")) float MotionFast = 0.15f;
	/** Marge intérieure d'un panneau du HUD (§2.7 : 8 px, Space_2). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Layout") float HudPanelPadding = 8.f;
	/** radius.panel (§2.8 : 4 px) : panneaux, cartes, infobulles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Layout") float RadiusPanel = 4.f;
	/** Bordure des panneaux (§2.9 : 1 px). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Layout") float PanelOutlineWidth = 1.f;
	/** Largeur maximale d'une infobulle (§7.1 : <= 80 caractères par ligne en TS_Body), à l'échelle de texte 100 %. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tooltip") float TooltipMaxWidth = 480.f;
	/** Écart entre l'infobulle et le haut de l'emplacement (§2.7, entre groupes : 8 px). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tooltip") float TooltipGap = 8.f;
	/** Sondage du survol des emplacements et pas du fondu (pas de NativeTick, §8.4). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tooltip", meta = (Units = "s")) float TooltipPollInterval = 0.05f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tooltip", meta = (Units = "s")) float TooltipFadeStep = 0.016f;

	/** Rafraîchissement du balayage et du chiffre pendant une recharge (pas de NativeTick). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "s")) float CooldownRefreshInterval = 0.05f;

	/** Hauteur de la barre de sorts (unités UMG, hors marge d'écran) : libellé + écart + ultime + bande de l'arc. */
	float GetAbilityBarHeight() const { return KeyLabelHeight + KeyLabelGap + UltimateSlotSize + CostArcBand; }
};

USTRUCT(BlueprintType)
struct FGenKeyGlyph
{
	GENERATED_BODY()

	/** Glyphe (ex : souris). Prioritaire sur le texte. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TObjectPtr<UTexture2D> Glyph = nullptr;

	/** Texte court (ex : SpaceBar -> "SPC"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FText ShortText;
};

/** Touche -> glyphe ou texte court (UI_Guidelines §8.3 DA_UIKeyGlyphs). */
UCLASS(BlueprintType, Const)
class GEN_API UGenUIKeyGlyphs : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Keys") TMap<FKey, FGenKeyGlyph> Glyphs;
};
