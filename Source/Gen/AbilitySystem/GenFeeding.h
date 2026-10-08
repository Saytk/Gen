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

	/** Rayon d'explosion d'un tir : FedRadius dès ExplosionMinFeed unités, sinon BaseRadius (0 = pas d'explosion, ex : Pyroblast 120 cm). */
	inline float GetShotExplosionRadius(int32 Fed, int32 ExplosionMinFeed, float FedRadius, float BaseRadius)
	{
		return ReachesThreshold(Fed, ExplosionMinFeed) ? FedRadius : FMath::Max(BaseRadius, 0.f);
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
	 * que pendant MinLockDuration - Tolerance : un client honnête n'est jamais refusé, un tricheur gagne au plus Tolerance.
	 * MinLockDuration = la durée la PLUS COURTE possible du verrou : un bond qui se pose tôt (marche, rebord)
	 * libère le client avant sa durée nominale (Gen.Feeding.CastLockRule, cas de l'atterrissage précoce).
	 * Temps en double : GetTimeSeconds() d'un serveur allumé depuis des jours mangerait la tolérance en float.
	 */
	inline double GetCastLockEnforcedUntil(double LockStart, float MinLockDuration, float Tolerance)
	{
		return LockStart + FMath::Max(MinLockDuration - Tolerance, 0.f);
	}

	/** Activation refusée par le verrou ? Le côté qui prédit (client, hôte, IA) le respecte toujours ; le serveur seulement avant EnforcedUntil. */
	inline bool IsRefusedByCastLock(bool bLocked, bool bPredictingSide, double Now, double EnforcedUntil)
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

	/**
	 * Revue V2-V4, I1 : fenêtre de grâce (s) du serveur pour un client distant autour d'un changement de
	 * State.FastFeeding / State.FreeResource. Le tag (posé par un GE du serveur, ex : fin de l'embrasement de Combustion)
	 * change chez le client ~½ RTT après le serveur : un appui dans cet intervalle est pris avec l'ancien état côté
	 * client. Le serveur lui accorde l'état "favorable" (rapide, gratuit) si le changement est à moins de Grace de
	 * l'instant de référence. Un tricheur y gagne au plus Grace de nourrissage rapide ou un lancer gratuit en fin d'état.
	 */
	inline constexpr float ServerTagGrace = 0.25f;

	/** Le changement de tag (ChangeTime) tombe-t-il dans la fenêtre de grâce autour de ReferenceTime ? Temps en double. */
	inline bool IsTagChangeInGrace(double ChangeTime, double ReferenceTime, float Grace = ServerTagGrace)
	{
		return FMath::Abs(ChangeTime - ReferenceTime) <= Grace;
	}
}
