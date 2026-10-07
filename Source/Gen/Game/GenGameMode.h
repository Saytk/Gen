#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GenGameMode.generated.h"

class AGenCharacterBase;

/**
 * Mode de jeu arène 3v3 (serveur uniquement).
 * - Répartit les joueurs dans l'équipe la moins remplie.
 * - Fait apparaître chaque équipe sur les PlayerStart tagués "Team0" / "Team1".
 * - Respawn après un délai (à remplacer plus tard par un système de rounds).
 */
UCLASS()
class GEN_API AGenGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGenGameMode();

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Appelé par un personnage qui vient de mourir. */
	virtual void OnCharacterDied(AGenCharacterBase* Victim, AActor* Killer);

protected:
	uint8 PickTeamForNewPlayer() const;
	void RespawnPlayer(TWeakObjectPtr<AController> Controller);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Teams", meta = (ClampMin = "1", ClampMax = "254"))
	int32 NumTeams = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Teams", meta = (ClampMin = "1"))
	int32 MaxPlayersPerTeam = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Respawn", meta = (ClampMin = "0.0"))
	float RespawnDelay = 5.f;
};
