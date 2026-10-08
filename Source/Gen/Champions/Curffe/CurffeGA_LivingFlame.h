#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "CurffeGA_LivingFlame.generated.h"

class AGenGroundArea;
class UAnimMontage;
class UGameplayEffect;

/**
 * Flamme vivante (R, 25 d'énergie) : après 0.1 s d'incantation, Curffe devient flamme vivante, intouchable 0.5 s et
 * sans sort (guidelines §3.5). À la fin : anneau de 2.5 m (zone, 8 dégâts, repousse 3 m), Foyer rempli à 5, puis
 * +30 % de vitesse pendant 2 s, pendant lesquelles il peut lancer ses sorts. Le remplissage est appliqué (prédit) dès le
 * départ de la forme et affiché à sa fin (revue P3 T8-10, I2 : le combo vers la grande boule de feu sans attendre un RTT).
 *
 * - Hâte : multiplicateur de vitesse local (AGenCharacterBase::SetLocalMoveSpeedMultiplier) posé par chaque machine à SA
 *   fin de forme et retiré HasteDuration plus tard : prédite chez le client. Aux bornes, les mouvements en attente partent
 *   avant les RPC et le serveur absorbe un petit écart (grâce bornée, UGenCharacterMovementComponent). Lancer refusé par
 *   le serveur : le client retire sa hâte (OnLaunchCaughtUp).
 * - Forme : un état à durée prédit (State.Untouchable + State.Curffe.LivingFlame, vu par tous) et le verrou de
 *   lancement (SetCastLock : tag local + fenêtre du serveur, UGenAbilitySystemComponent::NoteCastLock).
 * - Visuels : la forme se lit comme une canalisation (barre qui se vide, StartChannel), télégraphe centré de
 *   BurstRadius pendant la forme (GetSelfTelegraphRadius), CastMontage calé sur la forme, FinishMontage à l'anneau.
 * - La touche d'annulation ne coupe pas la forme (sort déjà parti) ; la mort la termine (EndAbility).
 */
UCLASS()
class GEN_API UCurffeGA_LivingFlame : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UCurffeGA_LivingFlame();

	/** Télégraphe centré : l'anneau, pendant la forme seulement (canalisation). */
	virtual float GetSelfTelegraphRadius(bool bChannel) const override { return bChannel ? BurstRadius : 0.f; }

	float GetFormDuration() const { return FormDuration; }
	float GetBurstRadius() const { return BurstRadius; }

	//~ UGenGameplayAbility (infobulle) : {Duration}, {Radius}, {Damage}, {Knockback}, {Haste}, {HasteDuration}
	virtual void GetTooltipArgs(FFormatNamedArguments& Args) const override;
	virtual void GetTooltipEffectLines(TArray<FText>& OutLines) const override;
	/** La forme de feu (« Forme de feu 0,5 s : intouchable »). */
	virtual FText GetCompactTooltipLine() const override;

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Plan Visuals V3 : la pose de la forme (CastMontage) dure la forme. */
	virtual float GetCastMontageTargetDuration() const override { return FormDuration; }

	UFUNCTION()
	void OnFormEnded();

	/**
	 * Client propriétaire : la clé de prédiction du lancer a rattrapé le serveur. Sans le temps de recharge du serveur, il a
	 * refusé le lancer (revue finale, M-5) : la hâte prédite est retirée (ou pas posée, si la forme dure encore).
	 */
	void OnLaunchCaughtUp();

	/** Retire la hâte de cette machine et son minuteur. */
	void ClearHaste();

	/** Durée de la forme de feu (intouchable, sans sort). Guidelines §3.5 : 0.5 s au plus. */
	UPROPERTY(EditDefaultsOnly, Category = "Living Flame", meta = (ClampMin = "0.05", ClampMax = "0.5", Units = "s"))
	float FormDuration = 0.5f;

	/** Zone de l'anneau (BP_Area_LivingFlame : effet d'impact). Par défaut la zone native, sans visuel. */
	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Burst")
	TSubclassOf<AGenGroundArea> BurstAreaClass;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Burst", meta = (ClampMin = "1.0", Units = "cm"))
	float BurstRadius = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Burst")
	float BurstDamage = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Burst", meta = (ClampMin = "0.0", Units = "cm"))
	float BurstKnockback = 300.f;

	/** Remplit le Foyer (UCurffeGE_HearthFill). */
	UPROPERTY(EditDefaultsOnly, Category = "Living Flame")
	TSubclassOf<UGameplayEffect> RefillEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Haste", meta = (ClampMin = "1.0"))
	float HasteMultiplier = 1.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Haste", meta = (ClampMin = "0.0", Units = "s"))
	float HasteDuration = 2.f;

	/** Plan Visuals V3 : geste de l'anneau, joué à la fin de la forme (vitesse 1, continue après le sort). Python : finish_montage. */
	UPROPERTY(EditDefaultsOnly, Category = "Living Flame|Animation")
	TObjectPtr<UAnimMontage> FinishMontage;

private:
	FActiveGameplayEffectHandle FormEffectHandle;
	/** Fin de la hâte (multiplicateur local de cette machine), HasteDuration après SA fin de forme. */
	FTimerHandle HasteTimer;
	/** Client : le serveur a refusé ce lancer (OnLaunchCaughtUp). */
	bool bLaunchRejected = false;
};
