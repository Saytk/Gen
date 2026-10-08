#include "AbilitySystem/Abilities/GenGA_Projectile.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenIndicatorRules.h"
#include "AbilitySystem/GenWorldQueries.h"
#include "Actors/GenProjectile.h"
#include "Character/GenCharacterBase.h"
#include "Engine/HitResult.h"
#include "HAL/IConsoleManager.h"

namespace GenProjectileAbilityPrivate
{
	TAutoConsoleVariable<int32> CVarShowBasicAttackAimLine(
		TEXT("gen.ShowBasicAttackAimLine"),
		0,
		TEXT("1 = ligne de visée pour les attaques de base (clic gauche, Pyroblast). Client seulement ; 0 par défaut (Art Bible §7.2)."),
		ECVF_Default);
}

UGenGA_Projectile::UGenGA_Projectile()
{
	ProjectileClass = AGenProjectile::StaticClass();
	DamageEffectClass = UGenGE_Damage::StaticClass();
	Damage = FScalableFloat(20.f);
}

bool UGenGA_Projectile::IsBasicAttack() const
{
	return !bFeedable && CastTime < BasicAttackMaxCastTime;
}

bool UGenGA_Projectile::WantsAimIndicator() const
{
	if (IsBasicAttack())
	{
		return Super::WantsAimIndicator() && GenProjectileAbilityPrivate::CVarShowBasicAttackAimLine.GetValueOnGameThread() != 0;
	}
	return Super::WantsAimIndicator();
}

bool UGenGA_Projectile::GetAimGeometry(const AGenCharacterBase& Caster, int32 Fed, const FVector& Cursor, FGenAimGeometry& Out) const
{
	const AGenProjectile* ProjectileCDO = ProjectileClass ? ProjectileClass->GetDefaultObject<AGenProjectile>() : nullptr;
	if (!ProjectileCDO)
	{
		return false;
	}

	// Les valeurs que SpawnProjectile utilisera (jamais recopiées) : le dessin = la hitbox à chaque seuil
	GenIndicatorRules::FProjectileAimParams Params;
	Params.SpawnForwardOffset = SpawnForwardOffset;
	Params.Range = ProjectileCDO->GetMaxRange();
	Params.CollisionRadius = ProjectileCDO->GetCollisionRadius();
	Params.ScaleAtMaxFeed = ScaleAtMaxFeed;
	Params.MaxFeed = MaxFeed;
	Params.ExplosionMinFeed = ExplosionMinFeed;
	Params.ExplosionRadius = ExplosionRadius;
	Params.BaseExplosionRadius = BaseExplosionRadius;
	Params.KnockbackMinFeed = KnockbackMinFeed;

	const FVector Origin = Caster.GetActorLocation();
	const FVector Direction = GenIndicatorRules::FlatDirection(Cursor - Origin);

	// Premier mur sur le trajet (même requête que le projectile), distance depuis le lanceur
	float WallDistance = -1.f;
	FHitResult WallHit;
	if (GenWorldQueries::FindWallHit(Caster.GetWorld(), Origin, Origin + Direction * (SpawnForwardOffset + Params.Range), { &Caster }, WallHit))
	{
		WallDistance = WallHit.Distance;
	}

	GenIndicatorRules::ComputeProjectileAim(Origin, Direction, Params, bFeedable ? Fed : 0, WallDistance, Out);
	return true;
}

void UGenGA_Projectile::OnCastLaunched(const FGenCastRelease& Release)
{
	if (const AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		SpawnProjectile(Avatar->GetActorLocation() + Release.AimDirection * 1000.f, Release.Fed);
	}
	FinishAbility();
}

void UGenGA_Projectile::SpawnProjectile(const FVector& TargetLocation, int32 Fed)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || !ProjectileClass)
	{
		return;
	}

	const FVector Origin = Avatar->GetActorLocation();
	FVector Direction = (TargetLocation - Origin).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = Avatar->GetActorForwardVector();
	}

	const float ClassSpeed = ProjectileClass->GetDefaultObject<AGenProjectile>()->GetSpeed();
	FGenProjectileShotParams ShotParams;
	ShotParams.Speed = GenFeeding::ScaleByFeed(ClassSpeed, ClassSpeed * SpeedMultiplierAtMaxFeed, Fed, MaxFeed);
	ShotParams.Scale = GenFeeding::ScaleByFeed(1.f, ScaleAtMaxFeed, Fed, MaxFeed);
	ShotParams.ExplosionRadius = GenFeeding::GetShotExplosionRadius(Fed, ExplosionMinFeed, ExplosionRadius, BaseExplosionRadius);
	ShotParams.KnockbackDistance = GenFeeding::ReachesThreshold(Fed, KnockbackMinFeed) ? KnockbackDistance : 0.f;

	const int32 Level = GetAbilityLevel();
	SpawnProjectileShot(ProjectileClass, Origin + Direction * SpawnForwardOffset, Direction, ShotParams,
		DamageEffectClass, Damage.GetValueAtLevel(Level) + DamagePerFeed * Fed, EnergyOnHit + EnergyPerFeed * Fed, ResourceOnHit);
}
