#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "GenGA_Leap.generated.h"

class AGenGroundArea;
class UAnimMontage;

/**
 * Bond visible (guidelines §3.6) vers le curseur, ramené à MaxDistance. Nourrissable : le décollage
 * (CastTime + nourrissage) s'allonge, la sous-classe utilise les unités nourries à l'atterrissage.
 * Pendant le vol : aucun sort (State.CastLocked), un contrôle dur ne coupe pas le bond (il interrompt le décollage).
 * Point d'atterrissage répliqué à tous pendant le vol (AGenCharacterBase::GetLeapTarget : cercle vu aussi par les ennemis).
 * Atterrissage : zone de dégâts (A) autour du point d'impact, puis fin du sort.
 * Pas de CancelAbilitiesWithTag dans l'asset : un sort lancé ne doit pas couper le vol (le verrou les refuse de toute façon).
 */
UCLASS()
class GEN_API UGenGA_Leap : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_Leap();

	/** En vol (après le décollage, avant l'atterrissage). */
	bool IsAirborne() const { return bAirborne; }

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual bool IsInterruptedByHardCC() const override { return !bAirborne; }

	/** Atterrissage (serveur et client). Par défaut : montage, effet, zone d'atterrissage (serveur). */
	virtual void OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation);

	/** Fin du bond : posé au sol (au plus tôt à LeapDuration × 0.5), ou filet de sécurité MaxFlightDuration. */
	UFUNCTION()
	void OnLanded();

	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxDistance = 700.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.0", Units = "cm"))
	float LeapHeight = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.1", Units = "s"))
	float LeapDuration = 0.45f;

	/**
	 * Filet de sécurité : le bond se termine au plus tard après cette durée de vol, même sans atterrissage détecté.
	 * La tâche de saut (bFinishOnLanded) n'a pas de délai propre : sans ce filet, un bond qui ne retrouve jamais le sol
	 * (vide, géométrie coincée) garderait State.CastLocked. Toujours supérieur à LeapDuration.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.2", Units = "s"))
	float MaxFlightDuration = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing")
	TSubclassOf<AGenGroundArea> LandingAreaClass;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing", meta = (ClampMin = "1.0", Units = "cm"))
	float LandingRadius = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing")
	float LandingDamage = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing")
	float LandingEnergyOnHit = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|Landing", meta = (ClampMin = "0.0", Units = "cm"))
	float LandingKnockback = 0.f;

	/** Effet en boucle pendant le vol (retiré à la fin du sort). */
	UPROPERTY(EditDefaultsOnly, Category = "Leap|FX", meta = (Categories = "GameplayCue"))
	FGameplayTag TrailCueTag;

	/** Effet à l'atterrissage. */
	UPROPERTY(EditDefaultsOnly, Category = "Leap|FX", meta = (Categories = "GameplayCue"))
	FGameplayTag ImpactCueTag;

	UPROPERTY(EditDefaultsOnly, Category = "Leap|FX")
	TObjectPtr<UAnimMontage> LandMontage;

private:
	/** Efface le point d'atterrissage répliqué (AGenCharacterBase::LeapTarget). */
	void ClearLeapTarget();

	FGenCastRelease LeapRelease;
	bool bAirborne = false;
};
