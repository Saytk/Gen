#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "GenProjectile.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class USoundBase;
class USphereComponent;

/**
 * Projectile répliqué. Créé par le serveur (UGenGA_Projectile), il porte le spec du GE de dégâts.
 *
 * - Traverse les alliés et les morts, explose sur un ennemi ou un obstacle.
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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Serveur : applique les dégâts éventuels puis déclenche l'explosion. */
	void Explode(AActor* HitActor, const FVector& Location);

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
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

private:
	bool bImpactEffectsPlayed = false;
};
