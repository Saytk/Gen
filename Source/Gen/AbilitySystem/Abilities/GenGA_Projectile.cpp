#include "AbilitySystem/Abilities/GenGA_Projectile.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenFeeding.h"
#include "Actors/GenProjectile.h"

UGenGA_Projectile::UGenGA_Projectile()
{
	ProjectileClass = AGenProjectile::StaticClass();
	DamageEffectClass = UGenGE_Damage::StaticClass();
	Damage = FScalableFloat(20.f);
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
	ShotParams.ExplosionRadius = GenFeeding::ReachesThreshold(Fed, ExplosionMinFeed) ? ExplosionRadius : 0.f;
	ShotParams.KnockbackDistance = GenFeeding::ReachesThreshold(Fed, KnockbackMinFeed) ? KnockbackDistance : 0.f;

	const int32 Level = GetAbilityLevel();
	SpawnProjectileShot(ProjectileClass, Origin + Direction * SpawnForwardOffset, Direction, ShotParams,
		DamageEffectClass, Damage.GetValueAtLevel(Level) + DamagePerFeed * Fed, EnergyOnHit + EnergyPerFeed * Fed, ResourceOnHit);
}
