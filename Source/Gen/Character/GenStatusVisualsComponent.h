#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/TimerHandle.h"
#include "GameplayTagContainer.h"
#include "GenStatusVisualsComponent.generated.h"

class UAbilitySystemComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Forme provisoire affichée tant qu'un état (tag) est actif. Art Bible §7.6 : une forme unique par état,
 * la même pour tous, jamais une simple teinte ; la coque est réservée à State.Shielded.
 */
USTRUCT(BlueprintType)
struct FGenStatusVisual
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status", meta = (Categories = "State"))
	FGameplayTag Tag;

	/** Forme dessinée. Vide = aucune (ex : un état qui ne fait que changer le matériau du corps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	TObjectPtr<UStaticMesh> Mesh;

	/** Ex : M_VFX_StatusShape (lit Colour, Opacity, RimOnly, RimPower et Flash), ou M_ST_FireOrb tel quel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	FLinearColor Colour = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.5f;

	/** Seulement le contour de la forme (Fresnel) : une bande ou un halo plutôt qu'un volume plein. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	bool bRimOnly = false;

	/** Finesse du contour (exposant du Fresnel) quand bRimOnly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status", meta = (ClampMin = "0.5", EditCondition = "bRimOnly"))
	float RimPower = 3.f;

	/** Flash à l'apparition : paramètre Flash du matériau à 1 pendant cette durée. 0 = aucun (ex : anneau blanc de la résilience). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status", meta = (ClampMin = "0.0", Units = "s"))
	float AppearFlashDuration = 0.f;

	/** Position par rapport au centre du personnage (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	FVector Scale = FVector::OneVector;

	/**
	 * Matériau imposé à toutes les sections du corps tant que l'état est actif, puis rendu. Ex : intouchable,
	 * corps tramé « fantôme » (masqué, sans translucidité) ; le contour et l'anneau d'équipe restent (Art Bible §7.6).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	TObjectPtr<UMaterialInterface> OwnerMeshMaterial;
};

/**
 * Formes d'état du personnage (contre, étourdi, intouchable...) : suit les tags de l'ASC, répliqués
 * à tous les clients. Purement cosmétique : rien sur un serveur dédié. À remplacer par la bibliothèque
 * de GameplayCues d'états quand elle existera.
 */
UCLASS(ClassGroup = (Gen), meta = (BlueprintSpawnableComponent))
class GEN_API UGenStatusVisualsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UGenStatusVisualsComponent();

	/** (Re)branche sur un ASC ; rappelable (respawn, OnRep_PlayerState multiples). */
	void Bind(UAbilitySystemComponent* InASC, const TArray<FGenStatusVisual>& InVisuals);
	void Unbind();

	/** Forme de l'état Tag affichée (lu par les tests et le PIE). */
	UFUNCTION(BlueprintPure, Category = "Gen|Status")
	bool IsStatusShown(FGameplayTag Tag) const;

	/** Flash d'apparition de l'état Tag en cours (lu par les tests et le PIE). */
	UFUNCTION(BlueprintPure, Category = "Gen|Status")
	bool IsStatusFlashing(FGameplayTag Tag) const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnTagChanged(const FGameplayTag Tag, int32 NewCount);
	void SetShown(int32 Index, bool bShown);
	void SetFlash(int32 Index, bool bFlash);
	void RefreshOwnerMesh();

	TArray<FGenStatusVisual> Visuals;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Shapes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ShapeMIDs;

	/** Matériaux d'origine du corps, gardés tant qu'un OwnerMeshMaterial est imposé. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> OriginalOwnerMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> AppliedOwnerMaterial;

	TArray<bool> Shown;
	TArray<bool> Flashing;
	TArray<FTimerHandle> FlashTimers;
	TArray<FDelegateHandle> TagHandles;
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
};
