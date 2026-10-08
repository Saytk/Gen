#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
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
 * Gen.Net.InputBuffer (revue PIE finale, C-5) : tampon des appuis du client propriétaire. Bond de test (vol de 0.8 s) :
 * un clic gauche tapé dans les 0.2 dernières secondes du vol attend et part à l'atterrissage, dans l'image même ; tapé
 * 0.5 s avant l'atterrissage, il est perdu (comme avant : « Vol, sans sort »).
 */
NETWORK_TEST_CLASS(InputBuffer, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TSubclassOf<UGameplayAbility> FireballClass;
	TWeakObjectPtr<UAbilitySystemComponent> ClientASC;
	FDelegateHandle ActivatedHandle;
	/** Temps du monde du client à chaque activation de la boule de feu. */
	TArray<double> FireballTimes;
	double FlightStart = -1.0;
	double TapTime = -1.0;
	double LandTime = -1.0;
	bool bBufferedAtTap = false;
	int32 FireballsAtTap = 0;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		// Pion apparu dans le sol de test pas encore reçu par le client (sans effet sur le test)
		TestRunner->AddExpectedMessage(TEXT("could not resolve the new relative movement base actor"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, -1);
		FireballClass = LoadCurffeAbilityClass(TEXT("GA_Fireball"));
		ASSERT_THAT(IsNotNull(FireballClass.Get(), TEXT("GA_Fireball introuvable")));
		// Touche du bond de test (lue sur le CDO à l'octroi), comme Gen.Net.MeteorLeap
		GetMutableDefault<UGenNetTestGA_MeteorLeap>()->InputTag = Tag(TEXT("InputTag.Ability.3"));
		UGenNetTestGA_MeteorLeap::TestCooldownTags = FGameplayTagContainer(Tag(TEXT("Cooldown.Ability.FlameLeap")));
		UGenNetTestGA_MeteorLeap::TestRingSpawnOffset = 0.f;

		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(1)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	AFTER_EACH()
	{
		if (ClientASC.IsValid() && ActivatedHandle.IsValid())
		{
			ClientASC->AbilityActivatedCallbacks.Remove(ActivatedHandle);
		}
		ActivatedHandle.Reset();
	}

	static UGenAbilitySystemComponent* GetClientGenASC(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return Cast<UGenAbilitySystemComponent>(GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr));
	}

	static bool IsCastLocked(const FBasePIENetworkComponentState& Client)
	{
		const UGenAbilitySystemComponent* ASC = GetClientGenASC(Client);
		return ASC && ASC->HasMatchingGameplayTag(Tag(TEXT("State.CastLocked")));
	}

	/** Appui puis relâché aussitôt (comme le PlayerController, traité dans la foulée). */
	static void Tap(const FBasePIENetworkComponentState& Client, const FGameplayTag& InputTag)
	{
		UGenAbilitySystemComponent* ASC = GetClientGenASC(Client);
		ASC->AbilityInputTagPressed(InputTag);
		ASC->ProcessAbilityInput(0.f, false);
		ASC->AbilityInputTagReleased(InputTag);
		ASC->ProcessAbilityInput(0.f, false);
	}

	/** Bond accordé, lanceur au sol, boule de feu suivie ; le bond part (sans flamme) et le vol commence. */
	void QueueLeap()
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [this](FBasePIENetworkComponentState& Server) { ASSERT_THAT(IsNotNull(SpawnTestFloor(Server.World))); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : lanceur posé, bond accordé"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				PlaceOnFloor(Caster, 0.f, 0.f);
				Cast<UGenAbilitySystemComponent>(Caster->GetAbilitySystemComponent())->GrantAbilities({ UGenNetTestGA_MeteorLeap::StaticClass() }, nullptr);
			})
			.UntilClient(TEXT("Client 0 : bond répliqué, posé au sol"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const ACharacter* Pawn = GetLocalController(Client)->GetPawn<ACharacter>();
				return FindAbilitySpec(GetClientGenASC(Client), UGenNetTestGA_MeteorLeap::StaticClass()) != nullptr
					&& Pawn && FVector::Dist2D(Pawn->GetActorLocation(), FVector::ZeroVector) < 20.f && Pawn->GetCharacterMovement()->IsMovingOnGround();
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : visée, suivi de la boule de feu, bond"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(500.f, 0.f, StandingHeight);
				UGenAbilitySystemComponent* ASC = GetClientGenASC(Client);
				ASSERT_THAT(IsNotNull(FindAbilitySpec(ASC, FireballClass), TEXT("Boule de feu accordée")));
				ClientASC = ASC;
				UWorld* World = Client.World;
				ActivatedHandle = ASC->AbilityActivatedCallbacks.AddLambda([this, World](UGameplayAbility* Ability)
				{
					if (Ability && Ability->GetClass() == FireballClass.Get())
					{
						FireballTimes.Add(World->GetTimeSeconds());
					}
				});
				Tap(Client, Tag(TEXT("InputTag.Ability.3")));
			})
			.UntilClient(TEXT("Client 0 : en vol (verrou de lancement)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				if (IsCastLocked(Client))
				{
					FlightStart = Client.World->GetTimeSeconds();
					return true;
				}
				return false;
			}, DefaultWait());
	}

	/** Tap du clic gauche quand il reste RemainingFlight s de vol prévu, puis attente de l'atterrissage + 0.4 s. */
	void QueueTapAndLand(float RemainingFlight)
	{
		const float LeapDuration = GetDefault<UGenNetTestGA_MeteorLeap>()->GetLeapDuration();
		Network
			.UntilClient(TEXT("Client 0 : instant du tap"), 0, [this, LeapDuration, RemainingFlight](FBasePIENetworkComponentState& Client)
			{
				return Client.World->GetTimeSeconds() >= FlightStart + LeapDuration - RemainingFlight;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : tap du clic gauche en vol"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(IsCastLocked(Client), TEXT("Toujours en vol au moment du tap")));
				FireballsAtTap = FireballTimes.Num();
				TapTime = Client.World->GetTimeSeconds();
				Tap(Client, Tag(TEXT("InputTag.Ability.Primary")));
				bBufferedAtTap = GetClientGenASC(Client)->HasBufferedInput();
				ASSERT_THAT(AreEqual(FireballsAtTap, FireballTimes.Num(), TEXT("Rien ne part pendant le vol")));
			})
			.UntilClient(TEXT("Client 0 : atterrissage, puis 0.4 s"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const double Now = Client.World->GetTimeSeconds();
				if (LandTime < 0.0 && !IsCastLocked(Client))
				{
					LandTime = Now;
				}
				return LandTime >= 0.0 && Now >= LandTime + 0.4;
			}, DefaultWait());
	}

	TEST_METHOD(TapInLastFifthOfFlight_FiresOnLanding)
	{
		QueueLeap();
		QueueTapAndLand(0.15f);
		Network.ThenClient(TEXT("Client 0 : la boule de feu part à l'atterrissage"), 0, [this](FBasePIENetworkComponentState&)
		{
			TestRunner->AddInfo(FString::Printf(TEXT("Tap %.3f s avant l'atterrissage ; boules de feu : %d"), LandTime - TapTime, FireballTimes.Num() - FireballsAtTap));
			ASSERT_THAT(IsTrue(bBufferedAtTap, TEXT("Appui retenu par le tampon")));
			ASSERT_THAT(AreEqual(FireballsAtTap + 1, FireballTimes.Num(), TEXT("Une boule de feu après l'atterrissage")));
			ASSERT_THAT(IsTrue(FireballTimes.Last() >= LandTime - 0.05 && FireballTimes.Last() <= LandTime + 0.1,
				TEXT("Partie à l'atterrissage")));
		});
	}

	TEST_METHOD(TapHalfSecondBeforeLanding_Dropped)
	{
		QueueLeap();
		QueueTapAndLand(0.5f);
		Network.ThenClient(TEXT("Client 0 : l'appui est perdu"), 0, [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsFalse(bBufferedAtTap, TEXT("Trop tôt : pas de tampon")));
			ASSERT_THAT(AreEqual(FireballsAtTap, FireballTimes.Num(), TEXT("Aucune boule de feu")));
		});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
