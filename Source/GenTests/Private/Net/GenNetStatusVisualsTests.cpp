#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "Character/GenStatusVisualsComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.StatusVisuals : la forme d'état d'un joueur suit le tag répliqué sur chaque client, celui du
 * joueur (client 0, ASC complet) comme la copie simulée chez l'autre (client 1, tags minimaux), et rien
 * n'est dessiné sur le serveur dédié. Le tag vient d'un contrôle dur appliqué par le serveur (ApplyHardCC).
 * La configuration est posée à la main : celle de BP_Champion arrive avec les assets (Task 10 Step 9).
 */
NETWORK_TEST_CLASS(StatusVisuals, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	static constexpr float StunDuration = 1.f;

	/** PlayerId du joueur étourdi (client 0), identique sur toutes les machines. */
	int32 TargetPlayerId = INDEX_NONE;

	/** Composant de formes d'état du joueur ciblé, par client. */
	TMap<int32, TWeakObjectPtr<UGenStatusVisualsComponent>> ClientVisuals;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	static FGameplayTag StunnedTag()
	{
		// GenGameplayTags::* n'est pas exporté par le module Gen
		return FGameplayTag::RequestGameplayTag(TEXT("State.Stunned"));
	}

	static TArray<FGenStatusVisual> MakeConfig()
	{
		FGenStatusVisual Stunned;
		Stunned.Tag = StunnedTag();
		return { Stunned };
	}

	static UGenStatusVisualsComponent* FindVisuals(const AGenPlayerState* PlayerState)
	{
		const APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UGenStatusVisualsComponent>() : nullptr;
	}

	TEST_METHOD(HardCC_ShowsShapeOnEveryClient_NothingOnDedicatedServer)
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
			.ThenServer(TEXT("Serveur : cible = joueur du client 0"), [this](FBasePIENetworkComponentState& Server)
			{
				const AGenPlayerState* PS = GetServerController(Server, 0)->GetPlayerState<AGenPlayerState>();
				ASSERT_THAT(IsNotNull(PS));
				TargetPlayerId = PS->GetPlayerId();
			})
			.UntilClients(TEXT("Clients : formes d'état branchées sur le joueur ciblé"), [this](FBasePIENetworkComponentState& Client)
			{
				// Client 0 : son propre pion ; client 1 : la copie simulée du pion du client 0
				const AGenPlayerState* PS = FindPlayerStateById(Client.World, TargetPlayerId);
				UAbilitySystemComponent* ASC = GetASC(PS);
				UGenStatusVisualsComponent* Visuals = FindVisuals(PS);
				// Après l'initialisation de l'ASC sur ce pion (OnAbilitySystemInitialized rebranche une configuration vide)
				if (!ASC || !Visuals || ASC->GetAvatarActor() != Visuals->GetOwner())
				{
					return false;
				}
				Visuals->Bind(ASC, MakeConfig());
				ClientVisuals.Add(Client.ClientIndex, Visuals);
				return true;
			}, DefaultWait())
			.ThenClients(TEXT("Clients : caché au départ"), [this](FBasePIENetworkComponentState& Client)
			{
				const TWeakObjectPtr<UGenStatusVisualsComponent>* Visuals = ClientVisuals.Find(Client.ClientIndex);
				ASSERT_THAT(IsTrue(Visuals && Visuals->IsValid()));
				ASSERT_THAT(IsFalse((*Visuals)->IsStatusShown(StunnedTag())));
			})
			.ThenServer(TEXT("Serveur : étourdit le joueur ciblé ; aucune forme sur le serveur dédié"), [this](FBasePIENetworkComponentState& Server)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Server.World, TargetPlayerId);
				UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(GetASC(PS));
				ASSERT_THAT(IsNotNull(ASC, TEXT("ASC Gen attendu")));
				ASSERT_THAT(IsTrue(ASC->ApplyHardCC(StunnedTag(), StunDuration, nullptr).IsValid()));

				UGenStatusVisualsComponent* Visuals = FindVisuals(PS);
				ASSERT_THAT(IsNotNull(Visuals));
				Visuals->Bind(ASC, MakeConfig());
				ASSERT_THAT(IsFalse(Visuals->IsStatusShown(StunnedTag()), TEXT("Serveur dédié : Bind ne dessine rien")));
			})
			.UntilClients(TEXT("Clients : la forme apparaît avec le tag répliqué"), [this](FBasePIENetworkComponentState& Client)
			{
				const TWeakObjectPtr<UGenStatusVisualsComponent>* Visuals = ClientVisuals.Find(Client.ClientIndex);
				return Visuals && Visuals->IsValid() && (*Visuals)->IsStatusShown(StunnedTag());
			}, DefaultWait())
			.UntilClients(TEXT("Clients : la forme disparaît à la fin de l'étourdissement"), [this](FBasePIENetworkComponentState& Client)
			{
				const TWeakObjectPtr<UGenStatusVisualsComponent>* Visuals = ClientVisuals.Find(Client.ClientIndex);
				return Visuals && Visuals->IsValid() && !(*Visuals)->IsStatusShown(StunnedTag());
			}, DefaultWait());
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
