#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GenGE_MoveSpeedMultiplier.generated.h"

/**
 * GE infini qui multiplie MoveSpeed par SetByCaller.MoveSpeedMultiplier (ex: 0.5 = moitié moins vite).
 * Utilisé pendant les incantations ; à retirer manuellement via son handle.
 */
UCLASS()
class GEN_API UGenGE_MoveSpeedMultiplier : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGenGE_MoveSpeedMultiplier();
};
