#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/Effects/GenGE_Cooldown.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "Champions/Curffe/CurffeGameplayTags.h"
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
	TestFalse(TEXT("liaison : pas de flash"), SlotR->IsFlashing());

	// Revue P3 T8-10, M7 : arc de coût (Segments / SegmentSlots / Funded), financé selon la règle de CheckCost (M5)
	const int32 Slots = SlotF->GetArcSegmentSlots();
	TestTrue(TEXT("arc : emplacements de l'ultime"), Slots >= 4);
	TestEqual(TEXT("R : 1 segment"), SlotR->GetArcSegments(), 1);
	TestEqual(TEXT("R : même taille de segment que l'ultime"), SlotR->GetArcSegmentSlots(), Slots);
	TestEqual(TEXT("R à 20 : segment creux"), SlotR->GetArcFunded(), 0);
	TestEqual(TEXT("F : 4 segments"), SlotF->GetArcSegments(), 4);
	TestEqual(TEXT("F à 20 : aucun segment financé"), SlotF->GetArcFunded(), 0);

	// Changement d'énergie : l'état suit tout de suite (événement d'attribut, pas de minuteur)
	SetEnergy(25.f);
	TestEqual(TEXT("R à 25 (pile le coût) : prêt"), SlotR->GetState(), EGenAbilitySlotState::Ready);
	TestTrue(TEXT("R : pas assez -> prêt, flash « prêt » (M4)"), SlotR->IsFlashing());
	TestEqual(TEXT("R à 25 : segment financé"), SlotR->GetArcFunded(), 1);
	TestEqual(TEXT("F à 25 : toujours pas assez"), SlotF->GetState(), EGenAbilitySlotState::NoEnergy);
	TestEqual(TEXT("F à 25 : 1 segment sur 4"), SlotF->GetArcFunded(), 1);
	SetEnergy(24.99f);
	TestEqual(TEXT("R à 24.99 : pas assez (même règle que CheckCost)"), SlotR->GetState(), EGenAbilitySlotState::NoEnergy);
	TestEqual(TEXT("R à 24.99 : segment creux (même règle)"), SlotR->GetArcFunded(), 0);
	SetEnergy(99.f);
	TestEqual(TEXT("F à 99 : pas assez"), SlotF->GetState(), EGenAbilitySlotState::NoEnergy);
	TestEqual(TEXT("F à 99 : 3 segments sur 4"), SlotF->GetArcFunded(), 3);
	SetEnergy(100.f);
	TestEqual(TEXT("F à 100 : prêt"), SlotF->GetState(), EGenAbilitySlotState::Ready);
	TestEqual(TEXT("F à 100 : arc plein"), SlotF->GetArcFunded(), 4);
	TestEqual(TEXT("R à 100 : prêt"), SlotR->GetState(), EGenAbilitySlotState::Ready);

	// Contrôle dur : bloqué prime sur l'énergie, quel que soit le contrôle (silence ici, pas seulement l'étourdissement)
	SetEnergy(10.f);
	ASC->ApplyHardCC(GenGameplayTags::State_Silenced, 1.f, nullptr);
	TestEqual(TEXT("silence : R bloqué (prime sur l'énergie)"), SlotR->GetState(), EGenAbilitySlotState::Locked);
	TestEqual(TEXT("silence : F bloqué"), SlotF->GetState(), EGenAbilitySlotState::Locked);
	TestWorld.Advance(1.2f);
	TestEqual(TEXT("silence fini : de nouveau pas assez d'énergie"), SlotR->GetState(), EGenAbilitySlotState::NoEnergy);

	// Revue P3 T8-10, M4 : une recharge qui finit sans assez d'énergie ne flashe pas ; l'énergie qui arrive ensuite, si
	TestWorld.Advance(0.5f);
	const FGameplayEffectSpecHandle Cooldown = ASC->MakeOutgoingSpec(UGenGE_Cooldown::StaticClass(), 1.f, ASC->MakeEffectContext());
	Cooldown.Data->DynamicGrantedTags.AddTag(CurffeGameplayTags::Cooldown_Ability_LivingFlame);
	Cooldown.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Cooldown, 1.f);
	ASC->ApplyGameplayEffectSpecToSelf(*Cooldown.Data);
	TestEqual(TEXT("R : recharge"), SlotR->GetState(), EGenAbilitySlotState::Cooldown);
	TestWorld.Advance(1.3f);
	TestEqual(TEXT("recharge finie à 10 d'énergie : pas assez"), SlotR->GetState(), EGenAbilitySlotState::NoEnergy);
	TestFalse(TEXT("recharge -> pas assez : pas de flash"), SlotR->IsFlashing());
	SetEnergy(30.f);
	TestEqual(TEXT("30 d'énergie : prêt"), SlotR->GetState(), EGenAbilitySlotState::Ready);
	TestTrue(TEXT("pas assez -> prêt : flash"), SlotR->IsFlashing());
	SetEnergy(10.f);

	SlotR->Unbind();
	SlotF->Unbind();
	SetEnergy(100.f);
	TestEqual(TEXT("délié : plus d'écoute de l'énergie"), SlotR->GetState(), EGenAbilitySlotState::NoEnergy);
	return true;
}

#endif
