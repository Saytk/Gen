#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.FeedVisuals : cosmétiques du nourrissage (Plan Visuals V2), serveur dédié + 2 clients.
 * Le client 0 nourrit la grande boule de feu (2 flammes) ; le client 1 observe.
 * - Le seuil (OnFedThresholdReached, joué avec le GameplayCue local GameplayCue.Feed.Threshold) part sur le client
 *   du lanceur (compte prédit) et chez l'observateur (compte répliqué, OnRep_FedResource), seulement quand le compte
 *   augmente ; jamais au lancer (retour à 0) et jamais sur le serveur dédié.
 */
NETWORK_TEST_CLASS(FeedVisuals, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	/** Un changement du compte affiché vu par une machine, et s'il a déclenché le pop du seuil. */
	struct FFedRecord
	{
		int32 Old = 0;
		int32 New = 0;
		bool bPop = false;
	};

	/** Clé des relevés du serveur dédié (les clients utilisent leur index). */
	static constexpr int32 ServerKey = -1;

	TSubclassOf<UGameplayAbility> GreatFireballClass;
	int32 CasterPlayerId = INDEX_NONE;
	float ClientMark = 0.f;

	/** Relevés par machine (ServerKey, 0, 1). */
	TMap<int32, TArray<FFedRecord>> Records;
	/** Pops reçus sans changement juste avant (ne doit jamais arriver). */
	int32 OrphanPops = 0;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		GreatFireballClass = LoadCurffeAbilityClass(TEXT("GA_GreatFireball"));
		Records.Reset();
		OrphanPops = 0;

		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	// --- Outils -----------------------------------------------------------------------------------------

	static UGenAbilitySystemComponent* GetLocalGenASC(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return Cast<UGenAbilitySystemComponent>(GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr));
	}

	/** Appui (bPressed) ou relâché d'une touche de sort sur le client, traité dans la foulée comme le ferait le PC. */
	static void SendInput(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass, bool bPressed)
	{
		UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
		const UGenGameplayAbility* CDO = AbilityClass ? Cast<UGenGameplayAbility>(AbilityClass->GetDefaultObject()) : nullptr;
		if (!ASC || !CDO)
		{
			return;
		}
		if (bPressed)
		{
			ASC->AbilityInputTagPressed(CDO->InputTag);
		}
		else
		{
			ASC->AbilityInputTagReleased(CDO->InputTag);
		}
		ASC->ProcessAbilityInput(0.f, false);
	}

	AGenCharacterBase* FindCaster(const UWorld* World) const
	{
		const AGenPlayerState* PS = FindPlayerStateById(World, CasterPlayerId);
		return PS ? Cast<AGenCharacterBase>(PS->GetPawn()) : nullptr;
	}

	/** Branche les relevés de la machine Key sur le lanceur tel qu'elle le voit. */
	void Listen(AGenCharacterBase* Caster, int32 Key)
	{
		Records.FindOrAdd(Key);
		Caster->OnFedResourceChanged.AddLambda([this, Key](AGenCharacterBase*, int32 Old, int32 New)
		{
			Records.FindOrAdd(Key).Add({ Old, New, false });
		});
		Caster->OnFedThresholdReached.AddLambda([this, Key](AGenCharacterBase*, int32 New)
		{
			TArray<FFedRecord>& List = Records.FindOrAdd(Key);
			if (List.Num() > 0 && List.Last().New == New && !List.Last().bPop)
			{
				List.Last().bPop = true;
			}
			else
			{
				++OrphanPops;
			}
		});
	}

	/** Le client a vu le nourrissage monter puis revenir à 0 (lancer). */
	bool HasSeenFeedAndRelease(int32 Key) const
	{
		const TArray<FFedRecord>* List = Records.Find(Key);
		return List && List->Num() >= 2 && List->Last().New == 0;
	}

	// --- Seuils ------------------------------------------------------------------------------------------

	/** Touche tenue 0.75 s (seuils à 0.3 et 0.6 s) : 2 flammes, puis lancer. */
	TEST_METHOD(Threshold_FiresOnClientsOnlyWhenTheCountRises_NeverOnDedicatedServer)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : écarte l'autre joueur, écoute le lanceur"), [this](FBasePIENetworkComponentState& Server)
			{
				ASSERT_THAT(IsNotNull(GreatFireballClass.Get(), TEXT("GA_GreatFireball introuvable")));
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				Other->TeleportTo(Caster->GetActorLocation() + FVector(0.f, 5000.f, 0.f), Other->GetActorRotation(), false, true);
				Listen(Caster, ServerKey);
			})
			.UntilClients(TEXT("Clients : le pion du lanceur est connu"), [this](FBasePIENetworkComponentState& Client) { return FindCaster(Client.World) != nullptr; }, DefaultWait())
			.ThenClients(TEXT("Clients : écoutent le lanceur"), [this](FBasePIENetworkComponentState& Client)
			{
				Listen(FindCaster(Client.World), Client.ClientIndex);
			})
			.ThenClient(TEXT("Client 0 : visée déterministe, appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
				SendInput(Client, GreatFireballClass, true);
				ClientMark = Client.World->GetTimeSeconds();
			})
			.UntilClient(TEXT("Client 0 : touche tenue 0.75 s"), 0, [this](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + 0.75f; }, DefaultWait())
			.ThenClient(TEXT("Client 0 : relâche (2 flammes)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilClients(TEXT("Clients : nourrissage vu puis lancer"), [this](FBasePIENetworkComponentState& Client) { return HasSeenFeedAndRelease(Client.ClientIndex); }, DefaultWait())
			.ThenServer(TEXT("Serveur dédié : aucun événement cosmétique"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(0, Records.FindOrAdd(ServerKey).Num(), TEXT("Rien sur le serveur dédié")));
				ASSERT_THAT(AreEqual(0, OrphanPops, TEXT("Chaque pop suit un changement du compte")));
			})
			.ThenClients(TEXT("Clients : pop si et seulement si le compte augmente"), [this](FBasePIENetworkComponentState& Client)
			{
				const TArray<FFedRecord>& List = Records.FindOrAdd(Client.ClientIndex);
				int32 Pops = 0;
				int32 MaxShown = 0;
				for (const FFedRecord& Record : List)
				{
					ASSERT_THAT(AreEqual(Record.New > Record.Old, Record.bPop, *FString::Printf(TEXT("%d -> %d"), Record.Old, Record.New)));
					Pops += Record.bPop ? 1 : 0;
					MaxShown = FMath::Max(MaxShown, Record.New);
				}
				ASSERT_THAT(AreEqual(2, MaxShown, TEXT("2 flammes affichées au plus haut")));
				ASSERT_THAT(AreEqual(0, List.Last().New, TEXT("Retour à 0 au lancer, sans pop")));
				if (Client.ClientIndex == 0)
				{
					// Client du lanceur : son compte prédit monte d'un cran par seuil
					ASSERT_THAT(AreEqual(2, Pops, TEXT("Un pop par seuil sur le client du lanceur")));
				}
				else
				{
					// Observateur : un ou deux pops selon le regroupement de la réplication (0 -> 2 = un seul pop)
					ASSERT_THAT(IsTrue(Pops >= 1 && Pops <= 2, TEXT("L'observateur voit le seuil")));
				}
			});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
