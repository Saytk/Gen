#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown") FLinearColor Cooldown_Overlay = FLinearColor::Black;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown") FLinearColor Cooldown_Locked = FLinearColor::Black;
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Layout") float ScreenMargin = 32.f;
	/** Chiffre de recharge caché si la durée totale est inférieure (§4.1, tunable). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "s")) float CooldownHideBelowTotal = 2.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") float CooldownDesaturation = 0.7f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion", meta = (Units = "s")) float ReadyFlashDuration = 0.2f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion", meta = (Units = "s")) float UltimatePulseDuration = 0.3f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar") int32 UltimateSegments = 4;
	/** Rafraîchissement du balayage et du chiffre pendant une recharge (pas de NativeTick). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityBar", meta = (Units = "s")) float CooldownRefreshInterval = 0.05f;
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
