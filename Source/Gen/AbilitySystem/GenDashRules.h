#pragma once

#include "CoreMinimal.h"

/**
 * Règles pures de la ruée en zigzag (UGenGA_Dash, Space de Curffe : Flame Dash). Sans état, testées hors monde
 * (Gen.Dash.Zigzag). Le sort et son indicateur de visée appellent les mêmes fonctions : le dessin = le trajet.
 */
namespace GenDashRules
{
	struct FZigzagParams
	{
		/** Premier segment, le long de la visée (cm). */
		float BaseDistance = 300.f;
		/** Chaque segment ajouté par une unité nourrie (cm). */
		float SegmentDistance = 250.f;
		/** Écart des segments ajoutés par rapport à la visée (degrés) : le premier à gauche, puis à droite, etc. */
		float ZigzagAngle = 30.f;
		/** Plafond du nombre de segments (1 + MaxFeed). */
		int32 MaxSegments = 4;
	};

	/** 1 segment sans nourrissage, un de plus par unité nourrie, borné à [1, MaxSegments]. */
	inline int32 GetSegmentCount(int32 Fed, int32 MaxSegments)
	{
		return FMath::Clamp(1 + FMath::Max(Fed, 0), 1, FMath::Max(MaxSegments, 1));
	}

	/**
	 * Lacet (degrés) du segment Index : 0 = la visée ; ensuite gauche, droite, gauche... Lacet d'Unreal vu de dessus :
	 * + = vers la droite (X avant, Y droite), donc la gauche est AimYaw - ZigzagAngle.
	 */
	inline float GetSegmentYaw(float AimYaw, int32 Index, float ZigzagAngle)
	{
		if (Index <= 0)
		{
			return AimYaw;
		}
		return AimYaw + ((Index % 2 == 1) ? -ZigzagAngle : ZigzagAngle);
	}

	inline float GetSegmentLength(int32 Index, const FZigzagParams& P)
	{
		return FMath::Max(Index == 0 ? P.BaseDistance : P.SegmentDistance, 0.f);
	}

	/** Longueur totale du trajet nominal (sans mur) avec Fed unités. */
	inline float GetPathLength(int32 Fed, const FZigzagParams& P)
	{
		float Length = 0.f;
		for (int32 Index = 0; Index < GetSegmentCount(Fed, P.MaxSegments); ++Index)
		{
			Length += GetSegmentLength(Index, P);
		}
		return Length;
	}

	/**
	 * Points d'arrivée de chaque segment (Start non compris), à plat (Z de Start) : 1 + Fed points (borné à MaxSegments).
	 * Trajet nominal, sans mur : le sort le coupe ensuite au premier obstacle (UGenGA_Dash::ComputePath).
	 */
	inline TArray<FVector> ComputeZigzag(const FVector& Start, float AimYaw, int32 Fed, const FZigzagParams& P)
	{
		TArray<FVector> Points;
		const int32 Count = GetSegmentCount(Fed, P.MaxSegments);
		Points.Reserve(Count);
		FVector Current = Start;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector Direction = FRotator(0.f, GetSegmentYaw(AimYaw, Index, P.ZigzagAngle), 0.f).Vector();
			Current += Direction * GetSegmentLength(Index, P);
			Current.Z = Start.Z;
			Points.Add(Current);
		}
		return Points;
	}

	/** Durée d'un segment de Length cm : vitesse constante, SegmentDuration pour un segment nominal (NominalLength). */
	inline float GetSegmentDuration(float Length, float NominalLength, float SegmentDuration)
	{
		if (NominalLength <= UE_KINDA_SMALL_NUMBER)
		{
			return FMath::Max(SegmentDuration, 0.01f);
		}
		return FMath::Max(SegmentDuration * Length / NominalLength, 0.01f);
	}
}
