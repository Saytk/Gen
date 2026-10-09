#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "GenDevTuning.generated.h"

class AGenCharacterBase;

/**
 * Réglages développeur (panneau F10, hors Shipping). Les valeurs par défaut sont le jeu normal.
 * Le serveur fait autorité : il reçoit les réglages d'un client (AGenPlayerController::ServerSetDevTuning), les borne,
 * puis les réplique à tous ; client et serveur lisent donc les mêmes valeurs (recharges et incantations prédites).
 */
USTRUCT(BlueprintType)
struct GEN_API FGenDevTuning
{
	GENERATED_BODY()

	/** Aucune recharge n'est appliquée. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dev")
	bool bNoCooldowns = false;

	/** Multiplie la durée des recharges (1 = normal, 0 = aucune). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dev")
	float CooldownScale = 1.f;

	/** Multiplie le temps d'incantation et l'intervalle de nourrissage (1 = normal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dev")
	float CastTimeScale = 1.f;

	/** Les sorts ne coûtent plus d'énergie et l'énergie des joueurs reste pleine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dev")
	bool bInfiniteEnergy = false;

	/** La ressource des joueurs (flammes de Curffe) reste pleine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dev")
	bool bInfiniteResource = false;

	/** Les joueurs ne perdent plus de vie (les mannequins, si). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dev")
	bool bInvulnerable = false;

	/** Vitesse du jeu (dilatation du temps du monde, 1 = normal) : pratique pour observer les effets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dev")
	float GameSpeed = 1.f;

	bool operator==(const FGenDevTuning& Other) const = default;

	/** Bornes appliquées par le serveur : un client ne peut pas envoyer n'importe quoi. */
	FGenDevTuning Clamped() const;
};

namespace GenDevTuning
{
	inline constexpr float MinCooldownScale = 0.f;
	inline constexpr float MaxCooldownScale = 3.f;
	inline constexpr float MinCastTimeScale = 0.1f;
	inline constexpr float MaxCastTimeScale = 5.f;
	inline constexpr float MinGameSpeed = 0.1f;
	inline constexpr float MaxGameSpeed = 2.f;

	/** Réglages actifs dans ce monde : ceux de l'acteur répliqué, ou les valeurs par défaut (aucun acteur, Shipping). */
	GEN_API const FGenDevTuning& Get(const UObject* WorldContext);

	/** Durée de recharge après réglages ; 0 = aucune recharge. */
	GEN_API float ScaleCooldown(const FGenDevTuning& Tuning, float Duration);
}

DECLARE_MULTICAST_DELEGATE(FGenOnDevTuningChanged);

/**
 * Porte les réglages développeur, répliqués à tous (toujours pertinent). Créé par AGenGameMode hors Shipping.
 * Serveur : applique la vitesse du jeu et garde pleines l'énergie et la ressource des joueurs quand c'est demandé.
 */
UCLASS(NotPlaceable, Transient)
class GEN_API AGenDevTuningActor : public AActor
{
	GENERATED_BODY()

public:
	AGenDevTuningActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	const FGenDevTuning& GetTuning() const { return Tuning; }

	/** Serveur : nouveaux réglages (bornés) ; la vitesse du jeu passe par le WorldSettings (répliqué). */
	void SetTuning(const FGenDevTuning& NewTuning);

	/** Serveur : vie, énergie et ressource au maximum et recharges effacées, pour tous les personnages vivants. */
	void RefillAll();

private:
	UPROPERTY(ReplicatedUsing = OnRep_Tuning)
	FGenDevTuning Tuning;

	UFUNCTION()
	void OnRep_Tuning();

	void OnTuningChanged();
	void RefillCharacter(AGenCharacterBase& Character, bool bHealth, bool bEnergy, bool bResource, bool bCooldowns) const;
};

/** Accès à l'acteur de réglages du monde et diffusion des changements (serveur et clients). */
UCLASS()
class GEN_API UGenDevTuningSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UGenDevTuningSubsystem* Get(const UObject* WorldContext);

	AGenDevTuningActor* GetActor() const { return Actor.Get(); }
	void Register(AGenDevTuningActor* InActor);
	void Unregister(AGenDevTuningActor* InActor);

	/** Diffusé à chaque changement de réglages reçu ou appliqué (le panneau s'y abonne). */
	FGenOnDevTuningChanged OnChanged;

private:
	TWeakObjectPtr<AGenDevTuningActor> Actor;
};
