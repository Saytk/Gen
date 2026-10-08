#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "CurffeGA_Combustion.generated.h"

class AGenGroundArea;
class UGameplayEffect;

/**
 * Combustion (F, ultime, 100 d'énergie payés à la fin de l'incantation de 0.5 s, interrompue par un contrôle dur :
 * rien n'est payé). Au lancer : Foyer plein, nova de 3 m (zone, 20 dégâts, repousse 3 m), puis embrasé 5 s :
 * Pyroblast au clic gauche (GA_Pyroblast exige State.Curffe.Ablaze, GA_Fireball en est bloqué), flammes illimitées
 * (State.FreeResource : les flammes nourries ne sont pas dépensées, le Foyer reste plein après chaque sort), nourrissage
 * rapide (State.FastFeeding). Pas d'immunité aux contrôles. L'embrasement est un état à durée (UGenGE_TimedState) :
 * retiré à la mort avec les autres (UGenAbilitySystemComponent::RemoveTimedStates).
 * Télégraphes jamais sous 0.5 s : le nourrissage rapide ne raccourcit que le nourrissage, les zones retardées gardent
 * leur minimum (GenAreaRules).
 */
UCLASS()
class GEN_API UCurffeGA_Combustion : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UCurffeGA_Combustion();

	/** Télégraphe centré : la nova, pendant l'incantation seulement. */
	virtual float GetSelfTelegraphRadius(bool bChannel) const override { return bChannel ? 0.f : NovaRadius; }

	float GetAblazeDuration() const { return AblazeDuration; }
	float GetNovaRadius() const { return NovaRadius; }

	//~ UGenGameplayAbility (infobulle) : {Radius}, {Damage}, {Knockback}, {Duration}
	virtual void GetTooltipArgs(FFormatNamedArguments& Args) const override;
	virtual void GetTooltipEffectLines(TArray<FText>& OutLines) const override;
	/** La nova puis l'état embrasé (« Nova 4 m, 30 dégâts ; embrasé 6 s »). */
	virtual FText GetCompactTooltipLine() const override;

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;

	/** Zone de la nova (BP_Area_Nova : effet d'éruption). Par défaut la zone native, sans visuel. */
	UPROPERTY(EditDefaultsOnly, Category = "Combustion|Nova")
	TSubclassOf<AGenGroundArea> NovaAreaClass;

	UPROPERTY(EditDefaultsOnly, Category = "Combustion|Nova", meta = (ClampMin = "1.0", Units = "cm"))
	float NovaRadius = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combustion|Nova")
	float NovaDamage = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combustion|Nova", meta = (ClampMin = "0.0", Units = "cm"))
	float NovaKnockback = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combustion", meta = (ClampMin = "0.0", Units = "s"))
	float AblazeDuration = 5.f;

	/** Remplit le Foyer au lancer (UCurffeGE_HearthFill). */
	UPROPERTY(EditDefaultsOnly, Category = "Combustion")
	TSubclassOf<UGameplayEffect> RefillEffect;
};
