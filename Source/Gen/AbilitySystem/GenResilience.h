#pragma once

#include "CoreMinimal.h"

/**
 * Résilience (guidelines §3.3) : après 2.5 s de contrôle dur sur 5 s, immunité aux contrôles durs
 * pendant 1.5 s. Les longs contrôles restent possibles ; les enchaînements sans fin non.
 * Choix : l'immunité commence dès que le contrôle qui atteint le seuil est appliqué (il s'applique en entier)
 * et dure jusqu'à 1.5 s après sa fin. Deux contrôles simultanés ne comptent qu'une fois (union).
 */
namespace GenResilience
{
	inline constexpr float Window = 5.f;
	inline constexpr float Threshold = 2.5f;
	inline constexpr float ImmunityDuration = 1.5f;

	struct FHardCCHistory
	{
		/** Enregistre un contrôle dur appliqué à Now pendant Duration. Renvoie la durée d'immunité à accorder maintenant (0 = aucune). */
		float Record(float Now, float Duration)
		{
			const float WindowStart = Now - Window;
			Entries.RemoveAll([WindowStart](const FEntry& Entry) { return Entry.Start + Entry.Duration <= WindowStart; });
			Entries.Add({ Now, FMath::Max(Duration, 0.f) });

			TArray<FEntry> Sorted = Entries;
			Sorted.Sort([](const FEntry& A, const FEntry& B) { return A.Start < B.Start; });

			// Union des intervalles, chacun rogné au début de la fenêtre
			float Total = 0.f;
			float SpanStart = 0.f;
			float SpanEnd = -1.f;
			float LatestEnd = Now;
			for (const FEntry& Entry : Sorted)
			{
				const float Start = FMath::Max(Entry.Start, WindowStart);
				const float End = Entry.Start + Entry.Duration;
				LatestEnd = FMath::Max(LatestEnd, End);
				if (End <= Start)
				{
					continue;
				}
				if (Start > SpanEnd)
				{
					Total += FMath::Max(SpanEnd - SpanStart, 0.f);
					SpanStart = Start;
					SpanEnd = End;
				}
				else
				{
					SpanEnd = FMath::Max(SpanEnd, End);
				}
			}
			Total += FMath::Max(SpanEnd - SpanStart, 0.f);

			if (Total + 0.001f >= Threshold)
			{
				Entries.Reset();
				return (LatestEnd - Now) + ImmunityDuration;
			}
			return 0.f;
		}

		void Reset()
		{
			Entries.Reset();
		}

	private:
		struct FEntry
		{
			float Start = 0.f;
			float Duration = 0.f;
		};

		TArray<FEntry> Entries;
	};
}
