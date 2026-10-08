#include "Net/GenNetTestHelpers.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "Net/GenNetTestFloor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Player/GenPlayerState.h"

namespace GenNetTest
{
	TSubclassOf<AGameModeBase> LoadGameModeClass()
	{
		return LoadClass<AGameModeBase>(nullptr, TEXT("/Game/Gen/Core/BP_GenGameMode.BP_GenGameMode_C"));
	}

	TSubclassOf<UGameplayAbility> LoadCurffeAbilityClass(const TCHAR* AssetName)
	{
		const FString Path = FString::Printf(TEXT("/Game/Gen/Champions/Curffe/Abilities/%s.%s_C"), AssetName, AssetName);
		return LoadClass<UGameplayAbility>(nullptr, *Path);
	}

	void IgnoreUntitledMapNetWarnings(FAutomationTestBase& Test)
	{
		// Occurrences < 0 : messages ignorés, quel que soit leur nombre (y compris zéro)
		Test.AddExpectedMessage(TEXT("FNetGUIDCache::SupportsObject: Level /Temp/UEDPIE_\\d+_Untitled"),
			ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1, /*IsRegex*/ true);
		Test.AddExpectedMessage(TEXT("RegisterNetGUID_Client: Guid with pathname\\. FullNetGUIDPath: \\[\\d+\\]WorldSettings"),
			ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1, /*IsRegex*/ true);
	}

	APlayerController* GetServerController(const FBasePIENetworkComponentState& ServerState, int32 ClientIndex)
	{
		if (!ServerState.ClientConnections.IsValidIndex(ClientIndex))
		{
			return nullptr;
		}
		const UNetConnection* Connection = ServerState.ClientConnections[ClientIndex];
		return Connection ? Connection->PlayerController.Get() : nullptr;
	}

	APlayerController* GetLocalController(const FBasePIENetworkComponentState& ClientState)
	{
		return ClientState.World ? ClientState.World->GetFirstPlayerController() : nullptr;
	}

	AGenPlayerState* FindPlayerStateById(const UWorld* World, int32 PlayerId)
	{
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		if (!GameState)
		{
			return nullptr;
		}
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (PlayerState && PlayerState->GetPlayerId() == PlayerId)
			{
				return Cast<AGenPlayerState>(PlayerState);
			}
		}
		return nullptr;
	}

	UAbilitySystemComponent* GetASC(const AGenPlayerState* PlayerState)
	{
		return PlayerState ? PlayerState->GetAbilitySystemComponent() : nullptr;
	}

	bool IsPlayerReady(const APlayerController* PlayerController)
	{
		if (!PlayerController)
		{
			return false;
		}
		const AGenPlayerCharacter* Pawn = PlayerController->GetPawn<AGenPlayerCharacter>();
		const AGenPlayerState* PlayerState = PlayerController->GetPlayerState<AGenPlayerState>();
		const UAbilitySystemComponent* ASC = GetASC(PlayerState);
		return Pawn && ASC && ASC->GetAvatarActor() == Pawn && ASC->GetActivatableAbilities().Num() > 0;
	}

	bool AreAllServerPlayersReady(const FBasePIENetworkComponentState& ServerState)
	{
		for (int32 ClientIndex = 0; ClientIndex < ServerState.ClientCount; ++ClientIndex)
		{
			if (!IsPlayerReady(GetServerController(ServerState, ClientIndex)))
			{
				return false;
			}
		}
		return true;
	}

	float GetAttribute(const UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute)
	{
		return ASC ? ASC->GetNumericAttribute(Attribute) : 0.f;
	}

	bool DoReplicatedAttributesMatch(const UAbilitySystemComponent* Expected, const UAbilitySystemComponent* Actual, FString* OutDiff)
	{
		if (!Expected || !Actual)
		{
			if (OutDiff)
			{
				*OutDiff = FString::Printf(TEXT("ASC manquant (attendu=%d, réel=%d)"), Expected != nullptr, Actual != nullptr);
			}
			return false;
		}

		const FGameplayAttribute Attributes[] =
		{
			UGenAttributeSet::GetHealthAttribute(),
			UGenAttributeSet::GetMaxHealthAttribute(),
			UGenAttributeSet::GetEnergyAttribute(),
			UGenAttributeSet::GetMaxEnergyAttribute(),
			UGenAttributeSet::GetResourceAttribute(),
			UGenAttributeSet::GetMaxResourceAttribute(),
			UGenAttributeSet::GetMoveSpeedAttribute(),
		};

		for (const FGameplayAttribute& Attribute : Attributes)
		{
			const float ExpectedValue = GetAttribute(Expected, Attribute);
			const float ActualValue = GetAttribute(Actual, Attribute);
			if (!FMath::IsNearlyEqual(ExpectedValue, ActualValue, KINDA_SMALL_NUMBER))
			{
				if (OutDiff)
				{
					*OutDiff = FString::Printf(TEXT("%s : attendu %.3f, réel %.3f"), *Attribute.GetName(), ExpectedValue, ActualValue);
				}
				return false;
			}
		}
		return true;
	}

	FGameplayAbilitySpec* FindAbilitySpec(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		if (!ASC || !AbilityClass)
		{
			return nullptr;
		}
		for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->GetClass() == AbilityClass)
			{
				return &Spec;
			}
		}
		return nullptr;
	}

	AActor* SpawnTestFloor(UWorld* ServerWorld)
	{
		if (!ServerWorld)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return ServerWorld->SpawnActor<AGenNetTestFloor>(FVector(0.f, 0.f, AGenNetTestFloor::TopZ - 50.f), FRotator::ZeroRotator, Params);
	}

	bool HasTestFloor(const UWorld* World)
	{
		for (TActorIterator<AGenNetTestFloor> It(World); It; ++It)
		{
			return true;
		}
		return false;
	}
}

#endif // ENABLE_PIE_NETWORK_TEST
