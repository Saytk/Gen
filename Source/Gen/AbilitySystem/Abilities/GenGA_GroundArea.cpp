#include "AbilitySystem/Abilities/GenGA_GroundArea.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAbilityTooltipData.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenFeeding.h"
#include "Actors/GenGroundArea.h"
#include "Champions/Curffe/CurffeTuning.h"
#include "Engine/World.h"
#include "Player/GenPlayerController.h"

UGenGA_GroundArea::UGenGA_GroundArea()
{
	// Valeurs de départ du Pilier de flammes (Curffe.md §3) : 2 m + 0.5 m par flamme nourrie
	RadiusAtMaxFeed = Radius + 50.f * CurffeTuning::MaxFeedPerSpell;
	Damage = FScalableFloat(0.f);
}

void UGenGA_GroundArea::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	DestroyPreview();

	AGenPlayerController* Controller = GetGenPlayerControllerFromActorInfo();
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (bShowAimPreview && AreaClass && Controller && Avatar && IsLocallyControlled())
	{
		const FTransform PreviewTransform(FRotator::ZeroRotator, Avatar->GetActorLocation());
		Preview = GetWorld()->SpawnActorDeferred<AGenGroundArea>(AreaClass, PreviewTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Preview)
		{
			Preview->StartPreview(Controller, Range, Radius, bFeedable ? RadiusAtMaxFeed : Radius, MaxFeed);
			Preview->FinishSpawning(PreviewTransform);
		}
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UGenGA_GroundArea::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	DestroyPreview();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGenGA_GroundArea::DestroyPreview()
{
	if (Preview)
	{
		Preview->Destroy();
		Preview = nullptr;
	}
}

void UGenGA_GroundArea::OnCastLaunched(const FGenCastRelease& Release)
{
	DestroyPreview();

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar && Avatar->HasAuthority())
	{
		FGenAreaParams Params;
		Params.Radius = GetAreaRadius(Release.Fed);
		Params.Delay = GenAreaRules::GetImpactDelay(ImpactDelay, MinTelegraph);
		Params.StunDuration = StunDuration;
		Params.KnockbackDistance = KnockbackDistance;

		const int32 Level = GetAbilityLevel();
		const FVector Center = GenAreaRules::ClampToRange(Avatar->GetActorLocation(), Release.AimLocation, Range);
		SpawnGroundArea(AreaClass, Center, Params, UGenGE_Damage::StaticClass(),
			GetAreaDamage(Release.Fed, Level), EnergyOnHit + EnergyPerFeed * Release.Fed);
	}

	FinishAbility();
}

float UGenGA_GroundArea::GetAreaRadius(int32 Fed) const
{
	return bFeedable ? GenFeeding::ScaleByFeed(Radius, RadiusAtMaxFeed, Fed, MaxFeed) : Radius;
}

float UGenGA_GroundArea::GetAreaDamage(int32 Fed, int32 Level) const
{
	return Damage.GetValueAtLevel(Level) + DamagePerFeed * Fed;
}

#define LOCTEXT_NAMESPACE "GenGA_GroundArea"

void UGenGA_GroundArea::GetTooltipArgs(FFormatNamedArguments& Args) const
{
	Super::GetTooltipArgs(Args);
	Args.Add(TEXT("Damage"), GenAbilityTooltip::Number(GetAreaDamage(0)));
	Args.Add(TEXT("Radius"), GenAbilityTooltip::Meters(GetAreaRadius(0)));
	Args.Add(TEXT("RadiusMax"), GenAbilityTooltip::Meters(GetAreaRadius(bFeedable ? MaxFeed : 0)));
	Args.Add(TEXT("Range"), GenAbilityTooltip::Meters(Range));
	Args.Add(TEXT("Stun"), GenAbilityTooltip::Seconds(StunDuration));
	Args.Add(TEXT("Delay"), GenAbilityTooltip::Seconds(GenAreaRules::GetImpactDelay(ImpactDelay, MinTelegraph)));
	Args.Add(TEXT("Knockback"), GenAbilityTooltip::Meters(KnockbackDistance));
}

FText UGenGA_GroundArea::GetFeedTooltipLines(int32 Fed) const
{
	TArray<FText> Parts;
	Parts.Add(FText::Format(LOCTEXT("Radius", "rayon {0}"), GenAbilityTooltip::Meters(GetAreaRadius(Fed))));
	const float AreaDamage = GetAreaDamage(Fed);
	if (AreaDamage > 0.f)
	{
		Parts.Add(FText::Format(LOCTEXT("Damage", "{0} dégâts"), GenAbilityTooltip::Number(AreaDamage)));
	}
	return GenAbilityTooltip::Join(Parts, LOCTEXT("Comma", ", "));
}

void UGenGA_GroundArea::GetTooltipEffectLines(TArray<FText>& OutLines) const
{
	OutLines.Add(FText::Format(LOCTEXT("Impact", "Impact {0} après le lancer"), GenAbilityTooltip::Seconds(GenAreaRules::GetImpactDelay(ImpactDelay, MinTelegraph))));
	if (StunDuration > 0.f)
	{
		OutLines.Add(FText::Format(LOCTEXT("Stun", "Étourdit {0}"), GenAbilityTooltip::Seconds(StunDuration)));
	}
	if (KnockbackDistance > 0.f)
	{
		OutLines.Add(FText::Format(LOCTEXT("Knockback", "Recul {0}"), GenAbilityTooltip::Meters(KnockbackDistance)));
	}
}

#undef LOCTEXT_NAMESPACE
