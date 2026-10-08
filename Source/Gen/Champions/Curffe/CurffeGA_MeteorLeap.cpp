#include "Champions/Curffe/CurffeGA_MeteorLeap.h"

#include "AbilitySystem/GenAbilityTooltipData.h"
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
