#include "UI/GenAbilitySlot.h"

#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAbilityTooltipData.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenEnergy.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameplayEffect.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/VerticalBoxSlot.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "GenGameplayTags.h"
#include "Input/GenInputConfig.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/GenPlayerController.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UI/GenAbilityTooltip.h"
#include "UI/GenTextBlock.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUILog.h"
#include "UI/GenUISubsystem.h"

void UGenAbilitySlot::NativeConstruct()
{
	Super::NativeConstruct();

	if (IconImage)
	{
		IconMID = IconImage->GetDynamicMaterial();
		UE_CLOG(!IconMID, LogGenUI, Warning, TEXT("%s : IconImage n'a pas de matériau (M_UI_AbilityIcon attendu), icône et désaturation invisibles."), *GetPathName());
		if (IconMID)
		{
			UTexture* Default = nullptr;
			IconMID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Icon")), Default);
			DefaultIconTexture = Default;
		}
	}
	if (SweepImage)
	{
		SweepMID = SweepImage->GetDynamicMaterial();
		UE_CLOG(!SweepMID, LogGenUI, Warning, TEXT("%s : SweepImage n'a pas de matériau (M_UI_CooldownSweep attendu), recharge et flash invisibles."), *GetPathName());
	}
	if (ArcImage)
	{
		ArcMID = ArcImage->GetDynamicMaterial();
		UE_CLOG(!ArcMID, LogGenUI, Warning, TEXT("%s : ArcImage n'a pas de matériau (M_UI_SegmentArc attendu), arc d'énergie invisible."), *GetPathName());
	}
	ApplyLayout();
	RefreshVisuals();

	// Les touches se lisent dans les mappings actifs, reconstruits au tick qui suit un AddMappingContext
	// ou une réassignation : le libellé lu au Bind peut être vide, on le relit à chaque reconstruction (§8.4)
	if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetOwningLocalPlayer()))
	{
		Input->ControlMappingsRebuiltDelegate.AddUniqueDynamic(this, &ThisClass::HandleControlMappingsRebuilt);
	}

	// L'échelle DPI suit la taille du viewport : le bord doit rester >= 1 px physique après un redimensionnement (§7.1)
	if (!ViewportResizedHandle.IsValid())
	{
		ViewportResizedHandle = FViewport::ViewportResizedEvent.AddUObject(this, &ThisClass::HandleViewportResized);
	}
}

void UGenAbilitySlot::HandleControlMappingsRebuilt()
{
	RefreshKeyLabel();
}

void UGenAbilitySlot::HandleViewportResized(FViewport* Viewport, uint32 Unused)
{
	RefreshVisuals();
}

void UGenAbilitySlot::ApplyLayout()
{
	const UGenUIMetrics* Metrics = GetUIMetrics();
	const UGenUIPalette* Palette = GetUIPalette();

	// Disque de 64 px, 72 px pour l'ultime (§4.1)
	if (IconSizeBox)
	{
		const float Size = bIsUltimate ? Metrics->UltimateSlotSize : Metrics->SlotSize;
		IconSizeBox->SetWidthOverride(Size);
		IconSizeBox->SetHeightOverride(Size);
	}
	if (ArcImage)
	{
		ArcImage->SetVisibility(bIsUltimate ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		// L'arc déborde le disque de la bande de l'arc de chaque côté (image carrée centrée sur le disque)
		if (UOverlaySlot* ArcSlot = Cast<UOverlaySlot>(ArcImage->Slot))
		{
			ArcSlot->SetPadding(FMargin(-Metrics->CostArcBand));
		}
	}

	// Bandes verticales du §3.1 : libellé, écart, disque, arc
	if (KeyLabelBox)
	{
		KeyLabelBox->SetHeightOverride(Metrics->KeyLabelHeight);
	}
	if (KeyGap)
	{
		KeyGap->SetSize(FVector2D(1.f, Metrics->KeyLabelGap));
	}
	if (DiscStack)
	{
		if (UVerticalBoxSlot* DiscSlot = Cast<UVerticalBoxSlot>(DiscStack->Slot))
		{
			DiscSlot->SetPadding(FMargin(0.f, 0.f, 0.f, Metrics->CostArcBand));
		}
	}

	// Glyphes d'interface (souris, cadenas) : textures blanches teintées en text.primary (§2.11)
	if (KeyGlyphImage)
	{
		KeyGlyphImage->SetColorAndOpacity(Palette->Text_Primary);
	}
	LockImage->SetColorAndOpacity(Palette->Text_Primary);

	// Chiffre calé à droite dans sa boîte : le bord droit reste fixe quand la valeur change (§2.6).
	// Déjà réglé dans WBP_AbilitySlot ; on l'impose car UpdateCooldownTextBox en dépend.
	CooldownText->SetJustification(ETextJustify::Right);
}

void UGenAbilitySlot::NativeDestruct()
{
	if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetOwningLocalPlayer()))
	{
		Input->ControlMappingsRebuiltDelegate.RemoveDynamic(this, &ThisClass::HandleControlMappingsRebuilt);
	}
	FViewport::ViewportResizedEvent.Remove(ViewportResizedHandle);
	ViewportResizedHandle.Reset();
	Unbind();
	Super::NativeDestruct();
}

