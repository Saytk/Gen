#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Leap.h"
#include "CurffeGA_MeteorLeap.generated.h"

class AGenProjectile;

/**
 * Bond météore (Espace) de Curffe : chaque flamme nourrie jaillit en boule de feu à l'atterrissage,
 * en anneau régulier autour du point d'impact (au plus MaxFeed boules : 3 flammes = triangle). Une salve : un ennemi
 * n'est touché que par une boule de feu de l'anneau (même bloquée par un contre, la boule le prend). Les boules
 * suivent les règles de la boule de feu (portée, murs, contres).
 */
UCLASS()
class GEN_API UCurffeGA_MeteorLeap : public UGenGA_Leap
{
	GENERATED_BODY()

protected:
	virtual void OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation) override;

	/** Projectile de l'anneau (BP_Projectile_Fireball : portée, vitesse, effets). */
	UPROPERTY(EditDefaultsOnly, Category = "Meteor Leap")
	TSubclassOf<AGenProjectile> RingProjectileClass;

	UPROPERTY(EditDefaultsOnly, Category = "Meteor Leap")
	float RingDamage = 8.f;

	/** Énergie par boule de l'anneau qui touche (règles de la boule de feu ; pas de flamme : seul le clic gauche en rend). */
	UPROPERTY(EditDefaultsOnly, Category = "Meteor Leap")
	float RingEnergyOnHit = 2.f;

	/** Distance du point d'impact où apparaissent les boules. */
	UPROPERTY(EditDefaultsOnly, Category = "Meteor Leap", meta = (ClampMin = "0.0", Units = "cm"))
	float RingSpawnOffset = 70.f;
};
