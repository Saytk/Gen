#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenFeeding.h"

/**
 * Géométrie de l'indicateur de visée d'un sort, calculée par le sort lui-même à partir de SES valeurs de jeu
 * (Art Bible §7.1 règle 1 : le dessin = la hitbox). Tailles en cm, à plat ; le Z est posé au sol par le composant.
 */
struct FGenAimGeometry
{
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;

	/** Arc de portée (±30° autour de la visée). 0 = aucun. */
	float RangeArcRadius = 0.f;

	/** Couloir d'un projectile. LineLength 0 = aucun. */
	FVector LineStart = FVector::ZeroVector;
	float LineLength = 0.f;
	float LineWidth = 0.f;

	/** Cercle d'éclat au bout du couloir (0 = aucun) et rayons d'expulsion (repoussement). */
	FVector CapCenter = FVector::ZeroVector;
	float CapRadius = 0.f;
	bool bCapSpokes = false;

	/** Cercle cible (bond : point d'atterrissage). 0 = aucun. */
	FVector TargetCenter = FVector::ZeroVector;
	float TargetRadius = 0.f;

	/** Amorces de l'anneau de projectiles, une par unité nourrie. */
	TArray<FVector, TInlineAllocator<4>> StubDirections;
	float StubLength = 0.f;
	float StubWidth = 0.f;

	/**
	 * Trajet d'une ruée en zigzag (UGenGA_Dash) : départ puis arrivée de chaque segment, coupé au premier mur. Un segment
	 * par paire de points consécutifs (moins de 2 points = aucun), dessinés dans les emplacements des amorces.
	 */
	TArray<FVector, TInlineAllocator<5>> PathPoints;
	float PathWidth = 0.f;

	/** Unités nourries prises en compte (affichage et tests PIE). */
	int32 Fed = 0;
};

/** Règles pures des indicateurs de visée et des télégraphes. Sans état, testées hors monde. */
namespace GenIndicatorRules
{
	inline FVector FlatDirection(const FVector& Direction, const FVector& Fallback = FVector::ForwardVector)
	{
		const FVector Flat = Direction.GetSafeNormal2D();
		if (!Flat.IsNearlyZero())
		{
			return Flat;
		}
		const FVector FlatFallback = Fallback.GetSafeNormal2D();
		return FlatFallback.IsNearlyZero() ? FVector::ForwardVector : FlatFallback;
	}

	/** Valeurs du sort et de la classe de projectile (jamais recopiées à la main). */
	struct FProjectileAimParams
	{
		float SpawnForwardOffset = 0.f;
		float Range = 0.f;
		float CollisionRadius = 0.f;
		float ScaleAtMaxFeed = 1.f;
		int32 MaxFeed = 0;
		int32 ExplosionMinFeed = 0;
		float ExplosionRadius = 0.f;
		/** Explosion sans nourrissage (Pyroblast) : rayon sous ExplosionMinFeed (GenFeeding::GetShotExplosionRadius). */
		float BaseExplosionRadius = 0.f;
		int32 KnockbackMinFeed = 0;
	};

	/**
	 * Couloir d'un projectile : du point d'apparition jusqu'à sa portée (mesurée depuis ce point, comme AGenProjectile),
	 * coupé au premier mur. WallDistance = distance du mur depuis Origin le long de Direction (< 0 = aucun mur).
	 * Largeur = diamètre de collision × l'échelle que le serveur appliquera (ScaleByFeed). Éclat au rayon que le serveur
	 * donnera au tir (GenFeeding::GetShotExplosionRadius : ExplosionRadius dès ExplosionMinFeed, sinon BaseExplosionRadius),
	 * posé où le projectile exploserait au bout du couloir (contre le mur : centre à un rayon du mur).
	 */
	inline void ComputeProjectileAim(const FVector& Origin, const FVector& Direction, const FProjectileAimParams& P, int32 Fed, float WallDistance, FGenAimGeometry& Out)
	{
		Out = FGenAimGeometry();
		Out.Origin = Origin;
		Out.Direction = FlatDirection(Direction);
		Out.Fed = Fed;
		Out.LineStart = Origin + Out.Direction * P.SpawnForwardOffset;

		const float Scale = GenFeeding::ScaleByFeed(1.f, P.ScaleAtMaxFeed, Fed, P.MaxFeed);
		const float ScaledRadius = P.CollisionRadius * Scale;
		Out.LineWidth = 2.f * ScaledRadius;

		const float FullLength = FMath::Max(P.Range, 0.f);
		const bool bWall = WallDistance >= 0.f && WallDistance - P.SpawnForwardOffset < FullLength;
		Out.LineLength = bWall ? FMath::Max(WallDistance - P.SpawnForwardOffset, 0.f) : FullLength;

		const float CapRadius = GenFeeding::GetShotExplosionRadius(Fed, P.ExplosionMinFeed, P.ExplosionRadius, P.BaseExplosionRadius);
		if (CapRadius > 0.f)
		{
			const float CentreDistance = bWall ? FMath::Max(Out.LineLength - ScaledRadius, 0.f) : Out.LineLength;
			Out.CapCenter = Out.LineStart + Out.Direction * CentreDistance;
			Out.CapRadius = CapRadius;
			Out.bCapSpokes = GenFeeding::ReachesThreshold(Fed, P.KnockbackMinFeed);
		}
	}

