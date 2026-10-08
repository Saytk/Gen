#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "ScalableFloat.h"
#include "GenGA_GroundArea.generated.h"

class AGenGroundArea;

/**
 * Zone au sol visée (ex : Pilier de flammes). Nourrissage optionnel (rayon), incantation, puis
 * une zone retardée (télégraphe) au point visé, ramené à Range. Le lanceur voit un aperçu local
 * qui suit le curseur et grandit avec le nourrissage.
 */
UCLASS()
class GEN_API UGenGA_GroundArea : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenGA_GroundArea();

	/** Plan Visuals V6 : arc de portée seulement ; le cercle reste l'aperçu de la zone (Plan 2, AGenGroundArea). */
	virtual bool GetAimGeometry(const AGenCharacterBase& Caster, int32 Fed, const FVector& Cursor, FGenAimGeometry& Out) const override;

	/** Rayon de la zone avec Fed unités nourries : la zone posée (OnCastLaunched) et l'infobulle lisent cette valeur. */
	float GetAreaRadius(int32 Fed) const;

	/** Dégâts de la zone avec Fed unités nourries (zone et infobulle). */
	float GetAreaDamage(int32 Fed, int32 Level = 1) const;

	/**
	 * Délai réel entre le départ et l'impact (0 = immédiat) : ImpactDelay, jamais sous MinTelegraph + la marge de latence
	 * (GenAreaRules::TelegraphLatencyMargin). Revue finale, M-1 : seule source du jeu et de l'infobulle.
	 */
	float GetEffectiveImpactDelay() const;

	//~ UGenGameplayAbility (infobulle) : {Damage}, {Radius} (sans flamme), {RadiusMax}, {Range}, {Stun}, {Delay}, {Knockback}
	virtual void GetTooltipArgs(FFormatNamedArguments& Args) const override;
	virtual float GetTooltipRange() const override { return Range; }
	virtual FText GetFeedTooltipLines(int32 Fed) const override;
	virtual void GetTooltipEffectLines(TArray<FText>& OutLines) const override;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;

	/** Blueprint de la zone (télégraphe, effet d'impact). */
	UPROPERTY(EditDefaultsOnly, Category = "Area")
	TSubclassOf<AGenGroundArea> AreaClass;

	/** Distance max du centre de la zone au lanceur. */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (ClampMin = "0.0", Units = "cm"))
	float Range = 900.f;

	/** Rayon sans nourrissage. */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (ClampMin = "1.0", Units = "cm"))
	float Radius = 200.f;

	/**
	 * Rayon à MaxFeed (linéaire entre les deux). Pilier : 2 m + 0.5 m par flamme, soit 200 + 50 × CurffeTuning::MaxFeedPerSpell
	 * (350 cm). Exception assumée au plafond de surface des contrôles (Curffe.md).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (EditCondition = "bFeedable", ClampMin = "1.0", Units = "cm"))
	float RadiusAtMaxFeed;

	/** Délai entre le départ du sort et l'impact (télégraphe). 0 = impact immédiat. */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (ClampMin = "0.0", Units = "s"))
	float ImpactDelay = 0.8f;

	/**
	 * Un télégraphe ne descend jamais sous cette durée chez les autres joueurs (guidelines §3.1 : zones retardées
	 * 0.6–1.0 s). Le délai réel ajoute GenAreaRules::TelegraphLatencyMargin (latence d'apparition chez eux).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Area", meta = (ClampMin = "0.0", Units = "s"))
	float MinTelegraph = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Effects")
	FScalableFloat Damage;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Effects", meta = (EditCondition = "bFeedable"))
	float DamagePerFeed = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Effects", meta = (ClampMin = "0.0", Units = "s"))
	float StunDuration = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Effects", meta = (ClampMin = "0.0", Units = "cm"))
	float KnockbackDistance = 0.f;

	/** Énergie gagnée par le lanceur si la zone touche au moins un ennemi. */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Gains")
	float EnergyOnHit = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Gains", meta = (EditCondition = "bFeedable"))
	float EnergyPerFeed = 0.f;

	/** Aperçu de la zone sous le curseur pendant l'incantation (lanceur uniquement). */
	UPROPERTY(EditDefaultsOnly, Category = "Area")
	bool bShowAimPreview = true;

private:
	void DestroyPreview();

	UPROPERTY(Transient)
	TObjectPtr<AGenGroundArea> Preview;
};
