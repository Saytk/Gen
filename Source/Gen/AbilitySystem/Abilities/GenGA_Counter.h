#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "GenGA_Counter.generated.h"

/**
 * Posture de contre (guidelines §3.4) : après une courte incantation, le lanceur bloque les projectiles
 * et la mêlée pendant CounterWindow (les zones au sol passent), ralenti pendant la fenêtre.
 * Chaque coup bloqué (Event.Counter.Blocked, serveur) rapporte ResourcePerBlock, l'énergie une fois par
 * incantation, et repousse un attaquant au corps à corps. La fenêtre se termine à la fin du délai, à la
 * mort, sur un contrôle dur (surveillance de UGenGA_Cast, armée dès l'activation, quelle que soit CastTime)
 * ou quand le joueur active un autre sort avec sa touche (jamais un sort passif ou déclenché par un événement).
 * Posture (State.Countering) et ralenti : posés par chaque machine à SON départ et retirés à SA fin (revue Plan 2
 * Tasks 7-8, I-4) ; les autres joueurs reçoivent la posture du serveur (tag répliqué aux proxys simulés).
 * Pas de CancelAbilitiesWithTag dans l'asset : la fenêtre ne se termine que par les règles ci-dessus.
 */
UCLASS()
class GEN_API UGenGA_Counter : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_Counter();

	/** Coups bloqués pendant la fenêtre en cours (serveur ; tests). */
	int32 GetBlockCount() const { return BlockCount; }

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	/** Plan Visuals V3 : la posture (CastMontage) dure la fenêtre. */
	virtual float GetCastMontageTargetDuration() const override { return CounterWindow; }

	/** Serveur : un coup vient d'être bloqué. */
	UFUNCTION()
	void OnBlocked(FGameplayEventData Payload);

	UFUNCTION()
	void OnWindowFinished();

	/** Un autre sort vient d'être activé : la posture se termine si c'est un appui du joueur (EndsStanceOnActivation). */
	void OnAbilityActivated(UGameplayAbility* ActivatedAbility);

	/**
	 * Revue Plan 2 Tasks 7-8, I-3 : seule une activation par touche du joueur termine la posture (sort à InputTag,
	 * prédit ou local, non déclenché par un événement). Un passif, un sort déclenché (réaction à un coup, au kill) ou
	 * serveur seulement la laisse : sinon le serveur la terminerait seul, en plein coup bloqué.
	 */
	static bool EndsStanceOnActivation(const UGameplayAbility* ActivatedAbility);

	/** Pose (vrai) ou retire la posture et son ralenti sur CETTE machine. */
	void SetWindowState(bool bActive);

	/** Durée de la posture (après l'incantation). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter", meta = (ClampMin = "0.1", Units = "s"))
	float CounterWindow = 1.2f;

	/** Vitesse pendant la posture (guidelines §3.4 : un contre gâché coûte quelque chose). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WindowMoveSpeedMultiplier = 0.5f;

	/** Ressource gagnée à chaque coup bloqué (Curffe : flammes). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter|Rewards")
	float ResourcePerBlock = 0.f;

	/** Énergie gagnée au premier coup bloqué de l'incantation (guidelines §4.1 : +10). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter|Rewards")
	float EnergyOnFirstBlock = 10.f;

	/** Repoussement d'un attaquant au corps à corps bloqué (cm). 0 = aucun. */
	UPROPERTY(EditDefaultsOnly, Category = "Counter|Rewards", meta = (ClampMin = "0.0", Units = "cm"))
	float MeleeKnockbackDistance = 0.f;

	/** Effet joué à chaque blocage (GameplayCue exécutée sur le lanceur, vue par tous). */
	UPROPERTY(EditDefaultsOnly, Category = "Counter", meta = (Categories = "GameplayCue"))
	FGameplayTag BlockCueTag;

private:
	bool bWindowStateApplied = false;
	FDelegateHandle AbilityActivatedHandle;
	int32 BlockCount = 0;
};