void UGenAbilitySlot::Bind(UAbilitySystemComponent* InASC)
{
	// bIsUltimate est posé par la barre après notre NativeConstruct, et la palette n'est sûre qu'avec le joueur local :
	// on réapplique taille, arc et teintes
	ApplyLayout();

	Unbind();
	ASC = InASC;

	if (!ASC.IsValid())
	{
		RefreshVisuals();
		return;
	}

	// Contrôle dur qui empêche de lancer (étourdi, silence, peur, neutralisé) => bloqué (§4.1 Locked)
	for (const FGameplayTag& HardCCTag : GenGameplayTags::GetHardCCTags())
	{
		FDelegateHandle LockHandle = ASC->RegisterGameplayTagEvent(HardCCTag, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::OnStunTagChanged);
		TagHandles.Emplace(HardCCTag, LockHandle);
	}
	bLocked = ASC->HasAnyMatchingGameplayTags(GenGameplayTags::GetHardCCTags());

	// Le GE de recharge du serveur remplace le GE prédit (compte 1 -> 2 -> 1, aucun événement de tag) : on relit la recharge (§8.2)
	EffectAddedHandle = ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddUObject(this, &ThisClass::OnEffectAdded);

	// Énergie : arc de coût et état « pas assez d'énergie » (R, F), impulsion de l'ultime. Le coût du sort n'est connu
	// qu'après ResolveAbility (réessais) : chaque emplacement écoute, un événement par changement d'énergie (§8.4).
	EnergyHandle = ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).AddUObject(this, &ThisClass::OnEnergyChanged);
	MaxEnergyHandle = ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetMaxEnergyAttribute()).AddUObject(this, &ThisClass::OnEnergyChanged);

	if (bIsUltimate)
	{
		// Déjà pleine au moment du Bind (respawn, rebind) : pas d'impulsion
		const int32 Segments = GetUIMetrics()->UltimateSegments;
		bUltimateWasReadyFull = Segments > 0 && GenUIRules::FundedSegments(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()), ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()), Segments) == Segments;
	}

	// Sorts accordés après l'ASC (réplication des specs), retirés au respawn ou au changement de champion :
	// événements de l'ASC, pas de sondage
	if (UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(InASC))
	{
		AbilitiesChangedHandle = GenASC->OnAbilitiesChanged.AddUObject(this, &ThisClass::HandleAbilitiesChanged);
	}

	ResolveAbility();
	RefreshKeyLabel();

	// Survol du disque pour l'infobulle : sondé (la souris reste capturée par le jeu), seulement avec une infobulle
	if (TooltipClass && GetWorld() && !IsRunningDedicatedServer())
	{
		GetWorld()->GetTimerManager().SetTimer(HoverPollTimer, this, &ThisClass::PollHover, GetUIMetrics()->TooltipPollInterval, true);
	}
}

