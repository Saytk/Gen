#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "CurffeHearthComponent.generated.h"

class AGenCharacterBase;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UNiagaraSystem;
class UStaticMesh;

/**
 * Le Foyer de Curffe (Curffe-Visuals.md §5, Plan Visuals V8) : ses flammes (attribut Resource) tournent autour de lui,
 * visibles par tous.
 * - Toujours 5 emplacements fixes à 72° : un emplacement allumé = une flamme (floor(Resource) − nourries), les vides sont
 *   des braises sombres (0 flamme = cinq braises).
 * - À chaque seuil de nourrissage (AGenCharacterBase::OnFedThresholdReached), la plus haute flamme s'éteint et vole
 *   jusqu'au socket du sort (main, pieds) ; annulé ou interrompu, elle revient rallumer son emplacement ; lancé, rien ne vole.
 * - Un seul UInstancedStaticMeshComponent (5 emplacements + 3 vols) : un appel de dessin par Curffe. Contrat des assets
 *   (SM_Hearth_Flame, carte de 8 × 16 cm face à +X, haut +Z ; MI_Hearth_Flame) : NumCustomDataFloats = 2,
 *   [0] Lit 0..1 (1 flamme, 0 braise dessinée dans la même carte ; monte de 0 à 1 en RegenFadeDuration pour un gain
 *   discret), [1] Pop 0..1 (1 -> 0 en 2 images sur un gain). Échelle 1 = flamme, AblazeScale embrasé, jamais réduite
 *   pour une braise. Tournée vers la caméra locale par MakeFromXZ(−avant de la caméra, haut). Le scintillement est dans
 *   le matériau.
 * - Sans HearthFlameMesh / HearthFlameMaterial (assets pas encore assignés) : repli sur l'ancien rendu (FlameMesh,
 *   FlameMaterial, sphères), emplacements vides cachés.
 * Purement cosmétique : rien sur un serveur dédié.
 */
UCLASS(ClassGroup = (Gen), meta = (BlueprintSpawnableComponent))
class GEN_API UCurffeHearthComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCurffeHearthComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Emplacements allumés = floor(Resource) − unités nourries, bornés aux emplacements (lu par les tests PIE). */
	UFUNCTION(BlueprintPure, Category = "Curffe|Hearth")
	int32 GetVisibleFlameCount() const { return VisibleFlames; }

	/** Flammes en vol (vers le sort ou de retour au Foyer). */
	UFUNCTION(BlueprintPure, Category = "Curffe|Hearth")
	int32 GetFlyingFlameCount() const;

	/** Vols commencés depuis l'apparition, vers le sort et de retour (tests : un vol ne dure que FlightDuration). */
	int32 GetStartedFlightCount() const { return StartedFlights; }
	int32 GetStartedReturnFlightCount() const { return StartedReturnFlights; }

	/** Instances dessinées (5 emplacements + 3 vols), 0 sur un serveur dédié. */
	int32 GetInstanceCount() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Flamme du Foyer : SM_Hearth_Flame (carte face à +X), tournée vers la caméra locale. Vide = repli sur FlameMesh. */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	TObjectPtr<UStaticMesh> HearthFlameMesh;

	/** Matériau des flammes (MI_Hearth_Flame : lit PerInstanceCustomData 0..1). Vide = repli sur FlameMaterial. */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	TObjectPtr<UMaterialInterface> HearthFlameMaterial;

	/** Repli (rendu du Plan 1) : maillage et matériau d'avant, sans braises. */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Fallback")
	TObjectPtr<UStaticMesh> FlameMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Fallback")
	TObjectPtr<UMaterialInterface> FlameMaterial;

	/** Échelle d'une flamme du repli (sphère de 100 cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Fallback")
	float FlameScale = 0.18f;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth", meta = (Units = "cm"))
	float OrbitRadius = 70.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth", meta = (Units = "cm"))
	float OrbitHeight = 30.f;

	/** Curffe-Visuals.md §5 : plus lent que l'ancien 120°/s, pour compter d'un coup d'œil. */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	float OrbitSpeedDegrees = 60.f;

	/** Échelle de la carte (1 = flamme de 16 cm, ≈ 19 px à 1080p ; la braise est dans la même carte). */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth")
	float FlameCardScale = 1.f;

	/** Gain discret (une flamme, régénération) : Lit monte de 0 à 1 sur cette durée au lieu d'un éclat. */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth", meta = (Units = "s"))
	float RegenFadeDuration = 0.2f;

	/** Durée d'un vol (vers le sort ou de retour). */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Flight", meta = (Units = "s"))
	float FlightDuration = 0.12f;

	/** Hauteur de l'arc d'un vol. */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Flight", meta = (Units = "cm"))
	float FlightArcHeight = 20.f;

	/**
	 * Vol vers le sort / de retour en Niagara (NS_Curffe_FeedFly, NS_Curffe_FeedReturn ; aucun paramètre utilisateur,
	 * VibeUE #464). Contrat : attaché au socket du sort (main, pieds), tourné par MakeFromXZ(main − emplacement, haut)
	 * (axe local −X de la main vers l'emplacement du Foyer), échelle uniforme distance / 100. FeedFly va de (−100, 0, 0)
	 * à l'origine (arrive dans la main), FeedReturn de l'origine vers −X. Assigné : il remplace l'instance de vol du
	 * Foyer. Vide = la flamme vole en instance (repli).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Flight")
	TObjectPtr<UNiagaraSystem> FeedFlySystem;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Flight")
	TObjectPtr<UNiagaraSystem> FeedReturnSystem;

	/** Autres joueurs : délai pour savoir si une baisse du compte nourri est un lancer (Resource répliquée à part). */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Flight", meta = (Units = "s"))
	float SpentGrace = 0.25f;

	/** Recharge de 3 flammes ou plus d'un coup (Living Flame) : les emplacements s'allument un par un. */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth", meta = (Units = "s"))
	float RefillStagger = 0.05f;

	/** Embrasé (State.Curffe.Ablaze, Plan 3 Task 9) : flammes plus grandes, orbite plus rapide. */
	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Ablaze")
	float AblazeScale = 1.4f;

	UPROPERTY(EditDefaultsOnly, Category = "Hearth|Ablaze")
	float AblazeOrbitSpeedScale = 1.5f;

