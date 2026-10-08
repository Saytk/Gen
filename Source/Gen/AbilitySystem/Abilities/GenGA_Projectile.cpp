#include "AbilitySystem/Abilities/GenGA_Projectile.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAbilityTooltipData.h"
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

	SpawnProjectileShot(ProjectileClass, Origin + Direction * SpawnForwardOffset, Direction, GetShotParams(Fed),
		DamageEffectClass, GetShotDamage(Fed, GetAbilityLevel()), EnergyOnHit + EnergyPerFeed * Fed, ResourceOnHit);
}

float UGenGA_Projectile::GetShotDamage(int32 Fed, int32 Level) const
{
	return Damage.GetValueAtLevel(Level) + DamagePerFeed * Fed;
}

FGenProjectileShotParams UGenGA_Projectile::GetShotParams(int32 Fed) const
{
	const float ClassSpeed = ProjectileClass ? ProjectileClass->GetDefaultObject<AGenProjectile>()->GetSpeed() : 0.f;
	FGenProjectileShotParams ShotParams;
	ShotParams.Speed = GenFeeding::ScaleByFeed(ClassSpeed, ClassSpeed * SpeedMultiplierAtMaxFeed, Fed, MaxFeed);
	ShotParams.Scale = GenFeeding::ScaleByFeed(1.f, ScaleAtMaxFeed, Fed, MaxFeed);
	ShotParams.ExplosionRadius = GenFeeding::GetShotExplosionRadius(Fed, ExplosionMinFeed, ExplosionRadius, BaseExplosionRadius);
	ShotParams.KnockbackDistance = GenFeeding::ReachesThreshold(Fed, KnockbackMinFeed) ? KnockbackDistance : 0.f;
	return ShotParams;
}

#define LOCTEXT_NAMESPACE "GenGA_Projectile"

void UGenGA_Projectile::GetTooltipArgs(FFormatNamedArguments& Args) const
{
	Super::GetTooltipArgs(Args);
	const int32 Top = bFeedable ? MaxFeed : 0;
	const FGenProjectileShotParams Max = GetShotParams(Top);
	Args.Add(TEXT("Damage"), GenAbilityTooltip::Number(GetShotDamage(0)));
	Args.Add(TEXT("DamageMax"), GenAbilityTooltip::Number(GetShotDamage(Top)));
	Args.Add(TEXT("Range"), GenAbilityTooltip::Meters(GetTooltipRange()));
	Args.Add(TEXT("ExplosionRadius"), GenAbilityTooltip::Meters(Max.ExplosionRadius));
	Args.Add(TEXT("Knockback"), GenAbilityTooltip::Meters(Max.KnockbackDistance));
	Args.Add(TEXT("EnergyOnHit"), GenAbilityTooltip::Number(EnergyOnHit));
}

float UGenGA_Projectile::GetTooltipRange() const
{
	return ProjectileClass ? ProjectileClass->GetDefaultObject<AGenProjectile>()->GetMaxRange() : 0.f;
}

FText UGenGA_Projectile::GetFeedTooltipLines(int32 Fed) const
{
	const FGenProjectileShotParams Shot = GetShotParams(Fed);
	TArray<FText> Parts;
	Parts.Add(FText::Format(LOCTEXT("Damage", "{0} dégâts"), GenAbilityTooltip::Number(GetShotDamage(Fed))));
	if (Shot.ExplosionRadius > 0.f)
	{
		Parts.Add(FText::Format(LOCTEXT("Explosion", "explosion {0}"), GenAbilityTooltip::Meters(Shot.ExplosionRadius)));
	}
	if (Shot.KnockbackDistance > 0.f)
	{
		Parts.Add(FText::Format(LOCTEXT("Knockback", "recul {0}"), GenAbilityTooltip::Meters(Shot.KnockbackDistance)));
	}
	return GenAbilityTooltip::Join(Parts, LOCTEXT("Plus", " + "));
}

#undef LOCTEXT_NAMESPACE
