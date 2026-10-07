#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "CurffeEffects.generated.h"

/** Foyer de Curffe (infini) : MaxResource = 5 flammes. À mettre en premier dans StartupEffects. */
UCLASS()
class GEN_API UCurffeGE_HearthSetup : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCurffeGE_HearthSetup();
};

/** Remplit le Foyer (instantané). Après HearthSetup dans StartupEffects. */
UCLASS()
class GEN_API UCurffeGE_HearthFill : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCurffeGE_HearthFill();
};

/** +1 flamme toutes les FlameRegenPeriod secondes (infini, périodique ; le max borne). */
UCLASS()
class GEN_API UCurffeGE_HearthRegen : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UCurffeGE_HearthRegen();
};
