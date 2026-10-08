#include "UI/GenAbilitySlot.h"

#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
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
#include "UI/GenTextBlock.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUILog.h"
#include "UI/GenUISubsystem.h"

namespace
{
	/** Le sort n'est pas encore répliqué au client quand l'ASC est prêt : on réessaie un peu. */
	constexpr float ResolveRetryInterval = 0.25f;
	constexpr int32 ResolveRetryMax = 40;
}

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
}

void UGenAbilitySlot::HandleControlMappingsRebuilt()
{
	RefreshKeyLabel();
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
}

void UGenAbilitySlot::NativeDestruct()
{
	if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetOwningLocalPlayer()))
	{
		Input->ControlMappingsRebuiltDelegate.RemoveDynamic(this, &ThisClass::HandleControlMappingsRebuilt);
	}
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
	ResolveAttempts = 0;

	if (!ASC.IsValid())
	{
		RefreshVisuals();
		return;
	}

	// Étourdi => bloqué (§4.1 Locked)
	FDelegateHandle StunHandle = ASC->RegisterGameplayTagEvent(GenGameplayTags::State_Stunned, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::OnStunTagChanged);
	TagHandles.Emplace(GenGameplayTags::State_Stunned, StunHandle);
	bLocked = ASC->HasMatchingGameplayTag(GenGameplayTags::State_Stunned);

	// Le GE de recharge du serveur remplace le GE prédit (compte 1 -> 2 -> 1, aucun événement de tag) : on relit la recharge (§8.2)
	EffectAddedHandle = ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddUObject(this, &ThisClass::OnEffectAdded);

	if (bIsUltimate)
	{
		EnergyHandle = ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).AddUObject(this, &ThisClass::OnEnergyChanged);
		MaxEnergyHandle = ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetMaxEnergyAttribute()).AddUObject(this, &ThisClass::OnEnergyChanged);

		// Déjà pleine au moment du Bind (respawn, rebind) : pas d'impulsion
		const int32 Segments = GetUIMetrics()->UltimateSegments;
		bUltimateWasReadyFull = Segments > 0 && GenUIRules::FundedSegments(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()), ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()), Segments) == Segments;
	}

	ResolveAbility();
	RefreshKeyLabel();
}

void UGenAbilitySlot::Unbind()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
		World->GetTimerManager().ClearTimer(ResolveRetryTimer);
	}

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
	}

	TagHandles.Reset();
	EnergyHandle.Reset();
	MaxEnergyHandle.Reset();
	EffectAddedHandle.Reset();
	ASC.Reset();
	SpecHandle = FGameplayAbilitySpecHandle();
	AbilityCDO.Reset();
	CooldownTags.Reset();
	CooldownEndTime = 0.f;
	CooldownDuration = 0.f;

	// Un rebind en plein flash ne doit pas figer un bord à moitié allumé
	FlashStartTime = -1.f;
	FlashDuration = 0.f;
	if (SweepMID)
	{
		SweepMID->SetScalarParameterValue(TEXT("RimFlash"), 0.f);
	}
	// Pas d'icône du sort précédent sur un slot délié
	if (IconMID)
	{
		IconMID->SetTextureParameterValue(TEXT("Icon"), DefaultIconTexture);
	}
}

