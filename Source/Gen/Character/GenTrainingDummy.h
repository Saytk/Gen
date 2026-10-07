#pragma once

#include "CoreMinimal.h"
#include "Character/GenCharacterBase.h"
#include "GenTrainingDummy.generated.h"

/**
 * Mannequin d'entraînement : possède son propre ASC, encaisse les sorts et
 * se remet à pleine vie quelques secondes après être tombé à 0 (ne meurt jamais).
 * Équipe neutre par défaut = touchable par tout le monde.
 */
UCLASS()
class GEN_API AGenTrainingDummy : public AGenCharacterBase
{
	GENERATED_BODY()

public:
	AGenTrainingDummy(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual uint8 GetTeamId() const override { return TeamId; }

protected:
	virtual void BeginPlay() override;
	virtual void HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser) override;

	void ResetHealth();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen|Abilities")
	TObjectPtr<UGenAbilitySystemComponent> DummyAbilitySystem;

	UPROPERTY()
	TObjectPtr<UGenAttributeSet> DummyAttributeSet;

	/** 255 = neutre (hostile à tous), 0/1 = équipe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gen|Team")
	uint8 TeamId = GenNoTeam;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gen|Dummy", meta = (ClampMin = "0.0"))
	float ResetDelay = 2.f;

private:
	FTimerHandle ResetTimerHandle;
};
