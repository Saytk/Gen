#include "Champions/Curffe/CurffeGA_MeteorLeap.h"

#include "AbilitySystem/GenAbilityTooltipData.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenSalvo.h"
#include "AbilitySystem/GenWorldQueries.h"
#include "Actors/GenProjectile.h"
#include "Engine/HitResult.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCurffeMeteorLeap, Log, All);

float UCurffeGA_MeteorLeap::GetRingProjectileRadius() const
{
	// Les boules de l'anneau partent sans échelle (FGenProjectileShotParams par défaut) : rayon de la classe
	return RingProjectileClass ? RingProjectileClass->GetDefaultObject<AGenProjectile>()->GetCollisionRadius() : 0.f;
}

void UCurffeGA_MeteorLeap::OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation)
{
	// Zone d'atterrissage d'abord (base), puis l'anneau, dans le même appel serveur (l'anneau part même si la zone n'a
	// pas de classe ou n'a pas pu apparaître)
	Super::OnLeapLanded(Release, LandingLocation);

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || Release.Fed <= 0 || !RingProjectileClass)
	{
		return;
	}

	// Revue Plan 2 Tasks 7-8, M-8 : une pente ou une marche n'arrête pas une boule (seulement un obstacle non praticable)
	const ACharacter* Character = Cast<ACharacter>(Avatar);
	const float WalkableFloorZ = Character && Character->GetCharacterMovement() ? Character->GetCharacterMovement()->GetWalkableFloorZ() : 0.71f;

	const TSharedPtr<FGenProjectileSalvo> Salvo = MakeShared<FGenProjectileSalvo>();
	for (const FVector& Direction : GenAreaRules::GetRingDirections(Release.Fed, Release.AimDirection))
	{
		// Jamais derrière un mur proche : la boule apparaît contre lui et y explose (règle des murs)
		FVector Origin = LandingLocation + Direction * RingSpawnOffset;
		FHitResult WallHit;
		if (GenWorldQueries::FindWallHit(GetWorld(), LandingLocation, Origin, { Avatar }, WallHit) && GenAreaRules::IsRingWall(WallHit.ImpactNormal, WalkableFloorZ))
		{
			Origin = FVector(WallHit.ImpactPoint) - Direction * 5.f;
		}

		SpawnProjectileShot(RingProjectileClass, Origin, Direction, FGenProjectileShotParams(), UGenGE_Damage::StaticClass(),
			RingDamage, RingEnergyOnHit, 0.f, Salvo);
	}

	UE_LOG(LogCurffeMeteorLeap, Verbose, TEXT("%s : anneau de %d boule(s) de feu"), *GetName(), Release.Fed);
}

#define LOCTEXT_NAMESPACE "CurffeGA_MeteorLeap"

void UCurffeGA_MeteorLeap::GetTooltipArgs(FFormatNamedArguments& Args) const
{
	Super::GetTooltipArgs(Args);
	Args.Add(TEXT("RingDamage"), GenAbilityTooltip::Number(RingDamage));
}

FText UCurffeGA_MeteorLeap::GetFeedTooltipLines(int32 Fed) const
{
	const FText TakeOff = Super::GetFeedTooltipLines(Fed);
	if (Fed <= 0 || !RingProjectileClass)
	{
		return TakeOff;
	}
	// Une boule par flamme (GenAreaRules::GetRingDirections(Release.Fed)), RingDamage chacune, une seule par ennemi (salve)
	return FText::Format(LOCTEXT("Ring", "{0} + anneau de {1} {1}|plural(one=boule,other=boules) de feu ({2} dégâts, un coup par ennemi)"),
		TakeOff, Fed, GenAbilityTooltip::Number(RingDamage));
}

#undef LOCTEXT_NAMESPACE
