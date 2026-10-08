#include "Champions/Curffe/CurffeGA_Combustion.h"

#include "AbilitySystem/GenAbilityTooltipData.h"
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

#define LOCTEXT_NAMESPACE "CurffeGA_Combustion"

void UCurffeGA_Combustion::GetTooltipArgs(FFormatNamedArguments& Args) const
{
	Super::GetTooltipArgs(Args);
	Args.Add(TEXT("Radius"), GenAbilityTooltip::Meters(NovaRadius));
	Args.Add(TEXT("Damage"), GenAbilityTooltip::Number(NovaDamage));
	Args.Add(TEXT("Knockback"), GenAbilityTooltip::Meters(NovaKnockback));
	Args.Add(TEXT("Duration"), GenAbilityTooltip::Seconds(AblazeDuration));
}

void UCurffeGA_Combustion::GetTooltipEffectLines(TArray<FText>& OutLines) const
{
	OutLines.Add(LOCTEXT("Payment", "Énergie payée à la fin de l'incantation ; interrompue par un contrôle dur, rien n'est payé"));
	TArray<FText> Nova;
	Nova.Add(FText::Format(LOCTEXT("Nova", "Nova {0}, {1} dégâts"), GenAbilityTooltip::Meters(NovaRadius), GenAbilityTooltip::Number(NovaDamage)));
	if (NovaKnockback > 0.f)
	{
		Nova.Add(FText::Format(LOCTEXT("NovaKnockback", "recul {0}"), GenAbilityTooltip::Meters(NovaKnockback)));
	}
	OutLines.Add(GenAbilityTooltip::Join(Nova, LOCTEXT("Comma", ", ")));
	if (RefillEffect)
	{
		OutLines.Add(LOCTEXT("Refill", "Foyer rempli"));
	}
	OutLines.Add(FText::Format(LOCTEXT("Ablaze", "Embrasé {0} : Pyroblast au clic gauche, flammes illimitées, nourrissage rapide"), GenAbilityTooltip::Seconds(AblazeDuration)));
}

#undef LOCTEXT_NAMESPACE
