#include "UI/GenUILibrary.h"

#include "UI/GenUIRules.h"

FLinearColor UGenUILibrary::HexToLinear(const FString& Hex, float Alpha)
{
	return GenUIRules::HexToLinear(Hex, Alpha);
}
