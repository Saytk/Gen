#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Champions/Curffe/CurffeGA_Combustion.h"
#include "Champions/Curffe/CurffeGA_LivingFlame.h"
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
 * Gen.Net.LivingFlame / Gen.Net.Combustion : sorts d'énergie de Curffe (Plan 3 Tasks 8 et 9), lancés par le client 0 à
 * travers le vrai chemin LocalPredicted, avec les classes natives (valeurs de la spec dans leur constructeur).
 * Clients : 0 lance (équipe 0), 1 ennemi (équipe 1), 2 allié (équipe 0). Sol de test posé dans chaque monde.
 * Les tags de Gen et de Curffe (non exportés) sont demandés par leur nom.
 */
namespace GenPowerSpellTest
{
	FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(Name);
	}

	UAbilitySystemComponent* LocalASC(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr);
	}

	bool IsActive(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		return Spec && Spec->IsActive();
	}

	bool HasTag(const UAbilitySystemComponent* ASC, const TCHAR* Name)
	{
		return ASC && ASC->HasMatchingGameplayTag(Tag(Name));
	}

	float Energy(const UAbilitySystemComponent* ASC) { return GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute()); }
	float Health(const UAbilitySystemComponent* ASC) { return GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()); }
	float Flames(const UAbilitySystemComponent* ASC) { return GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()); }
	float Speed(const UAbilitySystemComponent* ASC) { return GetAttribute(ASC, UGenAttributeSet::GetMoveSpeedAttribute()); }

	void PlaceOnFloor(ACharacter* Character, float X, float Y)
	{
		Character->TeleportTo(FVector(X, Y, StandingHeight), FRotator::ZeroRotator, false, true);
		Character->GetCharacterMovement()->StopMovementImmediately();
	}

	/** Serveur : dégâts de Source sur Target (GE de dégâts du projet). */
	void ApplyDamage(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Amount)
	{
		const FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(UGenGE_Damage::StaticClass(), 1.f, Source->MakeEffectContext());
		Spec.Data->SetSetByCallerMagnitude(Tag(TEXT("SetByCaller.Damage")), Amount);
		Target->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	/** Le client 0 active le sort AbilityClass (prédit) : faux si le client le refuse. */
	bool ClientActivate(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		UAbilitySystemComponent* ASC = LocalASC(Client);
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		return Spec && ASC->TryActivateAbility(Spec->Handle, true);
	}
}

using namespace GenPowerSpellTest;

/**
 * Gen.Net.PowerSpells (sorts accordés au lanceur, énergie et flammes réglées par QueueSetup) :
 *
 * LivingFlame_* (Task 8) : 60 d'énergie, Foyer vide. Pendant la forme : intouchable (serveur et client), contrôle
 * dur refusé, le client ne peut pas lancer d'autre sort, 25 payés une fois, recharge posée, canalisation vue par
 * l'ennemi. À la fin : anneau de 2.5 m qui touche l'ennemi (8 dégâts, repoussé) mais jamais le lanceur ni l'allié,
 * Foyer à 5, hâte +30 % pendant 2 s.
 *
 * Combustion_* (Task 9) : 100 d'énergie, 1 flamme.
 * - Incantation complète : rien de payé pendant les 0.5 s, 100 payés au lancer ; nova de 3 m sur l'ennemi seul (20,
 *   repoussé) ; embrasé 5 s (Pyroblast lançable, nourrissage rapide, flammes illimitées) vu par le client ; fin propre.
 * - Étourdi pendant l'incantation : rien n'est payé, pas d'embrasement.
 * - Mort pendant l'embrasement : tout disparaît (serveur et client).
 */
