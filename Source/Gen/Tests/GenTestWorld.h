#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Character/GenTrainingDummy.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

/** Outils partagés des tests GAS hors PIE (monde de jeu minimal, autorité). */
namespace GenTestWorld
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GenTestWorld"));
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

		/** Fait avancer le temps du monde (timers des effets à durée et périodiques). */
		void Advance(float Seconds, float Step = 0.05f)
		{
			for (float Elapsed = 0.f; Elapsed < Seconds; Elapsed += Step)
			{
				// FTimerManager::Tick ignore un second appel dans la même frame (GFrameCounter) : on simule donc une frame par pas.
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Step);
			}
		}

		AGenTrainingDummy* SpawnDummy()
		{
			AGenTrainingDummy* Dummy = World->SpawnActor<AGenTrainingDummy>();
			if (Dummy && !Dummy->HasActorBegunPlay())
			{
				Dummy->DispatchBeginPlay();
			}
			return Dummy;
		}

		UAbilitySystemComponent* SpawnDummyASC()
		{
			AGenTrainingDummy* Dummy = SpawnDummy();
			return Dummy ? Dummy->GetAbilitySystemComponent() : nullptr;
		}
	};

	inline float Get(const UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute)
	{
		return ASC->GetNumericAttribute(Attribute);
	}

	inline FActiveGameplayEffectHandle ApplyClass(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass)
	{
		return ASC->ApplyGameplayEffectToSelf(EffectClass->GetDefaultObject<UGameplayEffect>(), 1.f, ASC->MakeEffectContext());
	}
}

#endif
