#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GenInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

USTRUCT(BlueprintType)
struct FGenAbilityInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};

/**
 * Configuration des contrôles : mapping context, action de déplacement,
 * et association InputAction -> InputTag pour les sorts.
 */
UCLASS(BlueprintType, Const)
class GEN_API UGenInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	/** Action Axis2D (ZQSD). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<const UInputAction> MoveAction;

	/** Touche d'annulation : annule l'incantation en cours, nourrissage compris (guidelines §3.1). Python : cancel_action. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<const UInputAction> CancelAction;

	/**
	 * Optionnel : maintenir affiche l'infobulle de tous les sorts de la barre (UGenAbilityBar). Touche posée dans le
	 * mapping context (éditeur). Python : show_tooltips_action.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<const UInputAction> ShowTooltipsAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (TitleProperty = "InputTag"))
	TArray<FGenAbilityInputAction> AbilityInputActions;
};
