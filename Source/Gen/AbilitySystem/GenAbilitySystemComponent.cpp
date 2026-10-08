#include "AbilitySystem/GenAbilitySystemComponent.h"

#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "AbilitySystem/GenFeeding.h"
#include "Character/GenCharacterBase.h"
#include "Engine/World.h"
#include "Engine/EngineBaseTypes.h"
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

	// Revue P3 T8-10, I1 : les tags requis du sort accordé (instance : un sort peut les poser à l'octroi) sont suivis
	const UGenGameplayAbility* GenAbility = Cast<UGenGameplayAbility>(AbilitySpec.GetPrimaryInstance() ? AbilitySpec.GetPrimaryInstance() : AbilitySpec.Ability.Get());
	if (GenAbility)
	{
		RegisterGraceTags(GenAbility->GetActivationRequiredTagsForGrace());
	}

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
	// Revue PIE finale, C-5 : jamais d'appui en attente sous contrôle dur
	if (HasHardCC())
	{
		ClearInputBuffer();
	}

	TArray<FGameplayAbilitySpecHandle, TInlineAllocator<8>> AbilitiesToActivate;

	// Sorts "WhileInputActive" : se relancent tant que la touche est maintenue (ex: M1 en auto)
	CollectHeldActivations(AbilitiesToActivate);

	// Sorts dont la touche vient d'être pressée cette frame. Revue PIE finale, C-5 : décidé avant toute activation de
	// l'image (deux appuis dans la même image gardent leur règle : le second remplace le premier)
	TArray<FGameplayAbilitySpecHandle, TInlineAllocator<4>> PressesToBuffer;
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputPressedSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(SpecHandle))
		{
			Spec->InputPressed = true;

			if (Spec->IsActive())
			{
				// Déjà actif : on transmet l'appui au sort (WaitInputPress, sorts chargés...)
				AbilitySpecInputPressed(*Spec);
				// Sa propre incantation finit bientôt : le sort repart juste après (clics gauches tapés en rythme)
				if (ShouldBufferPress(*Spec))
				{
					PressesToBuffer.Add(SpecHandle);
				}
			}
			else
			{
				const UGenGameplayAbility* AbilityCDO = Cast<UGenGameplayAbility>(Spec->Ability);
				const bool bWantsActivation = AbilitiesToActivate.Contains(SpecHandle)
					|| (AbilityCDO && AbilityCDO->ActivationPolicy == EGenAbilityActivationPolicy::OnInputTriggered);
				if (bWantsActivation && ShouldBufferPress(*Spec))
				{
					// Verrou ou incantation qui finit bientôt : l'appui attend au lieu d'échouer ou de remplacer le sort
					PressesToBuffer.Add(SpecHandle);
					AbilitiesToActivate.Remove(SpecHandle);
				}
				else if (bWantsActivation)
				{
					AbilitiesToActivate.AddUnique(Spec->Handle);
				}
			}
		}
	}
	if (PressesToBuffer.Num() > 0)
	{
		BufferPress(PressesToBuffer); // le dernier appui gagne
	}

	// Un appui frais qui part remplace l'appui en attente ; sinon l'appui retenu part dès qu'il peut, seul dans l'image
	// (la répétition automatique attend : elle annulerait son incantation)
	const bool bFreshActivation = AbilitiesToActivate.ContainsByPredicate([this](const FGameplayAbilitySpecHandle& Handle)
	{
		return InputPressedSpecHandles.Contains(Handle);
	});
	FGameplayAbilitySpecHandle BufferedHandle;
	if (bFreshActivation)
	{
		ClearInputBuffer();
	}
	else if (TryTakeBufferedPress(BufferedHandle))
	{
		AbilitiesToActivate.Reset();
		AbilitiesToActivate.Add(BufferedHandle);
	}

	ActivateFromInput(AbilitiesToActivate);

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

void UGenAbilitySystemComponent::CollectHeldActivations(TArray<FGameplayAbilitySpecHandle, TInlineAllocator<8>>& OutAbilities) const
{
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
				OutAbilities.AddUnique(Spec->Handle);
			}
		}
	}
}

