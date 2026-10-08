#include "AbilitySystem/GenAbilitySystemComponent.h"

#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "AbilitySystem/GenFeeding.h"
#include "Character/GenCharacterBase.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GenGameplayTags.h"

UGenAbilitySystemComponent::UGenAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
}

TArray<FGameplayAbilitySpecHandle> UGenAbilitySystemComponent::GrantAbilities(const TArray<TSubclassOf<UGenGameplayAbility>>& Abilities, UObject* SourceObject)
{
	TArray<FGameplayAbilitySpecHandle> Handles;
	if (!IsOwnerActorAuthoritative())
	{
		return Handles;
	}

	for (const TSubclassOf<UGenGameplayAbility>& AbilityClass : Abilities)
	{
		if (!AbilityClass)
		{
			continue;
		}

		const UGenGameplayAbility* AbilityCDO = AbilityClass->GetDefaultObject<UGenGameplayAbility>();
		FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, SourceObject);

		// L'InputTag du sort est stocké dans les tags dynamiques du spec : c'est ce qui
		// relie une touche à ce sort (voir AbilityInputTagPressed)
		if (AbilityCDO->InputTag.IsValid())
		{
			Spec.GetDynamicSpecSourceTags().AddTag(AbilityCDO->InputTag);
		}

		Handles.Add(GiveAbility(Spec));
	}

	return Handles;
}

TArray<FActiveGameplayEffectHandle> UGenAbilitySystemComponent::ApplyEffectsToSelf(const TArray<TSubclassOf<UGameplayEffect>>& Effects, UObject* SourceObject)
{
	TArray<FActiveGameplayEffectHandle> Handles;
	if (!IsOwnerActorAuthoritative())
	{
		return Handles;
	}

	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddSourceObject(SourceObject);

	for (const TSubclassOf<UGameplayEffect>& EffectClass : Effects)
	{
		if (!EffectClass)
		{
			continue;
		}

		const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(EffectClass, 1.f, Context);
		if (SpecHandle.IsValid())
		{
			const FActiveGameplayEffectHandle ActiveHandle = ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			// Les effets instantanés ne renvoient pas de handle valide : rien à retirer plus tard
			if (ActiveHandle.IsValid())
			{
				Handles.Add(ActiveHandle);
			}
		}
	}

	return Handles;
}

void UGenAbilitySystemComponent::OnGiveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	Super::OnGiveAbility(AbilitySpec);
	OnAbilitiesChanged.Broadcast(AbilitySpec, /*bRemoved*/ false);
}

void UGenAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			InputPressedSpecHandles.AddUnique(Spec.Handle);
			InputHeldSpecHandles.AddUnique(Spec.Handle);
		}
	}
}

void UGenAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			InputReleasedSpecHandles.AddUnique(Spec.Handle);
			InputHeldSpecHandles.Remove(Spec.Handle);
		}
	}
}

bool UGenAbilitySystemComponent::IsAnotherAbilityCasting(FGameplayAbilitySpecHandle Except) const
{
	// Calculé localement (le tag State.Casting répliqué peut écraser le compte prédit côté client)
	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		const UGenGameplayAbility* Ability = Cast<UGenGameplayAbility>(Spec.Ability);
		if (Ability && Spec.Handle != Except && Spec.IsActive() && Ability->GetActivationOwnedTagsRO().HasTagExact(GenGameplayTags::State_Casting))
		{
			return true;
		}
	}
	return false;
}

FPredictionKey UGenAbilitySystemComponent::GetReplicatedTargetDataKey(FGameplayAbilitySpecHandle Handle, FPredictionKey ActivationKey) const
{
	const TSharedPtr<FAbilityReplicatedDataCache> Cached = AbilityTargetDataMap.Find(FGameplayAbilitySpecHandleAndPredictionKey(Handle, ActivationKey));
	return Cached.IsValid() ? Cached->PredictionKey : FPredictionKey();
}

