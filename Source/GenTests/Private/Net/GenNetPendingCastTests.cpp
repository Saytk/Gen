#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "Actors/GenProjectile.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "Net/GenNetTestAbilities.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.PendingCast : règle générique « un seul sort incanté à la fois » (UGenGA_Cast::CancelOtherPendingCasts),
 * revue de la Task 5 du plan 2 (I-4). Sorts C++ de test SANS CancelAbilitiesWithTag : seule la règle du code peut annuler.
 * - Un nouveau sort annule l'incantation en cours (client et serveur) : le premier ne part jamais, le second part.
 * - Un sort déjà parti qui reste actif (fenêtre de contre, bond) n'est jamais annulé par un autre sort.
 */
NETWORK_TEST_CLASS(PendingCast, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<UAbilitySystemComponent> ServerCasterASC;
	int32 ProjectileCount = 0;
	float ClientMark = 0.f;
	float ServerMark = 0.f;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
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

	static UAbilitySystemComponent* GetLocalASC(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr);
	}

	static const UGenGA_Cast* GetInstance(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		return Spec ? Cast<UGenGA_Cast>(Spec->GetPrimaryInstance()) : nullptr;
	}

	static bool IsActive(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		return Spec && Spec->IsActive();
	}

	/** Client 0 : active le sort par son handle (chemin LocalPredicted réel, sans touche). */
	void Activate(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		UAbilitySystemComponent* ASC = GetLocalASC(Client);
		FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		ASSERT_THAT(IsNotNull(Spec, TEXT("Sort de test non répliqué au client")));
		ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, true), TEXT("Activation refusée côté client")));
	}

	void QueueSetup()
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sorts de test accordés, cible écartée, projectiles suivis"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				ServerCaster = Caster;
				ServerCasterASC = Caster->GetAbilitySystemComponent();
				Other->TeleportTo(Caster->GetActorLocation() + FVector(0.f, 5000.f, 0.f), Other->GetActorRotation(), false, true);

				UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(ServerCasterASC.Get());
				ASSERT_THAT(IsNotNull(GenASC));
				GenASC->GrantAbilities({ UGenNetTestGA_SlowCast::StaticClass(), UGenNetTestGA_QuickCast::StaticClass(), UGenNetTestGA_Lingering::StaticClass() }, nullptr);

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
				{
					if (Cast<AGenProjectile>(Actor) && ServerCaster.IsValid() && Actor->GetInstigator() == ServerCaster.Get())
					{
						++ProjectileCount;
					}
				}));
			})
			.UntilClient(TEXT("Client 0 : sorts de test répliqués"), 0, [](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalASC(Client);
				return FindAbilitySpec(ASC, UGenNetTestGA_SlowCast::StaticClass()) && FindAbilitySpec(ASC, UGenNetTestGA_QuickCast::StaticClass())
					&& FindAbilitySpec(ASC, UGenNetTestGA_Lingering::StaticClass());
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : visée déterministe"), 0, [](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
			});
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

	/** Incantation longue en cours, nouveau sort : la première est annulée des deux côtés et ne part jamais. */
	TEST_METHOD(NewCast_CancelsPendingCast_WithoutCancelTags)
	{
		QueueSetup();
		Network.ThenClient(TEXT("Client 0 : lance le sort lent (1 s)"), 0, [this](FBasePIENetworkComponentState& Client) { Activate(Client, UGenNetTestGA_SlowCast::StaticClass()); });
		QueueClientWait(TEXT("Client 0 : en pleine incantation"), 0.3f);
		Network
			.ThenServer(TEXT("Serveur : sort lent en incantation"), [this](FBasePIENetworkComponentState&)
			{
				const UGenGA_Cast* Slow = GetInstance(ServerCasterASC.Get(), UGenNetTestGA_SlowCast::StaticClass());
				ASSERT_THAT(IsTrue(Slow && Slow->IsCastPending(), TEXT("Le serveur doit incanter le sort lent")));
			})
			.ThenClient(TEXT("Client 0 : lance le sort rapide"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				Activate(Client, UGenNetTestGA_QuickCast::StaticClass());
				ASSERT_THAT(IsFalse(IsActive(GetLocalASC(Client), UGenNetTestGA_SlowCast::StaticClass()), TEXT("Client : le sort lent est annulé par le nouveau sort")));
			})
			.UntilServer(TEXT("Serveur : sort lent annulé, projectile du sort rapide"), [this](FBasePIENetworkComponentState&)
			{
				return ProjectileCount > 0 && !IsActive(ServerCasterASC.Get(), UGenNetTestGA_SlowCast::StaticClass());
			}, DefaultWait());
		QueueServerWait(TEXT("Serveur : au-delà de la fin d'incantation du sort lent"), 1.2f);
		Network.ThenServer(TEXT("Serveur : un seul projectile (le sort lent n'est jamais parti)"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(AreEqual(1, ProjectileCount));
			ASSERT_THAT(IsFalse(IsActive(ServerCasterASC.Get(), UGenNetTestGA_SlowCast::StaticClass())));
		});
	}

	/** Sort déjà parti et toujours actif : un autre sort part sans l'annuler (ni client ni serveur). */
	TEST_METHOD(NewCast_LeavesReleasedCastAlone)
	{
		QueueSetup();
		Network
			.ThenClient(TEXT("Client 0 : lance le sort qui reste actif"), 0, [this](FBasePIENetworkComponentState& Client) { Activate(Client, UGenNetTestGA_Lingering::StaticClass()); })
			.UntilServer(TEXT("Serveur : sort parti, toujours actif"), [this](FBasePIENetworkComponentState&)
			{
				const UGenGA_Cast* Lingering = GetInstance(ServerCasterASC.Get(), UGenNetTestGA_Lingering::StaticClass());
				return Lingering && Lingering->IsActive() && !Lingering->IsCastPending();
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : lance le sort rapide pendant qu'il reste actif"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const UGenGA_Cast* Lingering = GetInstance(GetLocalASC(Client), UGenNetTestGA_Lingering::StaticClass());
				ASSERT_THAT(IsTrue(Lingering && Lingering->IsActive() && !Lingering->IsCastPending(), TEXT("Client : le premier sort doit être parti et actif")));
				Activate(Client, UGenNetTestGA_QuickCast::StaticClass());
				ASSERT_THAT(IsTrue(IsActive(GetLocalASC(Client), UGenNetTestGA_Lingering::StaticClass()), TEXT("Client : un sort parti n'est pas annulé")));
			})
			.UntilServer(TEXT("Serveur : projectile du sort rapide"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : le sort parti est toujours actif"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsTrue(IsActive(ServerCasterASC.Get(), UGenNetTestGA_Lingering::StaticClass()), TEXT("Serveur : un sort parti n'est pas annulé par un autre sort")));
			})
			.UntilServer(TEXT("Serveur : le sort qui reste actif finit de lui-même"), [this](FBasePIENetworkComponentState&)
			{
				return !IsActive(ServerCasterASC.Get(), UGenNetTestGA_Lingering::StaticClass());
			}, DefaultWait());
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