void UGenAbilitySystemComponent::ActivateFromInput(const TArray<FGameplayAbilitySpecHandle, TInlineAllocator<8>>& Abilities)
{
	// Revue V6-V8, I-2 : les mouvements en attente partent avant les RPC d'activation (ralentis locaux à la borne)
	if (Abilities.Num() > 0)
	{
		if (AGenCharacterBase* Character = Cast<AGenCharacterBase>(GetAvatarActor()))
		{
			Character->FlushMovesToServer();
		}
	}

	for (const FGameplayAbilitySpecHandle& SpecHandle : Abilities)
	{
		TryActivateAbility(SpecHandle);
	}
}

void UGenAbilitySystemComponent::NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled)
{
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);

	// Revue PIE finale, C-3 : pas de relance depuis EndAbility (le sort finit encore de se terminer) : en fin d'image.
	// Une annulation (touche d'annulation, sort remplacé) ne relance rien d'elle-même
	if (!bWasCancelled && InputHeldSpecHandles.Num() > 0)
	{
		RequestEndOfFrameInput();
	}
}

void UGenAbilitySystemComponent::RequestEndOfFrameInput()
{
	bEndOfFrameInputPending = true;
	if (!PostActorTickHandle.IsValid())
	{
		PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::OnWorldPostActorTick);
	}
}

void UGenAbilitySystemComponent::OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	// Une seule passe par demande, dans le monde de ce composant (plusieurs mondes en PIE) ; à chaque image tant qu'un
	// appui attend (revue PIE finale, C-5)
	if (World != GetWorld() || (!bEndOfFrameInputPending && BufferedPressHandles.Num() == 0))
	{
		return;
	}
	bEndOfFrameInputPending = false;
	ProcessEndOfFrameInput();
}

void UGenAbilitySystemComponent::ProcessEndOfFrameInput()
{
	// Après les minuteurs de l'image (fin d'incantation, départ du sort) : seulement les touches maintenues. Les appuis et
	// relâchés restent à ProcessAbilityInput (déjà passé dans cette image)
	if (HasMatchingGameplayTag(GenGameplayTags::State_Dead))
	{
		ClearInputBuffer();
		return;
	}
	// Revue PIE finale, C-5 : l'appui retenu d'abord, seul (la répétition automatique annulerait son incantation)
	TArray<FGameplayAbilitySpecHandle, TInlineAllocator<8>> AbilitiesToActivate;
	FGameplayAbilitySpecHandle BufferedHandle;
	if (TryTakeBufferedPress(BufferedHandle))
	{
		AbilitiesToActivate.Add(BufferedHandle);
	}
	else
	{
		CollectHeldActivations(AbilitiesToActivate);
	}
	ActivateFromInput(AbilitiesToActivate);
}

void UGenAbilitySystemComponent::OnUnregister()
{
	if (PostActorTickHandle.IsValid())
	{
		FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
		PostActorTickHandle.Reset();
	}
	bEndOfFrameInputPending = false;
	Super::OnUnregister();
}

void UGenAbilitySystemComponent::ClearAbilityInput()
{
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
	ClearInputBuffer();
}

void UGenAbilitySystemComponent::ClearInputBuffer()
{
	BufferedPressHandles.Reset();
	BufferedPressExpireTime = -1.0;
}

void UGenAbilitySystemComponent::NoteLocalCastLockDuration(float Duration)
{
	LocalCastLockEndTime = GetWorld() && Duration > 0.f ? GetWorld()->GetTimeSeconds() + Duration : -1.0;
}

bool UGenAbilitySystemComponent::HasHardCC() const
{
	return HasAnyMatchingGameplayTags(GenGameplayTags::GetHardCCTags());
}

