#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "GenPrimaryGameLayout.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetStack;

/**
 * Racine de l'interface d'un joueur local (UI_Guidelines §8.1, sur le modèle de Lyra, sans ses plugins).
 * Quatre piles par tag : UI.Layer.Game (HUD), GameMenu, Menu, Modal. Seule cette racine est ajoutée au viewport.
 */
UCLASS(Abstract)
class GEN_API UGenPrimaryGameLayout : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Empile un widget sur la couche demandée. Retourne l'instance (nullptr si la couche ou la classe manque). */
	UCommonActivatableWidget* PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass);

	UCommonActivatableWidgetStack* GetLayer(FGameplayTag LayerTag) const;

protected:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCommonActivatableWidgetStack> GameLayer;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCommonActivatableWidgetStack> GameMenuLayer;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCommonActivatableWidgetStack> MenuLayer;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCommonActivatableWidgetStack> ModalLayer;
};