void UGenAbilitySlot::ResolveAbility()
{
	if (!ASC.IsValid())
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		const UGenGameplayAbility* Ability = Cast<UGenGameplayAbility>(Spec.Ability);
		if (Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
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
		// Les sorts donnés par le serveur arrivent après l'ASC côté client : on réessaie
		if (++ResolveAttempts <= ResolveRetryMax && GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(ResolveRetryTimer, this, &ThisClass::ResolveAbility, ResolveRetryInterval, false);
		}
		RefreshVisuals();
		return;
	}

	for (const FGameplayTag& Tag : CooldownTags)
	{
		FDelegateHandle Handle = ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::OnCooldownTagChanged);
		TagHandles.Emplace(Tag, Handle);
	}

	// Par le paramètre du MID et non SetBrushFromTexture, qui remplacerait le matériau (cercle, désaturation)
	if (IconMID)
	{
		// Sort sans icône : texture par défaut du matériau, jamais l'icône d'un sort précédent
		UTexture2D* Icon = AbilityCDO->Icon.LoadSynchronous();
		IconMID->SetTextureParameterValue(TEXT("Icon"), Icon ? Icon : DefaultIconTexture.Get());
	}
	IconImage->SetToolTipText(AbilityCDO->DisplayName);

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
	}

	// CooldownEndTime n'est remis à zéro qu'à la fin d'une recharge et dans Unbind : un événement de tag
	// arrivant après la fin locale, ou un rebind, ne peut donc pas déclencher un second flash
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const bool bWasCooling = CooldownEndTime > 0.f;
	CooldownEndTime = Remaining > 0.f ? Now + Remaining : 0.f;
	CooldownDuration = Duration;

	if (Remaining > 0.f)
	{
		StartRefreshTimer();
	}
	else if (bWasCooling)
	{
		// Fin de recharge : flash du bord (§4.1 Ready flash)
		StartFlash(GetUIMetrics()->ReadyFlashDuration);
	}

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

	State = GenUIRules::ResolveSlotState(AbilityCDO.IsValid(), bLocked, Remaining);
	CooldownString = State == EGenAbilitySlotState::Cooldown ? GenUIRules::FormatCooldown(Remaining, CooldownDuration, Metrics->CooldownHideBelowTotal) : FString();

	IconImage->SetVisibility(State == EGenAbilitySlotState::Empty ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	UpdateCooldownTextBox();
	CooldownText->SetText(FText::FromString(CooldownString));
	CooldownText->SetVisibility(CooldownString.IsEmpty() ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	LockImage->SetVisibility(State == EGenAbilitySlotState::Locked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);

	// Avant le balayage : l'impulsion de l'ultime peut démarrer un flash à appliquer dès cette passe
	const bool bUltimateReady = bIsUltimate && UpdateUltimateArc();

	// La désaturation de la recharge s'applique à l'icône, pas au voile du balayage (§4.1)
	if (IconMID)
	{
		IconMID->SetScalarParameterValue(TEXT("DimAmount"), State == EGenAbilitySlotState::Cooldown ? Metrics->CooldownDesaturation : 0.f);
	}

	if (SweepMID)
	{
		const float Progress = (State == EGenAbilitySlotState::Cooldown && CooldownDuration > 0.f) ? Remaining / CooldownDuration : 0.f;
		float Flash = 0.f;
		if (FlashStartTime >= 0.f && FlashDuration > 0.f)
		{
			// Ease-out (§5.1) : 1 -> 0 sur la durée du flash
			const float Alpha = FMath::Clamp((Now - FlashStartTime) / FlashDuration, 0.f, 1.f);
			Flash = 1.f - FMath::InterpEaseOut(0.f, 1.f, Alpha, 3.f);
		}
		SweepMID->SetScalarParameterValue(TEXT("Progress"), Progress);
		SweepMID->SetScalarParameterValue(TEXT("RimFlash"), Flash);
		// Épaisseur du bord en rayons du disque : 1 px sur 64, anneau de 2 px sur l'ultime de 72 (§4.1)
		const float SlotSize = bIsUltimate ? Metrics->UltimateSlotSize : Metrics->SlotSize;
		const float RimPx = bIsUltimate ? Metrics->UltimateRimWidth : Metrics->SlotRimWidth;
		SweepMID->SetScalarParameterValue(TEXT("RimWidth"), RimPx / FMath::Max(SlotSize * 0.5f, 1.f));
		SweepMID->SetScalarParameterValue(TEXT("Locked"), State == EGenAbilitySlotState::Locked ? 1.f : 0.f);
		SweepMID->SetVectorParameterValue(TEXT("OverlayColour"), State == EGenAbilitySlotState::Locked ? Palette->Cooldown_Locked : Palette->Cooldown_Overlay);
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
	bLocked = NewCount > 0;
	RefreshVisuals();
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

bool UGenAbilitySlot::UpdateUltimateArc()
{
	if (!ASC.IsValid())
	{
		return false;
	}

	const UGenUIMetrics* Metrics = GetUIMetrics();
	const UGenUIPalette* Palette = GetUIPalette();

	const int32 Segments = Metrics->UltimateSegments;
	const int32 Funded = GenUIRules::FundedSegments(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()),
		ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()), Segments);
	const bool bFull = Segments > 0 && Funded == Segments;

	if (ArcMID)
	{
		ArcMID->SetScalarParameterValue(TEXT("Segments"), Segments);
		ArcMID->SetScalarParameterValue(TEXT("Funded"), Funded);
		ArcMID->SetScalarParameterValue(TEXT("FullOutline"), bFull ? 1.f : 0.f);
		ArcMID->SetVectorParameterValue(TEXT("Colour"), bFull ? Palette->Energy_Full : Palette->Energy_Charging);
		// Segments non financés : contour creux text.secondary ; contour de l'arc plein : text.primary (§4.1)
		ArcMID->SetVectorParameterValue(TEXT("HollowColour"), Palette->Text_Secondary);
		ArcMID->SetVectorParameterValue(TEXT("OutlineColour"), Palette->Text_Primary);
	}

	// Ultime prête : une seule impulsion de 300 ms via le RimFlash du balayage (indépendante de l'arc), jamais de boucle (§4.1).
	// Seulement quand elle est lançable : pleine pendant un étourdissement ou une recharge => impulsion quand elle le redevient.
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

	// État courant, sans le verrou de l'impulsion : une ultime vide (Empty) ou non lançable n'a jamais l'anneau à α 1.0
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

	// Mesures prises une fois dans la police du style (TS_Cooldown) : rien n'est codé en dur
	if (CooldownWidestDigit <= 0.f)
	{
		if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
		{
			return;
		}
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FSlateFontInfo& Font = CooldownText->GetFont();
		for (TCHAR Digit = TEXT('0'); Digit <= TEXT('9'); ++Digit)
		{
			CooldownWidestDigit = FMath::Max(CooldownWidestDigit, static_cast<float>(FontMeasure->Measure(FString::Chr(Digit), Font).X));
		}
		CooldownDotWidth = FontMeasure->Measure(TEXT("."), Font).X;
		// La mesure ignore le contour : on l'ajoute des deux côtés
		CooldownOutlineSize = Font.OutlineSettings.OutlineSize;
		UE_LOG(LogGenUI, Verbose, TEXT("%s : chiffre le plus large %.1f, point %.1f, contour %.1f"), *GetName(), CooldownWidestDigit, CooldownDotWidth, CooldownOutlineSize);
	}

	CooldownBoxClass = FormatClass;
	CooldownTextBox->SetWidthOverride(GenUIRules::CooldownBoxWidth(FormatClass, CooldownWidestDigit, CooldownDotWidth, CooldownOutlineSize));
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
