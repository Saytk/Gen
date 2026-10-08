#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Champions/Curffe/CurffeTuning.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.Smoke : serveur dédié + 2 clients avec le vrai mode de jeu (BP_GenGameMode -> BP_Curffe).
 * Vérifie la chaîne connexion -> possession -> ASC sur le PlayerState -> réplication des attributs.
 */
NETWORK_TEST_CLASS(Smoke, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	/** Monde du serveur, mémorisé par une étape ThenServer pour les comparaisons côté client. */
	UWorld* ServerWorld = nullptr;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	TEST_METHOD(TwoClients_PossessCurffe_AttributesMatchServer)
	{
		Network
			.UntilServer(TEXT("Serveur : chaque client a un Curffe possédé avec ses sorts"), [](FBasePIENetworkComponentState& Server)
			{
				return AreAllServerPlayersReady(Server);
			}, DefaultWait())
			.UntilClients(TEXT("Clients : pion local possédé, ASC initialisé, sorts répliqués"), [](FBasePIENetworkComponentState& Client)
			{
				return IsPlayerReady(GetLocalController(Client));
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : attributs de départ de Curffe"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;
				ASSERT_THAT(IsTrue(Server.World->GetNetMode() == NM_DedicatedServer, TEXT("Le serveur doit être dédié")));

				for (int32 ClientIndex = 0; ClientIndex < Server.ClientCount; ++ClientIndex)
				{
					const APlayerController* PC = GetServerController(Server, ClientIndex);
					ASSERT_THAT(IsNotNull(PC));
					const AGenPlayerState* PS = PC->GetPlayerState<AGenPlayerState>();
					ASSERT_THAT(IsNotNull(PS));
					const UAbilitySystemComponent* ASC = GetASC(PS);
					ASSERT_THAT(IsNotNull(ASC));

					// Foyer de Curffe (StartupEffects de BP_Curffe) : 5 flammes max, plein à l'apparition
					ASSERT_THAT(IsNear(static_cast<float>(CurffeTuning::MaxFlames), GetAttribute(ASC, UGenAttributeSet::GetMaxResourceAttribute()), 0.01f));
					ASSERT_THAT(IsNear(static_cast<float>(CurffeTuning::MaxFlames), GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()), 0.01f));
					// Vie pleine à l'apparition (OnAbilitySystemInitialized)
					ASSERT_THAT(IsTrue(GetAttribute(ASC, UGenAttributeSet::GetMaxHealthAttribute()) > 0.f));
					ASSERT_THAT(IsNear(GetAttribute(ASC, UGenAttributeSet::GetMaxHealthAttribute()), GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()), 0.01f));
				}

				// Deux équipes différentes (PickTeamForNewPlayer remplit l'équipe la moins remplie)
				const AGenPlayerState* PS0 = GetServerController(Server, 0)->GetPlayerState<AGenPlayerState>();
				const AGenPlayerState* PS1 = GetServerController(Server, 1)->GetPlayerState<AGenPlayerState>();
				ASSERT_THAT(AreNotEqual(PS0->GetTeamId(), PS1->GetTeamId()));
			})
			.UntilClients(TEXT("Clients : attributs du joueur local = valeurs serveur"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerState* LocalPS = GetLocalController(Client)->GetPlayerState<AGenPlayerState>();
				const AGenPlayerState* ServerPS = FindPlayerStateById(ServerWorld, LocalPS->GetPlayerId());
				return DoReplicatedAttributesMatch(GetASC(ServerPS), GetASC(LocalPS));
			}, DefaultWait())
			.ThenClients(TEXT("Clients : vérification finale"), [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(Client.World->GetNetMode() == NM_Client, TEXT("Monde client attendu")));

				const APlayerController* PC = GetLocalController(Client);
				const AGenPlayerCharacter* Pawn = PC->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Pawn));
				ASSERT_THAT(IsTrue(Pawn->IsLocallyControlled(), TEXT("Le pion du client doit être contrôlé localement")));

				const AGenPlayerState* LocalPS = PC->GetPlayerState<AGenPlayerState>();
				const AGenPlayerState* ServerPS = FindPlayerStateById(ServerWorld, LocalPS->GetPlayerId());
				ASSERT_THAT(IsNotNull(ServerPS));

				FString Diff;
				ASSERT_THAT(IsTrue(DoReplicatedAttributesMatch(GetASC(ServerPS), GetASC(LocalPS), &Diff), Diff));
				// L'équipe est répliquée
				ASSERT_THAT(AreEqual(ServerPS->GetTeamId(), LocalPS->GetTeamId()));
			});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
