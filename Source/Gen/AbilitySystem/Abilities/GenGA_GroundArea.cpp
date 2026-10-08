#include "AbilitySystem/Abilities/GenGA_GroundArea.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
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
		Params.Radius = bFeedable ? GenFeeding::ScaleByFeed(Radius, RadiusAtMaxFeed, Release.Fed, MaxFeed) : Radius;
		Params.Delay = GenAreaRules::GetImpactDelay(ImpactDelay, MinTelegraph);
		Params.StunDuration = StunDuration;
		Params.KnockbackDistance = KnockbackDistance;

		const int32 Level = GetAbilityLevel();
		const FVector Center = GenAreaRules::ClampToRange(Avatar->GetActorLocation(), Release.AimLocation, Range);
		SpawnGroundArea(AreaClass, Center, Params, UGenGE_Damage::StaticClass(),
			Damage.GetValueAtLevel(Level) + DamagePerFeed * Release.Fed, EnergyOnHit + EnergyPerFeed * Release.Fed);
	}

	FinishAbility();
}
