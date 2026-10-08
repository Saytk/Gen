#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GenUISettings.generated.h"

class UCommonActivatableWidget;
class UGenUIKeyGlyphs;
class UGenUIMetrics;
class UGenUIPalette;

/** Où trouver les jetons et les widgets racines de l'interface (Project Settings > Game > Gen UI). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Gen UI"))
class GEN_API UGenUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Tokens") TSoftObjectPtr<UGenUIPalette> Palette;
	UPROPERTY(Config, EditAnywhere, Category = "Tokens") TSoftObjectPtr<UGenUIMetrics> Metrics;
	UPROPERTY(Config, EditAnywhere, Category = "Tokens") TSoftObjectPtr<UGenUIKeyGlyphs> KeyGlyphs;
	/** TODO Tâche 4 : restreindre à TSoftClassPtr<UGenPrimaryGameLayout> une fois la classe créée. */
	UPROPERTY(Config, EditAnywhere, Category = "Layout") TSoftClassPtr<UCommonActivatableWidget> PrimaryLayoutClass;
	UPROPERTY(Config, EditAnywhere, Category = "Layout") TSoftClassPtr<UCommonActivatableWidget> HUDLayoutClass;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
