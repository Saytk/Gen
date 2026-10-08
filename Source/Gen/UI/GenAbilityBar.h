#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GenAbilityBar.generated.h"

class UAbilitySystemComponent;
class UGenAbilitySlot;

/** Barre de sorts : 7 emplacements dans l'ordre §4.1 (LMB, RMB, Espace, 1, 2, 3, Ultime). */
UCLASS(Abstract)
class GEN_API UGenAbilityBar : public UCommonUserWidget
{
	GENERATED_BODY()

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

private:
	void HandleAbilitySystemReady(UAbilitySystemComponent* ASC);
	TArray<UGenAbilitySlot*> GetSlots() const;

	FDelegateHandle ReadyHandle;
};
