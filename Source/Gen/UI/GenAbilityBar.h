#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GenAbilityBar.generated.h"

class UAbilitySystemComponent;
class UGenAbilitySlot;
class UPanelWidget;

/** Barre de sorts : 7 emplacements dans l'ordre §4.1 (LMB, RMB, Espace, 1, 2, 3, Ultime). */
UCLASS(Abstract)
class GEN_API UGenAbilityBar : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Touche « détails des sorts » (AGenPlayerController::OnShowAbilityDetailsChanged) : l'infobulle de chaque sort,
	 * dans DetailsPanel s'il existe (une carte par sort), sinon au-dessus de chaque emplacement. Public pour les tests.
	 */
	void SetAbilityDetailsShown(bool bShown);

	bool AreAbilityDetailsShown() const { return bDetailsShown; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> SlotPrimary;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> SlotSecondary;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> SlotMobility;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> Slot1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> Slot2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> Slot3;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenAbilitySlot> SlotUltimate;

	/**
	 * Optionnel : panneau des détails (une WrapBox au-dessus de la rangée, dans une toile de taille nulle), rempli
	 * d'une infobulle par sort tant que la touche des détails est maintenue. Sans panneau, chaque emplacement montre
	 * la sienne (elles se chevauchent).
	 */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UPanelWidget> DetailsPanel;

private:
	void HandleAbilitySystemReady(UAbilitySystemComponent* ASC);
	TArray<UGenAbilitySlot*> GetSlots() const;

	FDelegateHandle ReadyHandle;
	FDelegateHandle ShowDetailsHandle;
	bool bDetailsShown = false;
};
