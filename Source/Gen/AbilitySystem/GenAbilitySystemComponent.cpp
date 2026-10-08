#include "AbilitySystem/GenAbilitySystemComponent.h"

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
	return ApplyGameplayEffectSpecToSelf(*Spec.Data);
}

void UGenAbilitySystemComponent::OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec)
{
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
