#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenFeeding.h"
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
 * Gen.Net.CastRules : règles serveur des sorts de projectile de Curffe (contrat réseau de UGenGA_Cast, vu à travers
 * GA_Fireball et GA_GreatFireball),
 * serveur dédié + 2 clients, le client 0 lance, le client 1 observe.
 * - Coût et cooldown seulement au lancer : nourri à 2 -> exactement 2 flammes et le cooldown ; annulé, étourdi -> rien.
 * - Visée en avance : le serveur garde l'incantation (barre vue par les autres) jusqu'à sa propre fin, un seul tir
 *   même si la visée arrive deux fois, et un étourdissement pendant l'attente annule le tir sans coût.
 * - Répétition automatique : M1 maintenu ne relance pas la boule de feu pendant l'incantation d'un autre sort.
 * - Fenêtre de grâce du serveur (revue V2-V4, I1) : State.FastFeeding / State.FreeResource qui changent sur le serveur
 *   juste avant (ou juste après) l'appui ou le lancer du client ne lui coûtent ni une flamme ni une dépense ; changés
 *   longtemps avant, les règles du serveur s'appliquent (rien à gagner pour un client qui garderait le tag).
 * Les appuis passent par l'ASC du client (AbilityInputTagPressed / Released + ProcessAbilityInput), comme le
 * PlayerController, sans injection d'input.
 */
