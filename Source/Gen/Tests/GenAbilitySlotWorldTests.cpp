#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "Blueprint/UserWidget.h"
#include "Champions/Curffe/CurffeGA_Combustion.h"
#include "Champions/Curffe/CurffeGA_LivingFlame.h"
#include "Character/GenTrainingDummy.h"
#include "GenGameplayTags.h"
#include "Tests/GenTestWorld.h"
#include "UI/GenAbilitySlot.h"

using namespace GenTestWorld;

/**
 * Gen.UI.EnergySlotWidget (Plan 3 Task 10, Review Focus #6) : le vrai WBP_AbilitySlot lié à l'ASC d'un mannequin qui
 * porte Living Flame (R, 25) et Combustion (F, 100). L'état « pas assez d'énergie » suit GenEnergy::CanAfford (la règle
 * de CheckCost) et se met à jour à chaque changement d'énergie, sans sondage ; un contrôle dur (n'importe lequel) prime.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenEnergySlotWidgetTest, "Gen.UI.EnergySlotWidget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenEnergySlotWidgetTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Dummy = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Dummy ? Dummy->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	// Touches R et F prises dans l'InputTag des sorts (GrantAbilities)
	ASC->GrantAbilities({ UCurffeGA_LivingFlame::StaticClass(), UCurffeGA_Combustion::StaticClass() }, nullptr);

	UClass* SlotClass = LoadClass<UGenAbilitySlot>(nullptr, TEXT("/Game/Gen/UI/HUD/WBP_AbilitySlot.WBP_AbilitySlot_C"));
	if (!TestNotNull(TEXT("WBP_AbilitySlot"), SlotClass))
	{
		return false;
	}

	UGenAbilitySlot* SlotR = CreateWidget<UGenAbilitySlot>(TestWorld.World, SlotClass);
	UGenAbilitySlot* SlotF = CreateWidget<UGenAbilitySlot>(TestWorld.World, SlotClass);
	if (!TestNotNull(TEXT("emplacement R"), SlotR) || !TestNotNull(TEXT("emplacement F"), SlotF))
	{
		return false;
	}
	SlotR->InputTag = GenGameplayTags::InputTag_Ability_3;
	SlotF->InputTag = GenGameplayTags::InputTag_Ability_Ultimate;
	SlotF->bIsUltimate = true;

	auto SetEnergy = [ASC](float Energy) { ASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), Energy); };

	SetEnergy(20.f);
	SlotR->Bind(ASC);
	SlotF->Bind(ASC);
	TestEqual(TEXT("R à 20 : pas assez d'énergie"), SlotR->GetState(), EGenAbilitySlotState::NoEnergy);
	TestEqual(TEXT("F à 20 : pas assez d'énergie"), SlotF->GetState(), EGenAbilitySlotState::NoEnergy);

	// Changement d'énergie : l'état suit tout de suite (événement d'attribut, pas de minuteur)
	SetEnergy(25.f);
	TestEqual(TEXT("R à 25 (pile le coût) : prêt"), SlotR->GetState(), EGenAbilitySlotState::Ready);
	TestEqual(TEXT("F à 25 : toujours pas assez"), SlotF->GetState(), EGenAbilitySlotState::NoEnergy);
	SetEnergy(24.99f);
	TestEqual(TEXT("R à 24.99 : pas assez (même règle que CheckCost)"), SlotR->GetState(), EGenAbilitySlotState::NoEnergy);
	SetEnergy(99.f);
	TestEqual(TEXT("F à 99 : pas assez"), SlotF->GetState(), EGenAbilitySlotState::NoEnergy);
	SetEnergy(100.f);
	TestEqual(TEXT("F à 100 : prêt"), SlotF->GetState(), EGenAbilitySlotState::Ready);
	TestEqual(TEXT("R à 100 : prêt"), SlotR->GetState(), EGenAbilitySlotState::Ready);

	// Contrôle dur : bloqué prime sur l'énergie, quel que soit le contrôle (silence ici, pas seulement l'étourdissement)
	SetEnergy(10.f);
	ASC->ApplyHardCC(GenGameplayTags::State_Silenced, 1.f, nullptr);
	TestEqual(TEXT("silence : R bloqué (prime sur l'énergie)"), SlotR->GetState(), EGenAbilitySlotState::Locked);
	TestEqual(TEXT("silence : F bloqué"), SlotF->GetState(), EGenAbilitySlotState::Locked);
	TestWorld.Advance(1.2f);
	TestEqual(TEXT("silence fini : de nouveau pas assez d'énergie"), SlotR->GetState(), EGenAbilitySlotState::NoEnergy);

	SlotR->Unbind();
	SlotF->Unbind();
	SetEnergy(100.f);
	TestEqual(TEXT("délié : plus d'écoute de l'énergie"), SlotR->GetState(), EGenAbilitySlotState::NoEnergy);
	return true;
}

#endif
