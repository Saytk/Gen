#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GenGE_Cooldown.generated.h"

/**
 * GE de cooldown générique partagé par tous les sorts.
 * Durée = SetByCaller.Cooldown, tag accordé = CooldownTags du sort (ajouté dynamiquement).
 */
UCLASS()
class GEN_API UGenGE_Cooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGenGE_Cooldown();
};