void UGenAbilitySlot::Unbind()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
		World->GetTimerManager().ClearTimer(HoverPollTimer);
	}
	SetTooltipHovered(false);

	ClearResolvedAbility();

	if (ASC.IsValid())
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Pair : TagHandles)
		{
			ASC->RegisterGameplayTagEvent(Pair.Key, EGameplayTagEventType::NewOrRemoved).Remove(Pair.Value);
		}
		if (EnergyHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).Remove(EnergyHandle);
		}
		if (MaxEnergyHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetMaxEnergyAttribute()).Remove(MaxEnergyHandle);
		}
		if (EffectAddedHandle.IsValid())
		{
			ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(EffectAddedHandle);
		}
		if (UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(ASC.Get()))
		{
			GenASC->OnAbilitiesChanged.Remove(AbilitiesChangedHandle);
		}
	}

	TagHandles.Reset();
	EnergyHandle.Reset();
	MaxEnergyHandle.Reset();
	EffectAddedHandle.Reset();
	AbilitiesChangedHandle.Reset();
	ASC.Reset();

	// Un rebind en plein flash ne doit pas figer un bord à moitié allumé
	FlashStartTime = -1.f;
	FlashDuration = 0.f;
	if (SweepMID)
	{
		SweepMID->SetScalarParameterValue(TEXT("RimFlash"), 0.f);
	}
}

void UGenAbilitySlot::ClearResolvedAbility()
{
	if (ASC.IsValid())
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Pair : CooldownTagHandles)
		{
			ASC->RegisterGameplayTagEvent(Pair.Key, EGameplayTagEventType::NewOrRemoved).Remove(Pair.Value);
		}
	}
	CooldownTagHandles.Reset();

	SpecHandle = FGameplayAbilitySpecHandle();
	AbilityCDO.Reset();
	CooldownTags.Reset();
	// Remis à zéro : la recharge relue pour le nouveau sort ne déclenche pas de flash de fin
	CooldownEndTime = 0.f;
	CooldownDuration = 0.f;

	// Pas d'icône du sort précédent sur un slot délié ou en attente de son sort
	if (IconMID)
	{
		IconMID->SetTextureParameterValue(TEXT("Icon"), DefaultIconTexture);
	}
}

void UGenAbilitySlot::HandleAbilitiesChanged(const FGameplayAbilitySpec& Spec, bool bRemoved)
{
	if (bRemoved)
	{
		// Notre sort part (respawn, changement de champion) : on se recâble sur un autre sort de la même touche s'il y en a
		if (Spec.Handle == SpecHandle)
		{
			ClearResolvedAbility();
			ResolveAbility(Spec.Handle);
		}
		return;
	}

	// Sort accordé pour notre touche (sort tardif, nouveau corps) : il remplace l'ancien handle
	if (Spec.Handle != SpecHandle && Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
	{
		ClearResolvedAbility();
		ResolveAbility();
	}
}

void UGenAbilitySlot::ResolveAbility(FGameplayAbilitySpecHandle Excluded)
{
	if (!ASC.IsValid())
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		const UGenGameplayAbility* Ability = Cast<UGenGameplayAbility>(Spec.Ability);
		if (Ability && Spec.Handle != Excluded && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			SpecHandle = Spec.Handle;
			AbilityCDO = Ability;
			if (const FGameplayTagContainer* Tags = Ability->GetCooldownTags())
			{
				CooldownTags = *Tags;
			}
			break;
		}
	}

	if (!AbilityCDO.IsValid())
	{
		// Pas encore de sort pour cette touche (côté client, les specs arrivent après l'ASC) : HandleAbilitiesChanged
		// nous rappellera quand il sera accordé
		RefreshVisuals();
		return;
	}

	for (const FGameplayTag& Tag : CooldownTags)
	{
		FDelegateHandle Handle = ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::OnCooldownTagChanged);
		CooldownTagHandles.Emplace(Tag, Handle);
	}

	// Par le paramètre du MID et non SetBrushFromTexture, qui remplacerait le matériau (cercle, désaturation)
	if (IconMID)
	{
		// Sort sans icône : texture par défaut du matériau, jamais l'icône d'un sort précédent
		UTexture2D* Icon = AbilityCDO->Icon.LoadSynchronous();
		IconMID->SetTextureParameterValue(TEXT("Icon"), Icon ? Icon : DefaultIconTexture.Get());
	}
	// Infobulle déjà ouverte (sort accordé ou remplacé pendant le survol) : contenu relu
	if (HoverTooltip && HoverTooltip->IsShownOrShowing())
	{
		FillTooltip(*HoverTooltip);
	}

	RefreshCooldown();
}

