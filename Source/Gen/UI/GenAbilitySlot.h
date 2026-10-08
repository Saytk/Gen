#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "UI/GenUIRules.h"
#include "GenAbilitySlot.generated.h"

class UAbilitySystemComponent;
class UCanvasPanel;
class UGenAbilityTooltip;
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
struct FGenAbilityTooltipData;

/**
 * Un emplacement de la barre de sorts (UI_Guidelines §4.1). Logique en C++, disposition et style dans WBP_AbilitySlot.
 * Piloté par événements (tags de recharge, contrôles durs, énergie) ; un minuteur court ne tourne que
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

	/** Arc de coût affiché (tests, PIE) : segments du sort, emplacements de l'arc, segments financés (0 sans arc). */
	int32 GetArcSegments() const { return ArcSegments; }
	int32 GetArcSegmentSlots() const { return ArcSegmentSlots; }
	int32 GetArcFunded() const { return ArcFunded; }

	/** Flash du bord en cours (prêt ou impulsion de l'ultime). */
	bool IsFlashing() const;

	/** Tag d'entrée du sort affiché (InputTag.Ability.*). Posé par UGenAbilityBar, donc non éditable dans le WBP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen|UI", meta = (Categories = "InputTag")) FGameplayTag InputTag;

	/** Emplacement de l'ultime : arc d'énergie toujours visible, impulsion quand l'énergie est pleine. Posé par UGenAbilityBar. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen|UI") bool bIsUltimate = false;

	/** Classe de l'infobulle (Common/WBP_Tooltip). Sans classe : pas d'infobulle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gen|UI") TSubclassOf<UGenAbilityTooltip> TooltipClass;

	/** Contenu de l'infobulle du sort affiché (valeurs de jeu de son CDO). Faux si l'emplacement est vide. */
	bool BuildTooltipData(FGenAbilityTooltipData& Out) const;

	/** Libellé de la touche du sort (texte court, même si un glyphe est affiché). */
	const FText& GetKeyLabelText() const { return KeyLabelText; }

	/**
	 * Survol du disque (sondé par un minuteur, la souris restant au jeu) : infobulle après TooltipHoverDelay, cachée
	 * dès que le curseur sort. Public pour les tests.
	 */
	void SetTooltipHovered(bool bHovered);

	/**
	 * Touche « détails des sorts » maintenue (UGenAbilityBar). bInPlace : la barre n'a pas de panneau des détails,
	 * l'emplacement montre sa propre infobulle sans délai ; sinon il cache seulement son infobulle de survol.
	 */
	void SetDetailsShown(bool bShown, bool bInPlace);

	/** Infobulle de survol de l'emplacement (nulle avant le premier survol ; tests, PIE). */
	UGenAbilityTooltip* GetHoverTooltip() const { return HoverTooltip; }

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
	/**
	 * Toile de taille nulle en haut de l'emplacement : l'infobulle de survol y est posée au-dessus du disque, sans
	 * changer la disposition de la barre (et sans AddToViewport, §8.1).
	 */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCanvasPanel> TooltipCanvas;

