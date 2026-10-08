#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GenAbilityTooltip.generated.h"

class UBorder;
class UGenTextBlock;
class USizeBox;
struct FGenAbilityTooltipData;

/**
 * Infobulle d'un sort de la barre (UI_Guidelines §4.1, infobulle). Logique en C++, disposition et styles dans
 * Common/WBP_Tooltip : TS_Body pour le texte, panneau bg.panelRaised à coins radius.panel, libellé de touche en
 * accent.brass. Aucune couleur, police ni taille en dur : palette et métriques (DA_UIPalette, DA_UIMetrics).
 * Jamais de saisie : toujours HitTestInvisible. Fondu motion.fast par un minuteur court (pas de NativeTick, §8.4).
 */
UCLASS(Abstract)
class GEN_API UGenAbilityTooltip : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Remplit l'infobulle (contenu généré par GenAbilityTooltip::Build) et le libellé de touche de l'emplacement. */
	void SetContent(const FGenAbilityTooltipData& Data, const FText& KeyLabel);

	/** Fondu d'entrée (motion.fast) après Delay secondes. */
	void Show(float Delay);

	/** Fondu de sortie (motion.fast), puis repliée. */
	void Hide();

	/** Visible ou en train d'apparaître (tests, PIE). */
	bool IsShownOrShowing() const { return bWantShown; }

	/** Opacité affichée, 0 à 1 (tests, PIE). */
	float GetDisplayedOpacity() const { return Opacity; }

	/** Textes affichés (tests, PIE). */
	FText GetNameText() const;
	FText GetStatsText() const;
	FText GetDescriptionText() const;
	FText GetLinesText() const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenTextBlock> NameText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenTextBlock> DescriptionText;
	/** Touche du sort (« R », « LMB »), teintée accent.brass. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UGenTextBlock> KeyText;
	/** En-tête : incantation, recharge, coût, portée, nourrissage. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UGenTextBlock> StatsText;
	/** Seuils de nourrissage et lignes d'effet. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UGenTextBlock> LinesText;
	/** Panneau : bg.panelRaised, radius.panel, bordure line.bronze, marge HudPanelPadding. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UBorder> Panel;
	/** Largeur maximale (TooltipMaxWidth). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USizeBox> ContentBox;

private:
	void ApplyStyle();
	void StartFade();
	void StepFade();
	void ApplyOpacity();

	FTimerHandle DelayTimer;
	FTimerHandle FadeTimer;
	bool bWantShown = false;
	float Opacity = 0.f;
	/** Texte posé avant NativeConstruct (création puis remplissage dans la même image). */
	bool bStyled = false;
};
