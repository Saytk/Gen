#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/PlayerState.h"
#include "GenPlayerState.generated.h"

class UGenAbilitySystemComponent;
class UGenAttributeSet;

/**
 * PlayerState : porte l'ASC et les attributs du joueur (survivent à la mort du pawn)
 * ainsi que son équipe (3v3).
 */
UCLASS()
class GEN_API AGenPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AGenPlayerState();

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UGenAbilitySystemComponent* GetGenAbilitySystemComponent() const { return AbilitySystemComponent; }
	UGenAttributeSet* GetAttributeSet() const { return AttributeSet; }

	uint8 GetTeamId() const { return TeamId; }

	/** Serveur uniquement. */
	void SetTeamId(uint8 NewTeamId) { TeamId = NewTeamId; }

	UFUNCTION(BlueprintPure, Category = "Gen|Team", meta = (DisplayName = "Get Team Id"))
	int32 K2_GetTeamId() const { return TeamId; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen|Abilities")
	TObjectPtr<UGenAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UGenAttributeSet> AttributeSet;

	UPROPERTY(Replicated)
	uint8 TeamId = 255;
};
