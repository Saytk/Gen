#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "AbilitySystem/GenDashRules.h"
#include "GenGA_Dash.generated.h"

class ACharacter;
class UAbilityTask_ApplyRootMotionMoveToForce;

/**
 * Ruée au sol en zigzag, nourrissable (Space de Curffe : Flame Dash ; générique, réutilisable par d'autres champions).
 * - Décollage = nourrissage puis CastTime (machinerie de UGenGA_Cast) ; un contrôle dur l'interrompt.
 * - Au lancer : un premier segment de BaseDistance le long de la visée, puis un segment de SegmentDistance par unité
 *   nourrie, alternativement à gauche puis à droite de la visée (±ZigzagAngle) : GenDashRules::ComputeZigzag.
 * - Chaque segment dure SegmentDuration (vitesse constante) ; le trajet s'arrête au premier mur (capsule balayée au
 *   lancer, les segments suivants sont abandonnés), ou si un segment n'atteint pas son point (obstacle imprévu).
 * - Pendant la ruée : aucun sort (State.CastLocked) ; un contrôle dur ne l'arrête pas. Aucun dégât.
 * - Réseau : trajet décidé au lancer (visée verrouillée). Le client annonce le lacet de SA visée (FillAimData, champs
 *   LeapDistance / LeapYaw de la visée), le serveur le reprend s'il est plausible (GenAreaRules::AcceptClientLeap sur le
 *   premier segment) : les deux machines construisent le même trajet depuis leur position. Mouvement racine prédit
 *   (une UAbilityTask_ApplyRootMotionMoveToForce par segment, enchaînées).
 * - Rotation : jamais forcée par segment (gen.AlwaysFaceAim : le personnage reste face au curseur).
 */
UCLASS()
class GEN_API UGenGA_Dash : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_Dash();

	/** Client : lacet de la visée mesuré depuis sa position (repris par le serveur s'il est plausible). */
	virtual void FillAimData(FGenTargetData_Aim& Data) const override;

	/** En pleine ruée (après le décollage). */
	bool IsDashing() const { return bDashing; }

	/** Paramètres du zigzag (valeurs de ce sort). */
	GenDashRules::FZigzagParams GetZigzagParams() const;

	/** Durée nominale de la ruée (sans mur) avec Fed unités. */
	float GetDashDuration(int32 Fed) const;

	float GetSegmentDuration() const { return SegmentDuration; }

	/**
	 * Trajet depuis Start (centre de capsule) : points d'arrivée des segments, coupé au premier mur (capsule de Character
	 * balayée au-dessus des marches, pions ignorés). Même calcul pour le sort et son indicateur.
	 */
	void ComputePath(const ACharacter* Character, const FVector& Start, float AimYaw, int32 Fed, TArray<FVector>& OutPoints) const;

	/** Indicateur : le trajet en zigzag, un segment de plus par seuil, coupé aux murs. */
	virtual bool GetAimGeometry(const AGenCharacterBase& Caster, int32 Fed, const FVector& Cursor, FGenAimGeometry& Out) const override;

	//~ UGenGameplayAbility (infobulle) : {Range} (premier segment), {Segment}, {Angle}, {DashTime}
	virtual void GetTooltipArgs(FFormatNamedArguments& Args) const override;
	virtual float GetTooltipRange() const override { return BaseDistance; }
	/** Décollage puis trajet avec Fed unités (segments, longueur). */
	virtual FText GetFeedTooltipLines(int32 Fed) const override;
	virtual void GetTooltipEffectLines(TArray<FText>& OutLines) const override;
	/** Sort nourri : décollage par flamme, un segment par flamme. */
	virtual FText GetCompactTooltipLine() const override;

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual bool IsInterruptedByHardCC() const override { return !bDashing; }
	/** Le geste de ruée (CastMontage) dure le trajet nominal. */
	virtual float GetCastMontageTargetDuration() const override { return GetDashDuration(GetReleaseFed()); }

	/**
	 * Lacet de la ruée. Serveur d'un client distant : celui annoncé par le client s'il est plausible
	 * (GenAreaRules::AcceptClientLeap sur le premier segment), sinon le sien ; ailleurs : celui du client lui-même.
	 */
	float ResolveDashYaw(const FGenCastRelease& Release, const FVector& Start) const;

	/** Premier segment, le long de la visée (0 flamme : la ruée entière). */
	UPROPERTY(EditDefaultsOnly, Category = "Dash", meta = (ClampMin = "0.0", Units = "cm"))
	float BaseDistance = 300.f;

	/** Segment ajouté par unité nourrie. */
	UPROPERTY(EditDefaultsOnly, Category = "Dash", meta = (ClampMin = "0.0", Units = "cm"))
	float SegmentDistance = 250.f;

	/** Écart des segments ajoutés par rapport à la visée : le premier à gauche, puis à droite... */
	UPROPERTY(EditDefaultsOnly, Category = "Dash", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float ZigzagAngle = 30.f;

	/** Durée d'un segment nominal (vitesse constante : un segment coupé par un mur dure moins). */
	UPROPERTY(EditDefaultsOnly, Category = "Dash", meta = (ClampMin = "0.02", Units = "s"))
	float SegmentDuration = 0.12f;

	/** Largeur du trajet dessiné par l'indicateur (0 = diamètre de la capsule du lanceur). */
	UPROPERTY(EditDefaultsOnly, Category = "Dash|Indicator", meta = (ClampMin = "0.0", Units = "cm"))
	float IndicatorWidth = 0.f;

	/** Effet en boucle pendant la ruée, vu par tous (la traînée ; retiré à la fin du sort). */
	UPROPERTY(EditDefaultsOnly, Category = "Dash|FX", meta = (Categories = "GameplayCue"))
	FGameplayTag TrailCueTag;

private:
	/** Segment suivant, ou fin de la ruée s'il n'y en a plus. */
	void StartNextSegment();

	UFUNCTION()
	void OnSegmentReached();

	/** Segment fini sans atteindre son point (obstacle imprévu) : les suivants sont abandonnés. */
	UFUNCTION()
	void OnSegmentBlocked();

	/** Filet de sécurité : la ruée finit au plus tard après sa durée prévue + une marge. */
	UFUNCTION()
	void OnSafetyNet();

	/** Fin de la ruée (une seule fois) : force retirée, verrou levé, fin du sort. */
	void EndDash();

	void StopSegmentTask();

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_ApplyRootMotionMoveToForce> SegmentTask;

	/** Points d'arrivée des segments (coupés aux murs) et durée de chacun. */
	TArray<FVector> PathPoints;
	TArray<float> SegmentDurations;
	int32 CurrentSegment = INDEX_NONE;
	bool bDashing = false;
};