void UGenAbilitySlot::RefreshKeyLabel()
{
	const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
	const AGenPlayerController* PC = Cast<AGenPlayerController>(GetOwningPlayer());
	const UGenInputConfig* InputConfig = PC ? PC->GetInputConfig() : nullptr;
	if (!UI || !InputConfig)
	{
		return;
	}

	const UInputAction* Action = nullptr;
	for (const FGenAbilityInputAction& Binding : InputConfig->AbilityInputActions)
	{
		if (Binding.InputTag == InputTag)
		{
			Action = Binding.InputAction;
			break;
		}
	}

	FText Label;
	UTexture2D* Glyph = nullptr;
	UI->ResolveKeyLabel(Action, Label, Glyph);
	KeyLabelText = Label;

	if (KeyGlyphImage && Glyph)
	{
		KeyGlyphImage->SetBrushFromTexture(Glyph);
		KeyGlyphImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		KeyText->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		if (KeyGlyphImage)
		{
			KeyGlyphImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		KeyText->SetText(Label);
		KeyText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UGenAbilitySlot::RefreshCooldown()
{
	float Remaining = 0.f;
	float Duration = 0.f;
	if (ASC.IsValid() && AbilityCDO.IsValid() && SpecHandle.IsValid())
	{
		AbilityCDO->GetCooldownTimeRemainingAndDuration(SpecHandle, ASC->AbilityActorInfo.Get(), Remaining, Duration);
		// Le GE répliqué du serveur peut annoncer un peu plus que la durée (heure serveur estimée en retard)
		Remaining = GenUIRules::ClampCooldownRemaining(Remaining, Duration);
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	CooldownEndTime = Remaining > 0.f ? Now + Remaining : 0.f;
	CooldownDuration = Duration;

	if (Remaining > 0.f)
	{
		StartRefreshTimer();
	}

	// Fin de recharge : le flash « prêt » part dans RefreshVisuals, au passage à l'état prêt seulement (§4.1, revue P3
	// T8-10, M4) : un événement de tag en retard (prêt -> prêt) ou une recharge qui finit sans assez d'énergie n'en fait pas
	RefreshVisuals();
}

void UGenAbilitySlot::StartRefreshTimer()
{
	if (UWorld* World = GetWorld())
	{
		const float Interval = GetUIMetrics()->CooldownRefreshInterval;
		if (!World->GetTimerManager().IsTimerActive(RefreshTimer))
		{
			World->GetTimerManager().SetTimer(RefreshTimer, this, &ThisClass::TickRefresh, Interval, true);
		}
	}
}

void UGenAbilitySlot::TickRefresh()
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const bool bCooling = CooldownEndTime > Now;
	const bool bFlashing = FlashStartTime >= 0.f && Now - FlashStartTime < FlashDuration;

	if (!bCooling && CooldownEndTime > 0.f)
	{
		// Recharge terminée localement : on relit l'ASC (autorité) puis on flashe
		RefreshCooldown();
		return;
	}

	if (!bCooling && !bFlashing)
	{
		FlashStartTime = -1.f;
		GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
	}

	RefreshVisuals();
}

bool UGenAbilitySlot::IsFlashing() const
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	return FlashStartTime >= 0.f && Now - FlashStartTime < FlashDuration;
}

void UGenAbilitySlot::StartFlash(float Duration)
{
	FlashStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	FlashDuration = FMath::Max(Duration, 0.01f);
	StartRefreshTimer();
}

void UGenAbilitySlot::RefreshVisuals()
{
	const UGenUIMetrics* Metrics = GetUIMetrics();
	const UGenUIPalette* Palette = GetUIPalette();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Remaining = FMath::Max(CooldownEndTime - Now, 0.f);

	const EGenAbilitySlotState OldState = State;
	State = GenUIRules::ResolveSlotState(AbilityCDO.IsValid(), bLocked, Remaining, CanAffordAbility());
	if (GenUIRules::IsReadyFlash(OldState, State))
	{
		// Flash « prêt » (§4.1) : l'emplacement devient lançable (fin de recharge, ou assez d'énergie)
		StartFlash(Metrics->ReadyFlashDuration);
	}
	CooldownString = State == EGenAbilitySlotState::Cooldown ? GenUIRules::FormatCooldown(Remaining, CooldownDuration, Metrics->CooldownHideBelowTotal) : FString();

	IconImage->SetVisibility(State == EGenAbilitySlotState::Empty ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	UpdateCooldownTextBox();
	// Séparateur décimal de la culture courante ("0,6" en français) ; CooldownString reste invariant pour la classe de format
	CooldownText->SetText(GenUIRules::CooldownDisplayText(CooldownString));
	CooldownText->SetVisibility(CooldownString.IsEmpty() ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	LockImage->SetVisibility(State == EGenAbilitySlotState::Locked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);

	// Avant le balayage : l'impulsion de l'ultime peut démarrer un flash à appliquer dès cette passe
	const bool bUltimateReady = UpdateCostArc();

	// La désaturation de la recharge s'applique à l'icône, pas au voile du balayage (§4.1)
	if (IconMID)
	{
		IconMID->SetScalarParameterValue(TEXT("DimAmount"), State == EGenAbilitySlotState::Cooldown ? Metrics->CooldownDesaturation : 0.f);
		// Pas assez d'énergie : icône à 55 % de luminosité (§4.1)
		IconMID->SetScalarParameterValue(TEXT("Brightness"), State == EGenAbilitySlotState::NoEnergy ? Metrics->NoEnergyBrightness : 1.f);
	}

	if (SweepMID)
	{
		// Pas assez d'énergie : voile cooldown.noEnergy sur tout le disque (balayage plein), sans chiffre ni mouvement
		const bool bNoEnergy = State == EGenAbilitySlotState::NoEnergy;
		const float Progress = bNoEnergy ? 1.f : ((State == EGenAbilitySlotState::Cooldown && CooldownDuration > 0.f) ? Remaining / CooldownDuration : 0.f);
		float Flash = 0.f;
		if (FlashStartTime >= 0.f && FlashDuration > 0.f)
		{
			// Ease-out (§5.1) : 1 -> 0 sur la durée du flash
			const float Alpha = FMath::Clamp((Now - FlashStartTime) / FlashDuration, 0.f, 1.f);
			Flash = 1.f - FMath::InterpEaseOut(0.f, 1.f, Alpha, 3.f);
		}
		SweepMID->SetScalarParameterValue(TEXT("Progress"), Progress);
		SweepMID->SetScalarParameterValue(TEXT("RimFlash"), Flash);
		// Épaisseur du bord en rayons du disque : 1 px sur 64, anneau de 2 px sur l'ultime de 72 (§4.1),
		// jamais sous 1 px physique quand l'échelle DPI descend sous 1 (§7.1) ; relue à chaque passe et à chaque redimensionnement
		const float SlotSize = bIsUltimate ? Metrics->UltimateSlotSize : Metrics->SlotSize;
		const float RimPx = GenUIRules::RimLayoutWidth(bIsUltimate ? Metrics->UltimateRimWidth : Metrics->SlotRimWidth, UWidgetLayoutLibrary::GetViewportScale(this));
		SweepMID->SetScalarParameterValue(TEXT("RimWidth"), RimPx / FMath::Max(SlotSize * 0.5f, 1.f));
		SweepMID->SetScalarParameterValue(TEXT("Locked"), State == EGenAbilitySlotState::Locked ? 1.f : 0.f);
		SweepMID->SetVectorParameterValue(TEXT("OverlayColour"), State == EGenAbilitySlotState::Locked ? Palette->Cooldown_Locked
			: (bNoEnergy ? Palette->Cooldown_NoEnergy : Palette->Cooldown_Overlay));
		// Bord : line.bronze ; ultime : anneau energy.full à α 0.5, α 1.0 seulement pleine ET lançable (§4.1)
		FLinearColor RimColour = Palette->Line_Bronze;
		if (bIsUltimate)
		{
			RimColour = Palette->Energy_Full;
			RimColour.A = bUltimateReady ? 1.f : 0.5f;
		}
		SweepMID->SetVectorParameterValue(TEXT("RimColour"), RimColour);
		SweepMID->SetVectorParameterValue(TEXT("FlashColour"), Palette->Flash_White);
	}
}

void UGenAbilitySlot::OnCooldownTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	RefreshCooldown();
}

void UGenAbilitySlot::OnStunTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	// Plusieurs tags bloquent : on relit l'ensemble plutôt que le compte de celui qui change
	bLocked = ASC.IsValid() && ASC->HasAnyMatchingGameplayTags(GenGameplayTags::GetHardCCTags());
	RefreshVisuals();
}

bool UGenAbilitySlot::CanAffordAbility() const
{
	const float Cost = AbilityCDO.IsValid() ? AbilityCDO->EnergyCost : 0.f;
	return !ASC.IsValid() || GenEnergy::CanAfford(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()), Cost);
}

void UGenAbilitySlot::OnEnergyChanged(const FOnAttributeChangeData& Data)
{
	RefreshVisuals();
}

void UGenAbilitySlot::OnEffectAdded(UAbilitySystemComponent* Target, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle)
{
	if (CooldownTags.IsEmpty())
	{
		return;
	}

	FGameplayTagContainer GrantedTags;
	Spec.GetAllGrantedTags(GrantedTags);
	if (GrantedTags.HasAny(CooldownTags))
	{
		RefreshCooldown();
	}
}

bool UGenAbilitySlot::UpdateCostArc()
{
	const UGenUIMetrics* Metrics = GetUIMetrics();
	const UGenUIPalette* Palette = GetUIPalette();

	// Toujours sur l'ultime ; ailleurs seulement si le sort coûte de l'énergie (R : 1 segment). Un segment garde la
	// taille d'un segment de l'ultime (SegmentSlots).
	const float MaxEnergy = ASC.IsValid() ? ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()) : 0.f;
	const float Cost = AbilityCDO.IsValid() ? AbilityCDO->EnergyCost : 0.f;
	const int32 CostSegments = GenUIRules::CostSegments(Cost, MaxEnergy, Metrics->UltimateSegments);
	const int32 Segments = CostSegments > 0 ? CostSegments : (bIsUltimate ? Metrics->UltimateSegments : 0);

	if (ArcImage)
	{
		ArcImage->SetVisibility(Segments > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	ArcSegments = 0;
	ArcSegmentSlots = 0;
	ArcFunded = 0;
	if (!ASC.IsValid() || Segments == 0)
	{
		return false;
	}

	// Revue P3 T8-10, M5 : un sort à coût suit la règle de CheckCost (GenEnergy::CanAfford sur son vrai coût) ; une
	// ultime gratuite garde les tranches de MaxEnergy
	const float Energy = ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute());
	const int32 Funded = Cost > 0.f
		? GenUIRules::CostFundedSegments(Energy, Cost, Segments)
		: FMath::Min(GenUIRules::FundedSegments(Energy, MaxEnergy, Metrics->UltimateSegments), Segments);
	ArcSegments = Segments;
	ArcSegmentSlots = Metrics->UltimateSegments;
	ArcFunded = Funded;
	const bool bFull = Funded == Segments;
	// energy.full et son contour : seulement l'arc complet de l'ultime (§4.1)
	const bool bUltimateFull = bIsUltimate && bFull;

	if (ArcMID)
	{
		ArcMID->SetScalarParameterValue(TEXT("SegmentSlots"), Metrics->UltimateSegments);
		ArcMID->SetScalarParameterValue(TEXT("Segments"), Segments);
		ArcMID->SetScalarParameterValue(TEXT("Funded"), Funded);
		ArcMID->SetScalarParameterValue(TEXT("FullOutline"), bUltimateFull ? 1.f : 0.f);
		ArcMID->SetVectorParameterValue(TEXT("Colour"), bUltimateFull ? Palette->Energy_Full : Palette->Energy_Charging);
		// Segments non financés : contour creux text.secondary ; contour de l'arc plein : text.primary (§4.1)
		ArcMID->SetVectorParameterValue(TEXT("HollowColour"), Palette->Text_Secondary);
		ArcMID->SetVectorParameterValue(TEXT("OutlineColour"), Palette->Text_Primary);
	}

	if (!bIsUltimate)
	{
		return false;
	}

	// Ultime prête : une seule impulsion de 300 ms via le RimFlash du balayage (indépendante de l'arc), jamais de boucle (§4.1).
	// Seulement quand elle est lançable : pleine pendant un contrôle dur ou une recharge => impulsion quand elle le redevient.
	// Tant que le sort n'est pas résolu (Empty), on garde l'état précédent.
	if (State != EGenAbilitySlotState::Empty)
	{
		const bool bReadyFull = bFull && State == EGenAbilitySlotState::Ready;
		if (bReadyFull && !bUltimateWasReadyFull)
		{
			StartFlash(Metrics->UltimatePulseDuration);
		}
		bUltimateWasReadyFull = bReadyFull;
	}

	// État courant, sans le verrou de l'impulsion : une ultime vide, non lançable ou sans assez d'énergie n'a jamais l'anneau à α 1.0
	return bFull && State == EGenAbilitySlotState::Ready;
}

void UGenAbilitySlot::UpdateCooldownTextBox()
{
	if (!CooldownTextBox || CooldownString.IsEmpty())
	{
		return;
	}

	const FIntPoint FormatClass = GenUIRules::CooldownFormatClass(CooldownString);
	if (FormatClass == CooldownBoxClass)
	{
		return;
	}

	GenUIRules::FCooldownBoxLayout Layout;
	if (!MeasureCooldownClass(FormatClass, Layout))
	{
		return;
	}

	CooldownBoxClass = FormatClass;
	CooldownTextBox->SetWidthOverride(Layout.Width);
	// Boîte centrée par l'Overlay : une marge droite de 2 x d déplace son centre de d vers la gauche
	if (UOverlaySlot* BoxSlot = Cast<UOverlaySlot>(CooldownTextBox->Slot))
	{
		BoxSlot->SetPadding(FMargin(FMath::Max(2.f * Layout.CentreShift, 0.f), 0.f, FMath::Max(-2.f * Layout.CentreShift, 0.f), 0.f));
	}
}

bool UGenAbilitySlot::MeasureCooldownClass(FIntPoint FormatClass, GenUIRules::FCooldownBoxLayout& OutLayout)
{
	if (const GenUIRules::FCooldownBoxLayout* Cached = CooldownBoxLayouts.Find(FormatClass))
	{
		OutLayout = *Cached;
		return true;
	}

	// Mesures dans la police du style (TS_Cooldown) : rien n'est codé en dur
	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return false;
	}
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo& Font = CooldownText->GetFont();
	// La mesure ignore le contour : on l'ajoute des deux côtés
	const float OutlineSize = Font.OutlineSettings.OutlineSize;

	const TArray<FString> Samples = GenUIRules::CooldownClassSamples(FormatClass);
	if (Samples.IsEmpty())
	{
		// 3 chiffres et plus : chiffre le plus large par position, boîte centrée
		float WidestDigit = 0.f;
		for (TCHAR Digit = TEXT('0'); Digit <= TEXT('9'); ++Digit)
		{
			WidestDigit = FMath::Max(WidestDigit, static_cast<float>(FontMeasure->Measure(FString::Chr(Digit), Font).X));
		}
		const FString Separator = GenUIRules::CooldownDisplayText(TEXT("0.5")).ToString().Mid(1, 1);
		OutLayout.Width = GenUIRules::CooldownBoxWidth(FormatClass, WidestDigit, FontMeasure->Measure(Separator.IsEmpty() ? FString(TEXT(".")) : Separator, Font).X, OutlineSize);
		OutLayout.CentreShift = 0.f;
	}
	else
	{
		float Narrowest = TNumericLimits<float>::Max();
		float Widest = 0.f;
		for (const FString& Sample : Samples)
		{
			// Mesuré tel qu'affiché (séparateur décimal de la culture)
			const float Width = FontMeasure->Measure(GenUIRules::CooldownDisplayText(Sample).ToString(), Font).X;
			Narrowest = FMath::Min(Narrowest, Width);
			Widest = FMath::Max(Widest, Width);
		}
		OutLayout = GenUIRules::CooldownBoxLayout(Narrowest, Widest, OutlineSize);
		UE_LOG(LogGenUI, Verbose, TEXT("%s : classe %d/%d, plus étroite %.1f, plus large %.1f, contour %.1f -> boîte %.1f, décalage %.2f"),
			*GetName(), FormatClass.X, FormatClass.Y, Narrowest, Widest, OutlineSize, OutLayout.Width, OutLayout.CentreShift);
	}

	CooldownBoxLayouts.Add(FormatClass, OutLayout);
	return true;
}

