#include "AbilitySystem/Abilities/GenGameplayAbility.h"

#include "AbilitySystem/Effects/GenGE_Cooldown.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystemComponent.h"
#include "Character/GenCharacterBase.h"
#include "Engine/World.h"
#include "GenGameplayTags.h"
#include "Player/GenPlayerController.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "GenGameplayAbility"

UGenGameplayAbility::UGenGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;

	// CooldownGameplayEffectClass reste vide : ApplyCooldown utilise alors UGenGE_Cooldown
	// avec CooldownDuration + CooldownTags (voir ApplyCooldown).

	ActivationBlockedTags.AddTag(GenGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(GenGameplayTags::State_Stunned);
}

const FGameplayTagContainer* UGenGameplayAbility::GetCooldownTags() const
{
	TempCooldownTags.Reset();

	if (const FGameplayTagContainer* ParentTags = Super::GetCooldownTags())
	{
		TempCooldownTags.AppendTags(*ParentTags);
	}
	TempCooldownTags.AppendTags(CooldownTags);

	return &TempCooldownTags;
}

bool UGenGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		return true;
	}

	// Contrôle dur (répliqué par le serveur) : refusé partout. Le tag du contrôle est rendu comme raison
	// de l'échec (AbilityFailedCallbacks, interface)
	if (ASC->HasAnyMatchingGameplayTags(GenGameplayTags::GetHardCCTags()))
	{
		if (OptionalRelevantTags)
		{
			FGameplayTagContainer OwnedTags;
			ASC->GetOwnedGameplayTags(OwnedTags);
			OptionalRelevantTags->AppendTags(OwnedTags.Filter(GenGameplayTags::GetHardCCTags()));
		}
		return false;
	}

	// Verrou de lancement : tag local posé par chaque machine à SON départ, retiré à SON atterrissage.
	// Le côté qui prédit fait foi ; le serveur ne refuse un client distant qu'au début du verrou
	// (sinon un sort lancé juste après l'atterrissage du client arriverait sous le verrou du serveur)
	const bool bLocked = ASC->HasMatchingGameplayTag(GenGameplayTags::State_CastLocked);
	const UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(ASC);
	const double EnforcedUntil = GenASC ? GenASC->GetCastLockEnforcedUntil() : -1.0;
	const double Now = ASC->GetWorld() ? ASC->GetWorld()->GetTimeSeconds() : 0.0;
	if (GenFeeding::IsRefusedByCastLock(bLocked, ActorInfo->IsLocallyControlled(), Now, EnforcedUntil))
	{
		if (OptionalRelevantTags)
		{
			OptionalRelevantTags->AddTag(GenGameplayTags::State_CastLocked);
		}
		return false;
	}
	return true;
}

void UGenGameplayAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	// Un GE de cooldown personnalisé a été assigné : comportement GAS standard
	if (CooldownGameplayEffectClass)
	{
		Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
		return;
	}

	const int32 Level = GetAbilityLevel(Handle, ActorInfo);
	const float Duration = CooldownDuration.GetValueAtLevel(Level);

	if (Duration <= 0.f || CooldownTags.IsEmpty())
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo, UGenGE_Cooldown::StaticClass(), Level);
	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->DynamicGrantedTags.AppendTags(CooldownTags);
		SpecHandle.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Cooldown, Duration);
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
	}
}

AGenCharacterBase* UGenGameplayAbility::GetGenCharacterFromActorInfo() const
{
	return Cast<AGenCharacterBase>(GetAvatarActorFromActorInfo());
}

AGenPlayerController* UGenGameplayAbility::GetGenPlayerControllerFromActorInfo() const
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	return Info ? Cast<AGenPlayerController>(Info->PlayerController.Get()) : nullptr;
}

#if WITH_EDITOR
EDataValidationResult UGenGameplayAbility::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Sans tag, le cooldown générique serait appliqué mais jamais détecté par CheckCooldown
	if (!CooldownGameplayEffectClass && CooldownDuration.GetValueAtLevel(1) > 0.f && CooldownTags.IsEmpty())
	{
		Context.AddError(LOCTEXT("MissingCooldownTags", "CooldownDuration > 0 mais CooldownTags est vide : ajoutez un tag Cooldown.Ability.<NomDuSort>."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
