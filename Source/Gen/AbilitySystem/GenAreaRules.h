#pragma once

#include "CoreMinimal.h"

/** Relation entre le joueur qui regarde et la source d'un effet. Valeurs = RelationIndex de M_VFX_Telegraph (UI §8.6). */
enum class EGenViewerRelation : uint8
{
	Self = 1,
	Ally = 2,
	Enemy = 3,
	Neutral = 4
};

/** Règles pures des zones au sol et des anneaux de projectiles. Sans état, testées hors monde. */
namespace GenAreaRules
{
	/** Point visé ramené à Range (cm) du lanceur, mesuré à plat. Le Z de Target est conservé. */
	inline FVector ClampToRange(const FVector& Origin, const FVector& Target, float Range)
	{
		const FVector Offset2D(Target.X - Origin.X, Target.Y - Origin.Y, 0.f);
		const double Distance = Offset2D.Size();
		if (Range <= 0.f)
		{
			return FVector(Origin.X, Origin.Y, Target.Z);
		}
		if (Distance <= Range)
		{
			return Target;
		}
		const FVector Clamped = Offset2D * (Range / Distance);
		return FVector(Origin.X + Clamped.X, Origin.Y + Clamped.Y, Target.Z);
	}

	/** Revue Plan 2 Tasks 7-8, I-1 : écart de lacet (degrés) toléré entre le bond annoncé par le client et celui du serveur. */
	inline constexpr float LeapYawTolerance = 5.f;

	/**
	 * Bond annoncé par le client (distance et lacet mesurés depuis SA position) : le serveur le reprend tel quel s'il ne
	 * dépasse pas la portée (1 cm de marge d'arrondi) et que son lacet est à YawTolerance près du sien. Les deux machines
	 * construisent alors la même force de saut (FRootMotionSource_JumpForce::Matches : distance exacte, rotation à 1°),
	 * le serveur peut synchroniser la source du client pendant les corrections. Refusé : le serveur garde ses valeurs.
	 */
	inline bool AcceptClientLeap(float ClientDistance, float ClientYaw, float MaxDistance, float ServerYaw, float YawTolerance = LeapYawTolerance)
	{
		return ClientDistance >= 0.f && ClientDistance <= MaxDistance + 1.f
			&& FMath::Abs(FMath::FindDeltaAngleDegrees(ServerYaw, ClientYaw)) <= YawTolerance;
	}

	/**
	 * Revue Plan 2 Tasks 7-8, M-8 : un obstacle sur le chemin d'une boule de l'anneau n'est un mur que s'il n'est pas
	 * praticable. Une pente ou le dessus d'une marche (normale assez verticale, WalkableFloorZ du personnage) ne l'est pas.
	 */
	inline bool IsRingWall(const FVector& ImpactNormal, float WalkableFloorZ)
	{
		return ImpactNormal.Z < WalkableFloorZ;
	}

	/** Count directions horizontales régulières (360° / Count), la première selon Forward aplati. */
	inline TArray<FVector> GetRingDirections(int32 Count, const FVector& Forward)
	{
		TArray<FVector> Directions;
		if (Count <= 0)
		{
			return Directions;
		}

		FVector Base = Forward.GetSafeNormal2D();
		if (Base.IsNearlyZero())
		{
			Base = FVector::ForwardVector;
		}

		Directions.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Directions.Add(Base.RotateAngleAxis(360.f * Index / Count, FVector::UpVector));
		}
		return Directions;
	}

	/**
	 * Points de la capsule testés pour la ligne de vue : centre, deux bords perpendiculaires à la ligne,
	 * haut. Une cible à moitié derrière un coin reste touchable (pas seulement son centre).
	 */
	inline TArray<FVector> GetLineOfSightSamples(const FVector& Origin, const FVector& TargetCenter, float Radius, float HalfHeight)
	{
		FVector Side = FVector::CrossProduct(FVector::UpVector, (TargetCenter - Origin).GetSafeNormal2D());
		if (Side.IsNearlyZero())
		{
			Side = FVector::RightVector;
		}
		return { TargetCenter, TargetCenter + Side * Radius, TargetCenter - Side * Radius, TargetCenter + FVector(0.f, 0.f, HalfHeight) };
	}

	/** Point de vue d'un joueur sur l'effet d'une source (couleur du télégraphe). */
	inline EGenViewerRelation GetViewerRelation(bool bViewerIsSource, uint8 ViewerTeam, uint8 SourceTeam, uint8 NoTeam)
	{
		if (bViewerIsSource)
		{
			return EGenViewerRelation::Self;
		}
		if (SourceTeam == NoTeam)
		{
			return EGenViewerRelation::Neutral;
		}
		return ViewerTeam == SourceTeam ? EGenViewerRelation::Ally : EGenViewerRelation::Enemy;
	}

	/**
	 * Délai d'impact : 0 = immédiat ; sinon jamais sous MinTelegraph (0.6 s par défaut : guidelines §3.1,
	 * zones retardées 0.6–1.0 s ; couvre aussi la spec Combustion, télégraphes ≥ 0.5 s).
	 */
	inline float GetImpactDelay(float Delay, float MinTelegraph)
	{
		return Delay <= 0.f ? 0.f : FMath::Max(Delay, MinTelegraph);
	}

	/**
	 * Revue Plan 2 Tasks 7-8, M-5 : un client distant reçoit la zone ~½ RTT après son apparition et montre donc son
	 * télégraphe Delay − latence. Le plancher des zones retardées (MinTelegraph) est relevé de cette marge pour qu'il
	 * reste respecté chez les autres joueurs jusqu'à 100 ms d'aller simple. Le remplissage suit l'heure serveur répliquée.
	 */
	inline constexpr float TelegraphLatencyMargin = 0.1f;

	/** Remplissage du télégraphe : 0 à l'apparition, 1 à l'impact. */
	inline float GetTelegraphFill(float Elapsed, float Delay)
	{
		return Delay > 0.f ? FMath::Clamp(Elapsed / Delay, 0.f, 1.f) : 1.f;
	}
}
