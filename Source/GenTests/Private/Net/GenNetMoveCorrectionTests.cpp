#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "Character/GenCharacterMovementComponent.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Net/GenNetCurffeTestAbilities.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.MoveCorrection (revue V6-V8, I-2) : les ralentis propres à chaque machine (incantation, fenêtre de contre)
 * ne provoquent aucune correction du serveur, même en bougeant sans arrêt à travers leurs bornes : le client envoie ses
 * mouvements en attente avant les RPC de sort (FlushMovesToServer), et le serveur accepte un petit écart juste après
 * un changement de ralenti (UGenCharacterMovementComponent, grâce de correction). Revue finale, I-1 : cette grâce est un
 * budget par changement de ralenti ; un client qui triche pendant la grâce est corrigé une fois le budget épuisé.
 */
NETWORK_TEST_CLASS(MoveCorrection, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	int32 CorrectionsBefore = 0;
	float ClientMark = 0.f;
	float ClientEndTime = -1.f;
	float BaseClientSpeed = 0.f;
	float MinClientSpeed = 0.f;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		// Avant la mesure : le pion apparaît dans le sol de test pas encore reçu par le client, la correction du serveur
		// nomme une base que le client ne connaît pas encore (sans effet sur la mesure, relative)
		TestRunner->AddExpectedMessage(TEXT("could not resolve the new relative movement base actor"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, -1);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(1)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	static AGenPlayerCharacter* GetLocalCharacter(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return PC ? PC->GetPawn<AGenPlayerCharacter>() : nullptr;
	}

	/** Client : entrée de mouvement continue (+Y), une fois par image. */
	static void MoveOn(const FBasePIENetworkComponentState& Client)
	{
		if (AGenPlayerCharacter* Character = GetLocalCharacter(Client))
		{
			Character->AddMovementInput(FVector(0.f, 1.f, 0.f), 1.f);
		}
	}

	UGenCharacterMovementComponent* GetServerMovement() const
	{
		return ServerCaster.IsValid() ? Cast<UGenCharacterMovementComponent>(ServerCaster->GetCharacterMovement()) : nullptr;
	}

	int32 GetServerCorrections() const
	{
		const UGenCharacterMovementComponent* Movement = GetServerMovement();
		return Movement ? Movement->GetServerCorrectionCount() : -1;
	}

	/** Revue finale, I-1 : vitesse ajoutée par le client tricheur (+55 % ; ~5 cm par mouvement à 60 Hz, sous l'ancienne tolérance par mouvement de 10 cm). */
	static constexpr float CheatSpeed = 300.f;
	/** Durée de la triche, dans la fenêtre de grâce du serveur (0.2 s). */
	static constexpr float CheatDuration = 0.12f;
	float AcceptedDistanceBefore = 0.f;

	TEST_METHOD(BackfireWindow_ContinuousMovement_NoServerCorrection)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [](FBasePIENetworkComponentState& Server) { SpawnTestFloor(Server.World); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : lanceur placé, contre accordé, mouvement de Gen"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Cast<UGenCharacterMovementComponent>(Caster->GetCharacterMovement()), TEXT("Les personnages utilisent UGenCharacterMovementComponent")));
				ServerCaster = Caster;
				Caster->TeleportTo(FVector(0.f, -1500.f, StandingHeight), FRotator::ZeroRotator, false, true);
				Cast<UGenAbilitySystemComponent>(Caster->GetAbilitySystemComponent())->GrantAbilities({ UGenNetTestGA_Backfire::StaticClass() }, nullptr);
			})
			.UntilClient(TEXT("Client 0 : contre répliqué, posé au sol"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				return Character && FindAbilitySpec(Character->GetAbilitySystemComponent(), UGenNetTestGA_Backfire::StaticClass()) != nullptr
					&& Character->GetCharacterMovement()->IsMovingOnGround() && FVector::Dist2D(Character->GetActorLocation(), FVector(0.f, -1500.f, 0.f)) < 20.f;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : départ de la course"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ClientMark = Client.World->GetTimeSeconds();
				BaseClientSpeed = GetLocalCharacter(Client)->GetCharacterMovement()->MaxWalkSpeed;
				MinClientSpeed = BaseClientSpeed;
			})
			.UntilClient(TEXT("Client 0 : court 0.5 s (vitesse établie)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				return Client.World->GetTimeSeconds() >= ClientMark + 0.5f;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : relevé des corrections"), [this](FBasePIENetworkComponentState&)
			{
				CorrectionsBefore = GetServerCorrections();
				ASSERT_THAT(IsTrue(CorrectionsBefore >= 0));
			})
			.ThenClient(TEXT("Client 0 : lance le contre en courant"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(0.f, 3000.f, StandingHeight);
				AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, UGenNetTestGA_Backfire::StaticClass());
				// Même chemin que la touche (UGenAbilitySystemComponent::ProcessAbilityInput) : mouvements envoyés avant la RPC
				Character->FlushMovesToServer();
				ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, true), TEXT("Activation refusée côté client")));
				ClientEndTime = -1.f;
			})
			.UntilClient(TEXT("Client 0 : court pendant l'incantation et toute la fenêtre, puis 0.5 s"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				const AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				MinClientSpeed = FMath::Min(MinClientSpeed, Character->GetCharacterMovement()->MaxWalkSpeed);
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(Character->GetAbilitySystemComponent(), UGenNetTestGA_Backfire::StaticClass());
				const float Now = Client.World->GetTimeSeconds();
				if (ClientEndTime < 0.f && Spec && !Spec->IsActive())
				{
					ClientEndTime = Now;
				}
				return ClientEndTime >= 0.f && Now >= ClientEndTime + 0.5f;
			}, FTimespan::FromSeconds(20.0))
			.ThenClient(TEXT("Client 0 : le ralenti de la fenêtre a bien joué"), 0, [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(MinClientSpeed <= BaseClientSpeed * UGenNetTestGA_Backfire::TestWindowMoveSpeedMultiplier + 1.f,
					TEXT("Vitesse ralentie pendant la fenêtre")));
			})
			.ThenServer(TEXT("Serveur : aucune correction aux bornes des ralentis"), [this](FBasePIENetworkComponentState&)
			{
				const UGenCharacterMovementComponent* Movement = Cast<UGenCharacterMovementComponent>(ServerCaster->GetCharacterMovement());
				TestRunner->AddInfo(FString::Printf(TEXT("Corrections : %d avant, %d après ; écarts acceptés par la grâce : %d"),
					CorrectionsBefore, GetServerCorrections(), Movement ? Movement->GetGraceAcceptedCount() : -1));
				ASSERT_THAT(AreEqual(CorrectionsBefore, GetServerCorrections(), TEXT("Corrections du serveur pendant l'incantation et la fenêtre")));
			});
	}

	/**
	 * Revue finale, I-1 : la grâce d'un changement de ralenti n'est pas une fenêtre de triche. Un client modifié qui court
	 * plus vite que permis pendant la grâce (CheatSpeed en plus, par petits pas sous l'ancienne tolérance par mouvement)
	 * gagne au plus le budget de la fenêtre (SpeedChangeErrorTolerance, cumulé) ; au-delà, le serveur le corrige.
	 */
	TEST_METHOD(CheatingClient_InSpeedChangeGrace_CorrectedOnceBudgetUsed)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [](FBasePIENetworkComponentState& Server) { SpawnTestFloor(Server.World); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : coureur placé"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ServerCaster = Caster;
				ASSERT_THAT(IsNotNull(GetServerMovement(), TEXT("Les personnages utilisent UGenCharacterMovementComponent")));
				Caster->TeleportTo(FVector(0.f, -1500.f, StandingHeight), FRotator::ZeroRotator, false, true);
			})
			.UntilClient(TEXT("Client 0 : posé au sol"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				return Character && Character->GetCharacterMovement()->IsMovingOnGround() && FVector::Dist2D(Character->GetActorLocation(), FVector(0.f, -1500.f, 0.f)) < 20.f;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : départ de la course"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(TEXT("Client 0 : court 0.5 s (vitesse établie)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				return Client.World->GetTimeSeconds() >= ClientMark + 0.5f;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : changement de ralenti (ouvre la grâce), relevés"), [this](FBasePIENetworkComponentState&)
			{
				UGenCharacterMovementComponent* Movement = GetServerMovement();
				ASSERT_THAT(IsNotNull(Movement));
				CorrectionsBefore = Movement->GetServerCorrectionCount();
				AcceptedDistanceBefore = Movement->GetGraceAcceptedDistance();
				Movement->NoteLocalSpeedChange();
				ASSERT_THAT(IsTrue(Movement->IsInCorrectionGrace(), TEXT("Grâce ouverte")));
			})
			.ThenClient(TEXT("Client 0 : début de la triche"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(TEXT("Client 0 : court plus vite que permis pendant la grâce"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				if (AGenPlayerCharacter* Character = GetLocalCharacter(Client))
				{
					Character->AddActorWorldOffset(FVector(0.f, CheatSpeed * Client.World->GetDeltaSeconds(), 0.f));
				}
				return Client.World->GetTimeSeconds() >= ClientMark + CheatDuration;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : fin de la triche"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(TEXT("Client 0 : court honnêtement 0.3 s"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				return Client.World->GetTimeSeconds() >= ClientMark + 0.3f;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : écart accepté borné au budget, puis corrigé"), [this](FBasePIENetworkComponentState&)
			{
				const UGenCharacterMovementComponent* Movement = GetServerMovement();
				const float Accepted = Movement->GetGraceAcceptedDistance() - AcceptedDistanceBefore;
				TestRunner->AddInfo(FString::Printf(TEXT("Triche : %.1f cm acceptés par la grâce (budget %.1f), corrections %d -> %d"),
					Accepted, Movement->GetSpeedChangeErrorBudget(), CorrectionsBefore, Movement->GetServerCorrectionCount()));
				ASSERT_THAT(IsTrue(Accepted <= Movement->GetSpeedChangeErrorBudget() + 0.01f, TEXT("Écart total accepté pendant la grâce <= budget")));
				ASSERT_THAT(IsTrue(Movement->GetServerCorrectionCount() > CorrectionsBefore, TEXT("Le client tricheur est corrigé")));
			});
	}

	/** Revue PIE finale, C-1 : relevé des annonces ramenées au plafond avant la triche. */
	int32 ClampedBefore = 0;
	static FName CheatReason() { return FName(TEXT("TestCheatClaim")); }

	/**
	 * Revue PIE finale, C-1 : les mouvements portent le multiplicateur local du client, mais le serveur ne le croit pas sur
	 * parole. Un client modifié qui se donne x2 sans raison (aucun changement de ralenti sur le serveur) est ramené au
	 * plafond du serveur (x1) à chaque mouvement, donc corrigé.
	 */
	TEST_METHOD(CheatingClient_ClaimsHigherMultiplier_ClampedAndCorrected)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [](FBasePIENetworkComponentState& Server) { SpawnTestFloor(Server.World); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : coureur placé"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ServerCaster = Caster;
				ASSERT_THAT(IsNotNull(GetServerMovement(), TEXT("Les personnages utilisent UGenCharacterMovementComponent")));
				Caster->TeleportTo(FVector(0.f, -1500.f, StandingHeight), FRotator::ZeroRotator, false, true);
			})
			.UntilClient(TEXT("Client 0 : posé au sol"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				return Character && Character->GetCharacterMovement()->IsMovingOnGround() && FVector::Dist2D(Character->GetActorLocation(), FVector(0.f, -1500.f, 0.f)) < 20.f;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : départ de la course"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(TEXT("Client 0 : court 0.5 s (vitesse établie)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				return Client.World->GetTimeSeconds() >= ClientMark + 0.5f;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : relevés"), [this](FBasePIENetworkComponentState&)
			{
				CorrectionsBefore = GetServerCorrections();
				ClampedBefore = GetServerMovement()->GetClampedSpeedClaimCount();
			})
			.ThenClient(TEXT("Client 0 : se donne x2 localement"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				ASSERT_THAT(IsNotNull(Character));
				Character->SetLocalMoveSpeedMultiplier(Character, CheatReason(), 2.f);
				ClientMark = Client.World->GetTimeSeconds();
			})
			.UntilClient(TEXT("Client 0 : court 0.6 s à x2"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				return Client.World->GetTimeSeconds() >= ClientMark + 0.6f;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : fin de la triche"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				Character->ClearLocalMoveSpeedMultiplier(Character, CheatReason());
				ClientMark = Client.World->GetTimeSeconds();
			})
			.UntilClient(TEXT("Client 0 : attend 0.3 s"), 0, [this](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + 0.3f; }, DefaultWait())
			.ThenServer(TEXT("Serveur : annonces ramenées au plafond, client corrigé"), [this](FBasePIENetworkComponentState&)
			{
				const UGenCharacterMovementComponent* Movement = GetServerMovement();
				TestRunner->AddInfo(FString::Printf(TEXT("Annonces ramenées : %d ; corrections %d -> %d"),
					Movement->GetClampedSpeedClaimCount() - ClampedBefore, CorrectionsBefore, Movement->GetServerCorrectionCount()));
				ASSERT_THAT(IsTrue(Movement->GetClampedSpeedClaimCount() > ClampedBefore, TEXT("Annonce x2 ramenée au plafond du serveur")));
				ASSERT_THAT(IsTrue(Movement->GetServerCorrectionCount() > CorrectionsBefore, TEXT("Le client qui s'est donné x2 est corrigé")));
			});
	}
};

