#include "UI/GenAbilityBar.h"

#include "GenGameplayTags.h"
#include "UI/GenAbilitySlot.h"
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

	if (UGenUISubsystem* UI = UGenUISubsystem::Get(this))
	{
		ReadyHandle = UI->CallOrRegister_OnAbilitySystemReady(FGenOnAbilitySystemReady::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemReady));
	}
}

void UGenAbilityBar::NativeDestruct()
{
	if (UGenUISubsystem* UI = UGenUISubsystem::Get(this))
	{
		UI->UnregisterOnAbilitySystemReady(ReadyHandle);
	}
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
