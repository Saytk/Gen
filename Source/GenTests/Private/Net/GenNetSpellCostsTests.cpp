#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenTargetData.h"
#include "AbilitySystemComponent.h"
#include "Actors/GenProjectile.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "GameplayPrediction.h"
#include "GameplayTagContainer.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.SpellCosts : coût en énergie des sorts (Plan 3 Task 3), serveur dédié + 2 clients, le client 0 lance.
 * Guidelines §3.1 : l'énergie n'est dépensée que quand le sort part (CommitAbility au lancer, UGenGA_Cast).
 * - Payé au lancer : le serveur a dépensé le coût quand le projectile apparaît, le client converge.
 * - Énergie insuffisante (24.99 pour 25) : activation refusée par le client, rien côté serveur.
 * - Étourdi pendant l'incantation, visée en avance puis étourdi : rien de payé (serveur et client).
 * Touche d'annulation (Plan 3 Task 6, UGenAbilitySystemComponent::CancelPendingCasts) :
 * - sans sort en cours : rien ; pendant le nourrissage ou l'incantation : annulé sur le client et le serveur, sans coût ;
 * - serveur, visée du client déjà reçue (départ différé) : pas annulé, le sort part et ses coûts restent payés.
 * Le coût est réglé sur les INSTANCES du sort (serveur et client 0), jamais sur le CDO du Blueprint : la grande
 * boule de feu sert de sort de test (nourrissable, incantation de 0.5 s), Living Flame et Combustion arrivent plus tard.
 */