NETWORK_TEST_CLASS(PowerSpells, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };
	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<AGenPlayerCharacter> ServerEnemy;
	TWeakObjectPtr<AGenPlayerCharacter> ServerAlly;
	TWeakObjectPtr<UAbilitySystemComponent> CasterASC;
	float EnemyHealthBefore = 0.f;
	float AllyHealthBefore = 0.f;
	float CasterHealthBefore = 0.f;
	FVector EnemyStart = FVector::ZeroVector;
	FVector AllyStart = FVector::ZeroVector;
	float ServerMark = 0.f;
	float ClientMark = 0.f;
	
	void QueueSetup(const TArray<TSubclassOf<UGenGameplayAbility>>& Abilities, float StartEnergy, float StartFlames, float EnemyX, float AllyY)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [this](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [](FBasePIENetworkComponentState& Server) { SpawnTestFloor(Server.World); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [this](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : joueurs placés, sorts accordés, énergie et flammes"), [this, Abilities, StartEnergy, StartFlames, EnemyX, AllyY](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Enemy = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Ally = GetServerController(Server, 2)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Enemy));
				ASSERT_THAT(IsNotNull(Ally));
				ASSERT_THAT(IsTrue(Ally->GetTeamId() == Caster->GetTeamId() && Enemy->GetTeamId() != Caster->GetTeamId(), TEXT("Clients 0 et 2 alliés, client 1 ennemi")));
				ServerCaster = Caster;
				ServerEnemy = Enemy;
				ServerAlly = Ally;
				CasterASC = Caster->GetAbilitySystemComponent();
				PlaceOnFloor(Caster, 0.f, 0.f);
				PlaceOnFloor(Enemy, EnemyX, 0.f);
				PlaceOnFloor(Ally, 0.f, AllyY);
				Cast<UGenAbilitySystemComponent>(CasterASC.Get())->GrantAbilities(Abilities, nullptr);
				CasterASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), StartEnergy);
				CasterASC->SetNumericAttributeBase(UGenAttributeSet::GetResourceAttribute(), StartFlames);
			})
			.UntilClient(TEXT("Client 0 : sorts, énergie et flammes répliqués, posé"), 0, [this, Abilities, StartEnergy, StartFlames](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				for (const TSubclassOf<UGenGameplayAbility>& Ability : Abilities)
				{
					if (!FindAbilitySpec(ASC, Ability))
					{
						return false;
					}
				}
				const APawn* Pawn = GetLocalController(Client)->GetPawn();
				return FMath::IsNearlyEqual(Energy(ASC), StartEnergy, 0.01f) && FMath::IsNearlyEqual(Flames(ASC), StartFlames, 0.01f)
					&& Pawn && FVector::Dist2D(Pawn->GetActorLocation(), FVector::ZeroVector) < 20.f;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : relevés de départ"), [this](FBasePIENetworkComponentState&)
			{
				EnemyHealthBefore = Health(ServerEnemy->GetAbilitySystemComponent());
				AllyHealthBefore = Health(ServerAlly->GetAbilitySystemComponent());
				CasterHealthBefore = Health(CasterASC.Get());
				EnemyStart = ServerEnemy->GetActorLocation();
				AllyStart = ServerAlly->GetActorLocation();
			});
	}
	
	void QueueServerWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenServer(TEXT("Serveur : départ de l'attente"), [this](FBasePIENetworkComponentState& Server) { ServerMark = Server.World->GetTimeSeconds(); })
			.UntilServer(Description, [this, Seconds](FBasePIENetworkComponentState& Server) { return Server.World->GetTimeSeconds() >= ServerMark + Seconds; }, DefaultWait());
	}
	
	void QueueClientWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenClient(TEXT("Client 0 : départ de l'attente"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(Description, 0, [this, Seconds](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + Seconds; }, DefaultWait());
	}

	float BaseSpeed = 0.f;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(3)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	// --- Living Flame (Task 8) ---------------------------------------------------------------------------

	TEST_METHOD(LivingFlame_Form_Burst_Refill_Haste_EnemiesOnly)
	{
		// Ennemi à 1.5 m, allié à 1.5 m : tous deux dans l'anneau de 2.5 m
		QueueSetup({ UCurffeGA_LivingFlame::StaticClass() }, 60.f, 0.f, 150.f, 150.f);
		Network
			.ThenServer(TEXT("Serveur : vitesse de base"), [this](FBasePIENetworkComponentState&) { BaseSpeed = Speed(CasterASC.Get()); })
			.ThenClient(TEXT("Client 0 : lance la flamme vivante"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(ClientActivate(Client, UCurffeGA_LivingFlame::StaticClass()), TEXT("Activation refusée côté client")));
			})
			.UntilClient(TEXT("Client 0 : forme de feu (prédite)"), 0, [this](FBasePIENetworkComponentState& Client) { return HasTag(LocalASC(Client), TEXT("State.Untouchable")); }, DefaultWait())
			.ThenClient(TEXT("Client 0 : sans sort pendant la forme"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("State.CastLocked")), TEXT("Verrou de lancement")));
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("State.Curffe.LivingFlame")), TEXT("Tag de la forme (visuel de Curffe)")));
				// N'importe quel autre sort du kit (BP_Curffe) est refusé par le verrou
				bool bAnyOtherActivated = false;
				for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
				{
					if (!Spec.IsActive() && Spec.Ability && !Spec.Ability->IsA<UCurffeGA_LivingFlame>())
					{
						bAnyOtherActivated |= ASC->TryActivateAbility(Spec.Handle, true);
					}
				}
				ASSERT_THAT(IsFalse(bAnyOtherActivated, TEXT("Aucun autre sort pendant la forme")));
			})
			.UntilServer(TEXT("Serveur : forme de feu"), [this](FBasePIENetworkComponentState&) { return HasTag(CasterASC.Get(), TEXT("State.Untouchable")); }, DefaultWait())
			.ThenServer(TEXT("Serveur : intouchable, payé une fois, recharge, canalisation"), [this](FBasePIENetworkComponentState&)
			{
				UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(CasterASC.Get());
				ASSERT_THAT(IsFalse(ASC->ApplyHardCC(Tag(TEXT("State.Stunned")), 1.f, nullptr).IsValid(), TEXT("Contrôle dur refusé pendant la forme")));
				ASSERT_THAT(IsNear(35.f, Energy(ASC), 0.01f, TEXT("60 - 25 = 35, payé une fois")));
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("Cooldown.Ability.LivingFlame")), TEXT("Recharge de 16 s")));
				ASSERT_THAT(IsTrue(ServerCaster->GetCastInfo().bChannel, TEXT("La forme se lit comme une canalisation")));
				ASSERT_THAT(IsNear(EnemyHealthBefore, Health(ServerEnemy->GetAbilitySystemComponent()), 0.01f, TEXT("Pas encore d'anneau")));
			})
			.UntilServer(TEXT("Serveur : fin de la forme"), [this](FBasePIENetworkComponentState&) { return !IsActive(CasterASC.Get(), UCurffeGA_LivingFlame::StaticClass()); }, DefaultWait())
			.ThenServer(TEXT("Serveur : anneau sur l'ennemi seul, Foyer plein, hâte"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsNear(EnemyHealthBefore - 8.f, Health(ServerEnemy->GetAbilitySystemComponent()), 0.01f, TEXT("Ennemi : 8 dégâts")));
				ASSERT_THAT(IsNear(AllyHealthBefore, Health(ServerAlly->GetAbilitySystemComponent()), 0.01f, TEXT("Allié épargné")));
				ASSERT_THAT(IsNear(CasterHealthBefore, Health(CasterASC.Get()), 0.01f, TEXT("Lanceur épargné")));
				ASSERT_THAT(IsNear(5.f, Flames(CasterASC.Get()), 0.01f, TEXT("Foyer rempli à 5")));
				ASSERT_THAT(IsNear(BaseSpeed * 1.3f, Speed(CasterASC.Get()), 0.5f, TEXT("Hâte +30 %")));
				ASSERT_THAT(IsFalse(HasTag(CasterASC.Get(), TEXT("State.Untouchable"))));
				ASSERT_THAT(IsFalse(HasTag(CasterASC.Get(), TEXT("State.CastLocked")), TEXT("Il peut de nouveau lancer")));
			});
		QueueServerWait(TEXT("Serveur : repoussement en cours"), 0.4f);
		Network
			.ThenServer(TEXT("Serveur : l'ennemi est repoussé, pas l'allié ni le lanceur"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(FVector::Dist2D(ServerEnemy->GetActorLocation(), EnemyStart) > 150.f, TEXT("Ennemi repoussé")));
				ASSERT_THAT(IsTrue(FVector::Dist2D(ServerAlly->GetActorLocation(), AllyStart) < 20.f, TEXT("Allié immobile")));
				ASSERT_THAT(IsTrue(FVector::Dist2D(ServerCaster->GetActorLocation(), FVector::ZeroVector) < 20.f, TEXT("Lanceur immobile")));
			})
			.UntilClient(TEXT("Client 0 : Foyer plein, hâte, énergie 35, recharge (répliqués)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return FMath::IsNearlyEqual(Flames(ASC), 5.f, 0.01f) && FMath::IsNearlyEqual(Speed(ASC), BaseSpeed * 1.3f, 0.5f)
					&& FMath::IsNearlyEqual(Energy(ASC), 35.f, 0.01f) && HasTag(ASC, TEXT("Cooldown.Ability.LivingFlame"))
					&& !HasTag(ASC, TEXT("State.CastLocked")) && !IsActive(ASC, UCurffeGA_LivingFlame::StaticClass());
			}, DefaultWait());
		QueueServerWait(TEXT("Serveur : fin de la hâte"), 2.f);
		Network.ThenServer(TEXT("Serveur : vitesse de base"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsNear(BaseSpeed, Speed(CasterASC.Get()), 0.5f, TEXT("Hâte de 2 s finie")));
		});
	}

	// --- Combustion (Task 9) ------------------------------------------------------------------------------

	bool CanServerActivatePyroblast() const
	{
		UAbilitySystemComponent* ASC = CasterASC.Get();
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, UGenNetTestGA_Pyroblast::StaticClass());
		const UGameplayAbility* Ability = Spec ? Spec->GetPrimaryInstance() : nullptr;
		return Ability && Ability->CanActivateAbility(Spec->Handle, ASC->AbilityActorInfo.Get());
	}

	/** Client 0 lance la combustion, le serveur la voit en incantation. */
	void QueueCast()
	{
		Network
			.ThenClient(TEXT("Client 0 : lance la combustion"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(ClientActivate(Client, UCurffeGA_Combustion::StaticClass()), TEXT("Activation refusée côté client")));
			})
			.UntilServer(TEXT("Serveur : en incantation"), [this](FBasePIENetworkComponentState&) { return IsActive(CasterASC.Get(), UCurffeGA_Combustion::StaticClass()); }, DefaultWait());
	}

	TEST_METHOD(Combustion_CostAtCastEnd_Nova_Ablaze5s)
	{
		// Ennemi à 2 m, allié à 2 m : dans la nova de 3 m
		QueueSetup({ UCurffeGA_Combustion::StaticClass(), UGenNetTestGA_Pyroblast::StaticClass() }, 100.f, 1.f, 200.f, 200.f);
		Network.ThenServer(TEXT("Serveur : Pyroblast refusé hors embrasement"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsFalse(CanServerActivatePyroblast()));
		});
		QueueCast();
		QueueServerWait(TEXT("Serveur : 0.25 s d'incantation"), 0.25f);
		Network
			.ThenServer(TEXT("Serveur : rien de payé pendant l'incantation"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(IsActive(CasterASC.Get(), UCurffeGA_Combustion::StaticClass())));
				ASSERT_THAT(IsNear(100.f, Energy(CasterASC.Get()), 0.01f));
				ASSERT_THAT(IsFalse(HasTag(CasterASC.Get(), TEXT("State.Curffe.Ablaze"))));
				ASSERT_THAT(IsTrue(ServerCaster->GetCastInfo().IsCasting() && !ServerCaster->GetCastInfo().bChannel, TEXT("Barre d'incantation (télégraphe de 3 m)")));
			})
			.UntilServer(TEXT("Serveur : éruption"), [this](FBasePIENetworkComponentState&) { return HasTag(CasterASC.Get(), TEXT("State.Curffe.Ablaze")); }, DefaultWait())
			.ThenServer(TEXT("Serveur : payé au lancer, nova sur l'ennemi seul, embrasé"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = CasterASC.Get();
				ASSERT_THAT(IsNear(0.f, Energy(ASC), 0.01f, TEXT("100 payés à la fin de l'incantation")));
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("State.FastFeeding")), TEXT("Nourrissage rapide")));
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("State.FreeResource")), TEXT("Flammes illimitées")));
				ASSERT_THAT(IsNear(5.f, Flames(ASC), 0.01f, TEXT("Foyer plein")));
				ASSERT_THAT(IsTrue(CanServerActivatePyroblast(), TEXT("Pyroblast lançable pendant l'embrasement")));
				ASSERT_THAT(IsNear(EnemyHealthBefore - 20.f, Health(ServerEnemy->GetAbilitySystemComponent()), 0.01f, TEXT("Nova : 20 à l'ennemi")));
				ASSERT_THAT(IsNear(AllyHealthBefore, Health(ServerAlly->GetAbilitySystemComponent()), 0.01f, TEXT("Allié épargné")));
				ASSERT_THAT(IsNear(CasterHealthBefore, Health(ASC), 0.01f, TEXT("Lanceur épargné")));
			})
			.UntilClient(TEXT("Client 0 : embrasé, nourrissage rapide, flammes illimitées, énergie 0"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return HasTag(ASC, TEXT("State.Curffe.Ablaze")) && HasTag(ASC, TEXT("State.FastFeeding")) && HasTag(ASC, TEXT("State.FreeResource"))
					&& FMath::IsNearlyEqual(Energy(ASC), 0.f, 0.01f) && !IsActive(ASC, UCurffeGA_Combustion::StaticClass());
			}, DefaultWait())
			.UntilClient(TEXT("Client 1 (ennemi) : voit le lanceur embrasé"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Client.World, ServerCaster->GetPlayerState()->GetPlayerId());
				return HasTag(GetASC(PS), TEXT("State.Curffe.Ablaze"));
			}, DefaultWait());
		QueueServerWait(TEXT("Serveur : repoussement"), 0.4f);
		Network.ThenServer(TEXT("Serveur : l'ennemi est repoussé, pas l'allié"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(FVector::Dist2D(ServerEnemy->GetActorLocation(), EnemyStart) > 150.f, TEXT("Ennemi repoussé")));
			ASSERT_THAT(IsTrue(FVector::Dist2D(ServerAlly->GetActorLocation(), AllyStart) < 20.f, TEXT("Allié immobile")));
		});
		QueueServerWait(TEXT("Serveur : fin de l'embrasement (5 s)"), 4.8f);
		Network
			.ThenServer(TEXT("Serveur : embrasement fini proprement"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = CasterASC.Get();
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.Curffe.Ablaze"))));
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.FastFeeding"))));
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.FreeResource"))));
				ASSERT_THAT(IsFalse(CanServerActivatePyroblast(), TEXT("Pyroblast refusé après l'embrasement")));
			})
			.UntilClient(TEXT("Client 0 : embrasement fini"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return !HasTag(ASC, TEXT("State.Curffe.Ablaze")) && !HasTag(ASC, TEXT("State.FastFeeding")) && !HasTag(ASC, TEXT("State.FreeResource"));
			}, DefaultWait());
	}

	TEST_METHOD(Combustion_StunnedDuringCast_NoCost_NoAblaze)
	{
		QueueSetup({ UCurffeGA_Combustion::StaticClass() }, 100.f, 1.f, 200.f, 200.f);
		QueueCast();
		QueueServerWait(TEXT("Serveur : 0.2 s d'incantation"), 0.2f);
		Network.ThenServer(TEXT("Serveur : étourdit le lanceur"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(Cast<UGenAbilitySystemComponent>(CasterASC.Get())->ApplyHardCC(Tag(TEXT("State.Stunned")), 0.5f, nullptr).IsValid()));
		});
		QueueServerWait(TEXT("Serveur : bien après la fin d'incantation"), 1.f);
		Network
			.ThenServer(TEXT("Serveur : rien de payé, pas d'embrasement ni de nova"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsFalse(IsActive(CasterASC.Get(), UCurffeGA_Combustion::StaticClass())));
				ASSERT_THAT(IsNear(100.f, Energy(CasterASC.Get()), 0.01f, TEXT("Énergie gardée")));
				ASSERT_THAT(IsFalse(HasTag(CasterASC.Get(), TEXT("State.Curffe.Ablaze"))));
				ASSERT_THAT(IsNear(EnemyHealthBefore, Health(ServerEnemy->GetAbilitySystemComponent()), 0.01f, TEXT("Pas de nova")));
			})
			.UntilClient(TEXT("Client 0 : énergie gardée, pas embrasé"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return !IsActive(ASC, UCurffeGA_Combustion::StaticClass()) && FMath::IsNearlyEqual(Energy(ASC), 100.f, 0.01f) && !HasTag(ASC, TEXT("State.Curffe.Ablaze"));
			}, DefaultWait());
	}

	TEST_METHOD(Combustion_DeathWhileAblaze_EndsCleanly)
	{
		QueueSetup({ UCurffeGA_Combustion::StaticClass() }, 100.f, 1.f, 1000.f, 1000.f);
		QueueCast();
		Network
			.UntilClient(TEXT("Client 0 : embrasé"), 0, [this](FBasePIENetworkComponentState& Client) { return HasTag(LocalASC(Client), TEXT("State.Curffe.Ablaze")); }, DefaultWait())
			.ThenServer(TEXT("Serveur : le lanceur meurt"), [this](FBasePIENetworkComponentState&)
			{
				ApplyDamage(ServerEnemy->GetAbilitySystemComponent(), CasterASC.Get(), 100000.f);
				ASSERT_THAT(IsTrue(ServerCaster->IsDead()));
				UAbilitySystemComponent* ASC = CasterASC.Get();
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.Curffe.Ablaze")), TEXT("Mort : plus embrasé")));
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.FastFeeding"))));
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.FreeResource"))));
			})
			.UntilClient(TEXT("Client 0 : plus rien après la mort"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return !HasTag(ASC, TEXT("State.Curffe.Ablaze")) && !HasTag(ASC, TEXT("State.FastFeeding")) && !HasTag(ASC, TEXT("State.FreeResource"));
			}, DefaultWait());
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
