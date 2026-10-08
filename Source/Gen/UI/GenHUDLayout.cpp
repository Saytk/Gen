#include "UI/GenHUDLayout.h"

#include "CommonInputModeTypes.h"
#include "Input/UIActionBindingHandle.h"

UGenHUDLayout::UGenHUDLayout(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = true;
}

TOptional<FUIInputConfig> UGenHUDLayout::GetDesiredInputConfig() const
{
	// Jeu : les entrées vont au gameplay, la souris reste visible et capturée (§8.1)
	return FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown, EMouseLockMode::LockAlways, /*bHideCursorDuringViewportCapture*/ false);
}
