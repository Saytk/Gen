#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Game/GenDevTuning.h"
#include "GenPlayerController.generated.h"

class UGenAbilitySystemComponent;
class UGenInputConfig;
struct FInputActionValue;

/** Touche « détails des sorts » maintenue (vrai) ou relâchée (faux) : l'interface affiche toutes les infobulles. */
DECLARE_MULTICAST_DELEGATE_OneParam(FGenOnShowAbilityDetailsChanged, bool /*bShown*/);

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

	/** Touche « détails des sorts » maintenue (UGenInputConfig::ShowTooltipsAction). */
	bool IsShowingAbilityDetails() const { return bShowingAbilityDetails; }

	/** Diffusé quand la touche « détails des sorts » change ; l'interface s'y abonne (le jeu ne connaît aucun widget). */
	FGenOnShowAbilityDetailsChanged OnShowAbilityDetailsChanged;

	/** Tests PIE : viser DebugAimLocation au lieu du curseur (ignoré en Shipping). */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Gen|Debug")
	bool bDebugAimOverride = false;

	UPROPERTY(Transient, BlueprintReadWrite, Category = "Gen|Debug")
	FVector DebugAimLocation = FVector::ZeroVector;

	/** Panneau développeur (F10) : nouveaux réglages, bornés puis répliqués par le serveur. Sans effet en Shipping. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Gen|Dev")
	void ServerSetDevTuning(const FGenDevTuning& Tuning);

	/** Panneau développeur : vie, énergie, ressource au maximum et recharges effacées pour tous. Sans effet en Shipping. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Gen|Dev")
	void ServerDevRefillAll();

	/** Ouvre ou ferme le panneau développeur (touche F10, hors Shipping). */
	void ToggleDevPanel();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	virtual void PlayerTick(float DeltaTime) override;

	/**
	 * Client propriétaire : la possession du pion est confirmée. Si son ASC est déjà initialisé (OnRep_PlayerState
	 * passé avant que le contrôleur soit connu), on prévient l'interface ici : la barre de sorts ne reste pas vide.
	 */
	virtual void AcknowledgePossession(APawn* P) override;

	/** Le joueur local change de PlayerState : les relations par point de vue des corps (soi, allié, ennemi) sont recalculées. */
	virtual void OnRep_PlayerState() override;

	/**
	 * La rotation de contrôle suit le curseur. Elle part au serveur avec chaque mouvement,
	 * ce qui permet au personnage de faire face à la visée pendant une incantation.
	 */
	void UpdateAimRotation();

	void Move(const FInputActionValue& Value);
	void AbilityInputPressed(FGameplayTag InputTag);
	void AbilityInputReleased(FGameplayTag InputTag);
	/** Touche d'annulation : annule l'incantation en cours (UGenAbilitySystemComponent::CancelPendingCasts). */
	void CancelCast();
	/** Touche « détails des sorts » : appui et relâché. */
	void ShowAbilityDetailsPressed();
	void ShowAbilityDetailsReleased();
	void SetShowAbilityDetails(bool bShown);

	UGenAbilitySystemComponent* GetGenAbilitySystemComponent() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gen|Input")
	TObjectPtr<UGenInputConfig> InputConfig;

private:
	bool bShowingAbilityDetails = false;
};
