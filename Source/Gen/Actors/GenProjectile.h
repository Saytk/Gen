#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystem/GenSalvo.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "GenProjectile.generated.h"

class AGenCharacterBase;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class USoundBase;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;

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
 * - Marqueur au sol (Art Bible §7.1 règle 6) : un plan sous le projectile aux couleurs du point de vue (soi, allié,
 *   ennemi), créé sur les clients seulement (jamais sur le serveur dédié), sans tick.
 * - Rayon d'explosion répliqué à l'apparition : l'impact d'éclaboussure a la taille réelle chez chaque client.
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
	 * Serveur, avant FinishSpawning : équipe du lanceur retenue au tir (même nom et type que sur curffe-plan2, revue
	 * Plan 2 Tasks 7-8, M-6). Répliquée à l'apparition seulement (revue V6-V8, M-9) : le marqueur au sol a la bonne
	 * couleur avant l'arrivée du pion du lanceur. Sans appel : équipe du pion instigateur.
	 */
	void SetSourceTeam(uint8 InSourceTeam);

	/** Équipe retenue au tir, ou celle du pion instigateur (GenNoTeam si aucun). */
	uint8 GetSourceTeam() const;

	float GetSpeed() const { return Speed; }

	/** Portée max (cm) : lue sur le CDO par la visée (V1). */
	float GetMaxRange() const { return MaxRange; }

	/** Rayon de collision non mis à l'échelle (CollisionSphere ; lisible sur le CDO). */
	float GetCollisionRadius() const;

	/** Échelle du tir (répliquée à l'apparition). */
	float GetShotScale() const { return ShotScale; }

	/** Rayon d'éclaboussure (cm, 0 = aucune) : fixé par le serveur, répliqué à l'apparition. */
	float GetExplosionRadius() const { return ExplosionRadius; }

	/** Rayon du marqueur au sol (cm) : collision × ShotScale × GroundMarkerRadiusScale. */
	float GetGroundMarkerRadius() const;

	/** Plan du marqueur au sol (nul sur le serveur dédié, sans matériau ou si le projectile a déjà explosé). */
	UStaticMeshComponent* GetGroundMarker() const { return GroundMarker; }

	/** A explosé (consommé par un impact) ; répliqué. */
	bool HasExploded() const { return bExploded; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void OnRep_Instigator() override;

	/** Clients : crée le plan du marqueur au sol (une fois), le pose au sol et le colore. */
	void SetupGroundMarker();

	/** Couleur du marqueur selon le point de vue du joueur local (soi, allié, ennemi). */
	void UpdateGroundMarkerRelation();

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

	/**
	 * Impact d'éclaboussure, joué EN PLUS d'ImpactFX quand le tir a un rayon d'explosion. Échelle =
	 * ExplosionRadius / SplashImpactReferenceRadius : le cercle dessiné = la zone touchée.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX")
	TObjectPtr<UNiagaraSystem> SplashImpactFX;

	/** Rayon pour lequel SplashImpactFX est dessiné à l'échelle 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX", meta = (ClampMin = "1.0", Units = "cm"))
	float SplashImpactReferenceRadius = 150.f;

	/** Matériau du marqueur au sol (défaut C++ : MI_Telegraph_Marker). Sans matériau : pas de marqueur. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|Ground Marker")
	TObjectPtr<UMaterialInterface> GroundMarkerMaterial;

	/** Rayon du marqueur = rayon de collision mis à l'échelle × ce facteur (un peu plus large que la boule). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|Ground Marker", meta = (ClampMin = "0.1"))
	float GroundMarkerRadiusScale = 1.2f;

	/** Demi-hauteur du lanceur supposée tant que son pion n'est pas connu (le tir part du centre de sa capsule). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|Ground Marker", meta = (ClampMin = "0.0", Units = "cm"))
	float GroundMarkerFallbackHeight = 88.f;

	/** Maillage du marqueur (plan de 100 cm du moteur). */
	UPROPERTY()
	TObjectPtr<UStaticMesh> GroundMarkerMesh;

	/** Plan du marqueur, créé en BeginPlay sur les clients seulement (son MID : une fois, au premier affichage). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> GroundMarker;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GroundMarkerMID;

	UPROPERTY(ReplicatedUsing = OnRep_Exploded)
	bool bExploded = false;

	UPROPERTY(Replicated)
	FVector_NetQuantize ImpactLocation;

	/** Échelle du tir (visuel + collision), répliquée à l'apparition. */
	UPROPERTY(Replicated)
	float ShotScale = 1.f;

	/** Rayon d'éclaboussure (0 = aucune) : fixé par le serveur, répliqué à l'apparition (taille de l'impact). */
	UPROPERTY(Replicated)
	float ExplosionRadius = 0.f;

	/** Serveur uniquement. */
	float KnockbackDistance = 0.f;

	bool IsValidTarget(const AGenCharacterBase* Character) const;

	/**
	 * Éclaboussure : ennemis vivants dans le rayon, en ligne de vue depuis Origin (pas à travers les murs),
	 * hors Excluded (cible directe, touchée ou bloquée) et qui ne l'ignorent pas (nature : zone).
	 */
	void AddExplosionTargets(const FVector& Origin, const AGenCharacterBase* Excluded, TArray<AGenCharacterBase*>& InOutTargets) const;

	/** Dégâts + repoussement éventuel sur une cible. */
	void ApplyHit(AGenCharacterBase* Target, const FVector& Origin, bool bDirectHit);

private:
	bool bImpactEffectsPlayed = false;

	/** Équipe du lanceur au tir (serveur), valable si bHasSourceTeam. Répliquées à l'apparition seulement. */
	UPROPERTY(Replicated)
	uint8 SourceTeam = 255;
	UPROPERTY(Replicated)
	bool bHasSourceTeam = false;
};
