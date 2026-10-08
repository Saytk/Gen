#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GenAreaRules.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "GenGroundArea.generated.h"

class AGenCharacterBase;
class AGenPlayerController;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;
class UStaticMeshComponent;

/** Réglages d'une zone, fixés par le sort avant FinishSpawning (serveur). */
struct FGenAreaParams
{
	/** Rayon (cm) : celui du télégraphe ET de la zone touchée (guidelines : le dessin = la hitbox). */
	float Radius = 200.f;
	/** Délai avant l'impact (s). 0 = impact immédiat, sans télégraphe. */
	float Delay = 0.f;
	/** Étourdissement infligé (s). 0 = aucun. */
	float StunDuration = 0.f;
	/** Repoussement depuis le centre (cm). 0 = aucun. */
	float KnockbackDistance = 0.f;
};

/**
 * Zone au sol (A) : télégraphe visible par tous pendant Delay, puis impact sur les ennemis vivants du
 * lanceur dans le rayon, en ligne de vue depuis le centre (pas à travers les murs). Ne déclenche pas les contres.
 * - Serveur : dégâts, étourdissement, repoussement, gains du lanceur s'il touche au moins un ennemi.
 *   L'équipe du lanceur est retenue à l'apparition : la zone reste juste s'il meurt avant l'impact.
 * - Clients : télégraphe aux couleurs du point de vue (soi, allié, ennemi), puis effet d'impact.
 * - Aperçu : un exemplaire local, non répliqué, suit le curseur du lanceur pendant l'incantation.
 */
UCLASS()
class GEN_API AGenGroundArea : public AActor
{
	GENERATED_BODY()

public:
	AGenGroundArea();

	/** Serveur, avant FinishSpawning. */
	void InitializeArea(const FGenAreaParams& Params, uint8 InSourceTeam);

	/** Machine du lanceur, avant FinishSpawning : aperçu local qui suit le curseur (rayon selon le nourrissage). */
	void StartPreview(AGenPlayerController* InController, float InRange, float InBaseRadius, float InRadiusAtMaxFeed, int32 InMaxFeed);

	/** Rempli par le sort avant FinishSpawning (serveur uniquement, non répliqué). */
	FGameplayEffectSpecHandle DamageEffectSpecHandle;

	/** Gains du lanceur, appliqués si la zone touche au moins un ennemi (serveur). */
	FGameplayEffectSpecHandle InstigatorOnHitSpecHandle;

	UFUNCTION(BlueprintPure, Category = "Gen|Area")
	float GetRadius() const { return Radius; }

	UFUNCTION(BlueprintPure, Category = "Gen|Area")
	bool HasDetonated() const { return bDetonated; }

	/** Équipe du lanceur retenue à l'apparition (GenNoTeam si aucune). */
	uint8 GetSourceTeam() const { return SourceTeam; }

	/** Exemplaire local d'aperçu (jamais répliqué, n'inflige rien). */
	bool IsPreview() const { return bIsPreview; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	/** Serveur : impact. */
	void Detonate();

	bool IsValidTarget(const AGenCharacterBase* Character) const;
	void ApplyHit(AGenCharacterBase* Target);

	UFUNCTION()
	void OnRep_Detonated();

	/** Cache le télégraphe, joue l'effet et le son d'impact (toutes les machines sauf serveur dédié). */
	void PlayImpactEffects();

	void SetTelegraphRadius(float InRadius);
	void UpdateTelegraphFill();
	void UpdatePreview();
	EGenViewerRelation GetLocalViewerRelation() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Gen|Area", meta = (DisplayName = "On Impact"))
	void K2_OnImpact();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> AreaRoot;

	/** Plan (100 x 100 cm) mis à l'échelle du rayon, matériau M_VFX_Telegraph. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TelegraphMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Telegraph")
	TObjectPtr<UMaterialInterface> TelegraphMaterial;

	/** Remplissage, soi et alliés (UI §4.12 : 0.15–0.25). */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Telegraph", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FillAlpha = 0.2f;

	/** Remplissage vu par un ennemi (avec les hachures fixes du motif ennemi). */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Telegraph", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnemyFillAlpha = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Telegraph", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BorderAlpha = 0.9f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Impact")
	TObjectPtr<UNiagaraSystem> ImpactFX;

	/** Rayon pour lequel ImpactFX est dessiné à l'échelle 1 (il suit le rayon réel). */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Impact", meta = (ClampMin = "1.0", Units = "cm"))
	float ImpactFXReferenceRadius = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Area|Impact")
	TObjectPtr<USoundBase> ImpactSound;

	/** Durée de vie après l'impact (le temps que bDetonated soit répliqué et que l'effet se joue). */
	UPROPERTY(EditDefaultsOnly, Category = "Area|Impact", meta = (ClampMin = "0.1", Units = "s"))
	float LingerAfterImpact = 1.f;

	UPROPERTY(Replicated)
	float Radius = 200.f;

	UPROPERTY(Replicated)
	float Delay = 0.f;

	/** Apparition, en temps serveur (GameState) : sert au remplissage du télégraphe chez les clients. */
	UPROPERTY(Replicated)
	float StartServerTime = 0.f;

	/** Équipe du lanceur à l'apparition. */
	UPROPERTY(Replicated)
	uint8 SourceTeam = 255;

	UPROPERTY(ReplicatedUsing = OnRep_Detonated)
	bool bDetonated = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TelegraphMID;

	/** Serveur uniquement. */
	float StunDuration = 0.f;
	float KnockbackDistance = 0.f;

private:
	bool bIsPreview = false;
	TWeakObjectPtr<AGenPlayerController> PreviewController;
	float PreviewRange = 0.f;
	float PreviewBaseRadius = 0.f;
	float PreviewRadiusAtMaxFeed = 0.f;
	int32 PreviewMaxFeed = 0;
	bool bImpactEffectsPlayed = false;
};
