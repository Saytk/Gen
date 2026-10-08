#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "ScalableFloat.h"
#include "GenGA_Projectile.generated.h"

class AGenProjectile;
struct FGenProjectileShotParams;
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

	/**
	 * Plan Visuals V6 : couloir jusqu'à la portée du projectile (coupé au premier mur), largeur = diamètre de collision ×
	 * l'échelle que le serveur appliquera pour Fed, éclat au rayon du tir (GenIndicatorRules::ComputeProjectileAim).
	 */
	virtual bool GetAimGeometry(const AGenCharacterBase& Caster, int32 Fed, const FVector& Cursor, FGenAimGeometry& Out) const override;

	/** Tir de base (bIsBasicAttack, déclaré par le sort) : jamais de ligne de visée par défaut. */
	bool IsBasicAttack() const { return bIsBasicAttack; }

	/** Dégâts d'un tir avec Fed unités nourries : le tir (SpawnProjectile) et l'infobulle lisent cette valeur. */
	float GetShotDamage(int32 Fed, int32 Level = 1) const;

	/** Vitesse, taille, explosion et repoussement d'un tir avec Fed unités nourries (tir et infobulle). */
	FGenProjectileShotParams GetShotParams(int32 Fed) const;

	//~ UGenGameplayAbility (infobulle) : {Damage} (sans flamme), {DamageMax}, {Range}, {ExplosionRadius}, {Knockback}, {EnergyOnHit}
	virtual void GetTooltipArgs(FFormatNamedArguments& Args) const override;
	virtual float GetTooltipRange() const override;
	virtual FText GetFeedTooltipLines(int32 Fed) const override;
	/** Sort nourri : dégâts par flamme, puis le seuil de l'explosion et du recul (« explosion dès 2 · recul à 3 »). */
	virtual FText GetCompactTooltipLine() const override;

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;

	/**
	 * Attaque de base (clic gauche, Pyroblast) : ligne de visée seulement si le joueur l'a demandée (console
	 * gen.ShowBasicAttackAimLine 1, client ; passera dans les réglages, UI §7.2). Art Bible §7.2 : Filler sans télégraphe.
	 */
	virtual bool WantsAimIndicator() const override;

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

	/**
	 * Explosion de zone même sans nourrissage (ex : Pyroblast, 120 cm). 0 = seulement via ExplosionMinFeed.
	 * Remplacée par ExplosionRadius dès ExplosionMinFeed unités nourries. Python : base_explosion_radius.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile", meta = (ClampMin = "0.0", Units = "cm"))
	float BaseExplosionRadius = 0.f;

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
