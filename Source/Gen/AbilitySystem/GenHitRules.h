#pragma once

#include "CoreMinimal.h"

/** Nature d'un coup : décide s'il déclenche les contres (guidelines §3.4). */
enum class EGenHitKind : uint8
{
	Projectile,
	Melee,
	/** Zone au sol (éclaboussure, pilier, nova...) : traverse les contres. */
	Area
};

/** Ce que la cible fait d'un coup ennemi. */
enum class EGenHitResponse : uint8
{
	/** Le coup s'applique (dégâts, contrôle, repoussement). */
	Hit,
	/** Bloqué par un contre : le coup n'inflige rien, le contre est prévenu. */
	Countered
};

/** Règles pures des coups et des contres. Sans état, testées hors monde. */
namespace GenHitRules
{
	/** Une seule règle pour tous les contres : projectiles et mêlée oui, zones au sol non. */
	inline bool TriggersCounter(EGenHitKind Kind)
	{
		return Kind == EGenHitKind::Projectile || Kind == EGenHitKind::Melee;
	}

	inline EGenHitResponse Resolve(bool bCountering, EGenHitKind Kind)
	{
		return bCountering && TriggersCounter(Kind) ? EGenHitResponse::Countered : EGenHitResponse::Hit;
	}

	/** Récompense d'un coup bloqué. */
	struct FCounterReward
	{
		float Resource = 0.f;
		float Energy = 0.f;
	};

	/**
	 * BlockIndex : 1 pour le premier coup bloqué de l'incantation, 2 pour le suivant...
	 * La ressource est gagnée à chaque blocage, l'énergie au premier seulement (spec Backfire).
	 */
	inline FCounterReward GetCounterReward(int32 BlockIndex, float ResourcePerBlock, float EnergyOnFirstBlock)
	{
		FCounterReward Reward;
		if (BlockIndex >= 1)
		{
			Reward.Resource = FMath::Max(ResourcePerBlock, 0.f);
			Reward.Energy = BlockIndex == 1 ? FMath::Max(EnergyOnFirstBlock, 0.f) : 0.f;
		}
		return Reward;
	}
}
