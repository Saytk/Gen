#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "GenNetTestAbilities.generated.h"

/**
 * Sorts de test (règles génériques de UGenGA_Cast) des Gen.Net.* (module éditeur GenTests, jamais dans le jeu) : classes C++ réglées
 * dans leur constructeur, sans asset ni tag d'annulation (CancelAbilitiesWithTag vide). Accordés par le serveur
 * pendant le test (UGenAbilitySystemComponent::GrantAbilities). Les tags (touche, recharge) sont posés sur le CDO
 * par le test (BEFORE_EACH) : pas de RequestGameplayTag pendant le chargement du module.
 * Cachés des listes de classes de l'éditeur (HideDropdown).
 */

/** Projectile à incantation longue (1 s), sans annulation par tag. */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_SlowCast : public UGenGA_Projectile
{
	GENERATED_BODY()

public:
	UGenNetTestGA_SlowCast()
	{
		CastTime = 1.f;
		Damage = FScalableFloat(1.f);
	}
};

/** Projectile à incantation courte (0.2 s), sans annulation par tag. */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_QuickCast : public UGenGA_Projectile
{
	GENERATED_BODY()

public:
	UGenNetTestGA_QuickCast()
	{
		CastTime = 0.2f;
		Damage = FScalableFloat(1.f);
	}
};

/** Sort qui reste actif LingerTime s après son départ (comme une fenêtre de contre), sans annulation par tag. */
UCLASS(NotBlueprintable, HideDropdown)
class UGenNetTestGA_Lingering : public UGenGA_Cast
{
	GENERATED_BODY()

public:
	UGenNetTestGA_Lingering()
	{
		CastTime = 0.1f;
		bTurnToAim = false;
	}

	static constexpr float LingerTime = 1.5f;

protected:
	virtual void OnCastLaunched(const FGenCastRelease& Release) override;

	UFUNCTION()
	void OnLingerFinished() { FinishAbility(); }
};
