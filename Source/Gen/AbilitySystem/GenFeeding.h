#pragma once

#include "CoreMinimal.h"

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
	 * l'estimation dans les deux sens, mais pas annoncer tous les seuils dès l'activation.
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
}
