#pragma once

#include "CoreMinimal.h"

/**
 * Règles pures de la barre de cast (sans UMG ni monde), testées hors jeu.
 * Une seule barre du premier appui au lancer, même pour un sort nourri.
 */
namespace GenCastBar
{
	/** Durée (s) du repli des segments inutilisés quand le nourrissage s'arrête. */
	inline constexpr float CollapseDuration = 0.1f;

	/** Paramètres d'une incantation, tels que répliqués dans FGenCastInfo. */
	struct FParams
	{
		/** Flammes disponibles à l'appui (≤ MaxFeed). 0 = incantation normale. */
		int32 FeedSlots = 0;
		float FeedInterval = 0.f;
		float CastTime = 0.f;
		/** Début de l'incantation (temps serveur). */
		float StartTime = 0.f;
		/** Fin du nourrissage (temps serveur). 0 = nourrissage en cours. */
		float FeedEndTime = 0.f;
		/** Flammes nourries : en direct pendant le nourrissage, définitives après. */
		int32 FedCount = 0;
	};

	/** Ce que le HUD dessine. */
	struct FLayout
	{
		/** Durée totale affichée (s), repli compris. */
		float TotalDuration = 0.f;
		/** Remplissage, 0..1. */
		float Fill = 0.f;
		/** Position des crans (0..1), un par seuil de flamme. */
		TArray<float, TInlineAllocator<8>> Ticks;
		/** Compteur de flammes affiché. */
		int32 Counter = 0;
		/** Sort nourri : le HUD dessine les crans et le compteur. */
		bool bFed = false;
	};

	/**
	 * Disposition de la barre à l'instant Now (temps serveur).
	 * I = FeedInterval, C = CastTime, S = FeedSlots, N = FedCount borné à 0..S, Te = FeedEndTime.
	 *
	 * - Incantation normale (S = 0, ou I ≤ 0) : total = C, remplissage = (Now − début) / C,
	 *   ni cran ni compteur.
	 * - Nourrissage en cours (Te = 0) : total T_S = S·I + C, remplissage = écoulé / T_S,
	 *   crans à k·I / T_S (k = 1..S), compteur = N (flammes réellement nourries, en direct). Le compteur
	 *   n'est pas déduit du temps : il ne recule jamais au relâché et suit les flammes de l'orbite.
	 * - Nourrissage terminé à Te avec N flammes : le total passe linéairement de T_S à T_N = N·I + C
	 *   en CollapseDuration à partir de Te ; p = clamp(Now − Te, 0, C) ; remplissage = (N·I + p) / total ;
	 *   crans à k·I / total (k = 1..N) ; compteur = N.
	 *   Le temps passé entre le dernier seuil et le relâché est abandonné exprès : la barre montre
	 *   les seuils, pas le temps réel.
	 * - Total nul (ni flamme ni incantation) : barre pleine.
	 */
	inline FLayout ComputeLayout(const FParams& Params, float Now)
	{
		FLayout Layout;

		const float CastTime = FMath::Max(Params.CastTime, 0.f);
		const float Interval = Params.FeedInterval;
		const int32 Slots = Interval > 0.f ? FMath::Max(Params.FeedSlots, 0) : 0;
		const float Elapsed = FMath::Max(Now - Params.StartTime, 0.f);

		if (Slots == 0)
		{
			Layout.TotalDuration = CastTime;
			Layout.Fill = CastTime > 0.f ? FMath::Clamp(Elapsed / CastTime, 0.f, 1.f) : 1.f;
			return Layout;
		}

		Layout.bFed = true;
		const float FeedingTotal = Slots * Interval + CastTime;

		// Temps "affiché" depuis le début de la barre et nombre de crans dessinés
		float Shown = 0.f;
		int32 TickCount = Slots;

		if (Params.FeedEndTime <= 0.f)
		{
			Layout.TotalDuration = FeedingTotal;
			Shown = Elapsed;
			Layout.Counter = FMath::Clamp(Params.FedCount, 0, Slots);
		}
		else
		{
			const int32 Fed = FMath::Clamp(Params.FedCount, 0, Slots);
			const float SinceEnd = FMath::Max(Now - Params.FeedEndTime, 0.f);
			const float CollapseAlpha = CollapseDuration > 0.f ? FMath::Clamp(SinceEnd / CollapseDuration, 0.f, 1.f) : 1.f;

			Layout.TotalDuration = FMath::Lerp(FeedingTotal, Fed * Interval + CastTime, CollapseAlpha);
			Shown = Fed * Interval + FMath::Min(SinceEnd, CastTime);
			Layout.Counter = Fed;
			TickCount = Fed;
		}

		if (Layout.TotalDuration <= 0.f)
		{
			Layout.Fill = 1.f;
			return Layout;
		}

		Layout.Fill = FMath::Clamp(Shown / Layout.TotalDuration, 0.f, 1.f);
		for (int32 K = 1; K <= TickCount; ++K)
		{
			Layout.Ticks.Add(K * Interval / Layout.TotalDuration);
		}
		return Layout;
	}
}
