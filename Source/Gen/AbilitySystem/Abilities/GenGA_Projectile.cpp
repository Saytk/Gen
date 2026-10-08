#include "AbilitySystem/Abilities/GenGA_Projectile.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAbilityTooltipData.h"
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

FText UGenGA_Projectile::GetCompactTooltipLine() const
{
	const int32 Top = GetTooltipMaxFeed();
	if (Top <= 0)
	{
		return Super::GetCompactTooltipLine();
	}
	TArray<float> DamageSeries;
	int32 FirstExplosion = INDEX_NONE;
	int32 FirstKnockback = INDEX_NONE;
	for (int32 Fed = 0; Fed <= Top; ++Fed)
	{
		DamageSeries.Add(GetShotDamage(Fed));
		const FGenProjectileShotParams Shot = GetShotParams(Fed);
		if (FirstExplosion == INDEX_NONE && Shot.ExplosionRadius > 0.f)
		{
			FirstExplosion = Fed;
		}
		if (FirstKnockback == INDEX_NONE && Shot.KnockbackDistance > 0.f)
		{
			FirstKnockback = Fed;
		}
	}
	const FGenProjectileShotParams Max = GetShotParams(Top);
	TArray<FText> Parts;
	Parts.Add(FText::Format(LOCTEXT("Damage", "{0} dégâts"), GenAbilityTooltip::Series(DamageSeries)));
	if (FirstExplosion != INDEX_NONE)
	{
		Parts.Add(FText::Format(LOCTEXT("Explosion", "explosion {0}"), GenAbilityTooltip::FromFeed(FirstExplosion, Top, GenAbilityTooltip::Meters(Max.ExplosionRadius))));
	}
	if (FirstKnockback != INDEX_NONE)
	{
		Parts.Add(FText::Format(LOCTEXT("Knockback", "recul {0}"), GenAbilityTooltip::FromFeed(FirstKnockback, Top, GenAbilityTooltip::Meters(Max.KnockbackDistance))));
	}
	return GenAbilityTooltip::FeedSummary(Top, Parts);
}

#undef LOCTEXT_NAMESPACE