	struct FLeapAimParams
	{
		float MaxDistance = 0.f;
		float LandingRadius = 0.f;
		float StubLength = 150.f;
		/** Rayon de collision du projectile de l'anneau (0 = pas d'amorces). */
		float RingProjectileRadius = 0.f;
	};

	/**
	 * Bond en vol (vu par tous, décision du 2026-10-08) : cercle d'atterrissage au point verrouillé et une amorce par
	 * unité nourrie selon GetRingDirections(Fed, direction du bond). Pas d'arc de portée (rien à viser).
	 */
	inline void ComputeLeapFlight(const FVector& Landing, const FVector& Direction, const FLeapAimParams& P, int32 Fed, FGenAimGeometry& Out)
	{
		Out = FGenAimGeometry();
		Out.Origin = Landing;
		Out.Fed = Fed;
		Out.Direction = FlatDirection(Direction);
		Out.TargetCenter = Landing;
		Out.TargetRadius = P.LandingRadius;
		if (P.RingProjectileRadius > 0.f)
		{
			for (const FVector& Ring : GenAreaRules::GetRingDirections(Fed, Out.Direction))
			{
				Out.StubDirections.Add(Ring);
			}
			Out.StubLength = P.StubLength;
			Out.StubWidth = 2.f * P.RingProjectileRadius;
		}
	}

	/**
	 * Bond : arc de portée, point d'atterrissage borné à MaxDistance (ClampToRange, comme le sort), et une amorce par
	 * unité nourrie selon GetRingDirections(Fed, direction du bond) : les directions exactes de l'anneau (ComputeLeapFlight).
	 */
	inline void ComputeLeapAim(const FVector& Origin, const FVector& Cursor, const FLeapAimParams& P, int32 Fed, FGenAimGeometry& Out,
		const FVector& FallbackForward = FVector::ForwardVector)
	{
		// Revue V6-V8, M-4 : curseur sur le lanceur => son avant, comme le client (FillAimData) et le serveur (AimDirection)
		const FVector Landing = GenAreaRules::ClampToRange(Origin, Cursor, P.MaxDistance);
		ComputeLeapFlight(FVector(Landing.X, Landing.Y, Origin.Z), FlatDirection(Landing - Origin, FallbackForward), P, Fed, Out);
		Out.Origin = Origin;
		Out.RangeArcRadius = P.MaxDistance;
	}

	/** Zone au sol : seulement l'arc de portée (le cercle est l'aperçu d'AGenGroundArea, Plan 2 Task 6). */
	inline void ComputeGroundAreaAim(const FVector& Origin, const FVector& Cursor, float Range, int32 Fed, FGenAimGeometry& Out)
	{
		Out = FGenAimGeometry();
		Out.Origin = Origin;
		Out.Fed = Fed;
		Out.Direction = FlatDirection(Cursor - Origin);
		Out.RangeArcRadius = Range;
	}

	/** Ordre de tri des formes au sol (Art Bible §7.5) : allié 1 < sa propre visée 2 < ennemi 3 ; marqueurs de projectile 4. */
	inline int32 GetSortPriority(EGenViewerRelation Relation)
	{
		switch (Relation)
		{
		case EGenViewerRelation::Self: return 2;
		case EGenViewerRelation::Enemy: return 3;
		default: return 1;
		}
	}
	inline constexpr int32 GroundMarkerSortPriority = 4;

	/** Un seuil de nourrissage est franchi quand le compte affiché AUGMENTE (jamais au lancer, à l'annulation ni à une correction vers le bas). */
	inline bool IsThresholdPop(int32 Old, int32 New)
	{
		return New > Old && New > 0;
	}
}
