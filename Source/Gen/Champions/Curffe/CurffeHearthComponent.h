#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CurffeHearthComponent.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Le Foyer de Curffe : ses flammes (attribut Resource) tournent autour de lui, visibles par tous.
 * Les flammes en cours de nourrissage (GetFedResource) quittent l'orbite.
 * Purement cosmétique : rien sur un serveur dédié.
 */
UCLASS(ClassGroup = (Gen), meta = (BlueprintSpawnableComponent))
class GEN_API UCurffeHearthComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCurffeHearthComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Flammes affichées dans l'orbite (lu par les tests PIE). */
	UFUNCTION(BlueprintPure, Category = "Curffe|Hearth")
	int32 GetVisibleFlameCount() const { return VisibleFlames; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	TObjectPtr<UStaticMesh> FlameMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	TObjectPtr<UMaterialInterface> FlameMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth", meta = (Units = "cm"))
	float OrbitRadius = 70.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth", meta = (Units = "cm"))
	float OrbitHeight = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	float OrbitSpeedDegrees = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	float FlameScale = 0.18f;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FlameComponents;

	float OrbitAngle = 0.f;
	int32 VisibleFlames = 0;
};
