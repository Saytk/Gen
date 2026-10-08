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
class UOverlay;
class USizeBox;
class USpacer;
class UTexture;
class FViewport;
struct FActiveGameplayEffectHandle;
struct FGameplayAbilitySpec;
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
	/** Boîte du disque : SlotSize, ou UltimateSlotSize pour l'ultime (DA_UIMetrics, §4.1). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USizeBox> IconSizeBox;
	/** Bande des libellés : KeyLabelHeight (DA_UIMetrics, §3.1). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USizeBox> KeyLabelBox;
	/** Écart libellé / disque : KeyLabelGap. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USpacer> KeyGap;
	/** Disque + arc : la bande de l'arc (CostArcBand) est réservée sous le disque, l'arc la déborde (§3.1). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UOverlay> DiscStack;
	/** Boîte du chiffre de recharge (chiffre calé à droite), à la largeur de la classe de format (§2.6), centrée sur le disque au mieux. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USizeBox> CooldownTextBox;

private:
	void ApplyLayout();
	/** Cherche le sort de InputTag dans l'ASC (sauf Excluded, un spec en cours de retrait) et s'y câble. */
	void ResolveAbility(FGameplayAbilitySpecHandle Excluded = FGameplayAbilitySpecHandle());
	/** Oublie le sort résolu (tags de recharge, icône) sans délier l'ASC. */
	void ClearResolvedAbility();
	/** Sort accordé ou retiré (UGenAbilitySystemComponent::OnAbilitiesChanged) : respawn, changement de champion, sort tardif. */
	void HandleAbilitiesChanged(const FGameplayAbilitySpec& Spec, bool bRemoved);
	/** Mappings Enhanced Input reconstruits (contexte ajouté, réassignation) : on relit le libellé de touche. */
	UFUNCTION() void HandleControlMappingsRebuilt();
	/** Viewport redimensionné : l'échelle DPI change, donc l'épaisseur minimale du bord (§7.1). */
	void HandleViewportResized(FViewport* Viewport, uint32 Unused);
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
	/** Met à jour l'arc et l'impulsion ; renvoie vrai si l'ultime est pleine ET lançable (anneau à α 1.0). */
	bool UpdateUltimateArc();
	/** Largeur et décalage de la boîte du chiffre, changés seulement quand la classe de format change. */
	void UpdateCooldownTextBox();
	/** Mesure (une fois par classe) la valeur la plus étroite et la plus large de la classe dans la police du chiffre. */
	bool MeasureCooldownClass(FIntPoint FormatClass, GenUIRules::FCooldownBoxLayout& OutLayout);
	const UGenUIMetrics* GetUIMetrics() const;
	const UGenUIPalette* GetUIPalette() const;

	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FGameplayAbilitySpecHandle SpecHandle;
	TWeakObjectPtr<const UGenGameplayAbility> AbilityCDO;
	FGameplayTagContainer CooldownTags;

	/** MID de M_UI_AbilityIcon : texture du sort (paramètre Icon) et désaturation pendant la recharge (DimAmount). */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> IconMID;
	/** Texture par défaut du matériau d'icône, remise quand le slot n'a plus d'icône. */
	UPROPERTY(Transient) TObjectPtr<UTexture> DefaultIconTexture;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SweepMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ArcMID;

	/** Tags de l'ASC (étourdissement), gardés tant que l'ASC est lié. */
	TArray<TPair<FGameplayTag, FDelegateHandle>> TagHandles;
	/** Tags de recharge du sort résolu, retirés quand le sort change. */
	TArray<TPair<FGameplayTag, FDelegateHandle>> CooldownTagHandles;
	FDelegateHandle AbilitiesChangedHandle;
	FDelegateHandle EnergyHandle;
	FDelegateHandle MaxEnergyHandle;
	FDelegateHandle EffectAddedHandle;
	FTimerHandle RefreshTimer;
	FDelegateHandle ViewportResizedHandle;

	EGenAbilitySlotState State = EGenAbilitySlotState::Empty;
	FString CooldownString;
	float CooldownEndTime = 0.f;
	float CooldownDuration = 0.f;
	float FlashStartTime = -1.f;
	float FlashDuration = 0.f;
	bool bLocked = false;
	/** Ultime pleine ET lançable lors du dernier rafraîchissement : l'impulsion part sur le front montant. */
	bool bUltimateWasReadyFull = false;
	/** Boîte du chiffre par classe de format, mesurée dans le style du chiffre (TS_Cooldown) à la première utilisation. */
	TMap<FIntPoint, GenUIRules::FCooldownBoxLayout> CooldownBoxLayouts;
	FIntPoint CooldownBoxClass = FIntPoint(-1, -1);
};
