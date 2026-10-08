#include "Champions/Curffe/CurffeGA_Combustion.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "Actors/GenGroundArea.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "Champions/Curffe/CurffeGameplayTags.h"
#include "GenGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogCurffeCombustion, Log, All);

UCurffeGA_Combustion::UCurffeGA_Combustion()
{
	// Valeurs de la spec (Curffe.md §3 F) : pas de recharge, l'énergie fait office de recharge
	InputTag = GenGameplayTags::InputTag_Ability_Ultimate;
	EnergyCost = 100.f;
	SetAssetTags(FGameplayTagContainer(CurffeGameplayTags::Ability_Combustion));

	CastTime = 0.5f;
	bTurnToAim = false;
	NovaAreaClass = AGenGroundArea::StaticClass();
	RefillEffect = UCurffeGE_HearthFill::StaticClass();
}

void UCurffeGA_Combustion::OnCastLaunched(const FGenCastRelease& Release)
{
	const int32 Level = GetAbilityLevel();

	// Foyer plein, puis embrasé (prédit chez le client : Pyroblast et nourrissage rapide sans attendre le serveur)
	if (RefillEffect)
	{
		ApplyGameplayEffectToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, RefillEffect->GetDefaultObject<UGameplayEffect>(), Level);
	}

	FGameplayEffectSpecHandle AblazeSpec = MakeOutgoingGameplayEffectSpec(UGenGE_TimedState::StaticClass(), Level);
	if (AblazeSpec.IsValid())
	{
		FGameplayTagContainer AblazeTags;
		AblazeTags.AddTag(CurffeGameplayTags::State_Ablaze);
		AblazeTags.AddTag(GenGameplayTags::State_FastFeeding);
		AblazeTags.AddTag(GenGameplayTags::State_FreeResource);
		UGenGE_TimedState::SetDuration(*AblazeSpec.Data, AblazeDuration, AblazeTags);
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, AblazeSpec);
	}

	// Éruption : nova (zone, traverse les contres)
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar && Avatar->HasAuthority())
	{
		FGenAreaParams Params;
		Params.Radius = NovaRadius;
		Params.KnockbackDistance = NovaKnockback;
		SpawnGroundArea(NovaAreaClass, Avatar->GetActorLocation(), Params, UGenGE_Damage::StaticClass(), NovaDamage, 0.f);
	}

	UE_LOG(LogCurffeCombustion, Verbose, TEXT("[%s] %s : éruption (%.0f cm), embrasé %.1fs"), CurrentActorInfo && CurrentActorInfo->IsNetAuthority() ? TEXT("SERVEUR") : TEXT("CLIENT"),
		*GetName(), NovaRadius, AblazeDuration);
	FinishAbility();
}
