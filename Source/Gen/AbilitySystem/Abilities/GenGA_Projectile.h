#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "ScalableFloat.h"
#include "GenGA_Projectile.generated.h"

class AGenProjectile;
class UGameplayEffect;

/**
 * Sort de projectile tiré vers le curseur (ex : boule de feu, grosse boule de feu).
 * Nourrissage, incantation, visée et coûts : voir UGenGA_Cast.
 * Au départ, le serveur fait apparaître le projectile répliqué, mis à l'échelle par le nourrissage
 * (dégâts, vitesse, taille, explosion de zone, repoussement), porteur du GE de dégâts et des gains
 * du lanceur (énergie, ressource) s'il touche.
 */
UCLASS()
class GEN_API UGenGA_Projectile : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_Projectile();

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;

	/** Serveur uniquement : fait apparaître le projectile en direction de TargetLocation. */
	UFUNCTION(BlueprintCallable, Category = "Gen|Projectile")
	void SpawnProjectile(const FVector& TargetLocation, int32 Fed = 0);

	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	TSubclassOf<AGenProjectile> ProjectileClass;

	/** GE appliqué à la cible touchée. Par défaut : UGenGE_Damage (SetByCaller.Damage). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** Dégâts infligés (peut varier selon le niveau du sort). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	FScalableFloat Damage;

	/** Distance devant le lanceur où apparaît le projectile. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	float SpawnForwardOffset = 70.f;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable"))
	float DamagePerFeed = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable"))
	float EnergyPerFeed = 0.f;

	/** Vitesse du projectile à MaxFeed, en multiple de sa vitesse de base. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.1"))
	float SpeedMultiplierAtMaxFeed = 1.f;

	/** Taille du projectile (visuel + collision) à MaxFeed. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0.1"))
	float ScaleAtMaxFeed = 1.f;

	/** Explosion de zone à partir de N unités nourries (0 = jamais). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0"))
	int32 ExplosionMinFeed = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", Units = "cm"))
	float ExplosionRadius = 150.f;

	/** Repoussement à partir de N unités nourries (0 = jamais). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", ClampMin = "0"))
	int32 KnockbackMinFeed = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Feeding", meta = (EditCondition = "bFeedable", Units = "cm"))
	float KnockbackDistance = 400.f;

	/** Énergie gagnée par le lanceur si le projectile touche au moins un ennemi. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Gains")
	float EnergyOnHit = 0.f;

	/** Ressource (flammes...) gagnée par le lanceur si le projectile touche au moins un ennemi. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Gains")
	float ResourceOnHit = 0.f;
};