bool UGenAbilitySlot::BuildTooltipData(FGenAbilityTooltipData& Out) const
{
	if (!AbilityCDO.IsValid())
	{
		return false;
	}
	GenAbilityTooltip::Build(*AbilityCDO, Out);
	return true;
}

bool UGenAbilitySlot::FillTooltip(UGenAbilityTooltip& Tooltip) const
{
	FGenAbilityTooltipData Data;
	if (!BuildTooltipData(Data))
	{
		return false;
	}
	Tooltip.SetContent(Data, KeyLabelText);
	return true;
}

UGenAbilityTooltip* UGenAbilitySlot::CreateFilledTooltip() const
{
	if (!TooltipClass || !AbilityCDO.IsValid() || !GetOwningPlayer())
	{
		return nullptr;
	}
	UGenAbilityTooltip* Tooltip = CreateWidget<UGenAbilityTooltip>(GetOwningPlayer(), TooltipClass);
	if (Tooltip && !FillTooltip(*Tooltip))
	{
		return nullptr;
	}
	return Tooltip;
}

UGenAbilityTooltip* UGenAbilitySlot::EnsureHoverTooltip()
{
	if (HoverTooltip || !TooltipClass || !GetOwningPlayer())
	{
		return HoverTooltip;
	}

	HoverTooltip = CreateWidget<UGenAbilityTooltip>(GetOwningPlayer(), TooltipClass);
	if (!HoverTooltip)
	{
		return nullptr;
	}

	// Au-dessus du disque, centrée, à TooltipGap ; taille propre (la toile de taille nulle ne change pas la barre)
	if (TooltipCanvas)
	{
		if (UCanvasPanelSlot* CanvasSlot = TooltipCanvas->AddChildToCanvas(HoverTooltip))
		{
			CanvasSlot->SetAutoSize(true);
			CanvasSlot->SetAnchors(FAnchors(0.5f, 0.f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 1.f));
			CanvasSlot->SetPosition(FVector2D(0.f, -GetUIMetrics()->TooltipGap));
		}
	}
	else
	{
		UE_LOG(LogGenUI, Warning, TEXT("%s : pas de TooltipCanvas dans le WBP, infobulle créée mais pas affichée."), *GetPathName());
	}
	return HoverTooltip;
}

