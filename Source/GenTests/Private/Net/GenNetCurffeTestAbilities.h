#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Counter.h"
#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/GenGA_GroundArea.h"
#include "Actors/GenGroundArea.h"
#include "Actors/GenProjectile.h"
#include "Champions/Curffe/CurffeGA_MeteorLeap.h"
#include "Components/SphereComponent.h"
#include "GenNetCurffeTestAbilities.generated.h"

/**
 * Sorts de Curffe du plan 2 en classes C++ de test (Gen.Net.*, module éditeur GenTests) : mêmes classes génériques
 * que les assets de la Task 10 (GA_Backfire, GA_FlamePillar, GA_FlameLeap), valeurs de la spec, sans asset.
 * Tags posés par le test (BEFORE_EACH), jamais pendant le chargement du module : la touche sur le CDO (lue par
 * GrantAbilities), la recharge du bond par TestCooldownTags. Cachés de l'éditeur (HideDropdown).
 */

/** Retour de flamme de test : fenêtre longue (3 s) pour laisser le temps au test, récompenses de la spec. */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_Backfire : public UGenGA_Counter
{
	GENERATED_BODY()

public:
	UGenNetTestGA_Backfire()
	{
		CastTime = 0.1f;
		CounterWindow = 3.f;
		WindowMoveSpeedMultiplier = 0.5f;
		ResourcePerBlock = 2.f;
		EnergyOnFirstBlock = 10.f;
		MeleeKnockbackDistance = 300.f;
	}
};

/** Pilier de flammes de test (valeurs de la spec), zone native sans matériau. */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_FlamePillar : public UGenGA_GroundArea
{
	GENERATED_BODY()

public:
	UGenNetTestGA_FlamePillar()
	{
		AreaClass = AGenGroundArea::StaticClass();
		CastTime = 0.4f;
		Range = 900.f;
		Radius = 200.f;
		ImpactDelay = 0.8f;
		Damage = FScalableFloat(12.f);
		StunDuration = 1.f;
		EnergyOnHit = 8.f;
	}
};

/**
 * Projectile d'anneau de test : grosse sphère (1 m) qui ignore le sol et lente, pour que toutes les boules
 * apparues au point d'atterrissage chevauchent la même cible voisine (test de la salve).
 */
UCLASS(NotBlueprintable, HideDropdown)
class AGenNetTestRingProjectile : public AGenProjectile
{
	GENERATED_BODY()

public:
	AGenNetTestRingProjectile()
	{
		CollisionSphere->InitSphereRadius(100.f);
		CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
		Speed = 300.f;
		MaxRange = 600.f;
	}
};

/** Bond météore de test : vol long (0.8 s) pour étourdir en plein vol, anneau au point d'atterrissage. */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_MeteorLeap : public UCurffeGA_MeteorLeap
{
	GENERATED_BODY()

public:
	UGenNetTestGA_MeteorLeap()
	{
		bFeedable = true;
		CastTime = 0.3f;
		bTurnToAim = true;
		MaxDistance = 700.f;
		LeapHeight = 200.f;
		LeapDuration = 0.8f;
		MaxFlightDuration = 2.f;
		LandingAreaClass = AGenGroundArea::StaticClass();
		LandingRadius = 150.f;
		LandingDamage = 5.f;
		LandingEnergyOnHit = 2.f;
		RingProjectileClass = AGenNetTestRingProjectile::StaticClass();
		RingDamage = 8.f;
		RingEnergyOnHit = 2.f;
		RingSpawnOffset = 0.f;
		CooldownDuration = FScalableFloat(10.f);
	}

	/**
	 * Tags de recharge posés par le test (BEFORE_EACH). Un CDO natif modifié après coup n'est pas recopié dans les
	 * instances (seules les propriétés qui diffèrent du constructeur le sont) : l'instance les reprend à l'activation.
	 */
	static FGameplayTagContainer TestCooldownTags;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
	{
		CooldownTags = TestCooldownTags;
		Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	}
};

/**
 * Pyroblast de test (Plan 3 Task 9) : comme l'asset GA_Pyroblast, n'est lançable que sous State.Curffe.Ablaze
 * (ActivationRequiredTags dans l'asset ; ici par CanActivateAbility, le tag de Curffe n'étant pas exporté).
 */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_Pyroblast : public UGenGA_Projectile
{
	GENERATED_BODY()

public:
	UGenNetTestGA_Pyroblast()
	{
		CastTime = 0.35f;
		Damage = FScalableFloat(13.f);
		BaseExplosionRadius = 120.f;
	}

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override
	{
		const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
		return ASC && ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Curffe.Ablaze")))
			&& Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
	}
};
