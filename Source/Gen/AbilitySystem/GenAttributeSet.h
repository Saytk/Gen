#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GenAttributeSet.generated.h"

/** Diffusé (serveur uniquement) quand la vie tombe à 0. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FGenOutOfHealthEvent, AActor* /*DamageInstigator*/, AActor* /*DamageCauser*/);

/**
 * Attributs communs à tous les champions.
 *
 * - Health / MaxHealth / Energy / MaxEnergy / MoveSpeed sont répliqués.
 * - IncomingDamage / IncomingHealing sont des "méta-attributs" : jamais répliqués,
 *   ils servent de tampon. Les GameplayEffects écrivent dedans, puis
 *   PostGameplayEffectExecute les convertit en perte/gain de vie (côté serveur).
 *   => Toute la logique de dégâts (armure, boucliers, réductions...) se branche ici.
 */
UCLASS()
class GEN_API UGenAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UGenAttributeSet();

	//~ UObject
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//~ UAttributeSet
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	mutable FGenOutOfHealthEvent OnOutOfHealth;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "Attributes|Health")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(UGenAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Attributes|Health")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(UGenAttributeSet, MaxHealth)

	/** Énergie (Battlerite : se charge en touchant, sert aux sorts EX / ultime). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Energy, Category = "Attributes|Energy")
	FGameplayAttributeData Energy;
	ATTRIBUTE_ACCESSORS_BASIC(UGenAttributeSet, Energy)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxEnergy, Category = "Attributes|Energy")
	FGameplayAttributeData MaxEnergy;
	ATTRIBUTE_ACCESSORS_BASIC(UGenAttributeSet, MaxEnergy)

	/** Vitesse de déplacement, appliquée au CharacterMovement (permet slows/haste via GE). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeed, Category = "Attributes|Movement")
	FGameplayAttributeData MoveSpeed;
	ATTRIBUTE_ACCESSORS_BASIC(UGenAttributeSet, MoveSpeed)

	/** Méta-attribut : dégâts entrants (serveur uniquement). */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Meta")
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS_BASIC(UGenAttributeSet, IncomingDamage)

	/** Méta-attribut : soins entrants (serveur uniquement). */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Meta")
	FGameplayAttributeData IncomingHealing;
	ATTRIBUTE_ACCESSORS_BASIC(UGenAttributeSet, IncomingHealing)

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Energy(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxEnergy(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MoveSpeed(const FGameplayAttributeData& OldValue);

private:
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

	/** Évite de diffuser OnOutOfHealth plusieurs fois pour la même mort. */
	bool bOutOfHealth = false;
};
