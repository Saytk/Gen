#include "Player/GenPlayerState.h"

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "Character/GenStatusVisualsComponent.h"
#include "Net/UnrealNetwork.h"

AGenPlayerState::AGenPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UGenAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	// Mixed : les GE ne sont répliqués qu'au propriétaire (cooldowns visibles par le joueur),
	// les tags et cues à tout le monde. Recommandé pour les personnages joueurs.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UGenAttributeSet>(TEXT("AttributeSet"));

	// Par défaut un PlayerState se met à jour très rarement : trop lent pour l'ASC
	SetNetUpdateFrequency(100.f);
}

UAbilitySystemComponent* AGenPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AGenPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGenPlayerState, TeamId);
}

void AGenPlayerState::SetTeamId(uint8 NewTeamId)
{
	TeamId = NewTeamId;
	OnRep_TeamId(); // Les RepNotify ne s'ex�cutent pas sur le serveur : appel manuel (listen server)
}

void AGenPlayerState::OnRep_TeamId()
{
	UGenStatusVisualsComponent::RefreshAllViewerRelations(GetWorld());
}
