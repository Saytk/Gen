#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GenUILibrary.generated.h"

UCLASS()
class GEN_API UGenUILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Jeton hexadécimal sRGB -> couleur linéaire (à utiliser pour remplir DA_UIPalette, jamais de valeurs collées). */
	UFUNCTION(BlueprintPure, Category = "Gen|UI")
	static FLinearColor HexToLinear(const FString& Hex, float Alpha = 1.f);
};
