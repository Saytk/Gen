#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.ServerAuthority : un changement fait par le serveur (GE de dégâts, GE de gain) se réplique
 * au client propriétaire ET à la vue simulée de l'autre client (attributs du PlayerState, COND_None).
 */
NETWORK_TEST_CLASS(ServerAuthority, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	static constexpr float DamageAmount = 30.f;
	static constexpr float EnergyGain = 10.f;

	UWorld* ServerWorld = nullptr;
	/** PlayerId du joueur ciblé (client 0), identique sur toutes les machines. */
	int32 TargetPlayerId = INDEX_NONE;
	float ExpectedHealth = 0.f;
	float ExpectedEnergy = 0.f;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	TEST_METHOD(ServerDamageAndGain_ReplicateToOwnerAndOtherClient)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server)
			{
				return AreAllServerPlayersReady(Server);
			}, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client)
			{
				return IsPlayerReady(GetLocalController(Client));
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : dégâts + gain d'énergie sur le joueur du client 0"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;

				const AGenPlayerState* PS = GetServerController(Server, 0)->GetPlayerState<AGenPlayerState>();
				UAbilitySystemComponent* ASC = GetASC(PS);
				ASSERT_THAT(IsNotNull(ASC));
				TargetPlayerId = PS->GetPlayerId();

				const float HealthBefore = GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute());
				const float EnergyBefore = GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute());
				ASSERT_THAT(IsTrue(HealthBefore > DamageAmount, TEXT("La vie de départ doit dépasser les dégâts du test")));
				ASSERT_THAT(IsTrue(EnergyBefore + EnergyGain <= GetAttribute(ASC, UGenAttributeSet::GetMaxEnergyAttribute()), TEXT("Le gain d'énergie ne doit pas être borné par le max")));

				// Dégâts : GE générique (SetByCaller.Damage -> IncomingDamage -> Health), comme un projectile
				const FGameplayEffectSpecHandle DamageSpec = ASC->MakeOutgoingSpec(UGenGE_Damage::StaticClass(), 1.f, ASC->MakeEffectContext());
				DamageSpec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage")), DamageAmount);
				ASC->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data);

				// Gain d'énergie (ressource inchangée : le Foyer est déjà plein)
				const FGameplayEffectSpecHandle GainSpec = ASC->MakeOutgoingSpec(UGenGE_Gain::StaticClass(), 1.f, ASC->MakeEffectContext());
				UGenGE_Gain::SetMagnitudes(*GainSpec.Data, EnergyGain, 0.f);
				ASC->ApplyGameplayEffectSpecToSelf(*GainSpec.Data);

				ExpectedHealth = HealthBefore - DamageAmount;
				ExpectedEnergy = EnergyBefore + EnergyGain;
				ASSERT_THAT(IsNear(ExpectedHealth, GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()), 0.01f));
				ASSERT_THAT(IsNear(ExpectedEnergy, GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute()), 0.01f));
			})
			.UntilClients(TEXT("Clients : la vie et l'énergie du joueur ciblé arrivent"), [this](FBasePIENetworkComponentState& Client)
			{
				// Client 0 : son propre PlayerState ; client 1 : la copie simulée du PlayerState du client 0
				const UAbilitySystemComponent* ASC = GetASC(FindPlayerStateById(Client.World, TargetPlayerId));
				return ASC
					&& FMath::IsNearlyEqual(ExpectedHealth, GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()))
					&& FMath::IsNearlyEqual(ExpectedEnergy, GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute()));
			}, DefaultWait())
			.ThenClients(TEXT("Clients : vérification finale"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerState* TargetPS = FindPlayerStateById(Client.World, TargetPlayerId);
				ASSERT_THAT(IsNotNull(TargetPS));
				const UAbilitySystemComponent* ASC = GetASC(TargetPS);
				ASSERT_THAT(IsNear(ExpectedHealth, GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()), 0.01f));
				ASSERT_THAT(IsNear(ExpectedEnergy, GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute()), 0.01f));

				// Le propriétaire (client 0) reçoit tout : ses attributs doivent égaler ceux du serveur
				const bool bIsOwner = GetLocalController(Client)->GetPlayerState<AGenPlayerState>() == TargetPS;
				ASSERT_THAT(AreEqual(Client.ClientIndex == 0, bIsOwner));
				if (bIsOwner)
				{
					FString Diff;
					ASSERT_THAT(IsTrue(DoReplicatedAttributesMatch(GetASC(FindPlayerStateById(ServerWorld, TargetPlayerId)), ASC, &Diff), Diff));
				}
			});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
