#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenNetTestFloor.generated.h"

class UStaticMeshComponent;

/**
 * Sol plat de test (100 x 100 m, surface à Z = 0) pour les Gen.Net.* : la carte vide n'en a pas.
 * Créé par le serveur seul et répliqué (toujours pertinent) : les clients le reçoivent avec un NetGUID, donc les
 * corrections de mouvement qui le désignent comme base se résolvent (un sol posé localement par chaque client ne
 * se résout pas). Tout est réglé dans le constructeur : un client le construit à l'identique.
 */
UCLASS(NotBlueprintable, HideDropdown)
class AGenNetTestFloor : public AActor
{
	GENERATED_BODY()

public:
	AGenNetTestFloor();

	/** Hauteur de la surface. */
	static constexpr float TopZ = 0.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Floor")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