NETWORK_TEST_CLASS(SpellCosts, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	static constexpr float Cost = 25.f;
	/**
	 * Revue P3 T3-7, I4 : énergie de départ des tests « payé (une fois) ». L'énergie est bornée à 0 : partir de Cost
	 * cacherait une double dépense (25 - 50 = 0 = 25 - 25). 60 - 25 = 35, une double dépense donnerait 10.
	 */
	static constexpr float PaidTestEnergy = 60.f;

	TSubclassOf<UGameplayAbility> GreatFireballClass;

	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<UGenAbilitySystemComponent> ServerCasterASC;
	float MaxFlames = 0.f;
	float StartEnergy = 0.f;

	/** Relevés à l'apparition des projectiles du lanceur (serveur). */
	int32 ProjectileCount = 0;
	float EnergyAtSpawn = -1.f;
	bool bCooldownAtSpawn = false;

	float ClientMark = 0.f;
	float ServerMark = 0.f;
	/** Énergie la plus basse vue par le client 0 après son lancer (double dépense transitoire prédite + base). */
	float ClientMinEnergy = TNumericLimits<float>::Max();

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		// Visée collée à l'activation (visée en avance) : Warning attendu, limité à un toutes les 5 s
		TestRunner->AddExpectedMessage(TEXT("Visée très en avance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);

		GreatFireballClass = LoadCurffeAbilityClass(TEXT("GA_GreatFireball"));

		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	AFTER_EACH()
	{
		if (ServerWorld.IsValid() && SpawnHandle.IsValid())
		{
			ServerWorld->RemoveOnActorSpawnedHandler(SpawnHandle);
		}
		SpawnHandle.Reset();
	}

	// --- Outils -----------------------------------------------------------------------------------------

	static FGameplayTag StunnedTag()
	{
		// GenGameplayTags::* n'est pas exporté par le module Gen
		return FGameplayTag::RequestGameplayTag(TEXT("State.Stunned"));
	}

	static const UGenGameplayAbility* GetCDO(TSubclassOf<UGameplayAbility> AbilityClass)
	{
		return AbilityClass ? Cast<UGenGameplayAbility>(AbilityClass->GetDefaultObject()) : nullptr;
	}

	bool HasCooldown(const UAbilitySystemComponent* ASC) const
	{
		const UGenGameplayAbility* CDO = GetCDO(GreatFireballClass);
		const FGameplayTagContainer* Tags = CDO ? CDO->GetCooldownTags() : nullptr;
		return ASC && Tags && !Tags->IsEmpty() && ASC->HasAnyMatchingGameplayTags(*Tags);
	}

	static UGenAbilitySystemComponent* GetLocalGenASC(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return Cast<UGenAbilitySystemComponent>(GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr));
	}

	bool IsCastActive(UAbilitySystemComponent* ASC) const
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, GreatFireballClass);
		return Spec && Spec->IsActive();
	}

	UGenGA_Projectile* GetInstance(UAbilitySystemComponent* ASC) const
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, GreatFireballClass);
		return Spec ? Cast<UGenGA_Projectile>(Spec->GetPrimaryInstance()) : nullptr;
	}

	static float GetEnergy(const UAbilitySystemComponent* ASC)
	{
		return GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute());
	}

	/** Appui (bPressed) ou relâché de la touche du sort sur le client, traité dans la foulée comme le ferait le PC. */
	void SendInput(const FBasePIENetworkComponentState& Client, bool bPressed) const
	{
		UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
		const UGenGameplayAbility* CDO = GetCDO(GreatFireballClass);
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

	/** Client modifié : envoie tout de suite une visée pour l'activation en cours (fenêtre de prédiction comme le vrai client). */
	void SendAimNow(const FBasePIENetworkComponentState& Client) const
	{
		UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
		FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, GreatFireballClass);
		const UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
		const APawn* Pawn = GetLocalController(Client)->GetPawn();
		if (!Instance || !Pawn)
		{
			return;
		}

		FGenTargetData_Aim* Aim = new FGenTargetData_Aim();
		Aim->HitResult.bBlockingHit = true;
		Aim->HitResult.Location = Pawn->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
		Aim->HitResult.ImpactPoint = Aim->HitResult.Location;
		Aim->FedCount = 0;
		FGameplayAbilityTargetDataHandle Data;
		Data.Add(Aim);

		FScopedPredictionWindow Window(ASC, true);
		ASC->CallServerSetReplicatedTargetData(Spec->Handle, Instance->GetCurrentActivationInfo().GetActivationPredictionKey(), Data, FGameplayTag(), ASC->ScopedPredictionKey);
	}

	/**
	 * Joueurs prêts, cible écartée, projectiles du lanceur suivis, visée déterministe, coût de la grande boule de feu
	 * réglé à Cost sur les instances du serveur et du client 0, énergie du lanceur à Energy (vue par le client 0).
	 */
	void QueueSetup(float Energy)
	{
		StartEnergy = Energy;
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : écarte la cible, suit les projectiles, coût et énergie"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				ASSERT_THAT(IsNotNull(GreatFireballClass.Get(), TEXT("GA_GreatFireball introuvable")));
				ServerCaster = Caster;
				ServerCasterASC = Cast<UGenAbilitySystemComponent>(Caster->GetAbilitySystemComponent());
				ASSERT_THAT(IsNotNull(ServerCasterASC.Get()));
				Other->TeleportTo(Caster->GetActorLocation() + FVector(0.f, 5000.f, 0.f), Other->GetActorRotation(), false, true);

				UGenGA_Projectile* Instance = GetInstance(ServerCasterASC.Get());
				ASSERT_THAT(IsNotNull(Instance, TEXT("Instance serveur de la grande boule de feu")));
				Instance->EnergyCost = Cost;
				ServerCasterASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), StartEnergy);
				MaxFlames = GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetMaxResourceAttribute());

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
				{
					const AGenProjectile* Projectile = Cast<AGenProjectile>(Actor);
					if (Projectile && ServerCaster.IsValid() && Projectile->GetInstigator() == ServerCaster.Get())
					{
						++ProjectileCount;
						EnergyAtSpawn = GetEnergy(ServerCasterASC.Get());
						bCooldownAtSpawn = HasCooldown(ServerCasterASC.Get());
					}
				}));
			})
			.ThenClient(TEXT("Client 0 : visée déterministe, coût sur son instance"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);

				UGenGA_Projectile* Instance = GetInstance(GetLocalGenASC(Client));
				ASSERT_THAT(IsNotNull(Instance, TEXT("Instance client de la grande boule de feu")));
				Instance->EnergyCost = Cost;
			})
			.UntilClient(TEXT("Client 0 : énergie de départ répliquée"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				return FMath::IsNearlyEqual(GetEnergy(GetLocalGenASC(Client)), StartEnergy, 0.001f);
			}, DefaultWait());
	}

	void QueueClientWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenClient(TEXT("Client 0 : départ de l'attente"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(Description, 0, [this, Seconds](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + Seconds; }, DefaultWait());
	}

	void QueueServerWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenServer(TEXT("Serveur : départ de l'attente"), [this](FBasePIENetworkComponentState& Server) { ServerMark = Server.World->GetTimeSeconds(); })
			.UntilServer(Description, [this, Seconds](FBasePIENetworkComponentState& Server) { return Server.World->GetTimeSeconds() >= ServerMark + Seconds; }, DefaultWait());
	}

	/** Sort terminé sans partir : ni projectile, ni énergie, ni cooldown, ni flamme dépensés (serveur puis client 0). */
	void QueueAssertNoCost()
	{
		Network
			.UntilServer(TEXT("Serveur : sort terminé"), [this](FBasePIENetworkComponentState&) { return !IsCastActive(ServerCasterASC.Get()); }, DefaultWait());
		// Au-delà de toute fin d'incantation possible : un tir différé serait parti
		QueueServerWait(TEXT("Serveur : 1 s après la fin"), 1.f);
		Network
			.ThenServer(TEXT("Serveur : aucun coût"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(0, ProjectileCount, TEXT("Aucun projectile")));
				ASSERT_THAT(IsNear(StartEnergy, GetEnergy(ServerCasterASC.Get()), 0.001f, TEXT("Aucune énergie dépensée")));
				ASSERT_THAT(IsFalse(HasCooldown(ServerCasterASC.Get()), TEXT("Pas de cooldown")));
				ASSERT_THAT(IsNear(MaxFlames, GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("Aucune flamme dépensée")));
			})
			.UntilClient(TEXT("Client 0 : sort terminé, énergie intacte, pas de cooldown"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				return !IsCastActive(ASC) && !HasCooldown(ASC) && FMath::IsNearlyEqual(GetEnergy(ASC), StartEnergy, 0.001f);
			}, DefaultWait());
	}

	/** Active sans tenir la touche (0 flamme) et envoie aussitôt la visée (client modifié) : le serveur diffère le départ. */
	void QueueEarlyAim()
	{
		Network
			.ThenClient(TEXT("Client 0 : active puis vise tout de suite"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, GreatFireballClass);
				ASSERT_THAT(IsNotNull(Spec));
				ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, true), TEXT("Activation refusée côté client")));
				SendAimNow(Client);
			})
			.UntilServer(TEXT("Serveur : visée reçue en avance, départ en attente"), [this](FBasePIENetworkComponentState&)
			{
				const UGenGA_Projectile* Instance = GetInstance(ServerCasterASC.Get());
				return Instance && Instance->IsWaitingForDeferredLaunch();
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : rien de payé pendant l'attente"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(0, ProjectileCount));
				ASSERT_THAT(IsNear(StartEnergy, GetEnergy(ServerCasterASC.Get()), 0.001f, TEXT("Énergie seulement au départ du tir")));
			});
	}

	// --- Énergie -----------------------------------------------------------------------------------------

	/** Pile le coût (25 pour 25) : activation acceptée (GenEnergy::CanAfford, sans dérive). Annulée ensuite : rien de payé. */
	TEST_METHOD(Energy_ExactCost_ActivationAccepted)
	{
		QueueSetup(Cost);
		Network.ThenClient(TEXT("Client 0 : appuie"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			SendInput(Client, true);
			ASSERT_THAT(IsTrue(IsCastActive(GetLocalGenASC(Client)), TEXT("25 d'énergie pour 25 : activation acceptée")));
			ASSERT_THAT(IsNear(StartEnergy, GetEnergy(GetLocalGenASC(Client)), 0.001f, TEXT("Rien de payé à l'activation")));
		});
		Network.UntilServer(TEXT("Serveur : activation acceptée"), [this](FBasePIENetworkComponentState&) { return IsCastActive(ServerCasterASC.Get()); }, DefaultWait());
		QueueCancelKey();
		QueueAssertNoCost();
	}

	/**
	 * 60 d'énergie, coût 25 : payé au lancer (35 quand le projectile apparaît), une seule fois (serveur et client), et le
	 * client ne descend jamais sous 35, même un instant (dépense prédite + valeur du serveur).
	 */
	TEST_METHOD(Energy_PaidOnceOnRelease)
	{
		QueueSetup(PaidTestEnergy);
		Network.ThenClient(TEXT("Client 0 : appuie"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			SendInput(Client, true);
			ASSERT_THAT(IsTrue(IsCastActive(GetLocalGenASC(Client))));
			ASSERT_THAT(IsNear(StartEnergy, GetEnergy(GetLocalGenASC(Client)), 0.001f, TEXT("Rien de payé à l'activation")));
		});
		QueueClientWait(TEXT("Client 0 : touche tenue 0.4 s"), 0.4f);
		Network
			.ThenServer(TEXT("Serveur : en nourrissage, rien de payé"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(IsCastActive(ServerCasterASC.Get())));
				ASSERT_THAT(IsNear(StartEnergy, GetEnergy(ServerCasterASC.Get()), 0.001f));
			})
			.ThenClient(TEXT("Client 0 : suit chaque changement d'énergie, relâche"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				ClientMinEnergy = GetEnergy(ASC);
				ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).AddLambda([this](const FOnAttributeChangeData& Data)
				{
					ClientMinEnergy = FMath::Min(ClientMinEnergy, Data.NewValue);
				});
				SendInput(Client, false);
			})
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : énergie payée au lancer avec le cooldown"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(StartEnergy - Cost, EnergyAtSpawn, 0.001f, TEXT("Coût payé quand le sort part")));
				ASSERT_THAT(IsTrue(bCooldownAtSpawn));
			});
		QueueServerWait(TEXT("Serveur : 0.5 s après le tir"), 0.5f);
		Network
			.ThenServer(TEXT("Serveur : payé une seule fois"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(StartEnergy - Cost, GetEnergy(ServerCasterASC.Get()), 0.001f, TEXT("60 - 25 = 35 (une double dépense donnerait 10)")));
			})
			.UntilClient(TEXT("Client 0 : énergie du serveur (prédiction confirmée)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				ClientMinEnergy = FMath::Min(ClientMinEnergy, GetEnergy(ASC));
				return !IsCastActive(ASC) && FMath::IsNearlyEqual(GetEnergy(ASC), StartEnergy - Cost, 0.001f);
			}, DefaultWait());
		QueueClientWait(TEXT("Client 0 : encore 0.3 s"), 0.3f);
		Network.ThenClient(TEXT("Client 0 : jamais sous 35"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			ClientMinEnergy = FMath::Min(ClientMinEnergy, GetEnergy(GetLocalGenASC(Client)));
			ASSERT_THAT(IsTrue(ClientMinEnergy >= StartEnergy - Cost - 0.001f, *FString::Printf(TEXT("Énergie la plus basse vue : %.2f"), ClientMinEnergy)));
		});
	}

	/** 24.99 pour 25 : le client refuse l'activation (CheckCost), le serveur ne voit rien partir. */
	TEST_METHOD(Energy_BelowCost_ActivationRefused)
	{
		QueueSetup(Cost - 0.01f);
		Network.ThenClient(TEXT("Client 0 : appuie"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			SendInput(Client, true);
			ASSERT_THAT(IsFalse(IsCastActive(GetLocalGenASC(Client)), TEXT("Pas assez d'énergie : activation refusée")));
			SendInput(Client, false);
		});
		QueueServerWait(TEXT("Serveur : 1 s"), 1.f);
		Network.ThenServer(TEXT("Serveur : rien n'est parti"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsFalse(IsCastActive(ServerCasterASC.Get())));
			ASSERT_THAT(AreEqual(0, ProjectileCount));
			ASSERT_THAT(IsNear(StartEnergy, GetEnergy(ServerCasterASC.Get()), 0.001f));
		});
	}

	/** Étourdi (ApplyHardCC) pendant l'incantation : interrompu, rien de payé. */
	TEST_METHOD(Energy_StunnedDuringCast_NoCost)
	{
		QueueSetup(Cost);
		Network.ThenClient(TEXT("Client 0 : appuie"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, true); });
		QueueClientWait(TEXT("Client 0 : 1 flamme nourrie"), 0.4f);
		Network.ThenClient(TEXT("Client 0 : relâche (incantation)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, false); });
		QueueClientWait(TEXT("Client 0 : en pleine incantation"), 0.15f);
		Network.ThenServer(TEXT("Serveur : étourdit le lanceur"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(IsCastActive(ServerCasterASC.Get()), TEXT("Le serveur doit être en pleine incantation")));
			ASSERT_THAT(IsTrue(ServerCasterASC->ApplyHardCC(StunnedTag(), 1.f, nullptr).IsValid()));
		});
		QueueAssertNoCost();
	}

	/** Visée en avance (départ différé), puis étourdi pendant l'attente du serveur : rien ne part, rien n'est payé. */
	TEST_METHOD(Energy_EarlyAimThenStun_NoCost)
	{
		QueueSetup(Cost);
		QueueEarlyAim();
		Network.ThenServer(TEXT("Serveur : étourdit le lanceur pendant l'attente"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(ServerCasterASC->ApplyHardCC(StunnedTag(), 1.f, nullptr).IsValid()));
		});
		QueueAssertNoCost();
	}

	/** Visée en avance : l'énergie est payée au départ différé du serveur, pas à la réception de la visée. */
	TEST_METHOD(Energy_EarlyAim_PaidAtDeferredLaunch)
	{
		QueueSetup(PaidTestEnergy);
		QueueEarlyAim();
		Network
			.UntilServer(TEXT("Serveur : projectile différé parti"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : payé au départ"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(StartEnergy - Cost, EnergyAtSpawn, 0.001f));
			});
		QueueServerWait(TEXT("Serveur : 0.8 s après (la vraie visée du client arrive)"), 0.8f);
		Network
			.ThenServer(TEXT("Serveur : payé une seule fois"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(StartEnergy - Cost, GetEnergy(ServerCasterASC.Get()), 0.001f));
			})
			.UntilClient(TEXT("Client 0 : énergie du serveur"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				return !IsCastActive(ASC) && FMath::IsNearlyEqual(GetEnergy(ASC), StartEnergy - Cost, 0.001f);
			}, DefaultWait());
	}

	// --- Touche d'annulation (Plan 3 Task 6) ------------------------------------------------------------------

	/** Annulation par la touche sur le client 0, qui relâche ensuite la touche du sort. */
	void QueueCancelKey()
	{
		Network.ThenClient(TEXT("Client 0 : touche d'annulation"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
			ASSERT_THAT(IsTrue(IsCastActive(ASC), TEXT("Le sort doit être en cours")));
			ASSERT_THAT(AreEqual(1, ASC->CancelPendingCasts(), TEXT("Un sort annulé")));
			ASSERT_THAT(IsFalse(IsCastActive(ASC), TEXT("Annulé tout de suite côté client")));
			SendInput(Client, false);
		});
	}

	/** Rien en cours : la touche ne fait rien. Pendant le nourrissage : annulé partout, rien de payé. */
	TEST_METHOD(CancelKey_DuringFeed_NoCost)
	{
		QueueSetup(Cost);
		Network.ThenClient(TEXT("Client 0 : touche d'annulation sans sort en cours"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			ASSERT_THAT(AreEqual(0, GetLocalGenASC(Client)->CancelPendingCasts(), TEXT("Rien à annuler")));
			SendInput(Client, true);
		});
		QueueClientWait(TEXT("Client 0 : 1 flamme nourrie"), 0.4f);
		Network.ThenServer(TEXT("Serveur : en nourrissage"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(IsCastActive(ServerCasterASC.Get())));
		});
		QueueCancelKey();
		QueueAssertNoCost();
	}

	/** Pendant l'incantation (après le nourrissage) : annulé partout, rien de payé. */
	TEST_METHOD(CancelKey_DuringCast_NoCost)
	{
		QueueSetup(Cost);
		Network.ThenClient(TEXT("Client 0 : appuie"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, true); });
		QueueClientWait(TEXT("Client 0 : 1 flamme nourrie"), 0.4f);
		Network.ThenClient(TEXT("Client 0 : relâche (incantation)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, false); });
		QueueClientWait(TEXT("Client 0 : en pleine incantation"), 0.15f);
		QueueCancelKey();
		QueueAssertNoCost();
	}

	/**
	 * Serveur, visée du client déjà reçue (départ différé) : l'annulation du client (touche d'annulation, chemin réel :
	 * ServerCancelAbility -> ForceCancelAbilityDueToReplication, qui force SetCanBeCanceled(true)) est refusée par le
	 * serveur (CanBeCanceled faux), comme un appel direct de CancelPendingCasts sur le serveur. Le sort part à la fin de
	 * l'incantation du serveur et le coût est payé une fois (60 -> 35).
	 */
	TEST_METHOD(CancelKey_ServerAfterAimArrived_NotCancelled_CostPaid)
	{
		QueueSetup(PaidTestEnergy);
		QueueEarlyAim();
		Network
			.ThenClient(TEXT("Client 0 : touche d'annulation après sa visée (client modifié)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(AreEqual(1, GetLocalGenASC(Client)->CancelPendingCasts(), TEXT("Le client annule sa copie (encore en incantation chez lui)")));
			});
		QueueServerWait(TEXT("Serveur : l'annulation du client est arrivée"), 0.1f);
		Network
			.ThenServer(TEXT("Serveur : annulation refusée, départ différé maintenu"), [this](FBasePIENetworkComponentState&)
			{
				const UGenGA_Projectile* Instance = GetInstance(ServerCasterASC.Get());
				ASSERT_THAT(IsTrue(ProjectileCount > 0 || (Instance && Instance->IsWaitingForDeferredLaunch()), TEXT("Le départ différé continue malgré l'annulation du client")));
				ASSERT_THAT(AreEqual(0, ServerCasterASC->CancelPendingCasts(), TEXT("Visée reçue : plus annulable")));
			})
			.UntilServer(TEXT("Serveur : projectile parti"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : coûts payés"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(StartEnergy - Cost, EnergyAtSpawn, 0.001f));
				ASSERT_THAT(IsTrue(bCooldownAtSpawn));
			});
		QueueServerWait(TEXT("Serveur : 0.5 s après"), 0.5f);
		Network.ThenServer(TEXT("Serveur : payé une seule fois"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(AreEqual(1, ProjectileCount));
			ASSERT_THAT(IsNear(StartEnergy - Cost, GetEnergy(ServerCasterASC.Get()), 0.001f));
		});
	}

	/**
	 * Revue P3 T3-7, M9 : seul le serveur est sous le coût (coût 0 sur l'instance du client, 25 sur celle du serveur,
	 * 20 d'énergie). Le client active (prédit) ; le serveur refuse l'activation (CheckCost) : rien ne part, rien n'est
	 * dépensé, le client retire sa prédiction.
	 */
	TEST_METHOD(Energy_OnlyServerBelowCost_ActivationRejected)
	{
		QueueSetup(Cost - 5.f);
		Network.ThenClient(TEXT("Client 0 : coût 0 sur son instance, appuie"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			GetInstance(GetLocalGenASC(Client))->EnergyCost = 0.f;
			SendInput(Client, true);
			ASSERT_THAT(IsTrue(IsCastActive(GetLocalGenASC(Client)), TEXT("Le client se croit assez riche : activation prédite")));
		});
		Network
			.UntilClient(TEXT("Client 0 : activation rejetée par le serveur"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				return !IsCastActive(GetLocalGenASC(Client));
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : relâche"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, false); });
		QueueServerWait(TEXT("Serveur : 1 s"), 1.f);
		Network
			.ThenServer(TEXT("Serveur : rien n'est parti, rien de payé"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsFalse(IsCastActive(ServerCasterASC.Get())));
				ASSERT_THAT(AreEqual(0, ProjectileCount));
				ASSERT_THAT(IsNear(StartEnergy, GetEnergy(ServerCasterASC.Get()), 0.001f));
				ASSERT_THAT(IsFalse(HasCooldown(ServerCasterASC.Get())));
			})
			.UntilClient(TEXT("Client 0 : énergie intacte, pas de cooldown"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				return !IsCastActive(ASC) && !HasCooldown(ASC) && FMath::IsNearlyEqual(GetEnergy(ASC), StartEnergy, 0.001f);
			}, DefaultWait());
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
