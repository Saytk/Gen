#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenIndicatorRules.h"
#include "Components/SceneComponent.h"
#include "GenSpellIndicatorComponent.generated.h"

class AGenCharacterBase;
class UGenGameplayAbility;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Emplacements de UGenSpellIndicatorComponent, un par rôle : le matériau d'un emplacement ne change jamais
 * (aucun MID recréé quand on passe d'un sort à l'autre).
 */
enum class EGenIndicatorSlot : uint8
{
	Arc,
	Lane,
	Cap,
	Spokes,
	Target,
	Stub0,
	Stub1,
	Stub2,
	Stub3,
	Self,
	Count
};

/** Un plan au sol réservé par UGenSpellIndicatorComponent (créé au premier usage, détruit avec le composant). */
USTRUCT()
struct FGenIndicatorPart
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	/** FillAlpha de l'instance parente (E2), rétabli quand la relation n'est plus ennemie. */
	float DefaultFillAlpha = 0.f;

	/** Derniers paramètres envoyés au MID : rien n'est renvoyé s'ils n'ont pas changé (EnemyPattern et FillAlpha suivent RelationIndex). */
	float SizeX = -1.f;
	float SizeY = -1.f;
	float RelationIndex = -1.f;
	float Fill = -1.f;

	/** Taille affichée (tests PIE), -1 = cachée. */
	float ShownSize = -1.f;
	float ShownLength = -1.f;
};

/**
 * Indicateurs au sol d'un personnage (Curffe-Visuals.md §1.2), sur M_VFX_Telegraph :
 * - visée du lanceur (couloir, éclat, arc de portée, atterrissage, amorces) : client propriétaire seulement ;
 * - télégraphe centré sur le lanceur (Combustion, Living Flame) : tous les clients, couleur du point de vue ;
 * - bond en vol (décision du 2026-10-08) : cercle d'atterrissage et amorces de l'anneau, tous les clients, couleur du
 *   point de vue, depuis le point répliqué (AGenCharacterBase::GetLeapTarget). Il reprend les emplacements Target et
 *   Stub de la visée : la visée du bond se ferme au lancer, le vol commence au départ, et rien ne vise pendant le vol.
 * Plans posés à plat, un emplacement fixe par rôle (réservé une fois), aucun sur un serveur dédié. Les tailles
 * sautent aux seuils (jamais d'interpolation : Art Bible §7.5, ne jamais exagérer). Aucune couleur ici : le rôle
 * (RelationIndex) choisit la couleur dans MPC_TeamColours, côté matériau.
 */
UCLASS(ClassGroup = (Gen), meta = (BlueprintSpawnableComponent))
class GEN_API UGenSpellIndicatorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UGenSpellIndicatorComponent();

	/** Client propriétaire : Ability fournit sa géométrie chaque image jusqu'à EndAim. Ignoré ailleurs. */
	void BeginAim(const UGenGameplayAbility* Ability);

	/** Ferme la visée d'Ability (nullptr : quelle qu'elle soit). Sans effet si une autre visée l'a remplacée. */
	void EndAim(const UGenGameplayAbility* Ability);

	UFUNCTION(BlueprintPure, Category = "Gen|Indicator")
	bool IsAiming() const { return AimAbility.IsValid(); }

	/**
	 * Taille affichée d'une partie (tests PIE) : "Lane" (largeur), "LaneLength", "Cap", "Spokes", "Target", "Arc",
	 * "Self" (rayons), "Stubs" (nombre). -1 si cachée.
	 */
	UFUNCTION(BlueprintPure, Category = "Gen|Indicator")
	float GetShownSize(FName Part) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_DEV_AUTOMATION_TESTS
	/** Tests : un même matériau pour toutes les parties (les instances MI_Telegraph_* ne sont assignées que dans BP_Champion). */
	void SetAllMaterialsForTests(UMaterialInterface* Material);
#endif

protected:
	virtual void BeginPlay() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Instances de M_VFX_Telegraph (Task E2) : MI_Telegraph_Disc, _Lane, _Arc, _Stub, _Spokes. Vide = partie jamais affichée. */
	UPROPERTY(EditDefaultsOnly, Category = "Indicator")
	TObjectPtr<UMaterialInterface> DiscMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Indicator")
	TObjectPtr<UMaterialInterface> LaneMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Indicator")
	TObjectPtr<UMaterialInterface> ArcMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Indicator")
	TObjectPtr<UMaterialInterface> StubMaterial;

	/** ⚑ F8 : laisser vide tant que l'Art Bible n'a pas accepté la forme. */
	UPROPERTY(EditDefaultsOnly, Category = "Indicator")
	TObjectPtr<UMaterialInterface> SpokesMaterial;

	/** Plan de 100 cm, face vers +Z (par défaut /Engine/BasicShapes/Plane). */
	UPROPERTY(EditDefaultsOnly, Category = "Indicator")
	TObjectPtr<UStaticMesh> PlaneMesh;

	/** Hauteur au-dessus du sol (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Indicator", meta = (Units = "cm"))
	float FloorOffset = 2.f;

	/** Opacité du remplissage d'un télégraphe ennemi (Art Bible §7.5 : motif + remplissage plus léger). */
	UPROPERTY(EditDefaultsOnly, Category = "Indicator", meta = (ClampMin = "0", ClampMax = "1"))
	float EnemyFillAlpha = 0.15f;

private:
	/**
	 * Montre l'emplacement Slot : plan centré en Centre, orienté selon Yaw, demi-tailles en cm. bUnitSize : disques et
	 * arcs (SizeX/SizeY = 1, comme AGenGroundArea), sinon SizeX/SizeY = demi-tailles en cm (couloir, amorce).
	 */
	void ShowPart(EGenIndicatorSlot Slot, UMaterialInterface* Material, const FVector& Centre, float Yaw, float HalfX, float HalfY,
		EGenViewerRelation Relation, float Fill, float ShownSize, bool bUnitSize, float ShownLength = -1.f);

	/** Cache les emplacements qui n'ont pas été montrés cette image. */
	void HideUnshownParts();

	void DrawAim(const AGenCharacterBase& Caster);
	void DrawSelfTelegraph(const AGenCharacterBase& Caster);
	void DrawLeapTarget(const AGenCharacterBase& Caster);

	/** Cercle cible et amorces de G (visée ou vol), dans la couleur de Relation. */
	void ShowTargetAndStubs(const FGenAimGeometry& G, float Z, EGenViewerRelation Relation, float Fill);

	float GetFloorZ(const AGenCharacterBase& Caster) const;
	float GetFloorZ(const AGenCharacterBase& Caster, float CapsuleCentreZ) const;
	EGenViewerRelation GetLocalRelation(const AGenCharacterBase& Caster) const;

	UPROPERTY(Transient)
	TArray<FGenIndicatorPart> Parts;

	TWeakObjectPtr<const UGenGameplayAbility> AimAbility;

	/** Emplacements montrés cette image (bit = EGenIndicatorSlot). */
	uint32 ShownThisFrame = 0;

	/** Emplacements actuellement visibles (bit = EGenIndicatorSlot) : rien à cacher quand il est nul. */
	uint32 VisibleParts = 0;

	bool bDisabled = false;
};