void UGenAbilitySlot::ShowHoverTooltip(float Delay)
{
	UGenAbilityTooltip* Tooltip = EnsureHoverTooltip();
	if (Tooltip && FillTooltip(*Tooltip))
	{
		Tooltip->Show(Delay);
	}
}

void UGenAbilitySlot::SetTooltipHovered(bool bInHovered)
{
	bHovered = bInHovered;
	if (bDetailsShown)
	{
		// Touche des détails maintenue : elle décide seule
		return;
	}
	if (bHovered)
	{
		ShowHoverTooltip(GetUIMetrics()->TooltipHoverDelay);
	}
	else if (HoverTooltip)
	{
		HoverTooltip->Hide();
	}
}

void UGenAbilitySlot::SetDetailsShown(bool bShown, bool bInPlace)
{
	bDetailsShown = bShown;
	bDetailsInPlace = bInPlace;
	if (bShown)
	{
		if (bInPlace)
		{
			ShowHoverTooltip(0.f);
		}
		else if (HoverTooltip)
		{
			HoverTooltip->Hide();
		}
		return;
	}

	// Touche relâchée : retour au survol
	if (bHovered)
	{
		ShowHoverTooltip(0.f);
	}
	else if (HoverTooltip)
	{
		HoverTooltip->Hide();
	}
}

void UGenAbilitySlot::PollHover()
{
	bool bOver = false;
	if (FSlateApplication::IsInitialized() && AbilityCDO.IsValid() && IsVisible())
	{
		const FGeometry& Disc = (IconSizeBox ? static_cast<UWidget*>(IconSizeBox) : static_cast<UWidget*>(IconImage))->GetCachedGeometry();
		bOver = Disc.GetLocalSize().X > 0.f && Disc.IsUnderLocation(FSlateApplication::Get().GetCursorPos());
	}
	if (bOver != bHovered)
	{
		SetTooltipHovered(bOver);
	}
}

const UGenUIMetrics* UGenAbilitySlot::GetUIMetrics() const
{
	const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
	return UI ? UI->GetMetrics() : GetDefault<UGenUIMetrics>();
}

const UGenUIPalette* UGenAbilitySlot::GetUIPalette() const
{
	const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
	return UI ? UI->GetPalette() : GetDefault<UGenUIPalette>();
}
