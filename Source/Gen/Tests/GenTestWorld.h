#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/GenTrainingDummy.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace GenTestWorld
{
	/** Monde de jeu minimal (standalone, autorité) pour tester le GAS et les personnages sans PIE. */
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
}

#endif
