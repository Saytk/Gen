#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Game/GenDevTuning.h"
#include "GenDevPanel.generated.h"

class UButton;
class UCheckBox;
class UCommonTextBlock;
class USlider;
class UVerticalBox;

/**
 * Panneau développeur (F10, hors Shipping) : recharges, incantations, énergie, ressource, invulnérabilité et vitesse
 * du jeu, envoyés au serveur (AGenPlayerController::ServerSetDevTuning) qui les réplique à tous.
 * Construit en C++ sur la couche UI.Layer.GameMenu ; couleurs, tailles et texte viennent des jetons (UI_Guidelines §8.3).
 * Le jeu reste jouable panneau ouvert (mode d'entrée All) ; il ne prend pas le focus clavier.
 */
UCLASS()
class GEN_API UGenDevPanel : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override { return nullptr; }
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;

private:
	void BuildTree();
	UCommonTextBlock* MakeText(const FText& Text, bool bAccent = false);
	UCheckBox* AddToggle(UVerticalBox* Box, const FText& Label);
	USlider* AddSlider(UVerticalBox* Box, float Min, float Max, float Step, TObjectPtr<UCommonTextBlock>& OutLabel);
	UButton* AddButton(UVerticalBox* Box, const FText& Label);

	/** Réglages lus dans les contrôles (curseurs arrondis à leur pas). */
	FGenDevTuning ReadWidgets() const;
	/** Contrôles mis aux réglages répliqués, sans renvoi au serveur. */
	void RefreshFromTuning();
	void RefreshSliderLabels();
	void SendTuning();

	UFUNCTION()
	void HandleToggleChanged(bool bChecked);
	UFUNCTION()
	void HandleSliderChanged(float Value);
	UFUNCTION()
	void HandleSliderReleased();
	UFUNCTION()
	void HandleRefillClicked();
	UFUNCTION()
	void HandleResetClicked();

	UPROPERTY(Transient) TObjectPtr<UCheckBox> NoCooldownsBox;
	UPROPERTY(Transient) TObjectPtr<USlider> CooldownSlider;
	UPROPERTY(Transient) TObjectPtr<UCommonTextBlock> CooldownLabel;
	UPROPERTY(Transient) TObjectPtr<USlider> CastTimeSlider;
	UPROPERTY(Transient) TObjectPtr<UCommonTextBlock> CastTimeLabel;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> InfiniteEnergyBox;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> InfiniteResourceBox;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> InvulnerableBox;
	UPROPERTY(Transient) TObjectPtr<USlider> GameSpeedSlider;
	UPROPERTY(Transient) TObjectPtr<UCommonTextBlock> GameSpeedLabel;

	FDelegateHandle TuningChangedHandle;
	/** Vrai pendant RefreshFromTuning : les contrôles changent sans renvoyer les réglages au serveur. */
	bool bApplyingTuning = false;
};
