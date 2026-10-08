#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "GenGameplayTags.h"
#include "Tests/GenTestWorld.h"

using namespace GenTestWorld;

namespace GenPowerWorldTests
{
	void SetEnergy(UAbilitySystemComponent* ASC, float Energy)
	{
		ASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), Energy);
	}
}

using namespace GenPowerWorldTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenEnergyCostTest, "Gen.Energy.CostCheckAndApply",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenEnergyCostTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	// Un sort concret quelconque. Le coût est réglé sur l'instance (InstancedPerActor), jamais sur le CDO
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(FGameplayAbilitySpec(UGenGA_Projectile::StaticClass(), 1));
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	UGenGA_Projectile* Ability = Spec ? Cast<UGenGA_Projectile>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("instance du sort"), Ability))
	{
		return false;
	}
	Ability->EnergyCost = 25.f;
	const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();

	SetEnergy(ASC, 24.f);
	TestFalse(TEXT("24 d'énergie : coût refusé"), Ability->CheckCost(Handle, ActorInfo));
	TestFalse(TEXT("24 d'énergie : activation refusée"), Ability->CanActivateAbility(Handle, ActorInfo));
	SetEnergy(ASC, 24.99f);
	TestFalse(TEXT("24.99 ne suffit pas"), Ability->CheckCost(Handle, ActorInfo));
	SetEnergy(ASC, 25.f);
	TestTrue(TEXT("25 d'énergie : accepté (pile le coût)"), Ability->CheckCost(Handle, ActorInfo));
	TestTrue(TEXT("25 d'énergie : activation possible"), Ability->CanActivateAbility(Handle, ActorInfo));

	// CommitAbility (au lancer pour UGenGA_Cast) passe par ApplyCost
	Ability->ApplyCost(Handle, ActorInfo, FGameplayAbilityActivationInfo());
	TestEqual(TEXT("payé : 25 -> 0"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 0.f);
	TestFalse(TEXT("plus assez pour un second"), Ability->CheckCost(Handle, ActorInfo));

	// Sort gratuit : jamais refusé, rien de dépensé
	Ability->EnergyCost = 0.f;
	TestTrue(TEXT("gratuit à 0 d'énergie"), Ability->CheckCost(Handle, ActorInfo));
	SetEnergy(ASC, 10.f);
	Ability->ApplyCost(Handle, ActorInfo, FGameplayAbilityActivationInfo());
	TestEqual(TEXT("gratuit : rien dépensé"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 10.f);
	return true;
}

#endif
