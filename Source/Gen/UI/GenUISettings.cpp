#include "UI/GenUISettings.h"

#include "CommonTextBlock.h"

UGenUISettings::UGenUISettings()
{
	// Style compact par défaut (UI_Guidelines §2.6) ; modifiable dans Project Settings > Game > Gen UI
	DevPanelTextStyle = TSoftClassPtr<UCommonTextStyle>(FSoftObjectPath(TEXT("/Game/Gen/UI/Foundation/TS_BodyCompact.TS_BodyCompact_C")));
}
