#include "Champions/Curffe/CurffeGA_MeteorLeap.h"

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenSalvo.h"
#include "AbilitySystem/GenWorldQueries.h"
#include "Actors/GenProjectile.h"
#include "Engine/HitResult.h"

DEFINE_LOG_CATEGORY_STATIC(LogCurffeMeteorLeap, Log, All);

void UCurffeGA_MeteorLeap::OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation)
{
	// Zone d'atterrissage d'abord (base), puis l'anneau : jamais l'un sans l'autre
	Super::OnLeapLanded(Release, LandingLocation);

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || Release.Fed <= 0 || !RingProjectileClass)
	{
		return;
	}

	const TSharedPtr<FGenProjectileSalvo> Salvo = MakeShared<FGenProjectileSalvo>();
	for (const FVector& Direction : GenAreaRules::GetRingDirections(Release.Fed, Release.AimDirection))
	{
		// Jamais derrière un mur proche : la boule apparaît contre lui et y explose (règle des murs)
		FVector Origin = LandingLocation + Direction * RingSpawnOffset;
		FHitResult WallHit;
		if (GenWorldQueries::FindWallHit(GetWorld(), LandingLocation, Origin, { Avatar }, WallHit))
		{
			Origin = FVector(WallHit.ImpactPoint) - Direction * 5.f;
		}

		SpawnProjectileShot(RingProjectileClass, Origin, Direction, FGenProjectileShotParams(), UGenGE_Damage::StaticClass(),
			RingDamage, RingEnergyOnHit, 0.f, Salvo);
	}

	UE_LOG(LogCurffeMeteorLeap, Verbose, TEXT("%s : anneau de %d boule(s) de feu"), *GetName(), Release.Fed);
}
