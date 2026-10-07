#include "Character/GenTrainingDummy.h"

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "TimerManager.h"

AGenTrainingDummy::AGenTrainingDummy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Pas de contrôleur IA : le mannequin reste planté là
	AutoPossessAI = EAutoPossessAI::Disabled;
	bRagdollOnDeath = false;

	DummyAbilitySystem = CreateDefaultSubobject<UGenAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	DummyAbilitySystem->SetIsReplicated(true);
	// Minimal : recommandé pour les PNJ (les GE ne sont pas répliqués, seulement tags/cues/attributs)
	DummyAbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	// Un AttributeSet sous-objet de l'acteur propriétaire est détecté automatiquement par l'ASC
	DummyAttributeSet = CreateDefaultSubobject<UGenAttributeSet>(TEXT("AttributeSet"));

	AbilitySystemComponent = DummyAbilitySystem;
	AttributeSet = DummyAttributeSet;
}

void AGenTrainingDummy::BeginPlay()
{
	Super::BeginPlay();

	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	OnAbilitySystemInitialized();
}

void AGenTrainingDummy::HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser)
{
	// Pas de mort : on remet la vie au max après un délai
	GetWorldTimerManager().SetTimer(ResetTimerHandle, this, &ThisClass::ResetHealth, FMath::Max(ResetDelay, 0.01f), false);
}

void AGenTrainingDummy::ResetHealth()
{
	if (AbilitySystemComponent && AttributeSet)
	{
		AbilitySystemComponent->SetNumericAttributeBase(UGenAttributeSet::GetHealthAttribute(), AttributeSet->GetMaxHealth());
	}
}