/**
 * Gen.Net.MoveCorrectionLag (revue PIE finale, C-1) : contre en courant sous 60 ms de latence (dans chaque sens) et à
 * 20 images/s, comme la matrice PIE finale où la fin de la fenêtre (275 -> 550 cm/s) donnait une correction de 11 à
 * 17 cm. Les mouvements portent le multiplicateur local du client : aucune correction, et aucune annonce ramenée au
 * plafond (le serveur connaît la fin prévue de la fenêtre).
 */
NETWORK_TEST_CLASS(MoveCorrectionLag, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };
	FPacketSimulationSettings LagSettings;

	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	int32 CorrectionsBefore = 0;
	int32 ClampedBefore = 0;
	float ClientMark = 0.f;
	float ClientEndTime = -1.f;
	float BaseClientSpeed = 0.f;
	float MinClientSpeed = 0.f;
	float PreviousMaxFPS = 0.f;

	static constexpr float TestFrameRate = 20.f;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		TestRunner->AddExpectedMessage(TEXT("could not resolve the new relative movement base actor"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, -1);
		LagSettings = FPacketSimulationSettings();
		LagSettings.PktLag = 60;
		// Images de la matrice PIE (17 à 20 images/s) : une seule image à une autre vitesse dépasse le budget de la grâce
		if (IConsoleVariable* MaxFPS = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
		{
			PreviousMaxFPS = MaxFPS->GetFloat();
			MaxFPS->Set(TestFrameRate, ECVF_SetByCode);
		}
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(1)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.WithPacketSimulationSettings(&LagSettings)
			.Build(Network);
	}

	AFTER_EACH()
	{
		if (IConsoleVariable* MaxFPS = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
		{
			MaxFPS->Set(PreviousMaxFPS, ECVF_SetByCode);
		}
	}

	static AGenPlayerCharacter* GetLocalCharacter(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return PC ? PC->GetPawn<AGenPlayerCharacter>() : nullptr;
	}

	static void MoveOn(const FBasePIENetworkComponentState& Client)
	{
		if (AGenPlayerCharacter* Character = GetLocalCharacter(Client))
		{
			Character->AddMovementInput(FVector(0.f, 1.f, 0.f), 1.f);
		}
	}

	UGenCharacterMovementComponent* GetServerMovement() const
	{
		return ServerCaster.IsValid() ? Cast<UGenCharacterMovementComponent>(ServerCaster->GetCharacterMovement()) : nullptr;
	}

	TEST_METHOD(BackfireWhileMoving_PacketLag60_NoCorrection)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [](FBasePIENetworkComponentState& Server) { SpawnTestFloor(Server.World); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : lanceur placé, contre accordé"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ServerCaster = Caster;
				ASSERT_THAT(IsNotNull(GetServerMovement(), TEXT("Les personnages utilisent UGenCharacterMovementComponent")));
				Caster->TeleportTo(FVector(0.f, -1500.f, StandingHeight), FRotator::ZeroRotator, false, true);
				Cast<UGenAbilitySystemComponent>(Caster->GetAbilitySystemComponent())->GrantAbilities({ UGenNetTestGA_Backfire::StaticClass() }, nullptr);
			})
			.UntilClient(TEXT("Client 0 : contre répliqué, posé au sol"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				return Character && FindAbilitySpec(Character->GetAbilitySystemComponent(), UGenNetTestGA_Backfire::StaticClass()) != nullptr
					&& Character->GetCharacterMovement()->IsMovingOnGround() && FVector::Dist2D(Character->GetActorLocation(), FVector(0.f, -1500.f, 0.f)) < 20.f;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : départ de la course"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ClientMark = Client.World->GetTimeSeconds();
				BaseClientSpeed = GetLocalCharacter(Client)->GetCharacterMovement()->MaxWalkSpeed;
				MinClientSpeed = BaseClientSpeed;
			})
			.UntilClient(TEXT("Client 0 : court 0.8 s (vitesse établie, latence absorbée)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				return Client.World->GetTimeSeconds() >= ClientMark + 0.8f;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : relevés"), [this](FBasePIENetworkComponentState&)
			{
				CorrectionsBefore = GetServerMovement()->GetServerCorrectionCount();
				ClampedBefore = GetServerMovement()->GetClampedSpeedClaimCount();
			})
			.ThenClient(TEXT("Client 0 : lance le contre en courant (chemin de la touche)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(0.f, 3000.f, StandingHeight);
				AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, UGenNetTestGA_Backfire::StaticClass());
				Character->FlushMovesToServer();
				ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, true), TEXT("Activation refusée côté client")));
				ClientEndTime = -1.f;
			})
			.UntilClient(TEXT("Client 0 : court pendant l'incantation, la fenêtre, puis 0.8 s"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				MoveOn(Client);
				const AGenPlayerCharacter* Character = GetLocalCharacter(Client);
				MinClientSpeed = FMath::Min(MinClientSpeed, Character->GetCharacterMovement()->MaxWalkSpeed);
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(Character->GetAbilitySystemComponent(), UGenNetTestGA_Backfire::StaticClass());
				const float Now = Client.World->GetTimeSeconds();
				if (ClientEndTime < 0.f && Spec && !Spec->IsActive())
				{
					ClientEndTime = Now;
				}
				return ClientEndTime >= 0.f && Now >= ClientEndTime + 0.8f;
			}, FTimespan::FromSeconds(20.0))
			.ThenClient(TEXT("Client 0 : le ralenti de la fenêtre a bien joué"), 0, [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(MinClientSpeed <= BaseClientSpeed * UGenNetTestGA_Backfire::TestWindowMoveSpeedMultiplier + 1.f,
					TEXT("Vitesse ralentie pendant la fenêtre")));
			})
			.ThenServer(TEXT("Serveur : aucune correction, aucune annonce ramenée"), [this](FBasePIENetworkComponentState&)
			{
				const UGenCharacterMovementComponent* Movement = GetServerMovement();
				TestRunner->AddInfo(FString::Printf(TEXT("Latence 60 ms, %.0f images/s : corrections %d -> %d ; annonces ramenées %d ; écarts acceptés par la grâce %d"),
					TestFrameRate, CorrectionsBefore, Movement->GetServerCorrectionCount(), Movement->GetClampedSpeedClaimCount() - ClampedBefore,
					Movement->GetGraceAcceptedCount()));
				ASSERT_THAT(AreEqual(CorrectionsBefore, Movement->GetServerCorrectionCount(), TEXT("Corrections du serveur (contre en courant, latence 60 ms)")));
				ASSERT_THAT(AreEqual(ClampedBefore, Movement->GetClampedSpeedClaimCount(), TEXT("Annonces du client honnête ramenées au plafond")));
			});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
