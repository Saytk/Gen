#include "UI/GenAbilityBar.h"

#include "Components/PanelWidget.h"
#include "Components/Spacer.h"
#include "GenGameplayTags.h"
#include "Player/GenPlayerController.h"
#include "UI/GenAbilityTooltip.h"
#include "UI/GenAbilitySlot.h"
#include "UI/GenUIDataAssets.h"
#include "UI/GenUISubsystem.h"

void UGenAbilityBar::NativeConstruct()
{
	Super::NativeConstruct();

	// Ordre et tags des emplacements (§4.1) ; l'ultime porte l'arc d'énergie
	const TPair<UGenAbilitySlot*, FGameplayTag> Layout[] = {
		{ SlotPrimary, GenGameplayTags::InputTag_Ability_Primary },
		{ SlotSecondary, GenGameplayTags::InputTag_Ability_Secondary },
		{ SlotMobility, GenGameplayTags::InputTag_Ability_Mobility },
		{ Slot1, GenGameplayTags::InputTag_Ability_1 },
		{ Slot2, GenGameplayTags::InputTag_Ability_2 },
		{ Slot3, GenGameplayTags::InputTag_Ability_3 },
		{ SlotUltimate, GenGameplayTags::InputTag_Ability_Ultimate },
	};
	for (const TPair<UGenAbilitySlot*, FGameplayTag>& Entry : Layout)
	{
		Entry.Key->InputTag = Entry.Value;
		Entry.Key->bIsUltimate = (Entry.Key == SlotUltimate);
	}

	// Espacements depuis DA_UIMetrics (§3.1, §9 Jetons) : écart entre emplacements (espaceurs de la rangée) et marge
	// d'écran sous la barre, la même que lit AGenHUD::GetAbilityBarTop. Les valeurs du WBP ne servent qu'à l'aperçu.
	const UGenUISubsystem* UIForMetrics = UGenUISubsystem::Get(this);
	const UGenUIMetrics* Metrics = UIForMetrics && UIForMetrics->GetMetrics() ? UIForMetrics->GetMetrics() : GetDefault<UGenUIMetrics>();
	if (const UPanelWidget* Row = SlotPrimary->GetParent())
	{
		for (UWidget* Child : Row->GetAllChildren())
		{
			if (USpacer* Gap = Cast<USpacer>(Child))
			{
				Gap->SetSize(FVector2D(Metrics->SlotGap, Gap->GetSize().Y));
			}
		}
	}
	SetPadding(FMargin(0.f, 0.f, 0.f, Metrics->ScreenMargin));

	if (UGenUISubsystem* UI = UGenUISubsystem::Get(this))
	{
		ReadyHandle = UI->CallOrRegister_OnAbilitySystemReady(FGenOnAbilitySystemReady::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemReady));
	}

	// Touche « détails des sorts » : le contrôleur diffuse, la barre affiche (le jeu ne connaît aucun widget, §8.1)
	if (AGenPlayerController* PC = Cast<AGenPlayerController>(GetOwningPlayer()))
	{
		ShowDetailsHandle = PC->OnShowAbilityDetailsChanged.AddUObject(this, &ThisClass::SetAbilityDetailsShown);
	}
	if (DetailsPanel)
	{
		DetailsPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UGenAbilityBar::NativeDestruct()
{
	if (UGenUISubsystem* UI = UGenUISubsystem::Get(this))
	{
		UI->UnregisterOnAbilitySystemReady(ReadyHandle);
	}
	if (AGenPlayerController* PC = Cast<AGenPlayerController>(GetOwningPlayer()))
	{
		PC->OnShowAbilityDetailsChanged.Remove(ShowDetailsHandle);
	}
	ShowDetailsHandle.Reset();
	for (UGenAbilitySlot* SlotWidget : GetSlots())
	{
		SlotWidget->Unbind();
	}
	Super::NativeDestruct();
}

void UGenAbilityBar::HandleAbilitySystemReady(UAbilitySystemComponent* ASC)
{
	for (UGenAbilitySlot* SlotWidget : GetSlots())
	{
		SlotWidget->Bind(ASC);
	}
}

TArray<UGenAbilitySlot*> UGenAbilityBar::GetSlots() const
{
	return { SlotPrimary, SlotSecondary, SlotMobility, Slot1, Slot2, Slot3, SlotUltimate };
}

void UGenAbilityBar::SetAbilityDetailsShown(bool bShown)
{
	if (bDetailsShown == bShown)
	{
		return;
	}
	bDetailsShown = bShown;

	const bool bInPlace = DetailsPanel == nullptr;
	for (UGenAbilitySlot* SlotWidget : GetSlots())
	{
		SlotWidget->SetDetailsShown(bShown, bInPlace);
	}
	if (bInPlace)
	{
		return;
	}

	if (bShown)
	{
		// Une carte par sort, recréée à chaque appui : les valeurs suivent le sort et la touche du moment
		DetailsPanel->ClearChildren();
		for (UGenAbilitySlot* SlotWidget : GetSlots())
		{
			if (UGenAbilityTooltip* Tooltip = SlotWidget->CreateFilledTooltip())
			{
				DetailsPanel->AddChild(Tooltip);
				Tooltip->Show(0.f);
			}
		}
		DetailsPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		for (UWidget* Child : DetailsPanel->GetAllChildren())
		{
			if (UGenAbilityTooltip* Tooltip = Cast<UGenAbilityTooltip>(Child))
			{
				Tooltip->Hide();
			}
		}
	}
}
