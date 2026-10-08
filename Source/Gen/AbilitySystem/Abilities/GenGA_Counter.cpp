#include "AbilitySystem/Abilities/GenGA_Counter.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystemComponent.h"
#include "Character/GenCharacterBase.h"
#include "GenGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenCounter, Log, All);

UGenGA_Counter::UGenGA_Counter()
{
	CastTime = 0.1f;
	bTurnToAim = false;
}

void UGenGA_Counter::OnCastLaunched(const FGenCastRelease& Release)
{
	BlockCount = 0;

	// Un seul effet porte la posture (State.Countering, vu par tous) et le ralenti de la fenêtre
	FGameplayEffectSpecHandle WindowSpec = MakeOutgoingGameplayEffectSpec(UGenGE_TimedMoveSpeed::StaticClass(), GetAbilityLevel());
	if (WindowSpec.IsValid())
	{
		UGenGE_TimedMoveSpeed::SetMagnitudes(*WindowSpec.Data, CounterWindow, WindowMoveSpeedMultiplier, FGameplayTagContainer(GenGameplayTags::State_Countering));
		WindowEffectHandle = ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, WindowSpec);
	}

	// Seul le serveur résout les coups : il reçoit les blocages
	if (CurrentActorInfo && CurrentActorInfo->IsNetAuthority())
	{
		UAbilityTask_WaitGameplayEvent* BlockTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, GenGameplayTags::Event_Counter_Blocked);
		BlockTask->EventReceived.AddDynamic(this, &ThisClass::OnBlocked);
		BlockTask->ReadyForActivation();
	}

	UAbilityTask_WaitDelay* WindowTask = UAbilityTask_WaitDelay::WaitDelay(this, CounterWindow);
	WindowTask->OnFinish.AddDynamic(this, &ThisClass::OnWindowFinished);
	WindowTask->ReadyForActivation();

	// Plan Visuals V4 : la fenêtre se lit comme une canalisation (barre qui se vide), serveur et client propriétaire
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StartChannel(GetClass(), CounterWindow); // retirée par UGenGA_Cast::EndAbility (StopCast)
	}

	// Lancer un autre sort termine la posture (le clic maintenu ne relance rien : State.Casting)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		AbilityActivatedHandle = ASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::OnAbilityActivated);
	}

	UE_LOG(LogGenCounter, Verbose, TEXT("%s : posture de contre (%.2fs)"), *GetName(), CounterWindow);
}

void UGenGA_Counter::OnBlocked(FGameplayEventData Payload)
{
	if (!IsActive())
	{
		return;
	}

	++BlockCount;

	const GenHitRules::FCounterReward Reward = GenHitRules::GetCounterReward(BlockCount, ResourcePerBlock, EnergyOnFirstBlock);
	const FGameplayEffectSpecHandle GainSpec = MakeGainSpec(Reward.Energy, Reward.Resource);
	if (GainSpec.IsValid())
	{
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, GainSpec);
	}

	// Mêlée bloquée : l'attaquant est repoussé. Nature décodée (Kind + 1, jamais 0) ; attaquant nul possible
	// (instigateur déjà détruit)
	EGenHitKind Kind = EGenHitKind::Projectile;
	const bool bKnownKind = GenHitRules::FromEventMagnitude(Payload.EventMagnitude, Kind);
	const AGenCharacterBase* Self = GetGenCharacterFromActorInfo();
	if (bKnownKind && Kind == EGenHitKind::Melee && MeleeKnockbackDistance > 0.f && Self)
	{
		if (AGenCharacterBase* Attacker = const_cast<AGenCharacterBase*>(Cast<AGenCharacterBase>(Payload.Instigator.Get())))
		{
			Attacker->ApplyKnockback(Attacker->GetActorLocation() - Self->GetActorLocation(), MeleeKnockbackDistance);
		}
	}

	if (BlockCueTag.IsValid())
	{
		K2_ExecuteGameplayCue(BlockCueTag, MakeEffectContext(CurrentSpecHandle, CurrentActorInfo));
	}

	UE_LOG(LogGenCounter, Verbose, TEXT("%s : coup bloqué n°%d (%s, nature %d), +%.0f ressource, +%.0f énergie"), *GetName(), BlockCount,
		*GetNameSafe(Payload.Instigator.Get()), bKnownKind ? static_cast<int32>(Kind) : -1, Reward.Resource, Reward.Energy);
}

void UGenGA_Counter::OnWindowFinished()
{
	FinishAbility();
}

void UGenGA_Counter::OnAbilityActivated(UGameplayAbility* ActivatedAbility)
{
	if (ActivatedAbility != this && IsActive())
	{
		UE_LOG(LogGenCounter, Verbose, TEXT("%s : posture terminée par %s"), *GetName(), *GetNameSafe(ActivatedAbility));
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
	}
}

void UGenGA_Counter::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AbilityActivatedCallbacks.Remove(AbilityActivatedHandle);
	}
	AbilityActivatedHandle.Reset();

	// Fin anticipée (autre sort, contrôle dur, mort) : la posture et le ralenti s'arrêtent avec le sort
	if (WindowEffectHandle.IsValid())
	{
		BP_RemoveGameplayEffectFromOwnerWithHandle(WindowEffectHandle);
		WindowEffectHandle.Invalidate();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
