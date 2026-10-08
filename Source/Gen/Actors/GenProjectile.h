#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystem/GenSalvo.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "GenProjectile.generated.h"

class AGenCharacterBase;
class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class USoundBase;
class USphereComponent;

/** Réglages propres à un tir, fixés par le sort avant FinishSpawning (serveur). */
struct FGenProjectileShotParams
{
	/** 0 = vitesse de la classe. */
	float Speed = 0.f;
	float Scale = 1.f;
	/** 0 = pas d'explosion de zone. */
	float ExplosionRadius = 0.f;
	/** 0 = pas de repoussement. */
	float KnockbackDistance = 0.f;
};

/**
 * Projectile répliqué. Créé par le serveur (UGenGA_Cast::SpawnProjectileShot), il porte le spec du GE de dégâts.
 *
 * - Traverse les alliés et les morts, explose sur un ennemi ou un obstacle.
 * - Coup direct = projectile (déclenche les contres) ; éclaboussure = zone (ne les déclenche pas).
 * - Seul le serveur applique les dégâts ; l'explosion est répliquée (bExploded) pour les FX.
 */
UCLASS()
class GEN_API AGenProjectile : public AActor
{
	GENERATED_BODY()

public:
	AGenProjectile();

	/** Rempli par le sort avant FinishSpawning (serveur uniquement, non répliqué). */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile", meta = (ExposeOnSpawn = true))
	FGameplayEffectSpecHandle DamageEffectSpecHandle;

	/** Gains du lanceur (énergie, ressource), appliqués s'il touche au moins un ennemi (serveur). */
	FGameplayEffectSpecHandle InstigatorOnHitSpecHandle;

	/** Salve dont fait partie ce projectile (anneau) : une cible touchée par la salve est traversée (serveur). */
	TSharedPtr<FGenProjectileSalvo> Salvo;

	/** Serveur, avant FinishSpawning. */
	void InitializeShot(const FGenProjectileShotParams& Params);

	/**
	 * Serveur, avant FinishSpawning : équipe du lanceur retenue au tir (revue Plan 2 Tasks 7-8, M-6, comme AGenGroundArea).
	 * Les cibles restent justes si le pion du lanceur disparaît pendant le vol. Sans appel : équipe du pion instigateur.
	 */
	void SetSourceTeam(uint8 InSourceTeam);

	/** Équipe retenue au tir, ou celle du pion instigateur (GenNoTeam si aucun). */
	uint8 GetSourceTeam() const;

	float GetSpeed() const { return Speed; }

	/** Portée max (cm), mesurée depuis le point d'apparition (indicateurs de visée : couloir). */
	float GetMaxRange() const { return MaxRange; }

	/** Rayon de la sphère de collision, sans l'échelle du tir (indicateurs : largeur du couloir, amorces). */
	float GetCollisionRadius() const;

	/** A explosé (consommé par un impact) ; répliqué. */
	bool HasExploded() const { return bExploded; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * Serveur : applique les dégâts éventuels puis déclenche l'explosion. DirectResponse = réponse déjà résolue
	 * de la cible directe (OnSphereOverlap), jamais Ignored : une cible intouchable est traversée sans exploser.
	 */
	void Explode(AActor* HitActor, const FVector& Location, EGenHitResponse DirectResponse = EGenHitResponse::Hit);

	UFUNCTION()
	void OnRep_Exploded();

	/** Joue les FX/sons d'impact et cache le projectile (toutes les machines). */
	void PlayImpactEffects();

	/** Hook Blueprint pour des FX additionnels à l'impact. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Projectile", meta = (DisplayName = "On Impact"))
	void K2_OnImpact(const FVector& Location);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** FX de traînée/visuel du projectile (assignez un Niagara System dans le Blueprint). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> ProjectileFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Projectile")
	float Speed = 1800.f;

	/** Portée max : arrivé au bout, le projectile explose (FX d'impact, sans dégâts). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	float MaxRange = 1500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX")
	TObjectPtr<UNiagaraSystem> ImpactFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX")
	TObjectPtr<USoundBase> ImpactSound;

	UPROPERTY(ReplicatedUsing = OnRep_Exploded)
	bool bExploded = false;

	UPROPERTY(Replicated)
	FVector_NetQuantize ImpactLocation;

	/** Échelle du tir (visuel + collision), répliquée à l'apparition. */
	UPROPERTY(Replicated)
	float ShotScale = 1.f;

	/** Serveur uniquement. */
	float ExplosionRadius = 0.f;
	float KnockbackDistance = 0.f;

	bool IsValidTarget(const AGenCharacterBase* Character) const;

	/** Character est un ennemi du lanceur (équipe retenue au tir). */
	bool IsEnemy(const AGenCharacterBase* Character) const;

	/**
	 * Éclaboussure : ennemis vivants dans le rayon, en ligne de vue depuis Origin (pas à travers les murs),
	 * hors Excluded (cible directe, touchée ou bloquée) et qui ne l'ignorent pas (nature : zone).
	 */
	void AddExplosionTargets(const FVector& Origin, const AGenCharacterBase* Excluded, TArray<AGenCharacterBase*>& InOutTargets) const;

	/** Dégâts + repoussement éventuel sur une cible. */
	void ApplyHit(AGenCharacterBase* Target, const FVector& Origin, bool bDirectHit);

private:
	bool bImpactEffectsPlayed = false;

	/** Équipe du lanceur au tir (serveur), valable si bHasSourceTeam. */
	uint8 SourceTeam = 255;
	bool bHasSourceTeam = false;
};