NETWORK_TEST_CLASS(CastRules, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TSubclassOf<UGameplayAbility> GreatFireballClass;
	TSubclassOf<UGameplayAbility> FireballClass;

	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<UAbilitySystemComponent> ServerCasterASC;
	int32 CasterPlayerId = INDEX_NONE;
	float MaxFlames = 0.f;

	/** Relevés à l'apparition des projectiles du lanceur (serveur). */
	int32 ProjectileCount = 0;
	float FlamesAtSpawn = -1.f;
	bool bCooldownAtSpawn = false;
	float SpawnTime = -1.f;

	/** Horloges des étapes (temps du monde du client 0 ou du serveur). */
	float ClientMark = 0.f;
	float ServerMark = 0.f;
	float CastStartServerTime = -1.f;

	/** Répétition automatique : activations de la boule de feu vues pendant l'incantation de la grande. */
	int32 GatedActivations = 0;
	int32 ActivationsAfterCast = 0;
	bool bFireballWasActive = false;
	bool bObserverSawCast = false;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		// Visée collée à l'activation (tests de la visée en avance) : Warning attendu, limité à un toutes les 5 s
		TestRunner->AddExpectedMessage(TEXT("Visée très en avance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);

		GreatFireballClass = LoadCurffeAbilityClass(TEXT("GA_GreatFireball"));
		FireballClass = LoadCurffeAbilityClass(TEXT("GA_Fireball"));

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

	static const UGenGameplayAbility* GetCDO(TSubclassOf<UGameplayAbility> AbilityClass)
	{
		return AbilityClass ? Cast<UGenGameplayAbility>(AbilityClass->GetDefaultObject()) : nullptr;
	}

	bool HasCooldown(const UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass) const
	{
		const UGenGameplayAbility* CDO = GetCDO(AbilityClass);
		const FGameplayTagContainer* Tags = CDO ? CDO->GetCooldownTags() : nullptr;
		return ASC && Tags && !Tags->IsEmpty() && ASC->HasAnyMatchingGameplayTags(*Tags);
	}

	static UGenAbilitySystemComponent* GetLocalGenASC(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return Cast<UGenAbilitySystemComponent>(GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr));
	}

	static bool IsAbilityActive(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		return Spec && Spec->IsActive();
	}

	UGenGA_Projectile* GetServerInstance(TSubclassOf<UGameplayAbility> AbilityClass) const
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpec(ServerCasterASC.Get(), AbilityClass);
		return Spec ? Cast<UGenGA_Projectile>(Spec->GetPrimaryInstance()) : nullptr;
	}

	/** Appui (bPressed) ou relâché d'une touche de sort sur le client, traité dans la foulée comme le ferait le PC. */
	static void SendInput(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass, bool bPressed)
	{
		UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
		const UGenGameplayAbility* CDO = GetCDO(AbilityClass);
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

	/** Client modifié : envoie tout de suite une visée pour l'activation en cours du sort (fenêtre de prédiction comme le vrai client). */
	static void SendAimNow(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
		FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
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

	/** Joueurs prêts, cible écartée, apparitions des projectiles du lanceur suivies, visée déterministe sur le client 0. */
	void QueueSetup()
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : écarte la cible, suit les projectiles, Foyer plein"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				ASSERT_THAT(IsNotNull(GreatFireballClass.Get(), TEXT("GA_GreatFireball introuvable")));
				ASSERT_THAT(IsNotNull(FireballClass.Get(), TEXT("GA_Fireball introuvable")));
				ServerCaster = Caster;
				ServerCasterASC = Caster->GetAbilitySystemComponent();
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				Other->TeleportTo(Caster->GetActorLocation() + FVector(0.f, 5000.f, 0.f), Other->GetActorRotation(), false, true);

				MaxFlames = GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetMaxResourceAttribute());
				ASSERT_THAT(IsTrue(MaxFlames >= 3.f, TEXT("Curffe doit avoir au moins 3 flammes")));
				ASSERT_THAT(IsNear(MaxFlames, GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetResourceAttribute()), 0.01f));

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
				{
					const AGenProjectile* Projectile = Cast<AGenProjectile>(Actor);
					if (Projectile && ServerCaster.IsValid() && Projectile->GetInstigator() == ServerCaster.Get())
					{
						++ProjectileCount;
						FlamesAtSpawn = GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetResourceAttribute());
						bCooldownAtSpawn = HasCooldown(ServerCasterASC.Get(), GreatFireballClass);
						SpawnTime = Actor->GetWorld()->GetTimeSeconds();
					}
				}));
			})
			.ThenClient(TEXT("Client 0 : visée déterministe"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
			});
	}

	/** Attend Seconds secondes du monde du client 0. */
	void QueueClientWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenClient(TEXT("Client 0 : départ de l'attente"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(Description, 0, [this, Seconds](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + Seconds; }, DefaultWait());
	}

	/** Attend Seconds secondes du monde du serveur. */
	void QueueServerWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenServer(TEXT("Serveur : départ de l'attente"), [this](FBasePIENetworkComponentState& Server) { ServerMark = Server.World->GetTimeSeconds(); })
			.UntilServer(Description, [this, Seconds](FBasePIENetworkComponentState& Server) { return Server.World->GetTimeSeconds() >= ServerMark + Seconds; }, DefaultWait());
	}

	/** Grande boule de feu terminée sans tir : ni projectile, ni cooldown, ni flamme dépensée, sur le serveur et le client 0. */
	void QueueAssertNoCost()
	{
		Network
			.UntilServer(TEXT("Serveur : grande boule de feu terminée"), [this](FBasePIENetworkComponentState&) { return !IsAbilityActive(ServerCasterASC.Get(), GreatFireballClass); }, DefaultWait());
		// Au-delà de toute fin d'incantation possible : un tir différé serait parti
		QueueServerWait(TEXT("Serveur : 1 s après la fin"), 1.f);
		Network
			.ThenServer(TEXT("Serveur : aucun coût"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(0, ProjectileCount, TEXT("Aucun projectile")));
				ASSERT_THAT(IsFalse(HasCooldown(ServerCasterASC.Get(), GreatFireballClass), TEXT("Pas de cooldown")));
				ASSERT_THAT(IsNear(MaxFlames, GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("Aucune flamme dépensée")));
				ASSERT_THAT(AreEqual(0, ServerCaster->GetFedResource(), TEXT("Plus aucune flamme en cours de nourrissage")));
			})
			.UntilClient(TEXT("Client 0 : sort terminé, pas de cooldown (prédiction annulée)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				return !IsAbilityActive(ASC, GreatFireballClass) && !HasCooldown(ASC, GreatFireballClass)
					&& FMath::IsNearlyEqual(GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()), MaxFlames, 0.01f);
			}, DefaultWait());
	}

	// --- Coût et cooldown au lancer ----------------------------------------------------------------------

	/** Touche tenue 0.75 s (seuils à 0.3 et 0.6 s) : 2 flammes, dépensées une seule fois avec le cooldown au lancer. */
	TEST_METHOD(GreatFireball_FedTwo_ServerSpendsTwoAndCommits)
	{
		QueueSetup();
		Network.ThenClient(TEXT("Client 0 : appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : touche tenue 0.75 s"), 0.75f);
		Network
			.ThenClient(TEXT("Client 0 : relâche (2 flammes)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : 2 flammes et cooldown au lancer"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsTrue(bCooldownAtSpawn, TEXT("Cooldown appliqué au lancer")));
				ASSERT_THAT(IsNear(MaxFlames - 2.f, FlamesAtSpawn, 0.01f, TEXT("Exactement 2 flammes dépensées")));
			});
		QueueServerWait(TEXT("Serveur : 0.5 s après le tir"), 0.5f);
		Network.ThenServer(TEXT("Serveur : toujours un seul tir et une seule dépense"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(AreEqual(1, ProjectileCount));
			ASSERT_THAT(IsFalse(IsAbilityActive(ServerCasterASC.Get(), GreatFireballClass)));
			// La régénération du Foyer (une flamme toutes les 3 s) ne peut qu'ajouter
			ASSERT_THAT(IsTrue(GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetResourceAttribute()) >= MaxFlames - 2.f - 0.01f));
		});
	}

	// --- Nourrissage rapide et ressource gratuite (Plan 3 Task 2) ------------------------------------------

	/** Tag lâche (non répliqué) posé sur le serveur et le client 0, comme le ferait un GE de Combustion des deux côtés. */
	void QueueLooseTagOnCaster(const TCHAR* TagName)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName));
		Network
			.ThenServer(TEXT("Serveur : tag sur le lanceur"), [this, Tag](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(Tag.IsValid(), TEXT("Tag inconnu")));
				ServerCasterASC->AddLooseGameplayTag(Tag);
			})
			.ThenClient(TEXT("Client 0 : tag sur son lanceur"), 0, [Tag](FBasePIENetworkComponentState& Client)
			{
				GetLocalGenASC(Client)->AddLooseGameplayTag(Tag);
			});
	}

	/**
	 * State.FastFeeding : seuils à 0.15 / 0.3 / 0.45 s. Touche tenue 0.6 s : 3 flammes (au rythme normal : 2 au plus).
	 * Le serveur valide sur SON intervalle figé au début du nourrissage : au rythme normal il corrigerait 3 -> 2
	 * (borne de temps floor(0.45 / 0.3) + 1), donc 3 flammes dépensées prouvent la validation sur l'intervalle rapide.
	 */
	TEST_METHOD(GreatFireball_FastFeeding_ThreeFlamesValidatedOnTheFastInterval)
	{
		QueueSetup();
		QueueLooseTagOnCaster(TEXT("State.FastFeeding"));
		Network.ThenClient(TEXT("Client 0 : appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : touche tenue 0.6 s"), 0.6f);
		Network
			.ThenClient(TEXT("Client 0 : relâche (3 flammes)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : 3 flammes dépensées, sans correction"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(MaxFlames - 3.f, FlamesAtSpawn, 0.01f, TEXT("Exactement 3 flammes, validées sur l'intervalle rapide")));
			});
	}

	/** State.FreeResource : 2 flammes nourries, le sort part avec ses 2 seuils et son cooldown, le Foyer garde ses flammes. */
	TEST_METHOD(GreatFireball_FreeResource_FedTwo_NoFlameSpent)
	{
		QueueSetup();
		QueueLooseTagOnCaster(TEXT("State.FreeResource"));
		Network.ThenClient(TEXT("Client 0 : appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : touche tenue 0.75 s"), 0.75f);
		Network
			.ThenClient(TEXT("Client 0 : relâche (2 flammes)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : cooldown, aucune flamme dépensée"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsTrue(bCooldownAtSpawn, TEXT("Le sort est bien lancé (cooldown)")));
				ASSERT_THAT(IsNear(MaxFlames, FlamesAtSpawn, 0.01f, TEXT("Flammes illimitées : rien de dépensé")));
				ASSERT_THAT(AreEqual(0, ServerCaster->GetFedResource(), TEXT("Plus aucune flamme en cours de nourrissage")));
			})
			.UntilClient(TEXT("Client 0 : Foyer plein (pas de dépense prédite restée)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				return FMath::IsNearlyEqual(GetAttribute(GetLocalGenASC(Client), UGenAttributeSet::GetResourceAttribute()), MaxFlames, 0.01f);
			}, DefaultWait());
	}

	/** Annulé pendant le nourrissage (EndAbility annulé, répliqué) : rien de payé. */
	TEST_METHOD(GreatFireball_CancelledDuringFeed_NoCost)
	{
		QueueSetup();
		Network.ThenClient(TEXT("Client 0 : appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : 1 flamme nourrie"), 0.4f);
		Network.ThenClient(TEXT("Client 0 : annule le sort"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
			const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, GreatFireballClass);
			ASSERT_THAT(IsTrue(Spec && Spec->IsActive(), TEXT("Le sort doit être en cours")));
			ASC->CancelAbilityHandle(Spec->Handle);
			SendInput(Client, GreatFireballClass, false);
		});
		QueueAssertNoCost();
	}

	/**
	 * Annulé par un appui sur la boule de feu pendant l'incantation : rien de payé pour la grande. Deux chemins y mènent
	 * (CancelAbilitiesWithTag de l'asset et UGenGA_Cast::CancelOtherPendingCasts) ; la règle du code seule est épinglée
	 * par Gen.Net.PendingCast (sorts C++ sans tag d'annulation).
	 */
	TEST_METHOD(GreatFireball_CancelledByFireballPress_NoCost)
	{
		QueueSetup();
		Network.ThenClient(TEXT("Client 0 : appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : 1 flamme nourrie"), 0.4f);
		Network.ThenClient(TEXT("Client 0 : relâche (incantation)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); });
		QueueClientWait(TEXT("Client 0 : en pleine incantation"), 0.2f);
		Network
			.ThenClient(TEXT("Client 0 : appuie sur la boule de feu"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				SendInput(Client, FireballClass, true);
				SendInput(Client, FireballClass, false);
				ASSERT_THAT(IsFalse(IsAbilityActive(GetLocalGenASC(Client), GreatFireballClass), TEXT("La boule de feu annule la grande côté client")));
			});
		// Seuls les projectiles comptent ici : la boule de feu tire, la grande non (pas de cooldown, pas de flamme)
		Network
			.UntilServer(TEXT("Serveur : grande boule de feu terminée"), [this](FBasePIENetworkComponentState&) { return !IsAbilityActive(ServerCasterASC.Get(), GreatFireballClass); }, DefaultWait());
		QueueServerWait(TEXT("Serveur : 1 s après"), 1.f);
		Network.ThenServer(TEXT("Serveur : grande boule de feu sans coût"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsFalse(HasCooldown(ServerCasterASC.Get(), GreatFireballClass), TEXT("Pas de cooldown")));
			ASSERT_THAT(IsNear(MaxFlames, GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("Aucune flamme dépensée")));
			ASSERT_THAT(IsTrue(ProjectileCount <= 1, TEXT("Au plus le projectile de la boule de feu")));
		});
	}

	/** Étourdi (contrôle dur) pendant l'incantation : rien de payé. */
	TEST_METHOD(GreatFireball_StunnedDuringCast_NoCost)
	{
		QueueSetup();
		Network.ThenClient(TEXT("Client 0 : appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : 1 flamme nourrie"), 0.4f);
		Network.ThenClient(TEXT("Client 0 : relâche (incantation)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); });
		QueueClientWait(TEXT("Client 0 : en pleine incantation"), 0.15f);
		Network.ThenServer(TEXT("Serveur : étourdit le lanceur"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(IsAbilityActive(ServerCasterASC.Get(), GreatFireballClass), TEXT("Le serveur doit être en pleine incantation")));
			ServerCasterASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")), 1, EGameplayTagReplicationState::TagOnly);
		});
		QueueAssertNoCost();
		Network.ThenServer(TEXT("Serveur : fin de l'étourdissement"), [this](FBasePIENetworkComponentState&)
		{
			ServerCasterASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")), 1, EGameplayTagReplicationState::TagOnly);
		});
	}

	/** Tout contrôle dur interrompt l'incantation (GenGameplayTags::GetHardCCTags), pas seulement l'étourdissement. */
	TEST_METHOD(GreatFireball_SilencedDuringCast_NoCost)
	{
		QueueSetup();
		Network.ThenClient(TEXT("Client 0 : appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : 1 flamme nourrie"), 0.4f);
		Network.ThenClient(TEXT("Client 0 : relâche (incantation)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); });
		QueueClientWait(TEXT("Client 0 : en pleine incantation"), 0.15f);
		Network.ThenServer(TEXT("Serveur : réduit le lanceur au silence"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(IsAbilityActive(ServerCasterASC.Get(), GreatFireballClass), TEXT("Le serveur doit être en pleine incantation")));
			ServerCasterASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Silenced")), 1, EGameplayTagReplicationState::TagOnly);
		});
		QueueAssertNoCost();
		Network.ThenServer(TEXT("Serveur : fin du silence"), [this](FBasePIENetworkComponentState&)
		{
			ServerCasterASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Silenced")), 1, EGameplayTagReplicationState::TagOnly);
		});
	}

	// --- Visée en avance -------------------------------------------------------------------------------

	/** Lance la grande boule de feu sans tenir la touche (0 flamme) et envoie aussitôt deux visées (client modifié). */
	void QueueEarlyAim()
	{
		Network
			.ThenClient(TEXT("Client 0 : active puis vise tout de suite, deux fois"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, GreatFireballClass);
				ASSERT_THAT(IsNotNull(Spec));
				ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, true), TEXT("Activation refusée côté client")));
				SendAimNow(Client, GreatFireballClass);
				SendAimNow(Client, GreatFireballClass);
			})
			.UntilServer(TEXT("Serveur : visée reçue en avance, tir en attente"), [this](FBasePIENetworkComponentState& Server)
			{
				const UGenGA_Projectile* Instance = GetServerInstance(GreatFireballClass);
				if (Instance && Instance->IsWaitingForDeferredLaunch())
				{
					if (CastStartServerTime < 0.f)
					{
						CastStartServerTime = Server.World->GetTimeSeconds();
					}
					return true;
				}
				return false;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : rien de payé pendant l'attente, incantation gardée"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(0, ProjectileCount));
				ASSERT_THAT(IsFalse(HasCooldown(ServerCasterASC.Get(), GreatFireballClass), TEXT("Cooldown seulement au départ du tir")));
				ASSERT_THAT(IsTrue(ServerCaster->GetCastInfo().IsCasting(), TEXT("Barre de cast gardée jusqu'au départ")));
				const float BaseSpeed = ServerCasterASC->GetNumericAttributeBase(UGenAttributeSet::GetMoveSpeedAttribute());
				ASSERT_THAT(IsTrue(GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetMoveSpeedAttribute()) < BaseSpeed - 1.f, TEXT("Ralenti d'incantation gardé jusqu'au départ")));
			});
	}

	/** Visée en avance : barre gardée pour les autres joueurs, un seul tir (seconde visée ignorée), coût au départ. */
	TEST_METHOD(GreatFireball_EarlyAim_KeepsCastUntilLaunch_SingleShot)
	{
		QueueSetup();
		QueueEarlyAim();
		QueueServerWait(TEXT("Serveur : 0.15 s dans l'attente"), 0.15f);
		Network
			.UntilClient(TEXT("Client 1 : voit la barre de cast du lanceur"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Client.World, CasterPlayerId);
				const AGenPlayerCharacter* Caster = PS ? PS->GetPawn<AGenPlayerCharacter>() : nullptr;
				const UGenGA_Projectile* Instance = GetServerInstance(GreatFireballClass);
				// Vu pendant que le serveur attend encore : la barre n'a pas disparu à la réception de la visée
				bObserverSawCast = Caster && Caster->GetCastInfo().IsCasting() && Instance && Instance->IsWaitingForDeferredLaunch();
				return bObserverSawCast || ProjectileCount > 0;
			}, DefaultWait())
			.ThenClient(TEXT("Client 1 : barre vue pendant l'attente"), 1, [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(bObserverSawCast, TEXT("L'observateur doit voir la barre de cast jusqu'au départ du tir")));
			})
			.UntilServer(TEXT("Serveur : projectile différé parti"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : un tir, cooldown au départ, pas avant la fin de l'incantation"), [this](FBasePIENetworkComponentState&)
			{
				const UGenGA_Projectile* CDO = Cast<UGenGA_Projectile>(GreatFireballClass->GetDefaultObject());
				ASSERT_THAT(IsNotNull(CDO));
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsTrue(bCooldownAtSpawn, TEXT("Cooldown appliqué au départ du tir")));
				ASSERT_THAT(IsNear(MaxFlames, FlamesAtSpawn, 0.01f, TEXT("0 flamme nourrie : rien dépensé")));
				// Le serveur a vu l'attente commencer au plus une image après son début d'incantation
				ASSERT_THAT(IsTrue(SpawnTime - CastStartServerTime >= 0.5f - GenFeeding::CastTimeTolerance - 0.1f, TEXT("Projectile pas avant la fin de l'incantation du serveur")));
			});
		QueueServerWait(TEXT("Serveur : 0.8 s après le tir (la vraie visée du client arrive)"), 0.8f);
		Network
			.ThenServer(TEXT("Serveur : toujours un seul tir"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsFalse(IsAbilityActive(ServerCasterASC.Get(), GreatFireballClass)));
			})
			.UntilClient(TEXT("Client 0 : cooldown confirmé par le serveur"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				return !IsAbilityActive(ASC, GreatFireballClass) && HasCooldown(ASC, GreatFireballClass);
			}, DefaultWait());
	}

	/** Visée en avance puis étourdi pendant l'attente : tir annulé, ni projectile ni coût (serveur et client). */
	TEST_METHOD(GreatFireball_EarlyAimThenStun_NoCostNoProjectile)
	{
		QueueSetup();
		QueueEarlyAim();
		Network.ThenServer(TEXT("Serveur : étourdit le lanceur pendant l'attente"), [this](FBasePIENetworkComponentState&)
		{
			ServerCasterASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")), 1, EGameplayTagReplicationState::TagOnly);
		});
		QueueAssertNoCost();
		Network.ThenServer(TEXT("Serveur : fin de l'étourdissement"), [this](FBasePIENetworkComponentState&)
		{
			ServerCasterASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")), 1, EGameplayTagReplicationState::TagOnly);
		});
	}


	// --- Fenêtre de grâce du serveur (revue V2-V4, I1) -----------------------------------------------------

	/** Tag lâche (non répliqué) sur une seule machine : le serveur (bServer) ou le client 0. bAdd faux = retrait. */
	void QueueLooseTagOnOne(const TCHAR* TagName, bool bServer, bool bAdd)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName));
		if (bServer)
		{
			Network.ThenServer(bAdd ? TEXT("Serveur seul : pose le tag") : TEXT("Serveur seul : retire le tag"), [this, Tag, bAdd](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(Tag.IsValid(), TEXT("Tag inconnu")));
				if (bAdd)
				{
					ServerCasterASC->AddLooseGameplayTag(Tag);
				}
				else
				{
					ServerCasterASC->RemoveLooseGameplayTag(Tag);
				}
			});
		}
		else
		{
			Network.ThenClient(bAdd ? TEXT("Client 0 seul : pose le tag") : TEXT("Client 0 seul : retire le tag"), 0, [Tag, bAdd](FBasePIENetworkComponentState& Client)
			{
				if (bAdd)
				{
					GetLocalGenASC(Client)->AddLooseGameplayTag(Tag);
				}
				else
				{
					GetLocalGenASC(Client)->RemoveLooseGameplayTag(Tag);
				}
			});
		}
	}

	/** Fin de l'embrasement sur le serveur juste avant l'appui (le client a encore le tag) : 3 flammes validées. */
	TEST_METHOD(GreatFireball_FastFeedingEndsOnServerJustBeforePress_ThreeValidated)
	{
		QueueSetup();
		QueueLooseTagOnCaster(TEXT("State.FastFeeding"));
		QueueLooseTagOnOne(TEXT("State.FastFeeding"), /*bServer*/ true, /*bAdd*/ false);
		Network.ThenClient(TEXT("Client 0 : appuie aussitôt (tag encore là chez lui)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : touche tenue 0.6 s (3 flammes au rythme rapide)"), 0.6f);
		Network
			.ThenClient(TEXT("Client 0 : relâche"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : 3 flammes validées dans la fenêtre de grâce"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(MaxFlames - 3.f, FlamesAtSpawn, 0.01f, TEXT("3 flammes, sans correction 3 -> 2")));
			});
	}

	/** Cas symétrique (début de l'embrasement) : le tag est posé sur le serveur juste APRÈS son activation. */
	TEST_METHOD(GreatFireball_FastFeedingStartsOnServerJustAfterPress_ThreeValidated)
	{
		QueueSetup();
		QueueLooseTagOnOne(TEXT("State.FastFeeding"), /*bServer*/ false, /*bAdd*/ true);
		Network
			.ThenClient(TEXT("Client 0 : appuie (tag prédit chez lui seulement)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); })
			.UntilServer(TEXT("Serveur : sort activé"), [this](FBasePIENetworkComponentState&) { return IsAbilityActive(ServerCasterASC.Get(), GreatFireballClass); }, DefaultWait());
		QueueLooseTagOnOne(TEXT("State.FastFeeding"), /*bServer*/ true, /*bAdd*/ true);
		QueueClientWait(TEXT("Client 0 : touche tenue 0.6 s"), 0.6f);
		Network
			.ThenClient(TEXT("Client 0 : relâche"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : 3 flammes validées"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(MaxFlames - 3.f, FlamesAtSpawn, 0.01f, TEXT("Tag posé juste après l'activation : intervalle rapide")));
			});
	}

	/**
	 * Fin des flammes illimitées sur le serveur juste avant le LANCER du client (fin de son incantation de 0.5 s) : le
	 * client lance avec le tag et ne dépense rien ; le serveur non plus (grâce), le Foyer ne remonte ni ne redescend.
	 */
	TEST_METHOD(GreatFireball_FreeResourceEndsOnServerJustBeforeRelease_NoSpend)
	{
		QueueSetup();
		QueueLooseTagOnCaster(TEXT("State.FreeResource"));
		Network.ThenClient(TEXT("Client 0 : appuie"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : touche tenue 0.7 s (2 flammes)"), 0.7f);
		Network.ThenClient(TEXT("Client 0 : relâche (incantation de 0.5 s)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); });
		QueueClientWait(TEXT("Client 0 : fin d'incantation imminente"), 0.45f);
		QueueLooseTagOnOne(TEXT("State.FreeResource"), /*bServer*/ true, /*bAdd*/ false);
		Network
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : aucune flamme dépensée dans la fenêtre de grâce"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsTrue(bCooldownAtSpawn, TEXT("Le sort est bien lancé")));
				ASSERT_THAT(IsNear(MaxFlames, FlamesAtSpawn, 0.01f, TEXT("Gratuit comme chez le client")));
			})
			.UntilClient(TEXT("Client 0 : Foyer plein"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				return FMath::IsNearlyEqual(GetAttribute(GetLocalGenASC(Client), UGenAttributeSet::GetResourceAttribute()), MaxFlames, 0.01f);
			}, DefaultWait());
	}

	/**
	 * Contrôle (rien à gagner hors de la fenêtre) : les deux tags retirés sur le serveur 1 s avant l'appui, gardés par
	 * le client. Le client nourrit 3 flammes au rythme rapide sans rien dépenser ; le serveur corrige 3 -> 2 (rythme
	 * normal) et dépense les 2.
	 */
	TEST_METHOD(GreatFireball_TagsEndedOnServerLongBefore_ServerRulesApply)
	{
		TestRunner->AddExpectedMessage(TEXT("Nourrissage corrigé par le serveur"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		QueueSetup();
		QueueLooseTagOnCaster(TEXT("State.FastFeeding"));
		QueueLooseTagOnCaster(TEXT("State.FreeResource"));
		QueueLooseTagOnOne(TEXT("State.FastFeeding"), /*bServer*/ true, /*bAdd*/ false);
		QueueLooseTagOnOne(TEXT("State.FreeResource"), /*bServer*/ true, /*bAdd*/ false);
		QueueServerWait(TEXT("Serveur : 1 s après la fin des tags"), 1.f);
		Network.ThenClient(TEXT("Client 0 : appuie (tags encore chez lui)"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, true); });
		QueueClientWait(TEXT("Client 0 : touche tenue 0.6 s"), 0.6f);
		Network
			.ThenClient(TEXT("Client 0 : relâche"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : ses règles s'appliquent"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(MaxFlames - 2.f, FlamesAtSpawn, 0.01f, TEXT("Corrigé à 2 (rythme normal) et dépensé")));
			});
	}

	/**
	 * Visée en avance, puis appui sur la boule de feu pendant l'attente du serveur (revue de la Task 5, I-4) : le nouveau
	 * sort essaie d'annuler la grande (CancelOtherPendingCasts et le tag de l'asset la voient encore « en incantation »),
	 * mais le serveur la garde (CanBeCanceled faux : le client a déjà lancé). Un seul tir de la grande, payé au départ,
	 * et la boule de feu part aussi.
	 */
	TEST_METHOD(GreatFireball_EarlyAimThenFireballPress_SingleShot)
	{
		QueueSetup();
		QueueEarlyAim();
		Network
			.ThenClient(TEXT("Client 0 : appuie sur la boule de feu pendant l'attente du serveur"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				SendInput(Client, FireballClass, true);
				SendInput(Client, FireballClass, false);
			})
			.UntilServer(TEXT("Serveur : la grande boule de feu et la boule de feu sont parties"), [this](FBasePIENetworkComponentState&)
			{
				return ProjectileCount >= 2;
			}, DefaultWait());
		QueueServerWait(TEXT("Serveur : 0.8 s après"), 0.8f);
		Network
			.ThenServer(TEXT("Serveur : un tir de chaque, grande boule de feu payée au départ"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(2, ProjectileCount, TEXT("Un projectile de la grande boule de feu et un de la boule de feu, pas plus")));
				ASSERT_THAT(IsTrue(HasCooldown(ServerCasterASC.Get(), GreatFireballClass), TEXT("La grande boule de feu a été lancée (cooldown payé au départ)")));
				ASSERT_THAT(IsFalse(IsAbilityActive(ServerCasterASC.Get(), GreatFireballClass)));
				ASSERT_THAT(IsNear(MaxFlames, GetAttribute(ServerCasterASC.Get(), UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("0 flamme nourrie : rien dépensé")));
			})
			.UntilClient(TEXT("Client 0 : cooldown de la grande boule de feu reçu du serveur"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				return !IsAbilityActive(ASC, GreatFireballClass) && HasCooldown(ASC, GreatFireballClass);
			}, DefaultWait());
	}

	// --- Répétition automatique ------------------------------------------------------------------------

	/**
	 * M1 maintenu pendant l'incantation de la grande boule de feu : aucune relance ; elle reprend après.
	 * M1 et M2 dans la même image : ProcessAbilityInput active la boule de feu puis la grande, qui annule toujours une
	 * boule de feu encore en incantation (CancelOtherPendingCasts, voulu). bFireballWasActive n'est qu'un point de départ.
	 */
	TEST_METHOD(AutoRepeat_HeldPrimaryBlockedWhileGreatFireballCasts)
	{
		QueueSetup();
		Network.ThenClient(TEXT("Client 0 : M1 et M2 enfoncés dans la même image"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
			ASSERT_THAT(IsNotNull(ASC));
			ASC->AbilityInputTagPressed(GetCDO(FireballClass)->InputTag);
			ASC->AbilityInputTagPressed(GetCDO(GreatFireballClass)->InputTag);
			ASC->ProcessAbilityInput(0.f, false);
			ASSERT_THAT(IsTrue(IsAbilityActive(ASC, GreatFireballClass), TEXT("La grande boule de feu doit être active")));
			bFireballWasActive = IsAbilityActive(ASC, FireballClass);
			ClientMark = Client.World->GetTimeSeconds();
		});
		// Pendant 0.6 s de nourrissage, chaque image : traitement des touches (comme le PC) et relances comptées
		Network
			.UntilClient(TEXT("Client 0 : M1 maintenu pendant l'incantation"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				ASC->ProcessAbilityInput(0.f, false);
				const bool bFireballActive = IsAbilityActive(ASC, FireballClass);
				if (bFireballActive && !bFireballWasActive && IsAbilityActive(ASC, GreatFireballClass))
				{
					++GatedActivations;
				}
				bFireballWasActive = bFireballActive;
				return Client.World->GetTimeSeconds() >= ClientMark + 0.6f;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : aucune relance, relâche M2"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(AreEqual(0, GatedActivations, TEXT("M1 maintenu ne doit pas relancer pendant l'incantation d'un autre sort")));
				UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				ASC->AbilityInputTagReleased(GetCDO(GreatFireballClass)->InputTag);
				ASC->ProcessAbilityInput(0.f, false);
			})
			// Contrôle : une fois la grande boule de feu partie, M1 toujours maintenu relance bien la boule de feu
			.UntilClient(TEXT("Client 0 : la boule de feu reprend après l'incantation"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				ASC->ProcessAbilityInput(0.f, false);
				const bool bFireballActive = IsAbilityActive(ASC, FireballClass);
				const bool bGreatActive = IsAbilityActive(ASC, GreatFireballClass);
				if (bFireballActive && !bFireballWasActive)
				{
					if (bGreatActive)
					{
						++GatedActivations;
					}
					else
					{
						++ActivationsAfterCast;
					}
				}
				bFireballWasActive = bFireballActive;
				return ActivationsAfterCast > 0;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : relâche M1"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(AreEqual(0, GatedActivations, TEXT("Aucune relance pendant l'incantation")));
				UGenAbilitySystemComponent* ASC = GetLocalGenASC(Client);
				ASC->AbilityInputTagReleased(GetCDO(FireballClass)->InputTag);
				ASC->ProcessAbilityInput(0.f, false);
			});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
