#include "AbilitySystem/Abilities/GenGA_Counter.h"

#include "AbilitySystem/GenAbilityTooltipData.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
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

	// Posture et ralenti de la fenêtre, sur cette machine (serveur ou client propriétaire)
	SetWindowState(true);

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

	if (BlockCueTag.IsValid() && Self)
	{
		// Contrat de NS_Curffe_BackfireBlock (non attaché) : Location = point de blocage (la source bloquée, sinon
		// l'attaquant), Normal = direction lanceur -> point à plat ; l'axe local −X du système pointe vers le lanceur
		const AActor* SourceActor = Cast<AActor>(Payload.OptionalObject.Get());
		const AActor* InstigatorActor = Payload.Instigator.Get();
		const FVector CasterLocation = Self->GetActorLocation();
		FVector BlockPoint = SourceActor ? SourceActor->GetActorLocation() : (InstigatorActor ? InstigatorActor->GetActorLocation() : CasterLocation);
		FVector Normal = (BlockPoint - CasterLocation).GetSafeNormal2D();
		if (Normal.IsNearlyZero())
		{
			// Source apparue dans le lanceur (ou inconnue) : devant lui
			Normal = Self->GetActorForwardVector().GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
			BlockPoint = CasterLocation + Normal * 50.f;
		}

		FGameplayCueParameters CueParams;
		CueParams.Location = BlockPoint;
		CueParams.Normal = Normal;
		CueParams.Instigator = const_cast<AActor*>(InstigatorActor);
		CueParams.EffectCauser = const_cast<AGenCharacterBase*>(Self);
		CueParams.SourceObject = Payload.OptionalObject;
		K2_ExecuteGameplayCueWithParams(BlockCueTag, CueParams);
	}

	UE_LOG(LogGenCounter, Verbose, TEXT("%s : coup bloqué n°%d (%s, nature %d), +%.0f ressource, +%.0f énergie"), *GetName(), BlockCount,
		*GetNameSafe(Payload.Instigator.Get()), bKnownKind ? static_cast<int32>(Kind) : -1, Reward.Resource, Reward.Energy);
}

void UGenGA_Counter::OnWindowFinished()
{
	FinishAbility();
}

bool UGenGA_Counter::EndsStanceOnActivation(const UGameplayAbility* ActivatedAbility)
{
	const UGenGameplayAbility* GenAbility = Cast<UGenGameplayAbility>(ActivatedAbility);
	if (!GenAbility || !GenAbility->InputTag.IsValid() || GenAbility->IsTriggeredActivation())
	{
		return false;
	}
	const EGameplayAbilityNetExecutionPolicy::Type Policy = GenAbility->GetNetExecutionPolicy();
	return Policy == EGameplayAbilityNetExecutionPolicy::LocalPredicted || Policy == EGameplayAbilityNetExecutionPolicy::LocalOnly;
}

void UGenGA_Counter::SetWindowState(bool bActive)
{
	if (bWindowStateApplied == bActive)
	{
		return;
	}
	bWindowStateApplied = bActive;

	// Revue Plan 2 Tasks 7-8, I-4 : plus de GE prédit (il restait ~1 RTT chez le propriétaire après sa fin, ou revenait
	// après une fin anticipée : bande qui clignote, corrections du mouvement). Chaque machine pose la posture à son départ
	// et la retire à sa fin. Serveur : tag aussi répliqué aux proxys simulés (les autres joueurs voient la bande), jamais
	// au propriétaire qui a le sien.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const bool bAuthority = CurrentActorInfo && CurrentActorInfo->IsNetAuthority();
		const EGameplayTagReplicationState Replication = bAuthority ? EGameplayTagReplicationState::SimulatedTagOnly : EGameplayTagReplicationState::None;
		if (bActive)
		{
			ASC->AddLooseGameplayTag(GenGameplayTags::State_Countering, 1, Replication);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(GenGameplayTags::State_Countering, 1, Replication);
		}
	}

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		static const FName WindowReason(TEXT("CounterWindow"));
		if (bActive)
		{
			Character->SetLocalMoveSpeedMultiplier(this, WindowReason, WindowMoveSpeedMultiplier);
		}
		else
		{
			Character->ClearLocalMoveSpeedMultiplier(this, WindowReason);
		}
	}
}

void UGenGA_Counter::OnAbilityActivated(UGameplayAbility* ActivatedAbility)
{
	if (ActivatedAbility != this && IsActive() && EndsStanceOnActivation(ActivatedAbility))
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

	// Fin (délai, autre sort, contrôle dur, mort) : la posture et le ralenti s'arrêtent avec le sort, sur cette machine
	SetWindowState(false);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

#define LOCTEXT_NAMESPACE "GenGA_Counter"

void UGenGA_Counter::GetTooltipArgs(FFormatNamedArguments& Args) const
{
	Super::GetTooltipArgs(Args);
	Args.Add(TEXT("Window"), GenAbilityTooltip::Seconds(CounterWindow));
	Args.Add(TEXT("ResourcePerBlock"), GenAbilityTooltip::Number(ResourcePerBlock));
	Args.Add(TEXT("EnergyOnFirstBlock"), GenAbilityTooltip::Number(EnergyOnFirstBlock));
	Args.Add(TEXT("WindowSpeed"), GenAbilityTooltip::Percent(WindowMoveSpeedMultiplier));
	Args.Add(TEXT("Knockback"), GenAbilityTooltip::Meters(MeleeKnockbackDistance));
}

void UGenGA_Counter::GetTooltipEffectLines(TArray<FText>& OutLines) const
{
	OutLines.Add(FText::Format(LOCTEXT("Window", "Posture {0} : bloque les projectiles et la mêlée (pas les zones au sol)"), GenAbilityTooltip::Seconds(CounterWindow)));
	if (WindowMoveSpeedMultiplier < 1.f)
	{
		OutLines.Add(FText::Format(LOCTEXT("Speed", "Vitesse {0} pendant la posture"), GenAbilityTooltip::Percent(WindowMoveSpeedMultiplier)));
	}
	if (ResourcePerBlock > 0.f)
	{
		OutLines.Add(FText::Format(LOCTEXT("Resource", "+{0} {0}|plural(one=flamme,other=flammes) par coup bloqué"), ResourcePerBlock));
	}
	if (EnergyOnFirstBlock > 0.f)
	{
		OutLines.Add(FText::Format(LOCTEXT("Energy", "+{0} énergie au premier coup bloqué"), GenAbilityTooltip::Number(EnergyOnFirstBlock)));
	}
	if (MeleeKnockbackDistance > 0.f)
	{
		OutLines.Add(FText::Format(LOCTEXT("Knockback", "Repousse un attaquant au corps à corps de {0}"), GenAbilityTooltip::Meters(MeleeKnockbackDistance)));
	}
	OutLines.Add(LOCTEXT("Ends", "Finit à la fin de la posture, sur un contrôle dur ou quand on appuie sur la touche d'un autre sort"));
}

#undef LOCTEXT_NAMESPACE
