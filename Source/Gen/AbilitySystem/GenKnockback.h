#pragma once

#include "CoreMinimal.h"

/** Maths du repoussement : un petit saut balistique qui parcourt la distance voulue. */
namespace GenKnockback
{
	/** Vitesse verticale du saut (cm/s). Basse : le vol dure ~0.3 s. */
	inline constexpr float UpSpeed = 150.f;

	/** Vitesse de lancement pour parcourir Distance (cm) à plat pendant le vol. GravityZ est négatif. */
	inline FVector ComputeLaunchVelocity(const FVector& Direction2D, float Distance, float GravityZ)
	{
		const float Gravity = FMath::Abs(GravityZ);
		const float AirTime = Gravity > KINDA_SMALL_NUMBER ? 2.f * UpSpeed / Gravity : 0.3f;
		return Direction2D.GetSafeNormal2D() * (Distance / AirTime) + FVector(0.f, 0.f, UpSpeed);
	}
}
