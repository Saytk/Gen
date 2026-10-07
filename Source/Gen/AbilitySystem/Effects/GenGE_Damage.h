#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GenGE_Damage.generated.h"

/**
 * GE de dégâts instantané générique : ajoute SetByCaller.Damage au méta-attribut IncomingDamage.
 * Faites-en un Blueprint enfant pour y ajouter des GameplayCues (hit react, son...) si besoin.
 */
UCLASS()
class GEN_API UGenGE_Damage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGenGE_Damage();
};