bool UGenAbilitySystemComponent::ShouldBufferPress(const FGameplayAbilitySpec& Spec) const
{
	const UWorld* World = GetWorld();
	if (!World || HasHardCC() || !Cast<UGenGameplayAbility>(Spec.Ability))
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();

	// Verrou de lancement (bond en vol, forme de feu) : dans ses InputBufferLead dernières secondes (fin inconnue : retenu,
	// l'expiration du tampon borne l'attente)
	if (HasMatchingGameplayTag(GenGameplayTags::State_CastLocked))
	{
		return LocalCastLockEndTime < 0.0 || LocalCastLockEndTime - Now <= InputBufferLead;
	}

	// Une incantation (celle de ce sort ou d'un autre) finit dans moins de InputBufferLead s
	for (const FGameplayAbilitySpec& Other : ActivatableAbilities.Items)
	{
		const UGenGA_Cast* CastAbility = Other.IsActive() ? Cast<UGenGA_Cast>(Other.GetPrimaryInstance()) : nullptr;
		if (CastAbility && CastAbility->IsCastPending())
		{
			const float Remaining = CastAbility->GetPendingCastTimeRemaining();
			if (Remaining >= 0.f && Remaining <= InputBufferLead)
			{
				return true;
			}
		}
	}
	return false;
}

bool UGenAbilitySystemComponent::IsBufferedPressBlocked(const FGameplayAbilitySpec& Spec) const
{
	if (Spec.IsActive() || HasMatchingGameplayTag(GenGameplayTags::State_CastLocked))
	{
		return true;
	}
	for (const FGameplayAbilitySpec& Other : ActivatableAbilities.Items)
	{
		const UGenGA_Cast* CastAbility = Other.IsActive() ? Cast<UGenGA_Cast>(Other.GetPrimaryInstance()) : nullptr;
		if (CastAbility && CastAbility->IsCastPending())
		{
			return true;
		}
	}
	// Encore refusé pour une raison passagère (fin du sort qui posait le verrou...) : il attend, jusqu'à expiration
	const UGameplayAbility* Ability = Spec.GetPrimaryInstance() ? Spec.GetPrimaryInstance() : Spec.Ability.Get();
	return !Ability || !Ability->CanActivateAbility(Spec.Handle, AbilityActorInfo.Get());
}

void UGenAbilitySystemComponent::BufferPress(const TArray<FGameplayAbilitySpecHandle, TInlineAllocator<4>>& Handles)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Tous les sorts de l'appui (touche partagée : boule de feu et Pyroblast) ; part celui qui le peut au moment voulu
	BufferedPressHandles.Reset();
	BufferedPressHandles.Append(Handles);
	BufferedPressExpireTime = World->GetTimeSeconds() + InputBufferDuration;
	// Fin d'image : il part dans l'image où le verrou ou l'incantation se termine
	RequestEndOfFrameInput();
}

bool UGenAbilitySystemComponent::TryTakeBufferedPress(FGameplayAbilitySpecHandle& OutHandle)
{
	if (BufferedPressHandles.Num() == 0)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() > BufferedPressExpireTime || HasHardCC() || HasMatchingGameplayTag(GenGameplayTags::State_Dead))
	{
		ClearInputBuffer();
		return false;
	}
	for (const FGameplayAbilitySpecHandle& Handle : BufferedPressHandles)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		if (Spec && !IsBufferedPressBlocked(*Spec))
		{
			OutHandle = Handle;
			ClearInputBuffer();
			return true;
		}
	}
	return false;
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

	// Seuls les contrôles durs passent ici (ex : State.Countering n'en est pas un). Vérifié d'abord (revue P3 T3-7, M5) :
	// une erreur d'appel ne doit pas passer inaperçue parce que la cible est intouchable ou immunisée
	if (!ensureMsgf(GenGameplayTags::GetHardCCTags().HasTagExact(StateTag), TEXT("ApplyHardCC : %s n'est pas un contrôle dur"), *StateTag.ToString()))
	{
		return FActiveGameplayEffectHandle();
	}

	// Plan 3 Task 4 : intouchable (forme de feu...), aucun contrôle dur ne prend.
	// Plan 3 Task 5 : immunisé par la résilience non plus. Refusé => jamais compté pour la résilience.
	if (HasMatchingGameplayTag(GenGameplayTags::State_Untouchable) || HasMatchingGameplayTag(GenGameplayTags::State_CCImmune))
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

