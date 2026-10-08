#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "GenPlayerController.generated.h"

class UGenAbilitySystemComponent;
class UGenInputConfig;
struct FInputActionValue;

/**
 * Contrôleur joueur : curseur visible, déplacement ZQSD relatif à la caméra,
 * et transmission des touches de sorts à l'ASC sous forme d'InputTags.
 */
UCLASS()
class GEN_API AGenPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AGenPlayerController();

	/** Point du monde sous la souris, projeté sur le plan horizontal Z = PlaneZ. */
	UFUNCTION(BlueprintCallable, Category = "Gen|Input")
	bool GetCursorLocationOnPlane(float PlaneZ, FVector& OutLocation) const;

	const UGenInputConfig* GetInputConfig() const { return InputConfig; }

	/** Tests PIE : viser DebugAimLocation au lieu du curseur (ignoré en Shipping). */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Gen|Debug")
	bool bDebugAimOverride = false;

	UPROPERTY(Transient, BlueprintReadWrite, Category = "Gen|Debug")
	FVector DebugAimLocation = FVector::ZeroVector;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	virtual void PlayerTick(float DeltaTime) override;

	/**
	 * La rotation de contrôle suit le curseur. Elle part au serveur avec chaque mouvement,
	 * ce qui permet au personnage de faire face à la visée pendant une incantation.
	 */
	void UpdateAimRotation();

	void Move(const FInputActionValue& Value);
	void AbilityInputPressed(FGameplayTag InputTag);
	void AbilityInputReleased(FGameplayTag InputTag);

	UGenAbilitySystemComponent* GetGenAbilitySystemComponent() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Input")
	TObjectPtr<UGenInputConfig> InputConfig;
};
