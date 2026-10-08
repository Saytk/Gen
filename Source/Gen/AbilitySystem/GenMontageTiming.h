#pragma once

#include "CoreMinimal.h"

/**
 * Règles pures du calage des montages sur les durées de jeu (Art Bible §8.3 : l'anticipation dure exactement le temps
 * d'incantation, jamais plus). Le minuteur du sort reste la seule source du moment du lancer (Art Bible §8.4 #4).
 */
namespace GenMontageTiming
{
	inline constexpr float MinPlayRate = 0.5f;
	inline constexpr float MaxPlayRate = 2.5f;

	/** Au-delà de ces écarts (relatifs à la vitesse attendue), le clip devrait être recalé à la main (Art Bible §8.2, étape 4). */
	inline constexpr float WarnLowRatio = 0.8f;
	inline constexpr float WarnHighRatio = 1.25f;

	/** Vitesse pour qu'un clip de AuthoredLength s dure TargetDuration s. 1 si l'une des deux est nulle. */
	inline float GetPlayRate(float AuthoredLength, float TargetDuration)
	{
		if (AuthoredLength <= KINDA_SMALL_NUMBER || TargetDuration <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}
		return FMath::Clamp(AuthoredLength / TargetDuration, MinPlayRate, MaxPlayRate);
	}

	/**
	 * Vitesse attendue du montage de nourrissage (sections d'UN intervalle de base chacune) : 1 au rythme normal,
	 * 2 en nourrissage rapide (intervalle actif divisé par deux). Sert à ne pas avertir pour un x2 voulu.
	 */
	inline float GetExpectedFeedRate(float BaseInterval, float ActiveInterval)
	{
		return BaseInterval > KINDA_SMALL_NUMBER && ActiveInterval > KINDA_SMALL_NUMBER ? BaseInterval / ActiveInterval : 1.f;
	}

	/**
	 * Le montage de charge est-il calé sur CastTime ? Seulement si le sort le demande ET a un montage de lancer à part :
	 * sans CastMontage, le montage de charge est l'ancien montage unique (préparation + lancer, Plan 1), joué à vitesse 1,
	 * sinon son geste de lancer serait comprimé dans l'incantation.
	 */
	inline bool ShouldScaleChargeToCastTime(bool bScaleChargeToCastTime, bool bHasCastMontage, float CastTime)
	{
		return bScaleChargeToCastTime && bHasCastMontage && CastTime > KINDA_SMALL_NUMBER;
	}

	/** Sections de FedChargeMontage : Fed_1..Fed_3 (une par nombre d'unités nourries, non enchaînées). */
	inline constexpr int32 MaxFedChargeSection = 3;

	/**
	 * Section de la charge nourrie (UGenGA_Cast::FedChargeMontage) pour Fed unités nourries : Fed_<clamp(Fed, 1, 3)>,
	 * NAME_None sans nourrissage (ChargeMontage normal).
	 */
	inline FName GetFedChargeSection(int32 Fed)
	{
		return Fed > 0 ? FName(*FString::Printf(TEXT("Fed_%d"), FMath::Clamp(Fed, 1, MaxFedChargeSection))) : FName(NAME_None);
	}

	/** Vitesse de la section de charge nourrie : SA longueur (jamais celle du montage entier) / CastTime. */
	inline float GetFedChargeRate(float SectionLength, float CastTime)
	{
		return GetPlayRate(SectionLength, CastTime);
	}

	/** Vrai si PlayRate s'écarte trop de la vitesse attendue (1, ou 2 pour le nourrissage rapide voulu). */
	inline bool ShouldWarn(float PlayRate, float ExpectedRate = 1.f)
	{
		const float Ratio = ExpectedRate > KINDA_SMALL_NUMBER ? PlayRate / ExpectedRate : PlayRate;
		return Ratio < WarnLowRatio || Ratio > WarnHighRatio;
	}

	/**
	 * Section de nourrissage à tenir (Feed_N, 1-based) : celle du dernier seuil atteignable, FeedCap = min(MaxFeed,
	 * unités disponibles à l'appui). Le geste ne dépasse jamais ce seuil (sinon le lanceur montre Feed_3 avec 2
	 * flammes, le temps que la charge démarre). 0 si le montage n'a pas de section Feed_N ou rien à nourrir.
	 */
	inline int32 GetFeedHoldSection(int32 FeedCap, int32 FeedSectionCount)
	{
		return FeedCap > 0 && FeedSectionCount > 0 ? FMath::Min(FeedCap, FeedSectionCount) : 0;
	}

	/** Faut-il boucler la section tenue sur elle-même ? Seulement si elle n'est pas déjà la dernière (qui boucle). */
	inline bool ShouldHoldFeedSection(int32 FeedCap, int32 FeedSectionCount)
	{
		const int32 Hold = GetFeedHoldSection(FeedCap, FeedSectionCount);
		return Hold > 0 && Hold < FeedSectionCount;
	}

	/** Durée pendant laquelle le geste de lancer du sort précédent est protégé (clic gauche maintenu). */
	inline constexpr float DefaultCastReleaseHold = 0.15f;

	/** Part de l'incantation que le retard de la charge peut prendre au plus (le reste garde un geste lisible). */
	inline constexpr float MaxChargeDelayShare = 0.5f;

	/**
	 * Retard du montage de charge d'une nouvelle incantation quand le geste de lancer précédent joue encore
	 * (SinceCastMontage : temps écoulé depuis son départ, négatif = aucun geste en cours). Cosmétique seulement : le
	 * minuteur du sort (CastTime) ne change pas, la charge est jouée plus vite pour finir au même moment
	 * (GetDelayedChargeRate). Au plus MaxChargeDelayShare de l'incantation.
	 */
	inline float GetChargeStartDelay(float SinceCastMontage, float CastTime, float ReleaseHold = DefaultCastReleaseHold)
	{
		if (SinceCastMontage < 0.f || CastTime <= KINDA_SMALL_NUMBER || ReleaseHold <= 0.f)
		{
			return 0.f;
		}
		return FMath::Clamp(ReleaseHold - SinceCastMontage, 0.f, CastTime * MaxChargeDelayShare);
	}

	/** Vitesse de la charge retardée de Delay : elle finit toujours à CastTime (le lancer tombe au même moment). */
	inline float GetDelayedChargeRate(float AuthoredLength, float CastTime, float Delay)
	{
		return GetPlayRate(AuthoredLength, FMath::Max(CastTime - Delay, KINDA_SMALL_NUMBER));
	}
}
