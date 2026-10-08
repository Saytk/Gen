#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "GenGA_Leap.generated.h"

class AGenGroundArea;
class UAbilityTask_ApplyRootMotionJumpForce;
class UAnimMontage;
struct FGenLeapTarget;
struct FHitResult;

/**
 * Bond visible (guidelines §3.6) vers le curseur, ramené à MaxDistance. Nourrissable : le décollage
 * (CastTime + nourrissage) s'allonge, la sous-classe utilise les unités nourries à l'atterrissage.
 * Pendant le vol : aucun sort (State.CastLocked), un contrôle dur ne coupe pas le bond (il interrompt le décollage).
 * Point d'atterrissage répliqué à tous pendant le vol (AGenCharacterBase::GetLeapTarget : cercle vu aussi par les ennemis).
 * Atterrissage : zone de dégâts (A) autour du point d'impact, puis fin du sort.
 * Réseau (revue Plan 2 Tasks 7-8, I-1) : le client annonce la distance et le lacet de SON bond avec la visée, le serveur
 * les reprend s'ils sont plausibles : les deux forces de saut sont identiques. Toujours posé à au plus un rayon de
 * capsule du cercle montré aux autres (I-2) : à LeapDuration, la force de saut s'arrête et la gravité finit la chute.
 * Pas de CancelAbilitiesWithTag dans l'asset : un sort lancé ne doit pas couper le vol (le verrou les refuse de toute façon).
 */
UCLASS()
class GEN_API UGenGA_Leap : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_Leap();

	/** Client : distance et lacet de ce bond, mesurés depuis la position du client (repris par le serveur). */
	virtual void FillAimData(FGenTargetData_Aim& Data) const override;

	/** En vol (après le décollage, avant l'atterrissage). */
	bool IsAirborne() const { return bAirborne; }

	/** Durée nominale du vol (remplissage du cercle d'atterrissage). */
	float GetLeapDuration() const { return LeapDuration; }

	/** Plan Visuals V6 : arc de portée, cercle d'atterrissage et amorces de l'anneau (GenIndicatorRules::ComputeLeapAim). */
	virtual bool GetAimGeometry(const AGenCharacterBase& Caster, int32 Fed, const FVector& Cursor, FGenAimGeometry& Out) const override;

	/**
	 * Bond en vol, vu par tous (décision du 2026-10-08) : cercle d'atterrissage au point répliqué et une amorce par boule
	 * de l'anneau à venir (GenIndicatorRules::ComputeLeapFlight).
	 */
	void GetFlightGeometry(const FGenLeapTarget& Target, FGenAimGeometry& Out) const;

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual bool IsInterruptedByHardCC() const override { return !bAirborne; }
	/** Plan Visuals V3 : le clip de vol (CastMontage) finit avec la force de saut. */
	virtual float GetCastMontageTargetDuration() const override { return LeapDuration; }

	/** Rayon de collision du projectile de l'anneau (largeur des amorces), 0 = pas d'anneau. */
	virtual float GetRingProjectileRadius() const { return 0.f; }

	/** Atterrissage (serveur et client). Par défaut : montage, effet, zone d'atterrissage (serveur). */
	virtual void OnLeapLanded(const FGenCastRelease& Release, const FVector& LandingLocation);

	/** Force de saut : posé au sol avant LeapDuration (au plus tôt à LeapDuration × 0.5). */
	UFUNCTION()
	void OnJumpLanded();

	/** Force de saut finie à LeapDuration (temps de simulation du mouvement) : encore en l'air => la gravité finit la chute. */
	UFUNCTION()
	void OnJumpForceEnded();

	/** Chute après LeapDuration : posé au sol (ACharacter::LandedDelegate). */
	UFUNCTION()
	void OnCharacterLanded(const FHitResult& Hit);

	/** Filet de sécurité MaxFlightDuration : posé sur le sol trouvé sous le personnage, sans zone ni anneau s'il n'y en a pas. */
	UFUNCTION()
	void OnSafetyNet();

	/**
	 * Distance et lacet du bond. Serveur d'un client distant : ceux annoncés par le client s'ils sont plausibles
	 * (GenAreaRules::AcceptClientLeap), sinon les siens ; ailleurs : ceux du client lui-même (sa visée).
	 */
	void ResolveLeap(const FGenCastRelease& Release, const FVector& Start, float& OutDistance, float& OutYaw) const;

	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxDistance = 700.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.0", Units = "cm"))
	float LeapHeight = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Leap", meta = (ClampMin = "0.1", Units = "s"))
	float LeapDuration = 0.45f;

	/**
	 * Filet de sécurité : le bond se termine au plus tard après cette durée de vol, même sans atterrissage détecté (chute
	 * sans fin après LeapDuration : vide, géométrie coincée), sinon State.CastLocked resterait. Toujours supérieur à
	 * LeapDuration. L'atteindre veut dire que la géométrie du niveau est à revoir (avertissement dans le journal).
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

	/** Atterrissage (une seule fois) : verrou retiré, zone et effets (OnLeapLanded), fin du sort. */
	void Land(bool bSafetyNet);

	/** Retire la force de saut (vitesse finale nulle) et n'écoute plus l'atterrissage du personnage. */
	void StopJumpForce();

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_ApplyRootMotionJumpForce> JumpTask;

	FGenCastRelease LeapRelease;
	bool bAirborne = false;
	/** Après LeapDuration, en chute : LandedDelegate du personnage écouté. */
	bool bWaitingForFloor = false;
};
