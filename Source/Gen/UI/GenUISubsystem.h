#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GenUISubsystem.generated.h"

class UAbilitySystemComponent;
class UGenUIKeyGlyphs;
class UGenUIMetrics;
class UGenUIPalette;
class UInputAction;
class UTexture2D;

DECLARE_MULTICAST_DELEGATE_OneParam(FGenOnAbilitySystemReady, UAbilitySystemComponent*);

/**
 * Service d'interface par joueur local (UI_Guidelines §8.3) : jetons, libellés de touches,
 * et l'événement "ASC prêt" auquel les widgets s'abonnent (§8.2, §8.4).
 * Le code de gameplay n'inclut jamais d'en-tête UMG : il appelle seulement NotifyAbilitySystemReady.
 */
UCLASS()
class GEN_API UGenUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UGenUISubsystem* Get(const UObject* WorldContextOrLocalPlayerOwner);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const UGenUIPalette* GetPalette() const;
	const UGenUIMetrics* GetMetrics() const;

	/** Libellé actuel de l'action (mapping Enhanced Input actif) : glyphe si disponible, sinon texte court. */
	bool ResolveKeyLabel(const UInputAction* Action, FText& OutText, UTexture2D*& OutGlyph) const;

	/**
	 * Appelé quand l'ASC du pion local est initialisé : par le pion (OnRep_PlayerState / PossessedBy) et par le
	 * contrôleur (AcknowledgePossession), le premier des deux qui le voit prêt. Une seconde annonce pour le même
	 * ASC et le même avatar est ignorée ; un nouvel avatar (respawn) est diffusé.
	 */
	void NotifyAbilitySystemReady(UAbilitySystemComponent* ASC);

	/** Appelle tout de suite si l'ASC est déjà prêt, puis à chaque (ré)initialisation. */
	FDelegateHandle CallOrRegister_OnAbilitySystemReady(FGenOnAbilitySystemReady::FDelegate&& Delegate);
	void UnregisterOnAbilitySystemReady(FDelegateHandle Handle);

	UAbilitySystemComponent* GetAbilitySystem() const { return AbilitySystem.Get(); }

private:
	UPROPERTY(Transient) TObjectPtr<const UGenUIPalette> Palette;
	UPROPERTY(Transient) TObjectPtr<const UGenUIMetrics> Metrics;
	UPROPERTY(Transient) TObjectPtr<const UGenUIKeyGlyphs> KeyGlyphs;

	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	/** Avatar de l'ASC lors de la dernière annonce (dédoublonnage des deux chemins). */
	TWeakObjectPtr<AActor> AbilitySystemAvatar;
	FGenOnAbilitySystemReady OnAbilitySystemReady;
};
