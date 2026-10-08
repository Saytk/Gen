#include "UI/GenPrimaryGameLayout.h"

#include "CommonActivatableWidget.h"
#include "UI/GenUITags.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenUILayout, Log, All);

UCommonActivatableWidgetStack* UGenPrimaryGameLayout::GetLayer(FGameplayTag LayerTag) const
{
	if (LayerTag == GenUITags::UI_Layer_Game) { return GameLayer; }
	if (LayerTag == GenUITags::UI_Layer_GameMenu) { return GameMenuLayer; }
	if (LayerTag == GenUITags::UI_Layer_Menu) { return MenuLayer; }
	if (LayerTag == GenUITags::UI_Layer_Modal) { return ModalLayer; }
	return nullptr;
}

UCommonActivatableWidget* UGenPrimaryGameLayout::PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	UCommonActivatableWidgetStack* Layer = GetLayer(LayerTag);
	if (!Layer || !WidgetClass)
	{
		UE_LOG(LogGenUILayout, Warning, TEXT("Impossible d'empiler %s sur %s"), *GetNameSafe(WidgetClass), *LayerTag.ToString());
		return nullptr;
	}

	UCommonActivatableWidget* Widget = Layer->AddWidget<UCommonActivatableWidget>(WidgetClass);
	UE_LOG(LogGenUILayout, Log, TEXT("Empilé %s sur %s"), *GetNameSafe(Widget), *LayerTag.ToString());
	return Widget;
}