void UGenAbilitySystemComponent::ProcessAbilityInput(float DeltaTime, bool bGamePaused)
{
	// Mort ou étourdi : on jette les inputs (les sorts sont de toute façon bloqués par ActivationBlockedTags)
	if (HasMatchingGameplayTag(GenGameplayTags::State_Dead))
	{
		ClearAbilityInput();
		return;
	}

	TArray<FGameplayAbilitySpecHandle, TInlineAllocator<8>> AbilitiesToActivate;

	// Sorts "WhileInputActive" : se relancent tant que la touche est maintenue (ex: M1 en auto).
	// Répétition automatique bloquée pendant l'incantation d'un autre sort ; un nouvel appui passe
	// (et l'annule via CancelAbilitiesWithTag).
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputHeldSpecHandles)
	{
		if (const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(SpecHandle))
		{
			const UGenGameplayAbility* AbilityCDO = Cast<UGenGameplayAbility>(Spec->Ability);
			if (AbilityCDO && !Spec->IsActive() && AbilityCDO->ActivationPolicy == EGenAbilityActivationPolicy::WhileInputActive)
			{
				const bool bPressedThisFrame = InputPressedSpecHandles.Contains(SpecHandle);
				if (!bPressedThisFrame && IsAnotherAbilityCasting(SpecHandle))
				{
					continue;
				}
				AbilitiesToActivate.AddUnique(Spec->Handle);
			}
		}
	}

	// Sorts dont la touche vient d'être pressée cette frame
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputPressedSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(SpecHandle))
		{
			Spec->InputPressed = true;

			if (Spec->IsActive())
			{
				// Déjà actif : on transmet l'appui au sort (WaitInputPress, sorts chargés...)
				AbilitySpecInputPressed(*Spec);
			}
			else
			{
				const UGenGameplayAbility* AbilityCDO = Cast<UGenGameplayAbility>(Spec->Ability);
				if (AbilityCDO && AbilityCDO->ActivationPolicy == EGenAbilityActivationPolicy::OnInputTriggered)
				{
					AbilitiesToActivate.AddUnique(Spec->Handle);
				}
			}
		}
	}

	for (const FGameplayAbilitySpecHandle& SpecHandle : AbilitiesToActivate)
	{
		TryActivateAbility(SpecHandle);
	}

	// Sorts dont la touche vient d'être relâchée cette frame
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputReleasedSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(SpecHandle))
		{
			Spec->InputPressed = false;

			if (Spec->IsActive())
			{
				AbilitySpecInputReleased(*Spec);
			}
		}
	}

	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
}

void UGenAbilitySystemComponent::ClearAbilityInput()
{
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
}

void UGenAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputPressed(Spec);

	// On passe par les "replicated events" pour que les AbilityTasks WaitInputPress
	// fonctionnent aussi côté serveur
	if (Spec.IsActive())
	{
		if (const UGameplayAbility* Instance = Spec.GetPrimaryInstance())
		{
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, Instance->GetCurrentActivationInfo().GetActivationPredictionKey());
		}
	}
}

void UGenAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputReleased(Spec);

	if (Spec.IsActive())
	{
		if (const UGameplayAbility* Instance = Spec.GetPrimaryInstance())
		{
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Spec.Handle, Instance->GetCurrentActivationInfo().GetActivationPredictionKey());
		}
	}
}

