#include "UI/GenAbilitySlot.h"

#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GenGameplayTags.h"
#include "Input/GenInputConfig.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/GenPlayerController.h"
#include "TimerManager.h"
#include "UI/GenTextBlock.h"
#include "UI/GenUIDataAssets.h"
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

	if (SweepImage)
	{
		SweepMID = SweepImage->GetDynamicMaterial();
	}
	if (ArcImage)
	{
		ArcMID = ArcImage->GetDynamicMaterial();
		ArcImage->SetVisibility(bIsUltimate ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	RefreshVisuals();
}

void UGenAbilitySlot::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UGenAbilitySlot::Bind(UAbilitySystemComponent* InASC)
{
	// bIsUltimate est posé par la barre après notre NativeConstruct : on réapplique la visibilité de l'arc
	if (ArcImage)
	{
		ArcImage->SetVisibility(bIsUltimate ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

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

	if (bIsUltimate)
	{
		EnergyHandle = ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).AddUObject(this, &ThisClass::OnEnergyChanged);
		const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
		const int32 Segments = (UI ? UI->GetMetrics() : GetDefault<UGenUIMetrics>())->UltimateSegments;
		bUltimateWasFull = GenUIRules::FundedSegments(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()), ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()), Segments) == Segments;
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
	}

	TagHandles.Reset();
	EnergyHandle.Reset();
	ASC.Reset();
	SpecHandle = FGameplayAbilitySpecHandle();
	AbilityCDO.Reset();
	CooldownTags.Reset();
	CooldownEndTime = 0.f;
	CooldownDuration = 0.f;
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

	if (UTexture2D* Icon = AbilityCDO->Icon.LoadSynchronous())
	{
		IconImage->SetBrushFromTexture(Icon);
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

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const bool bWasCooling = CooldownEndTime > Now;
	CooldownEndTime = Remaining > 0.f ? Now + Remaining : 0.f;
	CooldownDuration = Duration;

	if (Remaining > 0.f)
	{
		StartRefreshTimer();
	}
	else if (bWasCooling)
	{
		// Fin de recharge : flash du bord (§4.1 Ready flash)
		StartFlash(UGenUISubsystem::Get(this) ? UGenUISubsystem::Get(this)->GetMetrics()->ReadyFlashDuration : 0.2f);
	}

	RefreshVisuals();
}

void UGenAbilitySlot::StartRefreshTimer()
{
	if (UWorld* World = GetWorld())
	{
		const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
		const float Interval = UI ? UI->GetMetrics()->CooldownRefreshInterval : 0.05f;
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
	const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
	const UGenUIMetrics* Metrics = UI ? UI->GetMetrics() : GetDefault<UGenUIMetrics>();
	const UGenUIPalette* Palette = UI ? UI->GetPalette() : GetDefault<UGenUIPalette>();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Remaining = FMath::Max(CooldownEndTime - Now, 0.f);

	State = GenUIRules::ResolveSlotState(AbilityCDO.IsValid(), bLocked, Remaining);
	CooldownString = State == EGenAbilitySlotState::Cooldown ? GenUIRules::FormatCooldown(Remaining, CooldownDuration, Metrics->CooldownHideBelowTotal) : FString();

	IconImage->SetVisibility(State == EGenAbilitySlotState::Empty ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	CooldownText->SetText(FText::FromString(CooldownString));
	CooldownText->SetVisibility(CooldownString.IsEmpty() ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	LockImage->SetVisibility(State == EGenAbilitySlotState::Locked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);

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
		SweepMID->SetScalarParameterValue(TEXT("DimAmount"), State == EGenAbilitySlotState::Cooldown ? Metrics->CooldownDesaturation : 0.f);
		SweepMID->SetScalarParameterValue(TEXT("RimFlash"), Flash);
		SweepMID->SetScalarParameterValue(TEXT("Locked"), State == EGenAbilitySlotState::Locked ? 1.f : 0.f);
		SweepMID->SetVectorParameterValue(TEXT("OverlayColour"), State == EGenAbilitySlotState::Locked ? Palette->Cooldown_Locked : Palette->Cooldown_Overlay);
		SweepMID->SetVectorParameterValue(TEXT("RimColour"), bIsUltimate ? Palette->Energy_Full : Palette->Line_Bronze);
		SweepMID->SetVectorParameterValue(TEXT("FlashColour"), Palette->Flash_White);
	}

	if (bIsUltimate)
	{
		UpdateUltimateArc();
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

void UGenAbilitySlot::UpdateUltimateArc()
{
	if (!ArcMID || !ASC.IsValid())
	{
		return;
	}

	const UGenUISubsystem* UI = UGenUISubsystem::Get(this);
	const UGenUIMetrics* Metrics = UI ? UI->GetMetrics() : GetDefault<UGenUIMetrics>();
	const UGenUIPalette* Palette = UI ? UI->GetPalette() : GetDefault<UGenUIPalette>();

	const int32 Funded = GenUIRules::FundedSegments(ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()),
		ASC->GetNumericAttribute(UGenAttributeSet::GetMaxEnergyAttribute()), Metrics->UltimateSegments);
	const bool bFull = Funded == Metrics->UltimateSegments;

	ArcMID->SetScalarParameterValue(TEXT("Segments"), Metrics->UltimateSegments);
	ArcMID->SetScalarParameterValue(TEXT("Funded"), Funded);
	ArcMID->SetScalarParameterValue(TEXT("FullOutline"), bFull ? 1.f : 0.f);
	ArcMID->SetVectorParameterValue(TEXT("Colour"), bFull ? Palette->Energy_Full : Palette->Energy_Charging);

	// Ultime prête : une seule impulsion de 300 ms, jamais de boucle (§4.1)
	if (bFull && !bUltimateWasFull)
	{
		StartFlash(Metrics->UltimatePulseDuration);
	}
	bUltimateWasFull = bFull;
}