private:
	struct FFlight
	{
		bool bActive = false;
		/** Vers le Foyer (annulation) : rallume Socket à l'arrivée. */
		bool bReturning = false;
		int32 Socket = INDEX_NONE;
		double StartTime = 0.0;
		/** Socket du mesh du sort (main, pieds), retenu au départ (l'incantation peut être finie au retour). */
		FName SpellSocket;
		/** Dessiné par un système Niagara (FeedFlySystem, FeedReturnSystem) : pas d'instance de vol. */
		bool bNiagaraVisual = false;
	};

	/** Baisse du compte nourri pas encore tranchée (autres joueurs : la Resource répliquée peut arriver après). */
	struct FPendingDrop
	{
		bool bActive = false;
		int32 FirstFedIndex = 0;
		int32 Count = 0;
		int32 FlamesWhileFeeding = 0;
		double Deadline = 0.0;
	};

	void OnFedResourceChanged(AGenCharacterBase* Character, int32 Old, int32 New);
	void OnFedThresholdReached(AGenCharacterBase* Character, int32 NewCount);

	/** Les unités FirstFedIndex .. FirstFedIndex + Count − 1 reviennent au Foyer. */
	void StartReturnFlights(int32 FirstFedIndex, int32 Count, int32 FeedingFlames);
	void StartFlight(int32 Socket, bool bReturning, FName SpellSocket);

	/** Emplacements tenus éteints par une baisse en attente (bits). */
	uint8 GetPendingDropSockets() const;

	FVector GetFlameSocketLocation(int32 Socket) const;
	FVector GetSpellLocation(FName SpellSocket) const;
	int32 GetFlamesNow() const;
	bool UsesFallback() const;
	double GetNow() const;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Flames;

	TWeakObjectPtr<AGenCharacterBase> OwnerCharacter;
	FDelegateHandle FedChangedHandle;
	FDelegateHandle ThresholdHandle;
	FGameplayTag AblazeTag;

	FFlight Flights[3];
	FPendingDrop PendingDrop;
	/** Baisse totale du compte nourri en attente (comparée à la baisse de ressource d'un lancer). */
	int32 DropAmount = 0;
	/** Socket du sort du dernier nourrissage (cible des vols de retour). */
	FName LastSpellSocket;

	/** Images d'éclat restantes par emplacement (2 : Pop 1, 1 : Pop 0.5, 0 : aucun). */
	uint8 PopFramesLeft[5] = { 0, 0, 0, 0, 0 };
	/** Début de la montée de Lit d'un gain discret, par emplacement (< 0 = aucune). */
	double FadeStart[5] = { -1.0, -1.0, -1.0, -1.0, -1.0 };
	/** Emplacements où une flamme de retour vient d'arriver (bits, pour l'image en cours). */
	uint8 ArrivedSockets = 0;
	/** Emplacements allumés à l'écran l'image précédente (bit = emplacement). */
	uint8 ShownLitSockets = 0;
	/** Recharge échelonnée : plafond d'emplacements allumés (INDEX_NONE = aucun) et prochain allumage. */
	int32 RefillCap = INDEX_NONE;
	double NextRefillTime = 0.0;

	/** Unités nourries déjà parties du Foyer (vols lancés), et flammes pendant le nourrissage. */
	int32 FlownCount = 0;
	int32 FlamesWhileFeeding = 0;

	/** Dernières données envoyées par instance (rien n'est renvoyé si elles n'ont pas changé). */
	TArray<float> SentCustomData;

	float OrbitAngle = 0.f;
	int32 VisibleFlames = 0;
	int32 StartedFlights = 0;
	int32 StartedReturnFlights = 0;
};