FActiveGameplayEffectHandle UGenAbilitySystemComponent::ApplyHardCC(FGameplayTag StateTag, float Duration, AActor* Source)
{
	if (!IsOwnerActorAuthoritative() || !StateTag.IsValid() || Duration <= 0.f)
	{
		return FActiveGameplayEffectHandle();
	}

	// Plan 3 Task 4 : intouchable (forme de feu...), aucun contrôle dur ne prend.
	// Plan 3 Task 5 : immunisé par la résilience non plus.
	if (HasMatchingGameplayTag(GenGameplayTags::State_Untouchable) || HasMatchingGameplayTag(GenGameplayTags::State_CCImmune))
	{
		return FActiveGameplayEffectHandle();
	}

	// Seuls les contrôles durs passent ici (ex : State.Countering n'en est pas un)
	if (!ensureMsgf(GenGameplayTags::GetHardCCTags().HasTagExact(StateTag), TEXT("ApplyHardCC : %s n'est pas un contrôle dur"), *StateTag.ToString()))
	{
		return FActiveGameplayEffectHandle();
	}

	// TODO (guidelines §3.2) : neutralisé (State.Incapacitated) doit finir au premier dégât subi ; pas encore fait

	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddInstigator(Source, Source);

	const FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(UGenGE_TimedMoveSpeed::StaticClass(), 1.f, Context);
	if (!Spec.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	// Étourdi ou neutralisé : ne bouge plus. Silence et peur laissent bouger (la fuite de la peur viendra avec son sort).
	const bool bImmobile = StateTag.MatchesTagExact(GenGameplayTags::State_Stunned) || StateTag.MatchesTagExact(GenGameplayTags::State_Incapacitated);
	UGenGE_TimedMoveSpeed::SetMagnitudes(*Spec.Data, Duration, bImmobile ? 0.f : 1.f, FGameplayTagContainer(StateTag));
	const FActiveGameplayEffectHandle Handle = ApplyGameplayEffectSpecToSelf(*Spec.Data);

	// Plan 3 Task 5, résilience (guidelines §3.3) : 2.5 s de contrôle dur sur 5 s (union des intervalles) => immunité
	// jusqu'à 1.5 s après la fin de celui-ci. Seul ce qui s'est vraiment appliqué compte.
	if (Handle.IsValid() && GetWorld())
	{
		const float Immunity = HardCCHistory.Record(GetWorld()->GetTimeSeconds(), Duration);
		if (Immunity > 0.f)
		{
			// UGenGE_TimedState : retiré avec les autres états à la mort (RemoveTimedStates)
			const FGameplayEffectSpecHandle ImmunitySpec = MakeOutgoingSpec(UGenGE_TimedState::StaticClass(), 1.f, MakeEffectContext());
			if (ImmunitySpec.IsValid())
			{
				UGenGE_TimedState::SetDuration(*ImmunitySpec.Data, Immunity, FGameplayTagContainer(GenGameplayTags::State_CCImmune));
				ApplyGameplayEffectSpecToSelf(*ImmunitySpec.Data);
			}
		}
	}
	return Handle;
}

void UGenAbilitySystemComponent::OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	// Diffusé avant que le spec quitte la liste (l'interface lâche son handle).
	OnAbilitiesChanged.Broadcast(AbilitySpec, /*bRemoved*/ true);

	if (AGenCharacterBase* Character = Cast<AGenCharacterBase>(GetAvatarActor_Direct()))
	{
		for (const UGameplayAbility* Instance : AbilitySpec.GetAbilityInstances())
		{
			Character->ClearFedResourceFrom(Instance);
		}
	}

	Super::OnRemoveAbility(AbilitySpec);
}

void UGenAbilitySystemComponent::RemoveTimedStates()
{
	if (!IsOwnerActorAuthoritative())
	{
		return;
	}

	FGameplayEffectQuery Query;
	Query.CustomMatchDelegate.BindLambda([](const FActiveGameplayEffect& Effect)
	{
		return Effect.Spec.Def && Effect.Spec.Def->IsA<UGenGE_TimedState>();
	});
	RemoveActiveEffects(Query);

	// Mort : la résilience repart de zéro (Plan 3 Task 5)
	HardCCHistory.Reset();
}

void UGenAbilitySystemComponent::NoteCastLock(float MinLockDuration)
{
	if (IsOwnerActorAuthoritative() && GetWorld())
	{
		CastLockEnforcedUntil = GenFeeding::GetCastLockEnforcedUntil(GetWorld()->GetTimeSeconds(), MinLockDuration, GenFeeding::CastTimeTolerance);
	}
}

void UGenAbilitySystemComponent::ClearCastLock()
{
	SetLooseGameplayTagCount(GenGameplayTags::State_CastLocked, 0);
	CastLockEnforcedUntil = -1.0;
}

int32 UGenAbilitySystemComponent::CancelPendingCasts()
{
	// Plan 3 Task 6. Liste d'abord : annuler un sort modifie les specs actifs
	TArray<UGenGA_Cast*, TInlineAllocator<4>> Pending;
	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		UGenGA_Cast* CastAbility = Spec.IsActive() ? Cast<UGenGA_Cast>(Spec.GetPrimaryInstance()) : nullptr;
		// CanBeCanceled : serveur, visée du client reçue (départ différé compris) => le sort part quand même
		if (CastAbility && CastAbility->IsCastPending() && CastAbility->CanBeCanceled())
		{
			Pending.Add(CastAbility);
		}
	}

	// Annulation prédite (rien n'a été payé : coûts au lancer), répliquée au serveur qui la refuse si la visée est
	// déjà arrivée (ordre des RPC du joueur : une visée envoyée avant l'annulation arrive avant elle)
	for (UGenGA_Cast* CastAbility : Pending)
	{
		CastAbility->CancelAbility(CastAbility->GetCurrentAbilitySpecHandle(), CastAbility->GetCurrentActorInfo(), CastAbility->GetCurrentActivationInfo(), true);
	}
	return Pending.Num();
}
