#include "Game/GenGameMode.h"

#include "Character/GenCharacterBase.h"
#include "Character/GenPlayerCharacter.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"
#include "TimerManager.h"
#include "UI/GenHUD.h"

AGenGameMode::AGenGameMode()
{
	DefaultPawnClass = AGenPlayerCharacter::StaticClass();
	PlayerControllerClass = AGenPlayerController::StaticClass();
	PlayerStateClass = AGenPlayerState::StaticClass();
	HUDClass = AGenHUD::StaticClass();
}

void AGenGameMode::PostLogin(APlayerController* NewPlayer)
{
	// L'équipe doit être connue AVANT Super::PostLogin, qui fait apparaître le joueur (ChoosePlayerStart)
	if (AGenPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AGenPlayerState>() : nullptr)
	{
		PS->SetTeamId(PickTeamForNewPlayer());
	}

	Super::PostLogin(NewPlayer);
}

uint8 AGenGameMode::PickTeamForNewPlayer() const
{
	TArray<int32> PlayersPerTeam;
	PlayersPerTeam.Init(0, NumTeams);

	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const AGenPlayerState* PS = Cast<AGenPlayerState>(PlayerState);
		if (PS && PS->GetTeamId() < NumTeams)
		{
			++PlayersPerTeam[PS->GetTeamId()];
		}
	}

	int32 BestTeam = 0;
	for (int32 TeamIndex = 1; TeamIndex < NumTeams; ++TeamIndex)
	{
		if (PlayersPerTeam[TeamIndex] < PlayersPerTeam[BestTeam])
		{
			BestTeam = TeamIndex;
		}
	}

	if (PlayersPerTeam[BestTeam] >= MaxPlayersPerTeam)
	{
		UE_LOG(LogTemp, Warning, TEXT("Toutes les équipes sont pleines (%d joueurs max par équipe)."), MaxPlayersPerTeam);
	}

	return static_cast<uint8>(BestTeam);
}

AActor* AGenGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	const AGenPlayerState* PS = Player ? Player->GetPlayerState<AGenPlayerState>() : nullptr;
	if (PS && PS->GetTeamId() != GenNoTeam)
	{
		const FName TeamTag(*FString::Printf(TEXT("Team%d"), PS->GetTeamId()));

		TArray<APlayerStart*> TeamStarts;
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (It->PlayerStartTag == TeamTag)
			{
				TeamStarts.Add(*It);
			}
		}

		if (TeamStarts.Num() > 0)
		{
			return TeamStarts[FMath::RandRange(0, TeamStarts.Num() - 1)];
		}
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

void AGenGameMode::OnCharacterDied(AGenCharacterBase* Victim, AActor* Killer)
{
	AController* Controller = Victim ? Victim->GetController() : nullptr;
	if (!Controller || !Controller->IsPlayerController())
	{
		return;
	}

	FTimerHandle RespawnTimerHandle;
	const FTimerDelegate RespawnDelegate = FTimerDelegate::CreateUObject(this, &ThisClass::RespawnPlayer, TWeakObjectPtr<AController>(Controller));
	GetWorldTimerManager().SetTimer(RespawnTimerHandle, RespawnDelegate, FMath::Max(RespawnDelay, 0.01f), false);
}

void AGenGameMode::RespawnPlayer(TWeakObjectPtr<AController> Controller)
{
	if (!Controller.IsValid())
	{
		return;
	}

	if (APawn* OldPawn = Controller->GetPawn())
	{
		Controller->UnPossess();
		OldPawn->Destroy();
	}

	RestartPlayer(Controller.Get());
}
