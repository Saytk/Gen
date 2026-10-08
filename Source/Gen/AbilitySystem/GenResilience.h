#pragma once

#include "CoreMinimal.h"

/**
 * Résilience (guidelines §3.3) : après 2.5 s de contrôle dur sur 5 s, immunité aux contrôles durs
 * pendant 1.5 s. Les longs contrôles restent possibles ; les enchaînements sans fin non.
 * Choix : l'immunité commence dès que le contrôle qui atteint le seuil est appliqué (il s'applique en entier)
 * et dure jusqu'à 1.5 s après sa fin. Deux contrôles simultanés ne comptent qu'une fois (union).
 * Lecture stricte (revue P3 T3-7, I3) : on compte l'union des contrôles rognée aux 5 s qui finissent à la fin du
 * dernier contrôle ([LatestEnd - 5, LatestEnd]) ; deux contrôles moyens espacés de presque 5 s ne suffisent pas.
 * Un contrôle retiré avant sa fin (futur nettoyage) compte quand même toute sa durée prévue : aucun ne l'est aujourd'hui.
 * Temps en double (temps du monde d'un serveur qui tourne longtemps, comme les verrous de GenFeeding).
 */
namespace GenResilience
{
	inline constexpr float Window = 5.f;
	inline constexpr float Threshold = 2.5f;
	inline constexpr float ImmunityDuration = 1.5f;

	struct FHardCCHistory
	{
		/** Enregistre un contrôle dur appliqué à Now pendant Duration. Renvoie la durée d'immunité à accorder maintenant (0 = aucune). */
		float Record(double Now, float Duration)
		{
			// Une fenêtre future finit au plus tôt maintenant : un contrôle fini avant Now - Window n'y entrera plus
			Entries.RemoveAll([Now](const FEntry& Entry) { return Entry.Start + Entry.Duration <= Now - Window; });
			Entries.Add({ Now, FMath::Max(Duration, 0.f) });

			TArray<FEntry> Sorted = Entries;
			Sorted.Sort([](const FEntry& A, const FEntry& B) { return A.Start < B.Start; });

			double LatestEnd = Now;
			for (const FEntry& Entry : Sorted)
			{
				LatestEnd = FMath::Max(LatestEnd, Entry.Start + Entry.Duration);
			}
			const double WindowStart = LatestEnd - Window;

			// Union des intervalles, chacun rogné au début de la fenêtre (leur fin est toujours <= LatestEnd)
			double Total = 0.0;
			double SpanStart = 0.0;
			double SpanEnd = -1.0;
			for (const FEntry& Entry : Sorted)
			{
				const double Start = FMath::Max(Entry.Start, WindowStart);
				const double End = Entry.Start + Entry.Duration;
				if (End <= Start)
				{
					continue;
				}
				if (Start > SpanEnd)
				{
					Total += FMath::Max(SpanEnd - SpanStart, 0.0);
					SpanStart = Start;
					SpanEnd = End;
				}
				else
				{
					SpanEnd = FMath::Max(SpanEnd, End);
				}
			}
			Total += FMath::Max(SpanEnd - SpanStart, 0.0);

			if (Total + 0.001 >= Threshold)
			{
				Entries.Reset();
				return static_cast<float>(LatestEnd - Now) + ImmunityDuration;
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
			double Start = 0.0;
			float Duration = 0.f;
		};

		TArray<FEntry> Entries;
	};
}
