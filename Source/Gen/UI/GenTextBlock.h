#pragma once

#include "CoreMinimal.h"
#include "CommonTextBlock.h"
#include "GenTextBlock.generated.h"

/** Bloc de texte du jeu (UI_Guidelines §8.8). L'échelle de texte viendra avec les options (fixée à 1 pour l'instant). */
UCLASS()
class GEN_API UGenTextBlock : public UCommonTextBlock
{
	GENERATED_BODY()
};
