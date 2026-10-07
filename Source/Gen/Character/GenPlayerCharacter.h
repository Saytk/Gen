#pragma once

#include "CoreMinimal.h"
#include "Character/GenCharacterBase.h"
#include "GenPlayerCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;

/**
 * Champion contrôlé par un joueur.
 *
 * - ASC + attributs sur le PlayerState (persistent entre deux vies).
 * - Caméra vue de dessus fixe façon Battlerite : ne tourne jamais, suit le joueur
 *   et se décale légèrement vers le curseur.
 */
UCLASS()
class GEN_API AGenPlayerCharacter : public AGenCharacterBase
{
	GENERATED_BODY()

public:
	AGenPlayerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ APawn
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void OnRep_PlayerState() override;

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;

	//~ AGenCharacterBase
	virtual uint8 GetTeamId() const override;

	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	UCameraComponent* GetTopDownCamera() const { return TopDownCamera; }

protected:
	void InitAbilitySystemFromPlayerState();
	void UpdateCameraCursorOffset(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> TopDownCamera;

	/** Part de la distance personnage→curseur appliquée à la caméra (0 = caméra centrée). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CameraCursorOffsetRatio = 0.15f;

	/** Décalage maximum de la caméra vers le curseur (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraCursorMaxOffset = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraCursorInterpSpeed = 6.f;
};
