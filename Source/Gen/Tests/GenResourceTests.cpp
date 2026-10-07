#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "Champions/Curffe/CurffeTuning.h"
#include "Character/GenTrainingDummy.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace GenResourceTests
{
	/** Monde de jeu minimal (standalone, autorité) pour tester le GAS sans PIE. */
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GenResourceTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FScopedTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		/** Fait avancer le temps du monde (timers des effets périodiques). */
		void Advance(float Seconds, float Step = 0.05f)
		{
			for (float Elapsed = 0.f; Elapsed < Seconds; Elapsed += Step)
			{
				// FTimerManager::Tick ignore un second appel dans la même frame (GFrameCounter) : on simule donc une frame par pas.
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Step);
			}
		}

		UAbilitySystemComponent* SpawnDummyASC()
		{
			AGenTrainingDummy* Dummy = World->SpawnActor<AGenTrainingDummy>();
			if (Dummy && !Dummy->HasActorBegunPlay())
			{
				Dummy->DispatchBeginPlay();
			}
			return Dummy ? Dummy->GetAbilitySystemComponent() : nullptr;
		}
	};

	float Get(UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute)
	{
		return ASC->GetNumericAttribute(Attribute);
	}

	FActiveGameplayEffectHandle ApplyClass(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass)
	{
		return ASC->ApplyGameplayEffectToSelf(EffectClass->GetDefaultObject<UGameplayEffect>(), 1.f, ASC->MakeEffectContext());
	}

	void ApplyGain(UAbilitySystemComponent* ASC, float Energy, float Resource)
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGenGE_Gain::StaticClass(), 1.f, ASC->MakeEffectContext());
		UGenGE_Gain::SetMagnitudes(*Spec.Data, Energy, Resource);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
}

using namespace GenResourceTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenResourceClampTest, "Gen.Resource.ClampedToMax",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenResourceClampTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	TestEqual(TEXT("pas de ressource par défaut"), Get(ASC, UGenAttributeSet::GetMaxResourceAttribute()), 0.f);
	ApplyGain(ASC, 0.f, 3.f);
	TestEqual(TEXT("sans max, la ressource reste à 0"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 0.f);

	ApplyClass(ASC, UCurffeGE_HearthSetup::StaticClass());
	ApplyClass(ASC, UCurffeGE_HearthFill::StaticClass());
	TestEqual(TEXT("Foyer : max 5"), Get(ASC, UGenAttributeSet::GetMaxResourceAttribute()), 5.f);
	TestEqual(TEXT("Foyer : plein à 5"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 5.f);

	ApplyGain(ASC, 0.f, 3.f);
	TestEqual(TEXT("gain au-delà du max borné"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 5.f);
	ApplyGain(ASC, 0.f, -2.f);
	TestEqual(TEXT("dépense de 2"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 3.f);
	ApplyGain(ASC, 0.f, -10.f);
	TestEqual(TEXT("dépense excessive bornée à 0"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenResourceSetupRemovedTest, "Gen.Resource.DropsWhenSetupRemoved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenResourceSetupRemovedTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	const FActiveGameplayEffectHandle Setup = ApplyClass(ASC, UCurffeGE_HearthSetup::StaticClass());
	ApplyClass(ASC, UCurffeGE_HearthFill::StaticClass());
	ASC->RemoveActiveGameplayEffect(Setup);

	TestEqual(TEXT("max retombé à 0"), Get(ASC, UGenAttributeSet::GetMaxResourceAttribute()), 0.f);
	TestEqual(TEXT("ressource bornée au nouveau max"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenHearthRegenTest, "Gen.Curffe.HearthRegen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenHearthRegenTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	ApplyClass(ASC, UCurffeGE_HearthSetup::StaticClass());
	ApplyClass(ASC, UCurffeGE_HearthFill::StaticClass());
	ApplyClass(ASC, UCurffeGE_HearthRegen::StaticClass());
	ApplyGain(ASC, 0.f, -2.f);
	TestEqual(TEXT("après dépense"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 3.f);

	TestWorld.Advance(CurffeTuning::FlameRegenPeriod + 0.1f);
	TestEqual(TEXT("+1 après une période"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 4.f);

	TestWorld.Advance(CurffeTuning::FlameRegenPeriod * 3.f);
	TestEqual(TEXT("jamais au-delà de 5"), Get(ASC, UGenAttributeSet::GetResourceAttribute()), 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenEnergyGainTest, "Gen.Resource.EnergyGain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenEnergyGainTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	TestEqual(TEXT("énergie de départ (règle des 25)"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 25.f);
	ApplyGain(ASC, 6.f, 0.f);
	TestEqual(TEXT("gain d'énergie"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 31.f);
	ApplyGain(ASC, 500.f, 0.f);
	TestEqual(TEXT("bornée à 100"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 100.f);
	return true;
}

#endif
