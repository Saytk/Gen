#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "UI/GenUIRules.h"
#include "GenAbilitySlot.generated.h"

class UAbilitySystemComponent;
class UGenGameplayAbility;
class UGenTextBlock;
class UGenUIMetrics;
class UGenUIPalette;
class UImage;
class UMaterialInstanceDynamic;
struct FActiveGameplayEffectHandle;
struct FGameplayEffectSpec;
struct FOnAttributeChangeData;

/**
 * Un emplacement de la barre de sorts (UI_Guidelines §4.1). Logique en C++, disposition et style dans WBP_AbilitySlot.
 * Piloté par événements (tags de recharge, étourdissement, énergie) ; un minuteur court ne tourne que
 * pendant une recharge ou un flash, jamais de NativeTick (§8.4).
 */
UCLASS(Abstract)
class GEN_API UGenAbilitySlot : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	void Bind(UAbilitySystemComponent* InASC);
	void Unbind();

	UFUNCTION(BlueprintPure, Category = "Gen|UI") EGenAbilitySlotState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Gen|UI") FString GetCooldownText() const { return CooldownString; }

	/** Tag d'entrée du sort affiché (InputTag.Ability.*). Posé par UGenAbilityBar, donc non éditable dans le WBP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen|UI", meta = (Categories = "InputTag")) FGameplayTag InputTag;

	/** Emplacement de l'ultime : arc d'énergie toujours visible, impulsion quand l'énergie est pleine. Posé par UGenAbilityBar. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen|UI") bool bIsUltimate = false;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> IconImage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> SweepImage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenTextBlock> KeyText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UGenTextBlock> CooldownText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> LockImage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> KeyGlyphImage;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> ArcImage;

private:
	void ResolveAbility();
	void RefreshKeyLabel();
	void RefreshCooldown();
	void RefreshVisuals();
	void StartRefreshTimer();
	void TickRefresh();
	void StartFlash(float Duration);
	void OnCooldownTagChanged(const FGameplayTag Tag, int32 NewCount);
	void OnStunTagChanged(const FGameplayTag Tag, int32 NewCount);
	void OnEnergyChanged(const FOnAttributeChangeData& Data);
	void OnEffectAdded(UAbilitySystemComponent* Target, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle);
	void UpdateUltimateArc();
	const UGenUIMetrics* GetUIMetrics() const;
	const UGenUIPalette* GetUIPalette() const;

	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FGameplayAbilitySpecHandle SpecHandle;
	TWeakObjectPtr<const UGenGameplayAbility> AbilityCDO;
	FGameplayTagContainer CooldownTags;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SweepMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ArcMID;

	TArray<TPair<FGameplayTag, FDelegateHandle>> TagHandles;
	FDelegateHandle EnergyHandle;
	FDelegateHandle MaxEnergyHandle;
	FDelegateHandle EffectAddedHandle;
	FTimerHandle RefreshTimer;
	FTimerHandle ResolveRetryTimer;

	EGenAbilitySlotState State = EGenAbilitySlotState::Empty;
	FString CooldownString;
	float CooldownEndTime = 0.f;
	float CooldownDuration = 0.f;
	float FlashStartTime = -1.f;
	float FlashDuration = 0.f;
	bool bLocked = false;
	/** Ultime pleine ET lançable lors du dernier rafraîchissement : l'impulsion part sur le front montant. */
	bool bUltimateWasReadyFull = false;
	int32 ResolveAttempts = 0;
};
