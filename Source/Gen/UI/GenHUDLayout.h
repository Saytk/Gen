#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "GenHUDLayout.generated.h"

/**
 * Seul widget de la couche UI.Layer.Game (UI_Guidelines §8.1). Le Blueprint WBP_HUDLayout porte la
 * disposition : Canvas racine > SafeZone > Overlay, la barre de sorts en bas au centre (marge 32 px).
 */
UCLASS(Abstract)
class GEN_API UGenHUDLayout : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UGenHUDLayout(const FObjectInitializer& ObjectInitializer);

	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
};
