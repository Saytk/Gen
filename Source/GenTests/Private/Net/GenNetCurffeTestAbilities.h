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
 * que les assets de la Task 10 (GA_Backfire, GA_FlamePillar, GA_FlameLeap = Flame Dash), valeurs de la spec, sans asset.
 * Tags posés par le test (BEFORE_EACH), jamais pendant le chargement du module : la touche sur le CDO (lue par
 * GrantAbilities), la recharge du bond par TestCooldownTags. Cachés de l'éditeur (HideDropdown).
 */

/** Retour de flamme de test : fenêtre longue (3 s) pour laisser le temps au test, récompenses de la spec. */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_Backfire : public UGenGA_Counter
{
	GENERATED_BODY()

public:
	/** Ralenti de la fenêtre (les tests le comparent au multiplicateur local du personnage). */
	static constexpr float TestWindowMoveSpeedMultiplier = 0.5f;

	UGenNetTestGA_Backfire()
	{
		CastTime = 0.1f;
		CounterWindow = 3.f;
		WindowMoveSpeedMultiplier = TestWindowMoveSpeedMultiplier;
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
 * Flame Dash de test (Space de Curffe, valeurs de la spec) : 3 m, puis 2.5 m par flamme à ±30°. Nom de classe gardé
 * (comme UCurffeGA_MeteorLeap) : les autres suites s'en servent comme sort nourrissable qui verrouille les sorts.
 */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_MeteorLeap : public UCurffeGA_MeteorLeap
{
	GENERATED_BODY()

public:
	UGenNetTestGA_MeteorLeap()
	{
		bFeedable = true;
		CastTime = 0.1f;
		bTurnToAim = true;
		BaseDistance = 300.f;
		SegmentDistance = 250.f;
		ZigzagAngle = 30.f;
		SegmentDuration = 0.12f;
		CooldownDuration = FScalableFloat(10.f);
	}

	/**
	 * Tags de recharge posés par le test (BEFORE_EACH). Un CDO natif modifié après coup n'est pas recopié dans les
	 * instances (seules les propriétés qui diffèrent du constructeur le sont) : l'instance les reprend à l'activation.
	 */
	static FGameplayTagContainer TestCooldownTags;

	/** Durée d'un segment, reprise à l'activation (0.12 s par défaut ; plus long pour agir en pleine ruée). */
	static float TestSegmentDuration;

	/** Durée de la ruée sans flamme avec TestSegmentDuration (un segment, sans mur). */
	static float GetTestDashDuration(int32 Fed) { return TestSegmentDuration * (1 + Fed); }

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
	{
		CooldownTags = TestCooldownTags;
		SegmentDuration = TestSegmentDuration;
		Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	}
};

/** Sort passif de test (revue Plan 2 Tasks 7-8, I-3) : sans touche, serveur seulement, se termine aussitôt. */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_Passive : public UGenGameplayAbility
{
	GENERATED_BODY()

public:
	UGenNetTestGA_Passive()
	{
		NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	}

	/** Activations, toutes machines confondues (un seul process). */
	static int32 ActivationCount;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
	{
		++ActivationCount;
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
};

/**
 * Sort de test déclenché par un événement (revue Plan 2 Tasks 7-8, I-3) : il a une touche et il est prédit, mais une
 * activation par événement n'est pas un appui du joueur. Touche et événement posés par le test (BEFORE_EACH) : la touche
 * à chaque activation (avant PreActivate, qui prévient la posture), l'événement sur le CDO (lu quand le sort est accordé).
 */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_Triggered : public UGenGameplayAbility
{
	GENERATED_BODY()

public:
	static FGameplayTag TestInputTag;
	static int32 ActivationCount;

	static void SetTriggerEvent(const FGameplayTag& EventTag)
	{
		UGenNetTestGA_Triggered* CDO = GetMutableDefault<UGenNetTestGA_Triggered>();
		CDO->AbilityTriggers.Reset();
		FAbilityTriggerData Trigger;
		Trigger.TriggerTag = EventTag;
		Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		CDO->AbilityTriggers.Add(Trigger);
	}

protected:
	virtual void PreActivate(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		FOnGameplayAbilityEnded::FDelegate* OnGameplayAbilityEndedDelegate, const FGameplayEventData* TriggerEventData = nullptr) override
	{
		InputTag = TestInputTag;
		Super::PreActivate(Handle, ActorInfo, ActivationInfo, OnGameplayAbilityEndedDelegate, TriggerEventData);
	}

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
	{
		++ActivationCount;
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
};

/**
 * Pyroblast de test (Plan 3 Task 9) : comme l'asset GA_Pyroblast, n'est lançable que sous State.Curffe.Ablaze, par
 * ActivationRequiredTags comme l'asset (revue P3 T8-10, I1 : la grâce du serveur porte sur les tags requis). Posé à
 * l'octroi sur l'instance, jamais pendant le chargement du module (tag de Curffe demandé par son nom).
 */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_Pyroblast : public UGenGA_Projectile
{
	GENERATED_BODY()

public:
	UGenNetTestGA_Pyroblast()
	{
		// Valeurs de l'asset (spec : 0.40 s, 15 dégâts) ; attaque de base déclarée, comme sur GA_Pyroblast
		CastTime = 0.4f;
		Damage = FScalableFloat(15.f);
		BaseExplosionRadius = 120.f;
		bIsBasicAttack = true;
	}

	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override
	{
		ActivationRequiredTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Curffe.Ablaze")));
		Super::OnGiveAbility(ActorInfo, Spec);
	}
};
