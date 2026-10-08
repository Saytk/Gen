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
	 * Serveur pour un client distant : son estimation du nourrissage (cosmétique, pour les autres joueurs) part
	 * avec ce retard (s) sur le début mesuré. Le client, qui a commencé une latence plus tôt, est en avance :
	 * son annonce corrige l'estimation vers le haut, ce qui ne ressemble jamais à un recul.
	 * Ne décale pas FeedStartTime, qui sert à valider le tir.
	 */
	inline constexpr float ServerEstimateLag = 0.1f;

	/**
	 * Serveur pour un client distant : nombre de flammes affiché aux autres joueurs pendant le nourrissage.
	 * - Sans annonce du client (Reported = INDEX_NONE) : l'estimation du serveur, bornée à la ressource et au maximum.
	 * - Annonce reçue (le client a fini de nourrir) : elle remplace l'estimation, bornée par la même règle que le tir
	 *   (ValidateFedCount : ressource, maximum, temps mesuré par le serveur + 1). Un bluff "tous les seuils à
	 *   l'activation" reste bloqué ; le temps qui avance lève la borne au fil des appels.
	 * - Jamais moins que Displayed : le compteur et l'orbite ne reculent pas pendant un même sort. Seul le compte
	 *   validé au lancer (visée) peut les faire descendre.
	 */
	inline int32 ReconcileDisplayedFed(int32 Displayed, int32 ServerEstimate, int32 Reported, int32 MaxFeed, float AvailableResource, float ElapsedFeedTime, float FeedInterval)
	{
		const int32 Candidate = Reported != INDEX_NONE
			? ValidateFedCount(Reported, MaxFeed, AvailableResource, ElapsedFeedTime, FeedInterval)
			: FMath::Clamp(ServerEstimate, 0, GetFeedLimit(MaxFeed, AvailableResource));
		return FMath::Max(FMath::Max(Displayed, 0), Candidate);
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
	 * Marge (s) au-delà de la tolérance avant de signaler une visée en avance dans le journal (Warning) :
	 * une activation renvoyée après une perte de paquet reste en deçà, une visée collée à l'activation non.
	 */
	inline constexpr float EarlyAimWarningMargin = 0.25f;

	/** Visée arrivée bien avant la fin de l'incantation mesurée par le serveur (triche ou lien très dégradé). */
	inline bool IsAimSuspiciouslyEarly(float CastTime, float ElapsedCastTime, float Tolerance)
	{
		return ElapsedCastTime < CastTime - FMath::Max(Tolerance, 0.f) - EarlyAimWarningMargin;
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
