#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenCastBarRules.h"
#include "Animation/AnimInstance.h"
#include "Champions/Curffe/CurffeEffects.h"
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
 * - Annuler (touche d'annulation) puis rappuyer : pas de pop au retour à 0, un pop pour le premier seuil du nouveau sort
 *   (revue V2-V4, M1/M2), chez le lanceur comme chez l'observateur.
 * - V3 : le montage de nourrissage part dès l'appui et l'observateur le voit pendant le nourrissage, puis la charge.
 * - V4 : une canalisation (fenêtre minutée) se réplique, la barre de l'observateur se vide pendant que la part écoulée
 *   grandit.
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

	/** Nombre de retours à 0 vus par la machine Key (annulation, lancer). */
	int32 CountReturnsToZero(int32 Key) const
	{
		const TArray<FFedRecord>* List = Records.Find(Key);
		int32 Count = 0;
		for (const FFedRecord& Record : List ? *List : TArray<FFedRecord>())
		{
			Count += Record.New == 0 ? 1 : 0;
		}
		return Count;
	}

	/** Le client a vu le nourrissage monter puis revenir à 0 (lancer). */
	bool HasSeenFeedAndRelease(int32 Key) const
	{
		const TArray<FFedRecord>* List = Records.Find(Key);
		return List && List->Num() >= 2 && List->Last().New == 0;
	}

	// --- Seuils ------------------------------------------------------------------------------------------

	/**
	 * Le client 0 tient la grande boule de feu HoldSeconds puis relâche ; serveur, lanceur et observateur relèvent les
	 * changements du compte affiché. Vérifie le pop si et seulement si le compte augmente, MaxShown au plus haut, le retour
	 * à 0 sans pop, et ExpectedOwnerPops pops chez le lanceur ; chez l'observateur, au moins MinObserverPops pops.
	 */
	void QueueHoldAndCheckPops(float HoldSeconds, int32 MaxShownExpected, int32 ExpectedOwnerPops, int32 MinObserverPops)
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
			.UntilClient(TEXT("Client 0 : touche tenue"), 0, [this, HoldSeconds](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + HoldSeconds; }, DefaultWait())
			.ThenClient(TEXT("Client 0 : relâche"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilClients(TEXT("Clients : nourrissage vu puis lancer"), [this](FBasePIENetworkComponentState& Client) { return HasSeenFeedAndRelease(Client.ClientIndex); }, DefaultWait())
			.ThenServer(TEXT("Serveur dédié : aucun événement cosmétique"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(0, Records.FindOrAdd(ServerKey).Num(), TEXT("Rien sur le serveur dédié")));
				ASSERT_THAT(AreEqual(0, OrphanPops, TEXT("Chaque pop suit un changement du compte")));
			})
			.ThenClients(TEXT("Clients : pop si et seulement si le compte augmente"), [this, MaxShownExpected, ExpectedOwnerPops, MinObserverPops](FBasePIENetworkComponentState& Client)
			{
				const TArray<FFedRecord>& List = Records.FindOrAdd(Client.ClientIndex);
				int32 Pops = 0;
				int32 MaxShown = 0;
				FString Seen;
				for (const FFedRecord& Record : List)
				{
					Seen += FString::Printf(TEXT("%d->%d%s "), Record.Old, Record.New, Record.bPop ? TEXT("*") : TEXT(""));
				}
				for (const FFedRecord& Record : List)
				{
					ASSERT_THAT(AreEqual(Record.New > Record.Old, Record.bPop, *FString::Printf(TEXT("%d -> %d (relevés : %s)"), Record.Old, Record.New, *Seen)));
					Pops += Record.bPop ? 1 : 0;
					MaxShown = FMath::Max(MaxShown, Record.New);
				}
				ASSERT_THAT(AreEqual(MaxShownExpected, MaxShown, TEXT("Flammes affichées au plus haut")));
				ASSERT_THAT(AreEqual(0, List.Last().New, TEXT("Retour à 0 au lancer, sans pop")));
				if (Client.ClientIndex == 0)
				{
					// Client du lanceur : son compte prédit monte d'un cran par seuil
					ASSERT_THAT(AreEqual(ExpectedOwnerPops, Pops, TEXT("Un pop par seuil sur le client du lanceur")));
				}
				else
				{
					// Observateur : regroupement possible de la réplication (0 -> 2 = un seul pop)
					ASSERT_THAT(IsTrue(Pops >= MinObserverPops && Pops <= ExpectedOwnerPops, *FString::Printf(TEXT("L'observateur voit les seuils (relevés : %s)"), *Seen)));
				}
			});
	}

	/** Touche tenue 0.75 s (seuils à 0.3 et 0.6 s) : 2 flammes, puis lancer. */
	TEST_METHOD(Threshold_FiresOnClientsOnlyWhenTheCountRises_NeverOnDedicatedServer)
	{
		QueueHoldAndCheckPops(0.75f, 2, 2, 1);
	}

	/**
	 * Revue P3 T3-7, I1 : touche tenue jusqu'au plafond (le client s'arrête seul à 3 flammes, 0.9 s). Le 3e seuil arrive
	 * chez l'observateur avec la fin du nourrissage (annonce du client + MarkFeedEnded, même image du serveur) : il doit
	 * quand même faire son pop. Seuils espacés de 0.3 s : 3 pops chez l'observateur aussi.
	 */
	TEST_METHOD(Threshold_HeldToTheCap_ObserverPopsAtEachOfTheThreeThresholds)
	{
		QueueHoldAndCheckPops(1.2f, 3, 3, 3);
	}

	/**
	 * Revue V2-V4 (test manquant) : 1 flamme, annulation par la touche d'annulation, nouvel appui aussitôt, 1 flamme, lancer.
	 * Pas de pop au retour à 0 ; un pop pour le premier seuil de chaque sort, chez le lanceur ET l'observateur.
	 * Tenue de 0.55 s : au-delà de l'estimation du serveur (0.1 + 0.3 s), avant le 2e seuil (0.6 s).
	 */
	TEST_METHOD(Threshold_CancelThenPressAgain_PopsForTheNewSpell)
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
			.ThenClient(TEXT("Client 0 : visée déterministe, appuie"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
				SendInput(Client, GreatFireballClass, true);
				ClientMark = Client.World->GetTimeSeconds();
			})
			.UntilClient(TEXT("Client 0 : touche tenue 0.55 s (1 flamme)"), 0, [this](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + 0.55f; }, DefaultWait())
			.ThenClient(TEXT("Client 0 : annule, relâche et rappuie"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(AreEqual(1, GetLocalGenASC(Client)->CancelPendingCasts(), TEXT("Le sort en nourrissage est annulé")));
				SendInput(Client, GreatFireballClass, false);
				SendInput(Client, GreatFireballClass, true);
				ClientMark = Client.World->GetTimeSeconds();
			})
			.UntilClient(TEXT("Client 0 : touche tenue 0.55 s (1 flamme)"), 0, [this](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + 0.55f; }, DefaultWait())
			.ThenClient(TEXT("Client 0 : relâche (lancer)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilServer(TEXT("Serveur : second sort terminé"), [this](FBasePIENetworkComponentState& Server)
			{
				const AGenCharacterBase* Caster = FindCaster(Server.World);
				return Caster && !Caster->GetCastInfo().IsCasting();
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : départ de l'attente"), [this](FBasePIENetworkComponentState& Server) { ClientMark = Server.World->GetTimeSeconds(); })
			.UntilServer(TEXT("Serveur : 1 s après"), [this](FBasePIENetworkComponentState& Server) { return Server.World->GetTimeSeconds() >= ClientMark + 1.f; }, DefaultWait())
			.ThenClients(TEXT("Clients : annulation puis lancer vus"), [this](FBasePIENetworkComponentState& Client)
			{
				FString Seen;
				for (const FFedRecord& Record : Records.FindOrAdd(Client.ClientIndex))
				{
					Seen += FString::Printf(TEXT("%d->%d%s "), Record.Old, Record.New, Record.bPop ? TEXT("*") : TEXT(""));
				}
				ASSERT_THAT(IsTrue(CountReturnsToZero(Client.ClientIndex) >= 2, *FString::Printf(TEXT("Client %d : %s"), Client.ClientIndex, *Seen)));
			})
			.ThenServer(TEXT("Serveur dédié : aucun événement cosmétique"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(0, Records.FindOrAdd(ServerKey).Num(), TEXT("Rien sur le serveur dédié")));
				ASSERT_THAT(AreEqual(0, OrphanPops, TEXT("Chaque pop suit un changement du compte")));
			})
			.ThenClients(TEXT("Clients : un pop par sort, aucun au retour à 0"), [this](FBasePIENetworkComponentState& Client)
			{
				const TArray<FFedRecord>& List = Records.FindOrAdd(Client.ClientIndex);
				int32 Pops = 0;
				for (const FFedRecord& Record : List)
				{
					ASSERT_THAT(AreEqual(Record.New > Record.Old, Record.bPop, *FString::Printf(TEXT("%d -> %d"), Record.Old, Record.New)));
					Pops += Record.bPop ? 1 : 0;
				}
				ASSERT_THAT(AreEqual(2, Pops, TEXT("Le premier seuil du nouveau sort fait son pop")));
			});
	}

	// --- Montage de nourrissage (V3) ------------------------------------------------------------------------

	/** Pose FeedMontage sur l'instance du sort (pas le CDO) : les assets des phases arrivent avec la Task E5. */
	static bool SetFeedMontage(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass, UAnimMontage* Montage)
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
		FObjectProperty* Property = FindFProperty<FObjectProperty>(UGenGA_Cast::StaticClass(), TEXT("FeedMontage"));
		if (!Instance || !Property)
		{
			return false;
		}
		Property->SetObjectPropertyValue_InContainer(Instance, Montage);
		return true;
	}

	/** ChargeMontage de la grande boule de feu (propriété protégée, lue par réflexion sur le CDO). */
	UAnimMontage* GetGreatFireballChargeMontage() const
	{
		FObjectProperty* Property = FindFProperty<FObjectProperty>(UGenGA_Cast::StaticClass(), TEXT("ChargeMontage"));
		const UObject* CDO = GreatFireballClass ? GreatFireballClass->GetDefaultObject() : nullptr;
		return Property && CDO ? Cast<UAnimMontage>(Property->GetObjectPropertyValue_InContainer(CDO)) : nullptr;
	}

	/** Montage en cours du lanceur vu par la machine (ASC du PlayerState). */
	UAnimMontage* GetCasterMontage(const UWorld* World) const
	{
		const UAbilitySystemComponent* ASC = GetASC(FindPlayerStateById(World, CasterPlayerId));
		return ASC ? ASC->GetCurrentMontage() : nullptr;
	}

	/**
	 * Art Bible §12 Q41 : le geste de nourrissage part dès l'appui et se réplique, l'observateur le voit PENDANT le
	 * nourrissage (avant le montage de charge). Le montage de la boule de feu sert de geste de nourrissage reconnaissable
	 * (la grande boule de feu joue AM_GreatFireball en charge).
	 */
	TEST_METHOD(FeedMontage_ObserverSeesTheFeedingPoseDuringTheFeed)
	{
		// Clip non calé sur l'intervalle : avertissement "recaler le clip" attendu
		TestRunner->AddExpectedMessage(TEXT("recaler le clip"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		UAnimMontage* FeedPose = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Gen/Champions/Curffe/Animations/AM_Fireball.AM_Fireball"));

		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : montage de nourrissage sur le sort du lanceur"), [this, FeedPose](FBasePIENetworkComponentState& Server)
			{
				ASSERT_THAT(IsNotNull(FeedPose, TEXT("AM_Fireball introuvable")));
				ASSERT_THAT(IsNotNull(GreatFireballClass.Get(), TEXT("GA_GreatFireball introuvable")));
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				Other->TeleportTo(Caster->GetActorLocation() + FVector(0.f, 5000.f, 0.f), Other->GetActorRotation(), false, true);
				ASSERT_THAT(IsTrue(SetFeedMontage(Caster->GetAbilitySystemComponent(), GreatFireballClass, FeedPose)));
			})
			.UntilClients(TEXT("Clients : le pion du lanceur est connu"), [this](FBasePIENetworkComponentState& Client) { return FindCaster(Client.World) != nullptr; }, DefaultWait())
			.ThenClient(TEXT("Client 0 : montage posé, visée déterministe, appuie"), 0, [this, FeedPose](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(SetFeedMontage(GetLocalGenASC(Client), GreatFireballClass, FeedPose)));
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
				SendInput(Client, GreatFireballClass, true);
				ASSERT_THAT(IsTrue(GetCasterMontage(Client.World) == FeedPose, TEXT("Le client du lanceur joue le geste dès l'appui (prédit)")));
			})
			// Touche toujours tenue : le nourrissage dure jusqu'à 3 flammes (0.9 s)
			.UntilClient(TEXT("Client 1 : voit le geste de nourrissage"), 1, [this, FeedPose](FBasePIENetworkComponentState& Client)
			{
				return GetCasterMontage(Client.World) == FeedPose;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : toujours en nourrissage"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = FindCaster(Client.World);
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsTrue(Caster->GetCastInfo().FeedEndTime <= 0.f, TEXT("Le geste a été vu pendant le nourrissage")));
				SendInput(Client, GreatFireballClass, false);
			})
			.ThenClient(TEXT("Client 1 : la grande boule de feu a un montage de charge"), 1, [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsNotNull(GetGreatFireballChargeMontage(), TEXT("ChargeMontage de GA_GreatFireball")));
			})
			// Revue V2-V4 : remplacé PAR LA CHARGE (pas seulement arrêté)
			.UntilClient(TEXT("Client 1 : le geste de nourrissage est remplacé par la charge"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				return GetCasterMontage(Client.World) == GetGreatFireballChargeMontage();
			}, DefaultWait());
	}

	// --- Geste figé sous le dernier seuil -----------------------------------------------------------------------

	/** Fin de Feed_1 et de Feed_2 dans AM_Curffe_FeedHand (temps du montage). */
	float Feed1End = 0.f;
	float Feed2End = 0.f;
	float ObserverHeldPosition = -1.f;
	float ServerHeldPosition = -1.f;

	/** AnimInstance du lanceur où l'ASC joue ses montages, vu par la machine (réplique de l'ASC chez l'observateur). */
	UAnimInstance* GetCasterAnimInstance(const UWorld* World) const
	{
		const UAbilitySystemComponent* ASC = GetASC(FindPlayerStateById(World, CasterPlayerId));
		return ASC && ASC->AbilityActorInfo.IsValid() ? ASC->AbilityActorInfo->GetAnimInstance() : nullptr;
	}

	/** Pose FeedInterval sur l'instance du sort de la machine (pas le CDO). */
	static bool SetFeedInterval(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass, float Interval)
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
		FFloatProperty* Property = FindFProperty<FFloatProperty>(UGenGA_Cast::StaticClass(), TEXT("FeedInterval"));
		if (!Instance || !Property)
		{
			return false;
		}
		Property->SetPropertyValue_InContainer(Instance, Interval);
		return true;
	}

	/**
	 * Foyer à 2 flammes : le geste de nourrissage s'arrête au seuil 2. Il ne boucle plus Feed_2 sur elle-même (le
	 * mouvement rejoué vers la pose faisait un pop) : le serveur le FIGE à la fin de Feed_2 (vitesse 0 par l'ASC) et
	 * l'observateur, qui reçoit la vitesse 0, garde une pose immobile (jamais Feed_3, jamais de boucle) jusqu'à la charge,
	 * qui la remplace et avance à sa propre vitesse.
	 * Sans retard, la fin du nourrissage du client arrive au serveur avant la fin de SA Feed_2 (jouée ServerEstimateLag plus
	 * tard) : le client du lanceur nourrit ici à 0.6 s par flamme (instance seulement), le serveur à 0.3 s. Le serveur
	 * atteint son plafond à 0.7 s et attend la fin du nourrissage (1.2 s) : 0.5 s de pose tenue, comme un client en retard.
	 */
	TEST_METHOD(FeedMontage_CapOfTwo_ObserverPoseHoldsStillAtTheEndOfFeed2)
	{
		// Client du lanceur à 0.6 s par flamme : geste à x0.5, avertissement "recaler le clip" attendu
		TestRunner->AddExpectedMessage(TEXT("recaler le clip"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		UAnimMontage* FeedHand = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Gen/Champions/Curffe/Animations/AM_Curffe_FeedHand.AM_Curffe_FeedHand"));

		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : Foyer à 2 flammes sans régénération, geste AM_Curffe_FeedHand"), [this, FeedHand](FBasePIENetworkComponentState& Server)
			{
				ASSERT_THAT(IsNotNull(FeedHand, TEXT("AM_Curffe_FeedHand introuvable")));
				ASSERT_THAT(IsNotNull(GreatFireballClass.Get(), TEXT("GA_GreatFireball introuvable")));
				const int32 Feed1 = FeedHand->GetSectionIndex(TEXT("Feed_1"));
				const int32 Feed2 = FeedHand->GetSectionIndex(TEXT("Feed_2"));
				ASSERT_THAT(IsTrue(Feed1 != INDEX_NONE && Feed2 != INDEX_NONE && FeedHand->GetSectionIndex(TEXT("Feed_3")) != INDEX_NONE, TEXT("Sections Feed_1..Feed_3")));
				float Start = 0.f;
				FeedHand->GetSectionStartAndEndTime(Feed1, Start, Feed1End);
				FeedHand->GetSectionStartAndEndTime(Feed2, Start, Feed2End);

				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				Other->TeleportTo(Caster->GetActorLocation() + FVector(0.f, 5000.f, 0.f), Other->GetActorRotation(), false, true);

				UAbilitySystemComponent* ASC = Caster->GetAbilitySystemComponent();
				ASC->RemoveActiveGameplayEffectBySourceEffect(UCurffeGE_HearthRegen::StaticClass(), nullptr);
				ApplyGain(ASC, 0.f, 2.f - GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()));
				ASSERT_THAT(IsNear(2.f, GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("2 flammes")));
				ASSERT_THAT(IsTrue(SetFeedMontage(ASC, GreatFireballClass, FeedHand)));
			})
			.UntilClients(TEXT("Clients : le pion du lanceur est connu"), [this](FBasePIENetworkComponentState& Client) { return FindCaster(Client.World) != nullptr; }, DefaultWait())
			.UntilClient(TEXT("Client 0 : 2 flammes répliquées"), 0, [](FBasePIENetworkComponentState& Client)
			{
				return FMath::IsNearlyEqual(GetAttribute(GetLocalGenASC(Client), UGenAttributeSet::GetResourceAttribute()), 2.f, 0.01f);
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : geste posé, 0.6 s par flamme, visée déterministe, appuie (tenu)"), 0, [this, FeedHand](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(SetFeedMontage(GetLocalGenASC(Client), GreatFireballClass, FeedHand)));
				ASSERT_THAT(IsTrue(SetFeedInterval(GetLocalGenASC(Client), GreatFireballClass, 0.6f)));
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
				SendInput(Client, GreatFireballClass, true);
				ASSERT_THAT(IsTrue(GetCasterMontage(Client.World) == FeedHand, TEXT("Le client du lanceur joue le geste dès l'appui")));
			})
			.UntilClient(TEXT("Client 1 : vitesse 0 répliquée sur le geste"), 1, [this, FeedHand](FBasePIENetworkComponentState& Client)
			{
				const UAnimInstance* AnimInstance = GetCasterAnimInstance(Client.World);
				return GetCasterMontage(Client.World) == FeedHand && AnimInstance && AnimInstance->Montage_IsPlaying(FeedHand)
					&& AnimInstance->Montage_GetPlayRate(FeedHand) == 0.f;
			}, DefaultWait())
			// Positions : seule la borne compte (jamais au-delà de Feed_2). Le montage d'un pion piloté par un client avance, sur
			// le serveur, avec les mouvements reçus (UCharacterMovementComponent::TickCharacterPose) : ici bien moins vite que
			// le temps réel, et l'observateur, recalé sur la position du serveur dans une même section, peut être en Feed_1
			.ThenClient(TEXT("Client 1 : pose figée avant la fin de Feed_2"), 1, [this, FeedHand](FBasePIENetworkComponentState& Client)
			{
				ObserverHeldPosition = GetCasterAnimInstance(Client.World)->Montage_GetPosition(FeedHand);
				ASSERT_THAT(IsTrue(ObserverHeldPosition <= Feed2End,
					*FString::Printf(TEXT("Figée avant la fin de Feed_2 (%.3f, fin de Feed_1 %.2f, de Feed_2 %.2f)"), ObserverHeldPosition, Feed1End, Feed2End)));
				ClientMark = Client.World->GetTimeSeconds();
			})
			.ThenServer(TEXT("Serveur : geste figé, nourrissage en cours"), [this, FeedHand](FBasePIENetworkComponentState& Server)
			{
				const UAnimInstance* AnimInstance = GetCasterAnimInstance(Server.World);
				ASSERT_THAT(IsNotNull(AnimInstance));
				ASSERT_THAT(IsTrue(GetCasterMontage(Server.World) == FeedHand && AnimInstance->Montage_IsPlaying(FeedHand), TEXT("Toujours le geste de nourrissage")));
				ASSERT_THAT(IsNear(0.f, AnimInstance->Montage_GetPlayRate(FeedHand), 0.0001f, TEXT("Vitesse 0")));
				ServerHeldPosition = AnimInstance->Montage_GetPosition(FeedHand);
				ASSERT_THAT(IsTrue(ServerHeldPosition <= Feed2End, *FString::Printf(TEXT("Serveur figé avant la fin de Feed_2 (%.3f)"), ServerHeldPosition)));
				const AGenCharacterBase* Caster = FindCaster(Server.World);
				ASSERT_THAT(IsTrue(Caster && Caster->GetCastInfo().FeedEndTime <= 0.f, TEXT("Pose tenue PENDANT le nourrissage")));
			})
			.UntilClient(TEXT("Client 1 : 0.2 s plus tard"), 1, [this](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + 0.2f; }, DefaultWait())
			.ThenClient(TEXT("Client 1 : la pose n'a pas bougé (ni boucle, ni Feed_3)"), 1, [this, FeedHand](FBasePIENetworkComponentState& Client)
			{
				const UAnimInstance* AnimInstance = GetCasterAnimInstance(Client.World);
				ASSERT_THAT(IsTrue(GetCasterMontage(Client.World) == FeedHand && AnimInstance->Montage_IsPlaying(FeedHand), TEXT("Toujours le geste de nourrissage")));
				ASSERT_THAT(IsNear(ObserverHeldPosition, AnimInstance->Montage_GetPosition(FeedHand), 0.0001f, TEXT("Position immobile")));
			})
			.ThenServer(TEXT("Serveur : toujours figé"), [this, FeedHand](FBasePIENetworkComponentState& Server)
			{
				const UAnimInstance* AnimInstance = GetCasterAnimInstance(Server.World);
				ASSERT_THAT(IsTrue(GetCasterMontage(Server.World) == FeedHand && AnimInstance->Montage_IsPlaying(FeedHand), TEXT("Toujours le geste de nourrissage")));
				ASSERT_THAT(IsNear(ServerHeldPosition, AnimInstance->Montage_GetPosition(FeedHand), 0.0001f, TEXT("Position immobile")));
			})
			.UntilClient(TEXT("Client 1 : la phase suivante remplace la pose figée"), 1, [this, FeedHand](FBasePIENetworkComponentState& Client)
			{
				const UAnimMontage* Current = GetCasterMontage(Client.World);
				return Current && Current != FeedHand;
			}, DefaultWait())
			.ThenClient(TEXT("Client 1 : la phase suivante avance"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const UAnimInstance* AnimInstance = GetCasterAnimInstance(Client.World);
				ASSERT_THAT(IsTrue(AnimInstance->Montage_GetPlayRate(GetCasterMontage(Client.World)) > 0.f, TEXT("Jouée à sa propre vitesse")));
			})
			.UntilServer(TEXT("Serveur : sort terminé"), [this](FBasePIENetworkComponentState& Server)
			{
				const AGenCharacterBase* Caster = FindCaster(Server.World);
				return Caster && !Caster->GetCastInfo().IsCasting();
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : 2 flammes dépensées"), [this](FBasePIENetworkComponentState& Server)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Server.World, CasterPlayerId);
				ASSERT_THAT(IsNear(0.f, GetAttribute(GetASC(PS), UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("Les 2 flammes nourries")));
			});
	}

	// --- Canalisation (V4) -----------------------------------------------------------------------------------

	static constexpr float ChannelDuration = 2.f;
	float ObserverFill = -1.f;
	float ObserverFraction = -1.f;

	/**
	 * Une fenêtre minutée (StartChannel, serveur) se réplique avec bChannel : l'observateur voit la barre se vider
	 * pendant que la part écoulée (horloge des télégraphes) grandit, les deux complémentaires. Le client du lanceur
	 * ne la reçoit pas (COND_SkipOwner) : il la prédit lui-même avec le même appel.
	 */
	TEST_METHOD(Channel_Replicates_ObserverSeesTheDrainingFraction)
	{
		UClass* WindowAbility = UGenGA_Cast::StaticClass();

		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : le lanceur ouvre une fenêtre de 2 s"), [this, WindowAbility](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				Caster->StartChannel(WindowAbility, ChannelDuration);
				ASSERT_THAT(IsTrue(Caster->GetCastInfo().bChannel));
			})
			.ThenClient(TEXT("Client 0 : prédit la même fenêtre"), 0, [this, WindowAbility](FBasePIENetworkComponentState& Client)
			{
				AGenCharacterBase* Caster = FindCaster(Client.World);
				ASSERT_THAT(IsNotNull(Caster));
				Caster->StartChannel(WindowAbility, ChannelDuration);
				GenCastBar::FLayout Layout;
				ASSERT_THAT(IsTrue(Caster->GetCastBarLayout(Layout) && Layout.bDrain, TEXT("Le client du lanceur voit sa barre se vider")));
			})
			.UntilClient(TEXT("Client 1 : canalisation répliquée"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = FindCaster(Client.World);
				return Caster && Caster->GetCastInfo().IsCasting() && Caster->GetCastInfo().bChannel;
			}, DefaultWait())
			.ThenClient(TEXT("Client 1 : première mesure"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = FindCaster(Client.World);
				GenCastBar::FLayout Layout;
				ASSERT_THAT(IsTrue(Caster->GetCastBarLayout(Layout)));
				ASSERT_THAT(IsTrue(Layout.bDrain, TEXT("La barre de l'observateur se vide")));
				ASSERT_THAT(AreEqual(0, Layout.Ticks.Num(), TEXT("Ni cran")));
				ObserverFill = Layout.Fill;
				ObserverFraction = Caster->GetCastElapsedFraction();
				ASSERT_THAT(IsNear(1.f, ObserverFill + ObserverFraction, 0.01f, TEXT("Barre restante + part écoulée = 1")));
				ClientMark = Client.World->GetTimeSeconds();
			})
			.UntilClient(TEXT("Client 1 : 0.5 s plus tard"), 1, [this](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + 0.5f; }, DefaultWait())
			.ThenClient(TEXT("Client 1 : la barre a baissé, l'horloge a monté"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = FindCaster(Client.World);
				GenCastBar::FLayout Layout;
				ASSERT_THAT(IsTrue(Caster->GetCastBarLayout(Layout)));
				const float Fraction = Caster->GetCastElapsedFraction();
				ASSERT_THAT(IsTrue(Layout.Fill < ObserverFill - 0.1f, TEXT("La barre se vide")));
				ASSERT_THAT(IsTrue(Fraction > ObserverFraction + 0.1f, TEXT("La part écoulée grandit")));
				ASSERT_THAT(IsNear(1.f, Layout.Fill + Fraction, 0.01f, TEXT("Toujours complémentaires")));
			})
			.ThenServer(TEXT("Serveur : fin de la fenêtre"), [this, WindowAbility](FBasePIENetworkComponentState& Server)
			{
				AGenCharacterBase* Caster = FindCaster(Server.World);
				ASSERT_THAT(IsNotNull(Caster));
				Caster->StopCast(WindowAbility);
			})
			.UntilClient(TEXT("Client 1 : plus de barre"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = FindCaster(Client.World);
				return Caster && !Caster->GetCastInfo().IsCasting();
			}, DefaultWait());
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