private:
	void ApplyLayout();
	/**
	 * Cherche le sort de InputTag dans l'ASC (sauf Excluded, un spec en cours de retrait) et s'y câble. Plusieurs sorts sur
	 * la touche (revue finale, M-9) : celui que les tags du propriétaire choisissent (le sort qui partirait), sinon le
	 * premier ; l'emplacement suit les changements de ces tags (OnSelectionTagChanged).
	 */
	void ResolveAbility(FGameplayAbilitySpecHandle Excluded = FGameplayAbilitySpecHandle());
	/** Spec que la touche lancerait maintenant (voir ResolveAbility) ; nul s'il n'y en a aucun. */
	const FGameplayAbilitySpec* FindPreferredSpec(FGameplayAbilitySpecHandle Excluded = FGameplayAbilitySpecHandle()) const;
	/** Un tag qui départage les sorts de la touche change (ex. State.Curffe.Ablaze) : nouveau sort affiché s'il change. */
	void OnSelectionTagChanged(const FGameplayTag Tag, int32 NewCount);
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
	/** Un contrôle dur (étourdi, silence, peur, neutralisé) apparaît ou disparaît : état Locked (§4.1). */
	void OnStunTagChanged(const FGameplayTag Tag, int32 NewCount);
	/** L'énergie couvre-t-elle le coût du sort (même règle que CheckCost, GenEnergy::CanAfford) ? Vrai pour un sort gratuit. */
	bool CanAffordAbility() const;
	void OnEnergyChanged(const FOnAttributeChangeData& Data);
	void OnEffectAdded(UAbilitySystemComponent* Target, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle);
	/**
	 * Arc de coût (§4.1) : sur l'ultime, et sur tout sort qui coûte de l'énergie (R : 1 segment, F : 4).
	 * Gère aussi l'impulsion de l'ultime ; renvoie vrai si l'ultime est pleine ET lançable (anneau à α 1.0).
	 */
	bool UpdateCostArc();
	/** Largeur et décalage de la boîte du chiffre, changés seulement quand la classe de format change. */
	void UpdateCooldownTextBox();
	/** Mesure (une fois par classe) la valeur la plus étroite et la plus large de la classe dans la police du chiffre. */
	bool MeasureCooldownClass(FIntPoint FormatClass, GenUIRules::FCooldownBoxLayout& OutLayout);
	/** Sondage du curseur sur le disque (pas de NativeTick, la capture de la souris par le jeu masque les survols UMG). */
	void PollHover();
	/** Crée l'infobulle de survol dans TooltipCanvas, une fois. */
	UGenAbilityTooltip* EnsureHoverTooltip();
	/** Remplit Tooltip avec le sort affiché ; faux sans sort. */
	bool FillTooltip(UGenAbilityTooltip& Tooltip) const;
	void ShowHoverTooltip(float Delay);

	const UGenUIMetrics* GetUIMetrics() const;
	const UGenUIPalette* GetUIPalette() const;

	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FGameplayAbilitySpecHandle SpecHandle;
	TWeakObjectPtr<const UGenGameplayAbility> AbilityCDO;
	FGameplayTagContainer CooldownTags;

	/**
	 * MID de M_UI_AbilityIcon : texture du sort (paramètre Icon), désaturation pendant la recharge (DimAmount) et
	 * luminosité sans assez d'énergie (Brightness, §4.1).
	 */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> IconMID;
	/** Texture par défaut du matériau d'icône, remise quand le slot n'a plus d'icône. */
	UPROPERTY(Transient) TObjectPtr<UTexture> DefaultIconTexture;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SweepMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ArcMID;

	/** Tags de l'ASC (contrôles durs), gardés tant que l'ASC est lié. */
	TArray<TPair<FGameplayTag, FDelegateHandle>> TagHandles;
	/** Tags de recharge du sort résolu, retirés quand le sort change. */
	TArray<TPair<FGameplayTag, FDelegateHandle>> CooldownTagHandles;
	/** Tags qui départagent les sorts de la touche (M-9), retirés quand le sort change. */
	TArray<TPair<FGameplayTag, FDelegateHandle>> SelectionTagHandles;
	FDelegateHandle AbilitiesChangedHandle;
	FDelegateHandle EnergyHandle;
	FDelegateHandle MaxEnergyHandle;
	FDelegateHandle EffectAddedHandle;
	FTimerHandle RefreshTimer;
	FDelegateHandle ViewportResizedHandle;
	FTimerHandle HoverPollTimer;

	UPROPERTY(Transient) TObjectPtr<UGenAbilityTooltip> HoverTooltip;
	FText KeyLabelText;
	bool bHovered = false;
	bool bDetailsShown = false;
	bool bDetailsInPlace = false;

	EGenAbilitySlotState State = EGenAbilitySlotState::Empty;
	int32 ArcSegments = 0;
	int32 ArcSegmentSlots = 0;
	int32 ArcFunded = 0;
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
