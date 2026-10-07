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
