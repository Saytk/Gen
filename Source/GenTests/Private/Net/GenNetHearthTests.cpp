#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Champions/Curffe/CurffeHearthComponent.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "Net/GenNetCurffeTestAbilities.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.Hearth (Plan Visuals V8) : le Foyer de Curffe (composant de BP_Curffe) vu par le lanceur (client 0) et un
 * observateur (client 1), avec le bond météore de test (nourrissable).
 * - 5 emplacements + 3 vols dans un seul composant instancié, rien sur le serveur dédié ;
 * - emplacements allumés = floor(Resource) − unités nourries (répliquées), sur chaque client ;
 * - une flamme vole vers le sort à chaque seuil (lanceur et observateur) ; annulé, les flammes reviennent ; lancé,
 *   rien ne revient.
 */
NETWORK_TEST_CLASS(Hearth, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	int32 CasterPlayerId = INDEX_NONE;
	float ClientMark = 0.f;
	/** Vols de retour vus par chaque client avant le lancer (index = client). */
	int32 ReturnsBeforeLaunch[2] = { 0, 0 };

	static FGameplayTag LeapInputTag() { return FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.3")); }

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		GetMutableDefault<UGenNetTestGA_MeteorLeap>()->InputTag = LeapInputTag();
		UGenNetTestGA_MeteorLeap::TestCooldownTags = FGameplayTagContainer();
		UGenNetTestGA_MeteorLeap::TestSegmentDuration = 0.12f;
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	AGenCharacterBase* GetCasterIn(const UWorld* World) const
	{
		const AGenPlayerState* PS = FindPlayerStateById(World, CasterPlayerId);
		return PS ? PS->GetPawn<AGenCharacterBase>() : nullptr;
	}

	UCurffeHearthComponent* GetHearthIn(const UWorld* World) const
	{
		const AGenCharacterBase* Caster = GetCasterIn(World);
		return Caster ? Caster->FindComponentByClass<UCurffeHearthComponent>() : nullptr;
	}

	/** Règle du Foyer sur ce client : allumés = floor(Resource) − nourries (bornés à 0..5). */
	bool HearthMatchesRule(const UWorld* World) const
	{
		const AGenCharacterBase* Caster = GetCasterIn(World);
		const UCurffeHearthComponent* Hearth = GetHearthIn(World);
		if (!Caster || !Hearth)
		{
			return false;
		}
		const int32 Flames = FMath::Clamp(FMath::FloorToInt32(Caster->GetResource()), 0, 5);
		return Hearth->GetVisibleFlameCount() == FMath::Clamp(Flames - Caster->GetFedResource(), 0, Flames);
	}

	static void SendLeapInput(const FBasePIENetworkComponentState& Client, bool bPressed)
	{
		const APlayerController* PC = GetLocalController(Client);
		UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr));
		if (!ASC)
		{
			return;
		}
		if (bPressed)
		{
			ASC->AbilityInputTagPressed(LeapInputTag());
		}
		else
		{
			ASC->AbilityInputTagReleased(LeapInputTag());
		}
		ASC->ProcessAbilityInput(0.f, false);
	}

	void QueueClientWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenClient(TEXT("Client 0 : départ de l'attente"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(Description, 0, [this, Seconds](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + Seconds; }, DefaultWait());
	}

	/** Sol, bond accordé au lanceur (client 0), observateur (client 1) à 6 m ; attend un Foyer plein sur chaque client. */
	void QueueSetup()
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [this](FBasePIENetworkComponentState& Server) { ASSERT_THAT(IsNotNull(SpawnTestFloor(Server.World))); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : bond accordé ; le Foyer ne dessine rien sur le serveur dédié"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Observer = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Observer));
				ServerCaster = Caster;
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				Caster->TeleportTo(FVector(0.f, 0.f, StandingHeight), FRotator::ZeroRotator, false, true);
				Observer->TeleportTo(FVector(0.f, 600.f, StandingHeight), FRotator::ZeroRotator, false, true);
				Cast<UGenAbilitySystemComponent>(Caster->GetAbilitySystemComponent())->GrantAbilities({ UGenNetTestGA_MeteorLeap::StaticClass() }, nullptr);

				const UCurffeHearthComponent* Hearth = Caster->FindComponentByClass<UCurffeHearthComponent>();
				ASSERT_THAT(IsNotNull(Hearth, TEXT("BP_Curffe porte le Foyer")));
				ASSERT_THAT(AreEqual(0, Hearth->GetInstanceCount(), TEXT("Rien sur le serveur dédié")));
			})
			.UntilClient(TEXT("Client 0 : bond répliqué"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const APlayerController* PC = GetLocalController(Client);
				return FindAbilitySpec(GetASC(PC->GetPlayerState<AGenPlayerState>()), UGenNetTestGA_MeteorLeap::StaticClass()) != nullptr;
			}, DefaultWait())
			.UntilClients(TEXT("Clients : Foyer plein, 8 instances, règle respectée"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				return Caster && Hearth && Hearth->GetInstanceCount() == 8 && Caster->GetResource() >= 5.f - KINDA_SMALL_NUMBER
					&& Hearth->GetVisibleFlameCount() == 5 && Caster->GetCharacterMovement()->IsMovingOnGround();
			}, DefaultWait());
	}

	TEST_METHOD(SocketsFollowFlames_FlightsAtThresholds_ReturnOnCancel)
	{
		QueueSetup();
		Network
			.ThenClient(TEXT("Client 0 : vise et nourrit le bond"), 0, [](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(500.f, 0.f, StandingHeight);
				SendLeapInput(Client, true);
			});
		QueueClientWait(TEXT("Client 0 : touche tenue 0.75 s (2 flammes)"), 0.75f);
		Network
			.ThenClient(TEXT("Client 0 : deux flammes nourries ; annule (touche d'annulation)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(AreEqual(2, Caster->GetFedResource(), TEXT("0.75 s : flammes à 0.3 et 0.6 s")));
				const APlayerController* PC = GetLocalController(Client);
				UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(GetASC(PC->GetPlayerState<AGenPlayerState>()));
				ASSERT_THAT(IsTrue(ASC && ASC->CancelPendingCasts() == 1, TEXT("Le bond en nourrissage est annulé")));
				SendLeapInput(Client, false);
			})
			// Lanceur : deux départs et deux retours exacts. Observateur : il voit les seuils répliqués (estimation du serveur,
			// en retard) : au moins un départ et autant de retours, après le délai de grâce (rien n'est dépensé)
			.UntilClients(TEXT("Clients : les flammes sont parties puis revenues, Foyer plein"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				const int32 Expected = Client.ClientIndex == 0 ? 2 : 1;
				if (!Caster || !Hearth || Caster->GetFedResource() != 0 || Hearth->GetStartedFlightCount() < Expected
					|| Hearth->GetStartedReturnFlightCount() < Expected || Hearth->GetFlyingFlameCount() > 0 || !HearthMatchesRule(Client.World))
				{
					return false;
				}
				ReturnsBeforeLaunch[Client.ClientIndex] = Hearth->GetStartedReturnFlightCount();
				return Hearth->GetVisibleFlameCount() == FMath::Clamp(FMath::FloorToInt32(Caster->GetResource()), 0, 5);
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : nourrit une flamme et lance le bond"), 0, [](FBasePIENetworkComponentState& Client) { SendLeapInput(Client, true); });
		QueueClientWait(TEXT("Client 0 : touche tenue 0.45 s (1 flamme)"), 0.45f);
		Network
			.ThenClient(TEXT("Client 0 : relâche"), 0, [](FBasePIENetworkComponentState& Client) { SendLeapInput(Client, false); })
			.UntilServer(TEXT("Serveur : bond terminé"), [this](FBasePIENetworkComponentState&)
			{
				const UAbilitySystemComponent* ASC = ServerCaster->GetAbilitySystemComponent();
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(const_cast<UAbilitySystemComponent*>(ASC), UGenNetTestGA_MeteorLeap::StaticClass());
				return Spec && !Spec->IsActive();
			}, DefaultWait())
			.UntilClients(TEXT("Clients : flamme dépensée au lancer, aucune ne revient"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				return Caster && Hearth && Caster->GetFedResource() == 0 && HearthMatchesRule(Client.World)
					&& Hearth->GetFlyingFlameCount() == 0;
			}, DefaultWait())
			.ThenClients(TEXT("Clients : pas de vol de retour pour une flamme dépensée"), [this](FBasePIENetworkComponentState& Client)
			{
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				ASSERT_THAT(IsNotNull(Hearth));
				ASSERT_THAT(AreEqual(ReturnsBeforeLaunch[Client.ClientIndex], Hearth->GetStartedReturnFlightCount()));
			});
	}

	/** Lanceur vu par chaque client, retenu avant sa mort (le PlayerState peut perdre son pion ensuite). */
	TMap<int32, TWeakObjectPtr<AGenCharacterBase>> ClientCasters;

	/**
	 * Revue finale, M-4 : mort en plein nourrissage. La remise à zéro du compte nourri (mort) et l'annulation du sort ne
	 * font revenir aucune flamme au Foyer d'un mort. Observateur : compte nourri et mort arrivent ensemble, aucun vol de
	 * retour. Lanceur : l'annulation du serveur peut le précéder d'une image ; le Foyer est éteint (rien ne vole).
	 */
	TEST_METHOD(DeathMidFeed_NoFlightBackIntoDeadCaster)
	{
		QueueSetup();
		Network
			.ThenClient(TEXT("Client 0 : vise et nourrit le bond"), 0, [](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(500.f, 0.f, StandingHeight);
				SendLeapInput(Client, true);
			});
		QueueClientWait(TEXT("Client 0 : touche tenue 0.75 s (2 flammes)"), 0.75f);
		Network
			.UntilClients(TEXT("Clients : flammes nourries vues ; relevé des retours"), [this](FBasePIENetworkComponentState& Client)
			{
				AGenCharacterBase* Caster = GetCasterIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				if (!Caster || !Hearth || Caster->GetFedResource() < 1)
				{
					return false;
				}
				ClientCasters.Add(Client.ClientIndex, Caster);
				ReturnsBeforeLaunch[Client.ClientIndex] = Hearth->GetStartedReturnFlightCount();
				return true;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : l'observateur tue le lanceur en plein nourrissage"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Observer = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Observer));
				ASSERT_THAT(IsTrue(ServerCaster.IsValid() && ServerCaster->GetFedResource() > 0, TEXT("Le lanceur nourrit encore")));
				ApplyDamage(Observer->GetAbilitySystemComponent(), ServerCaster->GetAbilitySystemComponent(), 100000.f);
				ASSERT_THAT(IsTrue(ServerCaster->IsDead(), TEXT("Lanceur mort")));
			})
			.UntilClients(TEXT("Clients : mort reçue, compte nourri à zéro"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = ClientCasters.FindRef(Client.ClientIndex).Get();
				return Caster && Caster->IsDead() && Caster->GetFedResource() == 0;
			}, DefaultWait());
		QueueClientWait(TEXT("Client 0 : 0.4 s (durée d'un vol de retour)"), 0.4f);
		Network
			.ThenClients(TEXT("Clients : rien ne vole vers le mort"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = ClientCasters.FindRef(Client.ClientIndex).Get();
				const UCurffeHearthComponent* Hearth = Caster ? Caster->FindComponentByClass<UCurffeHearthComponent>() : nullptr;
				ASSERT_THAT(IsNotNull(Hearth));
				TestRunner->AddInfo(FString::Printf(TEXT("Client %d : vols de retour %d -> %d"), Client.ClientIndex,
					ReturnsBeforeLaunch[Client.ClientIndex], Hearth->GetStartedReturnFlightCount()));
				ASSERT_THAT(AreEqual(0, Hearth->GetFlyingFlameCount(), TEXT("Aucun vol en cours")));
				ASSERT_THAT(AreEqual(0, Hearth->GetVisibleFlameCount(), TEXT("Foyer éteint")));
				if (Client.ClientIndex == 1)
				{
					ASSERT_THAT(AreEqual(ReturnsBeforeLaunch[1], Hearth->GetStartedReturnFlightCount(), TEXT("Observateur : aucun vol de retour")));
				}
			});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
