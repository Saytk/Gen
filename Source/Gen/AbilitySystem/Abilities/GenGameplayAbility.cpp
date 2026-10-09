#include "AbilitySystem/Abilities/GenGameplayAbility.h"

#include "AbilitySystem/Effects/GenGE_Cooldown.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAbilityTooltipData.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenEnergy.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/GenCharacterBase.h"
#include "Engine/World.h"
#include "Game/GenDevTuning.h"
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

namespace GenGameplayAbilityPrivate
{
	/** Tag bloquant commun à tous les sorts (mort, contrôle dur) : ne départage pas les sorts d'une touche. */
	bool IsUniversalBlock(const FGameplayTag& Tag)
	{
		return Tag.MatchesTagExact(GenGameplayTags::State_Dead) || GenGameplayTags::GetHardCCTags().HasTagExact(Tag);
	}
}

bool UGenGameplayAbility::IsSelectedByOwnerTags(const UAbilitySystemComponent& AbilitySystemComponent) const
{
	const FGameplayTagContainer& Owned = AbilitySystemComponent.GetOwnedGameplayTags();
	if (!Owned.HasAll(ActivationRequiredTags))
	{
		return false;
	}
	for (const FGameplayTag& Blocked : ActivationBlockedTags)
	{
		if (!GenGameplayAbilityPrivate::IsUniversalBlock(Blocked) && Owned.HasTag(Blocked))
		{
			return false;
		}
	}
	return true;
}

void UGenGameplayAbility::GetSelectionTags(FGameplayTagContainer& OutTags) const
{
	OutTags.AppendTags(ActivationRequiredTags);
	for (const FGameplayTag& Blocked : ActivationBlockedTags)
	{
		if (!GenGameplayAbilityPrivate::IsUniversalBlock(Blocked))
		{
			OutTags.AddTag(Blocked);
		}
	}
}

bool UGenGameplayAbility::DoesAbilitySatisfyTagRequirements(const UAbilitySystemComponent& AbilitySystemComponent, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	FGameplayTagContainer Relevant;
	if (Super::DoesAbilitySatisfyTagRequirements(AbilitySystemComponent, SourceTags, TargetTags, &Relevant))
	{
		return true;
	}

	// Grâce seulement sur le serveur, pour un client distant, quand SEULS des tags requis de l'activation manquent
	const UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(&AbilitySystemComponent);
	const FGameplayAbilityActorInfo* Info = AbilitySystemComponent.AbilityActorInfo.Get();
	const bool bRemoteOnServer = GenASC && Info && AbilitySystemComponent.IsOwnerActorAuthoritative() && !Info->IsLocallyControlled();
	// Blocages recalculés ici (le tag d'échec global ActivateFailTagsBlockedTag peut ne pas être configuré) : un sort
	// bloqué n'a jamais de grâce
	const FGameplayTagContainer& Owned = AbilitySystemComponent.GetOwnedGameplayTags();
	const bool bBlocked = AbilitySystemComponent.AreAbilityTagsBlocked(GetAssetTags())
		|| GetAssetTags().HasAny(AbilitySystemComponent.GetBlockedAbilityTags())
		|| Owned.HasAny(ActivationBlockedTags)
		|| (SourceTags && SourceTags->HasAny(SourceBlockedTags))
		|| (TargetTags && TargetTags->HasAny(TargetBlockedTags));
	const bool bOtherRequirementMissing = (SourceTags && !SourceRequiredTags.IsEmpty() && !SourceTags->HasAll(SourceRequiredTags))
		|| (TargetTags && !TargetRequiredTags.IsEmpty() && !TargetTags->HasAll(TargetRequiredTags));
	if (bRemoteOnServer && !bBlocked && !bOtherRequirementMissing && GenASC->GetWorld())
	{
		const double Now = GenASC->GetWorld()->GetTimeSeconds();
		bool bAllInGrace = true;
		for (const FGameplayTag& Required : ActivationRequiredTags)
		{
			if (!Owned.HasTag(Required) && !GenASC->WasGraceTagChangedNear(Required, /*bAdded*/ false, Now))
			{
				bAllInGrace = false;
				break;
			}
		}
		if (bAllInGrace)
		{
			return true;
		}
	}

	if (OptionalRelevantTags)
	{
		OptionalRelevantTags->AppendTags(Relevant);
	}
	return false;
}

bool UGenGameplayAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags))
	{
		return false;
	}
	// Panneau développeur (F10) : énergie infinie, mêmes réglages répliqués sur le client et le serveur
	if (EnergyCost <= 0.f || GenDevTuning::Get(ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr).bInfiniteEnergy)
	{
		return true;
	}

	// Même règle que la barre de sorts (« Pas assez d'énergie ») : 25.0 suffit pour 25, 24.99 non
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const float Energy = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetEnergyAttribute()) : 0.f;
	if (GenEnergy::CanAfford(Energy, EnergyCost))
	{
		return true;
	}

	// Comme Super : la raison du refus (ClientActivateAbilityFailed, retours « pas assez d'énergie » par tag d'échec)
	const FGameplayTag& CostTag = UAbilitySystemGlobals::Get().ActivateFailCostTag;
	if (OptionalRelevantTags && CostTag.IsValid())
	{
		OptionalRelevantTags->AddTag(CostTag);
	}
	return false;
}

void UGenGameplayAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	if (EnergyCost <= 0.f || GenDevTuning::Get(ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr).bInfiniteEnergy)
	{
		return;
	}

	// Appelé par CommitAbility : au lancer pour UGenGA_Cast (client dans la fenêtre de la visée, serveur dans celle
	// de la clé reçue, ou du départ différé), jamais à l'activation. Une incantation annulée ne paie donc rien.
	const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo, UGenGE_Gain::StaticClass(), GetAbilityLevel(Handle, ActorInfo));
	if (Spec.IsValid())
	{
		UGenGE_Gain::SetMagnitudes(*Spec.Data, -EnergyCost, 0.f);
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
	}
}

void UGenGameplayAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	// Panneau développeur (F10) : recharges coupées ou étirées, mêmes réglages répliqués sur le client et le serveur
	const FGenDevTuning& DevTuning = GenDevTuning::Get(ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr);

	// Un GE de cooldown personnalisé a été assigné : comportement GAS standard
	if (CooldownGameplayEffectClass)
	{
		if (!DevTuning.bNoCooldowns)
		{
			Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
		}
		return;
	}

	const int32 Level = GetAbilityLevel(Handle, ActorInfo);
	const float Duration = GenDevTuning::ScaleCooldown(DevTuning, CooldownDuration.GetValueAtLevel(Level));

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

void UGenGameplayAbility::GetTooltipArgs(FFormatNamedArguments& Args) const
{
	Args.Add(TEXT("EnergyCost"), GenAbilityTooltip::Number(EnergyCost));
	Args.Add(TEXT("Cooldown"), GenAbilityTooltip::Seconds(CooldownDuration.GetValueAtLevel(1)));
}

FText UGenGameplayAbility::GetCompactTooltipLine() const
{
	const int32 MaxFeed = GetTooltipMaxFeed();
	if (MaxFeed > 0)
	{
		const FText Top = GetFeedTooltipLines(MaxFeed);
		return Top.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("CompactTop", "{0} {0}|plural(one=flamme,other=flammes) : {1}"), MaxFeed, Top);
	}
	const FText Effect = GetFeedTooltipLines(0);
	if (!Effect.IsEmpty())
	{
		return Effect;
	}
	TArray<FText> Lines;
	GetTooltipEffectLines(Lines);
	return Lines.Num() > 0 ? Lines[0] : FText::GetEmpty();
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