void UGenAbilitySystemComponent::OnTagUpdated(const FGameplayTag& Tag, bool TagExists)
{
	Super::OnTagUpdated(Tag, TagExists);

	// Revue V2-V4, I1 : les tags qui changent la règle de nourrissage d'un sort déjà prédit par le client ; revue P3
	// T8-10, I1 : et ceux que les sorts accordés exigent (grâce de DoesAbilitySatisfyTagRequirements)
	if (IsGraceTag(Tag) && GetWorld())
	{
		FGraceTagTimes& Times = GraceTagTimes.FindOrAdd(Tag);
		(TagExists ? Times.Added : Times.Removed) = GetWorld()->GetTimeSeconds();
	}
}

bool UGenAbilitySystemComponent::IsGraceTag(const FGameplayTag& Tag) const
{
	return Tag == GenGameplayTags::State_FastFeeding || Tag == GenGameplayTags::State_FreeResource || RegisteredGraceTags.HasTagExact(Tag);
}

bool UGenAbilitySystemComponent::WasGraceTagChangedNear(const FGameplayTag& Tag, bool bAdded, double ReferenceTime) const
{
	const FGraceTagTimes* Times = GraceTagTimes.Find(Tag);
	return Times && GenFeeding::IsTagChangeInGrace(bAdded ? Times->Added : Times->Removed, ReferenceTime);
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

int32 UGenAbilitySystemComponent::CancelPendingCasts(const UGameplayAbility* Except)
{
	// Revue PIE finale, C-5 : la touche d'annulation vide aussi l'appui en attente
	if (!Except)
	{
		ClearInputBuffer();
	}

	// Plan 3 Task 6. Liste d'abord : annuler un sort modifie les specs actifs
	TArray<UGenGA_Cast*, TInlineAllocator<4>> Pending;
	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		UGenGA_Cast* CastAbility = Spec.IsActive() ? Cast<UGenGA_Cast>(Spec.GetPrimaryInstance()) : nullptr;
		// CanBeCanceled : serveur, visée du client reçue (départ différé compris) => le sort part quand même
		if (CastAbility && CastAbility != Except && CastAbility->IsCastPending() && CastAbility->CanBeCanceled())
		{
			Pending.Add(CastAbility);
		}
	}

	// Revue finale, M-3 : l'annulation retire tout de suite le ralenti d'incantation du client ; ses mouvements ralentis
	// encore en attente partent avant la RPC d'annulation (comme avant l'activation et la visée)
	if (Pending.Num() > 0)
	{
		if (AGenCharacterBase* Character = Cast<AGenCharacterBase>(GetAvatarActor()))
		{
			Character->FlushMovesToServer();
		}
	}

	// Annulation prédite (rien n'a été payé : coûts au lancer), répliquée au serveur qui la refuse si la visée est
	// déjà arrivée (ordre des RPC du joueur : une visée envoyée avant l'annulation arrive avant elle)
	for (UGenGA_Cast* CastAbility : Pending)
	{
		const FGameplayAbilitySpecHandle Handle = CastAbility->GetCurrentAbilitySpecHandle();
		CastAbility->CancelAbility(Handle, CastAbility->GetCurrentActorInfo(), CastAbility->GetCurrentActivationInfo(), true);

		// Revue P3 T3-7, I2 : un sort annulé dont la touche reste enfoncée (clic gauche en répétition automatique) ne se
		// relance pas tout seul dans la même image (ProcessAbilityInput) : il faut relâcher puis rappuyer. Tous les sorts de
		// la même touche (Pyroblast et boule de feu partagent le clic gauche) : la touche entière est « relâchée » pour eux.
		// Touche d'annulation seulement : un sort qui en remplace un autre (Except) peut partager sa touche et doit garder
		// son appui (relâchement du nourrissage). La répétition automatique du sort remplacé reste bloquée tant que le
		// nouveau s'incante (ProcessAbilityInput, IsAnotherAbilityCasting)
		if (Except)
		{
			continue;
		}
		InputHeldSpecHandles.Remove(Handle);
		InputPressedSpecHandles.Remove(Handle);
		if (CastAbility->InputTag.IsValid())
		{
			for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
			{
				if (Spec.GetDynamicSpecSourceTags().HasTagExact(CastAbility->InputTag))
				{
					InputHeldSpecHandles.Remove(Spec.Handle);
					InputPressedSpecHandles.Remove(Spec.Handle);
				}
			}
		}
	}
	return Pending.Num();
}
