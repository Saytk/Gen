#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

/**
 * Règles pures du "nourrissage" : un sort maintenu absorbe la ressource du champion
 * (ex : flammes de Curffe), une unité par intervalle. Sans état, testées hors monde.
 */
namespace GenFeeding
{
	/** Nombre d'unités que le sort peut absorber avec la ressource disponible. */
	inline int32 GetFeedLimit(int32 MaxFeed, float AvailableResource)
	{
		return FMath::Clamp(FMath::FloorToInt32(AvailableResource), 0, FMath::Max(MaxFeed, 0));
	}

	/**
	 * Serveur : borne le nombre annoncé par le client à la ressource du serveur et au temps
	 * qu'il a lui-même mesuré entre l'activation et la fin du nourrissage.
	 * Un intervalle de tolérance absorbe la gigue réseau.
	 */
	inline int32 ValidateFedCount(int32 ClientFed, int32 MaxFeed, float AvailableResource, float ElapsedFeedTime, float FeedInterval)
	{
		int32 Result = FMath::Clamp(ClientFed, 0, GetFeedLimit(MaxFeed, AvailableResource));
		if (FeedInterval > 0.f)
		{
			const int32 TimeLimit = FMath::FloorToInt32(FMath::Max(ElapsedFeedTime, 0.f) / FeedInterval) + 1;
			Result = FMath::Min(Result, TimeLimit);
		}
		return Result;
	}

	/**
	 * Serveur : borne le compte annoncé par le client à la fin de son nourrissage (affichage pour les
	 * autres joueurs). Accepté à ±1 de l'estimation du serveur (ses ticks ont une image de décalage),
	 * et jamais au-delà de la ressource disponible ni du maximum du sort : le client peut corriger
	 * l'estimation dans les deux sens, mais pas annoncer 5 flammes dès l'activation.
	 */
	inline int32 ClampReportedFed(int32 Reported, int32 ServerEstimate, int32 MaxFeed, float AvailableResource)
	{
		const int32 Limit = GetFeedLimit(MaxFeed, AvailableResource);
		const int32 Low = FMath::Clamp(ServerEstimate - 1, 0, Limit);
		const int32 High = FMath::Clamp(ServerEstimate + 1, 0, Limit);
		return FMath::Clamp(Reported, Low, High);
	}

	/** Tolérance (s) laissée à la gigue réseau quand le serveur vérifie l'incantation d'un client distant. */
	inline constexpr float CastTimeTolerance = 0.1f;

	/**
	 * Serveur : temps qu'il reste à attendre avant le tir quand la visée d'un client distant arrive
	 * après ElapsedCastTime secondes d'incantation mesurées par le serveur. 0 = le tir part tout de suite.
	 * Le client ne peut pas raccourcir l'incantation de plus de Tolerance.
	 */
	inline float GetServerCastWait(float CastTime, float ElapsedCastTime, float Tolerance)
	{
		return FMath::Max(CastTime - FMath::Max(Tolerance, 0.f) - ElapsedCastTime, 0.f);
	}

	/**
	 * Délai avant la prochaine flamme, calé sur le début du nourrissage : la flamme FedCount + 1 tombe à
	 * FeedStartTime + (FedCount + 1) × FeedInterval. Des minuteurs enchaînés hériteraient chacun du retard
	 * du précédent (une fraction d'image par tick) ; ici le retard ne dépasse jamais une image.
	 * 0 = seuil déjà dépassé (saccade) : tick à l'image suivante.
	 */
	inline float GetNextFeedTickDelay(float FeedStartTime, int32 FedCount, float FeedInterval, float Now)
	{
		return FMath::Max(FeedStartTime + (FMath::Max(FedCount, 0) + 1) * FMath::Max(FeedInterval, 0.f) - Now, 0.f);
	}

	/** Interpolation linéaire : AtZero sans nourrissage, AtMax à MaxFeed unités (bornée). */
	inline float ScaleByFeed(float AtZero, float AtMax, int32 Fed, int32 MaxFeed)
	{
		const float Alpha = MaxFeed > 0 ? FMath::Clamp(static_cast<float>(Fed) / MaxFeed, 0.f, 1.f) : 0.f;
		return FMath::Lerp(AtZero, AtMax, Alpha);
	}

	/** Effet débloqué à partir de Threshold unités (0 = jamais). */
	inline bool ReachesThreshold(int32 Fed, int32 Threshold)
	{
		return Threshold > 0 && Fed >= Threshold;
	}

	/**
	 * Affichage des unités nourries (elles quittent l'orbite) : un seul sort à la fois en est propriétaire.
	 * Un sort qui n'affiche rien (annulé avant de nourrir) ne peut pas effacer l'affichage d'un autre.
	 */
	struct FFedDisplay
	{
		uint8 Count = 0;
		FObjectKey Source;

		/** Renvoie vrai si l'affichage change. */
		bool Set(FObjectKey InSource, uint8 InCount)
		{
			if (InCount == 0 && Source != FObjectKey() && Source != InSource)
			{
				return false;
			}
			const bool bChanged = Count != InCount || (InCount > 0 && Source != InSource);
			Count = InCount;
			Source = InCount > 0 ? InSource : FObjectKey();
			return bChanged;
		}
	};

	/**
	 * Verrou de lancement (State.CastLocked, ex : bond en vol) : jusqu'à quand le serveur le fait respecter
	 * à un client distant. Chaque machine pose le verrou à SON départ et le retire à SON atterrissage ; celui
	 * du serveur commence ~½ RTT après celui du client et finit d'autant plus tard. Le serveur ne refuse donc
	 * que pendant LockDuration - Tolerance : un client honnête n'est jamais refusé, un tricheur gagne au plus Tolerance.
	 */
	inline float GetCastLockEnforcedUntil(float LockStart, float LockDuration, float Tolerance)
	{
		return LockStart + FMath::Max(LockDuration - Tolerance, 0.f);
	}

	/** Activation refusée par le verrou ? Le côté qui prédit (client, hôte, IA) le respecte toujours ; le serveur seulement avant EnforcedUntil. */
	inline bool IsRefusedByCastLock(bool bLocked, bool bPredictingSide, float Now, float EnforcedUntil)
	{
		return bLocked && (bPredictingSide || Now < EnforcedUntil);
	}

	/**
	 * Nourrissage rapide (State.FastFeeding, ex : Combustion) : intervalle divisé par deux.
	 * Curffe : 0.15 s au lieu de 0.3 s (CurffeTuning::FastFeedInterval, vérifié par Gen.Feeding.FastInterval).
	 */
	inline constexpr float FastFeedMultiplier = 0.5f;

	/** Intervalle de nourrissage, retenu au début du nourrissage (State.FastFeeding). Jamais nul. */
	inline float GetFeedInterval(float BaseInterval, bool bFastFeeding)
	{
		return FMath::Max(BaseInterval * (bFastFeeding ? FastFeedMultiplier : 1.f), 0.01f);
	}
}
