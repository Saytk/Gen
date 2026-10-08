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
 * mort, sur un contrôle dur (surveillance de UGenGA_Cast, posée dès l'incantation : garder CastTime > 0)
 * ou quand le joueur lance un autre sort.
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

	float GetCounterWindow() const { return CounterWindow; }

	//~ UGenGameplayAbility (infobulle) : {Window}, {ResourcePerBlock}, {EnergyOnFirstBlock}, {WindowSpeed}, {Knockback}
	virtual void GetTooltipArgs(FFormatNamedArguments& Args) const override;
	virtual void GetTooltipEffectLines(TArray<FText>& OutLines) const override;

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

	/** Un autre sort du joueur vient d'être activé : la posture se termine. */
	void OnAbilityActivated(UGameplayAbility* ActivatedAbility);

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
	FActiveGameplayEffectHandle WindowEffectHandle;
	FDelegateHandle AbilityActivatedHandle;
	int32 BlockCount = 0;
};
