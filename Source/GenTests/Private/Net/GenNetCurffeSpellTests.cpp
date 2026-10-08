#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "GameplayCueManager.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "Net/GenNetCurffeTestAbilities.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.FlamePillar / Gen.Net.Backfire / Gen.Net.MeteorLeap : sorts de Curffe du plan 2 (Tasks 6 à 8), joués par
 * le client 0 à travers le vrai chemin LocalPredicted, avec les classes C++ de test (GenNetCurffeTestAbilities.h :
 * mêmes classes génériques que les futurs assets, valeurs de la spec). Sol de test posé dans chaque monde.
 * Les tags de Gen (non exportés) sont demandés par leur nom.
 */
namespace GenCurffeSpellTest
{
	FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(Name);
	}

	UAbilitySystemComponent* GetLocalASC(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr);
	}

	bool IsAbilityActive(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		return Spec && Spec->IsActive();
	}

	template <typename T>
	T* GetInstance(UAbilitySystemComponent* ASC)
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, T::StaticClass());
		return Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
	}

	/** Le serveur a lancé le personnage (repoussement) dans cette image. */
	bool IsBeingLaunched(const ACharacter* Character)
	{
		return Character && !Character->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero();
	}

	/** Serveur : dégâts de Source sur Target (GE de dégâts du projet). */
	void ApplyDamage(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Amount)
	{
		const FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(UGenGE_Damage::StaticClass(), 1.f, Source->MakeEffectContext());
		Spec.Data->SetSetByCallerMagnitude(Tag(TEXT("SetByCaller.Damage")), Amount);
		Target->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	/** Serveur : gain (ou perte si négatif) d'énergie et de ressource. */
	void ApplyGain(UAbilitySystemComponent* ASC, float Energy, float Resource)
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGenGE_Gain::StaticClass(), 1.f, ASC->MakeEffectContext());
		UGenGE_Gain::SetMagnitudes(*Spec.Data, Energy, Resource);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	/** Pose le pion debout sur le sol de test en (X, Y). */
	void PlaceOnFloor(ACharacter* Character, float X, float Y)
	{
		Character->TeleportTo(FVector(X, Y, StandingHeight), FRotator::ZeroRotator, false, true);
		Character->GetCharacterMovement()->StopMovementImmediately();
	}

	/** Appui ou relâché d'une touche de sort sur le client, traité dans la foulée comme le ferait le PC. */
	void SendInput(const FBasePIENetworkComponentState& Client, const FGameplayTag& InputTag, bool bPressed)
	{
		UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(GetLocalASC(Client));
		if (!ASC)
		{
			return;
		}
		if (bPressed)
		{
			ASC->AbilityInputTagPressed(InputTag);
		}
		else
		{
			ASC->AbilityInputTagReleased(InputTag);
		}
		ASC->ProcessAbilityInput(0.f, false);
	}
}

using namespace GenCurffeSpellTest;

// =====================================================================================================================

/**
 * Gen.Net.FlamePillar (Review Focus #3) : le lanceur meurt, puis son pion disparaît, entre le départ du pilier et
 * l'impact. Le pilier tombe quand même et ne touche que les ennemis du lanceur (équipe retenue à l'apparition) :
 * l'ennemi prend 12 et est étourdi, l'allié du lanceur, dans le même rayon, ne prend rien.
 * Clients : 0 lance (équipe 0), 1 ennemi (équipe 1), 2 allié (équipe 0).
 */
NETWORK_TEST_CLASS(FlamePillar, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<AGenGroundArea> SpawnedArea;
	int32 EnemyPlayerId = INDEX_NONE;
	int32 AllyPlayerId = INDEX_NONE;
	uint8 CasterTeam = GenNoTeam;
	float EnemyHealthBefore = 0.f;
	float AllyHealthBefore = 0.f;
	bool bAreaDetonated = false;

	static constexpr float PillarDamage = 12.f;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(3)
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

	TEST_METHOD(CasterDiesBeforeImpact_HitsOnlyEnemies)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [this](FBasePIENetworkComponentState& Server) { ASSERT_THAT(IsNotNull(SpawnTestFloor(Server.World))); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol, pilier accordé, joueurs placés"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Enemy = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Ally = GetServerController(Server, 2)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Enemy));
				ASSERT_THAT(IsNotNull(Ally));
				ServerCaster = Caster;
				CasterTeam = Caster->GetTeamId();
				ASSERT_THAT(IsTrue(Ally->GetTeamId() == CasterTeam && Enemy->GetTeamId() != CasterTeam, TEXT("Clients 0 et 2 alliés, client 1 ennemi")));
				EnemyPlayerId = Enemy->GetPlayerState()->GetPlayerId();
				AllyPlayerId = Ally->GetPlayerState()->GetPlayerId();

				// Pilier visé en (600, 0) : l'ennemi et l'allié sont tous deux à 1 m du centre (rayon 2 m)
				PlaceOnFloor(Caster, 0.f, 0.f);
				PlaceOnFloor(Enemy, 600.f, -100.f);
				PlaceOnFloor(Ally, 600.f, 100.f);

				Cast<UGenAbilitySystemComponent>(Caster->GetAbilitySystemComponent())->GrantAbilities({ UGenNetTestGA_FlamePillar::StaticClass() }, nullptr);

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
				{
					if (AGenGroundArea* Area = Cast<AGenGroundArea>(Actor))
					{
						SpawnedArea = Area;
					}
				}));
			})
			.UntilClient(TEXT("Client 0 : pilier répliqué"), 0, [](FBasePIENetworkComponentState& Client)
			{
				return FindAbilitySpec(GetLocalASC(Client), UGenNetTestGA_FlamePillar::StaticClass()) != nullptr;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : vise (600, 0)"), 0, [](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(600.f, 0.f, StandingHeight);
			})
			.UntilClient(TEXT("Client 0 : posé au point de départ"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const APawn* Pawn = GetLocalController(Client)->GetPawn();
				return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), FVector::ZeroVector) < 20.f;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : vies de départ"), [this](FBasePIENetworkComponentState& Server)
			{
				EnemyHealthBefore = GetAttribute(GetASC(FindPlayerStateById(Server.World, EnemyPlayerId)), UGenAttributeSet::GetHealthAttribute());
				AllyHealthBefore = GetAttribute(GetASC(FindPlayerStateById(Server.World, AllyPlayerId)), UGenAttributeSet::GetHealthAttribute());
				ASSERT_THAT(IsTrue(EnemyHealthBefore > PillarDamage, TEXT("L'ennemi doit survivre au pilier")));
			})
			.ThenClient(TEXT("Client 0 : lance le pilier"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalASC(Client);
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, UGenNetTestGA_FlamePillar::StaticClass());
				ASSERT_THAT(IsNotNull(Spec));
				ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, true), TEXT("Activation refusée côté client")));
			})
			.UntilServer(TEXT("Serveur : pilier posé"), [this](FBasePIENetworkComponentState&) { return SpawnedArea.IsValid(); }, DefaultWait())
			.ThenServer(TEXT("Serveur : le lanceur meurt et son pion disparaît avant l'impact"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenGroundArea* Area = SpawnedArea.Get();
				ASSERT_THAT(IsFalse(Area->HasDetonated(), TEXT("Le pilier ne doit pas encore avoir frappé (télégraphe)")));
				ASSERT_THAT(IsTrue(Area->GetSourceTeam() == CasterTeam, TEXT("Équipe du lanceur retenue à l'apparition")));
				ASSERT_THAT(IsNear(200.f, Area->GetRadius(), 0.01f, TEXT("Rayon sans nourrissage : 2 m")));

				AGenPlayerCharacter* Caster = ServerCaster.Get();
				ASSERT_THAT(IsNotNull(Caster));
				ApplyDamage(GetASC(FindPlayerStateById(Server.World, EnemyPlayerId)), Caster->GetAbilitySystemComponent(), 100000.f);
				ASSERT_THAT(IsTrue(Caster->IsDead(), TEXT("Le lanceur doit être mort")));
				Caster->Destroy();
			})
			.UntilServer(TEXT("Serveur : impact du pilier"), [this](FBasePIENetworkComponentState&)
			{
				bAreaDetonated = bAreaDetonated || (SpawnedArea.IsValid() && SpawnedArea->HasDetonated());
				return bAreaDetonated;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : l'ennemi seul est touché"), [this](FBasePIENetworkComponentState& Server)
			{
				UAbilitySystemComponent* EnemyASC = GetASC(FindPlayerStateById(Server.World, EnemyPlayerId));
				UAbilitySystemComponent* AllyASC = GetASC(FindPlayerStateById(Server.World, AllyPlayerId));
				ASSERT_THAT(IsNear(EnemyHealthBefore - PillarDamage, GetAttribute(EnemyASC, UGenAttributeSet::GetHealthAttribute()), 0.01f, TEXT("L'ennemi prend 12")));
				ASSERT_THAT(IsTrue(EnemyASC->HasMatchingGameplayTag(Tag(TEXT("State.Stunned"))), TEXT("L'ennemi est étourdi")));
				ASSERT_THAT(IsNear(AllyHealthBefore, GetAttribute(AllyASC, UGenAttributeSet::GetHealthAttribute()), 0.01f, TEXT("L'allié du lanceur mort ne prend rien")));
				ASSERT_THAT(IsFalse(AllyASC->HasMatchingGameplayTag(Tag(TEXT("State.Stunned"))), TEXT("L'allié n'est pas étourdi")));
			})
			.UntilClient(TEXT("Client 1 : touché et étourdi (répliqué)"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const UAbilitySystemComponent* ASC = GetLocalASC(Client);
				return ASC && ASC->HasMatchingGameplayTag(Tag(TEXT("State.Stunned")))
					&& FMath::IsNearlyEqual(GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()), EnemyHealthBefore - PillarDamage, 0.01f);
			}, DefaultWait())
			.ThenClient(TEXT("Client 2 : l'allié n'a rien pris"), 2, [this](FBasePIENetworkComponentState& Client)
			{
				const UAbilitySystemComponent* ASC = GetLocalASC(Client);
				ASSERT_THAT(IsNear(AllyHealthBefore, GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()), 0.01f));
				ASSERT_THAT(IsFalse(ASC->HasMatchingGameplayTag(Tag(TEXT("State.Stunned")))));
			});
	}
};

// =====================================================================================================================

/**
 * Gen.Net.Backfire (Task 7, Review Focus #4) : posture de contre lancée par le client 0, vue par l'adversaire.
 * Deux projectiles et un coup de mêlée du client 1 sont bloqués : rien n'est subi, +2 flammes par blocage, +10 énergie
 * une seule fois, l'attaquant au corps à corps est repoussé. Un étourdissement termine la posture : State.Countering
 * disparaît partout et le sort se termine.
 */
NETWORK_TEST_CLASS(Backfire, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<AGenPlayerCharacter> ServerAttacker;
	int32 CasterPlayerId = INDEX_NONE;
	float ServerMark = 0.f;

	static constexpr float ProjectileDamage = 10.f;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	/** Serveur : projectile de l'attaquant qui apparaît dans le contreur (impact traité pendant FinishSpawning). */
	void FireProjectileInto(UWorld* World, AGenPlayerCharacter* Attacker, AGenPlayerCharacter* Target)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Target->GetActorLocation());
		AGenProjectile* Projectile = World->SpawnActorDeferred<AGenProjectile>(AGenProjectile::StaticClass(), SpawnTransform,
			Attacker, Attacker, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		ASSERT_THAT(IsNotNull(Projectile));
		Projectile->InitializeShot(FGenProjectileShotParams());

		UAbilitySystemComponent* AttackerASC = Attacker->GetAbilitySystemComponent();
		FGameplayEffectContextHandle Context = AttackerASC->MakeEffectContext();
		Context.AddSourceObject(Projectile);
		const FGameplayEffectSpecHandle DamageSpec = AttackerASC->MakeOutgoingSpec(UGenGE_Damage::StaticClass(), 1.f, Context);
		DamageSpec.Data->SetSetByCallerMagnitude(Tag(TEXT("SetByCaller.Damage")), ProjectileDamage);
		Projectile->DamageEffectSpecHandle = DamageSpec;
		Projectile->FinishSpawning(SpawnTransform);
		ASSERT_THAT(IsTrue(Projectile->HasExploded(), TEXT("Le projectile bloqué est consommé")));
	}

	TEST_METHOD(BlocksHits_RewardsOnce_EndsWhenStunned)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [this](FBasePIENetworkComponentState& Server) { ASSERT_THAT(IsNotNull(SpawnTestFloor(Server.World))); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol, contre accordé, Foyer vidé"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Attacker = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Attacker));
				ServerCaster = Caster;
				ServerAttacker = Attacker;
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				PlaceOnFloor(Caster, 0.f, 0.f);
				PlaceOnFloor(Attacker, 250.f, 0.f);

				UAbilitySystemComponent* ASC = Caster->GetAbilitySystemComponent();
				Cast<UGenAbilitySystemComponent>(ASC)->GrantAbilities({ UGenNetTestGA_Backfire::StaticClass() }, nullptr);

				// Foyer vide : les flammes gagnées ne sont pas bornées par le max (2 blocages = 4 flammes sur 5)
				ApplyGain(ASC, 0.f, -GetAttribute(ASC, UGenAttributeSet::GetMaxResourceAttribute()));
				ASSERT_THAT(IsNear(0.f, GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()), 0.01f));
				ASSERT_THAT(IsTrue(GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute()) + 10.f <= GetAttribute(ASC, UGenAttributeSet::GetMaxEnergyAttribute()),
					TEXT("Le gain d'énergie ne doit pas être borné par le max")));
			})
			.UntilClient(TEXT("Client 0 : contre répliqué"), 0, [](FBasePIENetworkComponentState& Client)
			{
				return FindAbilitySpec(GetLocalASC(Client), UGenNetTestGA_Backfire::StaticClass()) != nullptr;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : lance le contre"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(500.f, 0.f, StandingHeight);
				UAbilitySystemComponent* ASC = GetLocalASC(Client);
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, UGenNetTestGA_Backfire::StaticClass());
				ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, true), TEXT("Activation refusée côté client")));
			})
			.UntilServer(TEXT("Serveur : posture de contre"), [this](FBasePIENetworkComponentState&)
			{
				return ServerCaster.IsValid() && ServerCaster->GetAbilitySystemComponent()->HasMatchingGameplayTag(Tag(TEXT("State.Countering")));
			}, DefaultWait())
			.UntilClient(TEXT("Client 1 : voit la posture de l'adversaire"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const UAbilitySystemComponent* ASC = GetASC(FindPlayerStateById(Client.World, CasterPlayerId));
				return ASC && ASC->HasMatchingGameplayTag(Tag(TEXT("State.Countering")));
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : deux projectiles et un coup de mêlée bloqués"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = ServerCaster.Get();
				AGenPlayerCharacter* Attacker = ServerAttacker.Get();
				UAbilitySystemComponent* ASC = Caster->GetAbilitySystemComponent();
				const float HealthBefore = GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute());
				const float FlamesBefore = GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute());
				const float EnergyBefore = GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute());
				const float MaxFlames = GetAttribute(ASC, UGenAttributeSet::GetMaxResourceAttribute());

				FireProjectileInto(Server.World, Attacker, Caster);
				FireProjectileInto(Server.World, Attacker, Caster);
				ASSERT_THAT(IsNear(HealthBefore, GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()), 0.01f, TEXT("Coups bloqués : aucun dégât")));
				ASSERT_THAT(IsNear(FlamesBefore + 4.f, GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("+2 flammes par projectile bloqué")));
				ASSERT_THAT(IsNear(EnergyBefore + 10.f, GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute()), 0.01f, TEXT("+10 énergie une seule fois")));

				// Mêlée (aucune attaque de mêlée n'existe encore) : la cible répond comme pour un vrai coup
				const EGenHitResponse Melee = Caster->ResolveIncomingHit(Attacker, EGenHitKind::Melee, nullptr);
				ASSERT_THAT(IsTrue(Melee == EGenHitResponse::Countered, TEXT("La mêlée est bloquée par le contre")));
				ASSERT_THAT(IsTrue(IsBeingLaunched(Attacker), TEXT("L'attaquant au corps à corps est repoussé")));
				ASSERT_THAT(IsNear(FMath::Min(FlamesBefore + 6.f, MaxFlames), GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("+2 flammes pour la mêlée bloquée")));
				ASSERT_THAT(IsNear(EnergyBefore + 10.f, GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute()), 0.01f, TEXT("Pas d'énergie au-delà du premier blocage")));

				const UGenGA_Counter* Instance = GetInstance<UGenNetTestGA_Backfire>(ASC);
				ASSERT_THAT(IsNotNull(Instance));
				ASSERT_THAT(AreEqual(3, Instance->GetBlockCount()));

				// Une zone traverse le contre
				ASSERT_THAT(IsTrue(Caster->ResolveIncomingHit(Attacker, EGenHitKind::Area, nullptr) == EGenHitResponse::Hit, TEXT("Les zones au sol ne déclenchent pas le contre")));
			})
			.ThenServer(TEXT("Serveur : étourdit le contreur"), [this](FBasePIENetworkComponentState&)
			{
				UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(ServerCaster->GetAbilitySystemComponent());
				ASSERT_THAT(IsTrue(IsAbilityActive(ASC, UGenNetTestGA_Backfire::StaticClass()), TEXT("La fenêtre (3 s) doit être encore ouverte")));
				ASSERT_THAT(IsTrue(ASC->ApplyHardCC(Tag(TEXT("State.Stunned")), 1.f, ServerAttacker.Get()).IsValid()));
			})
			.UntilServer(TEXT("Serveur : posture terminée par l'étourdissement"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = ServerCaster->GetAbilitySystemComponent();
				return !ASC->HasMatchingGameplayTag(Tag(TEXT("State.Countering"))) && !IsAbilityActive(ASC, UGenNetTestGA_Backfire::StaticClass());
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : fin avant la fenêtre, ralenti retiré"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = ServerCaster->GetAbilitySystemComponent();
				ASSERT_THAT(IsTrue(ASC->HasMatchingGameplayTag(Tag(TEXT("State.Stunned")))));
				ASSERT_THAT(IsFalse(ASC->HasMatchingGameplayTag(Tag(TEXT("State.Countering"))), TEXT("State.Countering retiré")));
			})
			.UntilClients(TEXT("Clients : plus de posture de contre"), [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetASC(FindPlayerStateById(Client.World, CasterPlayerId));
				const bool bOwnerDone = Client.ClientIndex != 0 || !IsAbilityActive(ASC, UGenNetTestGA_Backfire::StaticClass());
				return ASC && bOwnerDone && !ASC->HasMatchingGameplayTag(Tag(TEXT("State.Countering")));
			}, DefaultWait());
	}
};

// =====================================================================================================================

/**
 * Gen.Net.MeteorLeap (Task 8, Review Focus #4) : bond nourrissable du client 0 vers (500, 0), l'ennemi (client 1) à
 * 1 m du point d'atterrissage. Anneau de test : boules nées au point d'atterrissage, assez grosses pour toutes chevaucher
 * l'ennemi. Attendu :
 * - 3 flammes : 3 boules d'une même salve, l'ennemi ne prend que la zone d'atterrissage + UNE boule (5 + 8) ; l'ennemi
 *   reçoit le point d'atterrissage (cercle, amorces de l'anneau) pendant le vol, effacé à l'atterrissage ;
 * - étourdi en plein vol : le vol continue, la zone et l'anneau partent, le sort se termine normalement ;
 * - étourdi pendant le décollage : sort annulé, ni recharge, ni flamme dépensée, ni bond.
 */
NETWORK_TEST_CLASS(MeteorLeap, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<AGenPlayerCharacter> ServerEnemy;
	TArray<TWeakObjectPtr<AGenNetTestRingProjectile>> RingProjectiles;
	/** Lacet et position de chaque boule de l'anneau à son apparition (ordre d'apparition). */
	TArray<float> RingYaws;
	TArray<FVector> RingOrigins;
	TWeakObjectPtr<AActor> ServerWall;
	TSet<const FGenProjectileSalvo*> RingSalvos;
	int32 AreaCount = 0;
	int32 CasterPlayerId = INDEX_NONE;
	FGenLeapTarget ObservedTarget;
	bool bObservedDuringFlight = false;
	float FlamesAtRing = -1.f;
	float MaxFlames = 0.f;
	float EnemyHealthBefore = 0.f;
	float ClientMark = 0.f;
	float ServerMark = 0.f;

	/** Signaux d'impact du bond joués, par monde (revue Plan 2 Tasks 7-8, M-4). */
	TMap<TWeakObjectPtr<UWorld>, int32> ImpactCues;
	/** Chez le propriétaire, son bond était encore actif quand le signal a joué (donc à SON atterrissage). */
	bool bOwnerCueDuringOwnLeap = false;
	FDelegateHandle CueHandle;

	static constexpr float LandingDamage = 5.f;
	static constexpr float RingDamage = 8.f;

	static FGameplayTag LeapInputTag() { return Tag(TEXT("InputTag.Ability.3")); }
	static FGameplayTag LeapCooldownTag() { return Tag(TEXT("Cooldown.Ability.FlameLeap")); }

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);

		// Touche et recharge du sort de test (tags demandés ici, pas pendant le chargement du module). La touche est lue
		// sur le CDO au moment d'accorder le sort (GrantAbilities) ; la recharge est reprise par l'instance à l'activation.
		GetMutableDefault<UGenNetTestGA_MeteorLeap>()->InputTag = LeapInputTag();
		UGenNetTestGA_MeteorLeap::TestCooldownTags = FGameplayTagContainer(LeapCooldownTag());
		UGenNetTestGA_MeteorLeap::TestRingSpawnOffset = 0.f;

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
		UGenNetTestGA_MeteorLeap::TestRingSpawnOffset = 0.f;
		UGenNetTestGA_MeteorLeap::TestImpactCueTag = FGameplayTag();
		if (CueHandle.IsValid())
		{
			UAbilitySystemGlobals::Get().GetGameplayCueManager()->OnGameplayCueRouted().Remove(CueHandle);
			CueHandle.Reset();
		}
	}

	bool HasLanded() const
	{
		return AreaCount > 0 && ServerCaster.IsValid() && !IsAbilityActive(ServerCaster->GetAbilitySystemComponent(), UGenNetTestGA_MeteorLeap::StaticClass());
	}

	void QueueSetup()
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [this](FBasePIENetworkComponentState& Server) { ASSERT_THAT(IsNotNull(SpawnTestFloor(Server.World))); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol, bond accordé, joueurs placés"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Enemy = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Enemy));
				ServerCaster = Caster;
				ServerEnemy = Enemy;
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				PlaceOnFloor(Caster, 0.f, 0.f);
				PlaceOnFloor(Enemy, 500.f, 100.f);

				UAbilitySystemComponent* ASC = Caster->GetAbilitySystemComponent();
				Cast<UGenAbilitySystemComponent>(ASC)->GrantAbilities({ UGenNetTestGA_MeteorLeap::StaticClass() }, nullptr);
				MaxFlames = GetAttribute(ASC, UGenAttributeSet::GetMaxResourceAttribute());
				ASSERT_THAT(IsTrue(MaxFlames >= 3.f));

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
				{
					if (!ServerCaster.IsValid() || Actor->GetInstigator() != ServerCaster.Get())
					{
						return;
					}
					if (AGenNetTestRingProjectile* Ring = Cast<AGenNetTestRingProjectile>(Actor))
					{
						RingProjectiles.Add(Ring);
						RingYaws.Add(Ring->GetActorRotation().Yaw);
						RingOrigins.Add(Ring->GetActorLocation());
						FlamesAtRing = GetAttribute(ServerCaster->GetAbilitySystemComponent(), UGenAttributeSet::GetResourceAttribute());
					}
					else if (Cast<AGenGroundArea>(Actor))
					{
						++AreaCount;
					}
				}));
			})
			.UntilClient(TEXT("Client 0 : bond répliqué"), 0, [](FBasePIENetworkComponentState& Client)
			{
				return FindAbilitySpec(GetLocalASC(Client), UGenNetTestGA_MeteorLeap::StaticClass()) != nullptr;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : vise (500, 0)"), 0, [](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(500.f, 0.f, StandingHeight);
			})
			.UntilClient(TEXT("Client 0 : posé au point de départ"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const ACharacter* Pawn = GetLocalController(Client)->GetPawn<ACharacter>();
				return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), FVector::ZeroVector) < 20.f && Pawn->GetCharacterMovement()->IsMovingOnGround();
			}, DefaultWait())
			.UntilServer(TEXT("Serveur : lanceur et ennemi au sol"), [this](FBasePIENetworkComponentState&)
			{
				return ServerCaster->GetCharacterMovement()->IsMovingOnGround() && ServerEnemy->GetCharacterMovement()->IsMovingOnGround();
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : vie de l'ennemi au départ"), [this](FBasePIENetworkComponentState&)
			{
				EnemyHealthBefore = GetAttribute(ServerEnemy->GetAbilitySystemComponent(), UGenAttributeSet::GetHealthAttribute());
				ASSERT_THAT(IsTrue(EnemyHealthBefore > LandingDamage + 3.f * RingDamage));
				ASSERT_THAT(IsNear(MaxFlames, GetAttribute(ServerCaster->GetAbilitySystemComponent(), UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("Foyer plein au départ")));
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

	/** Touche tenue HoldSeconds puis relâchée (nourrissage : une flamme toutes les 0.3 s, 3 au plus). */
	void QueueFeed(float HoldSeconds)
	{
		Network.ThenClient(TEXT("Client 0 : appuie sur le bond"), 0, [](FBasePIENetworkComponentState& Client) { SendInput(Client, LeapInputTag(), true); });
		QueueClientWait(TEXT("Client 0 : touche tenue"), HoldSeconds);
		Network.ThenClient(TEXT("Client 0 : relâche"), 0, [](FBasePIENetworkComponentState& Client) { SendInput(Client, LeapInputTag(), false); });
	}

	/** Atterrissage : zone et anneau de Fed boules d'une seule salve, l'ennemi touché une fois par l'anneau, recharge, plus de verrou. */
	void QueueAssertLanding(int32 Fed)
	{
		Network
			.UntilServer(TEXT("Serveur : atterrissage, zone et anneau"), [this, Fed](FBasePIENetworkComponentState&)
			{
				if (!HasLanded() || RingProjectiles.Num() < Fed)
				{
					return false;
				}
				// Toutes les boules sont encore là (une boule qui explose vit 0.5 s) : on relève leur salve
				for (const TWeakObjectPtr<AGenNetTestRingProjectile>& Ring : RingProjectiles)
				{
					RingSalvos.Add(Ring.IsValid() ? Ring->Salvo.Get() : nullptr);
				}
				return true;
			}, DefaultWait());
		QueueServerWait(TEXT("Serveur : 0.3 s après l'atterrissage"), 0.3f);
		Network
			.ThenServer(TEXT("Serveur : une zone, un anneau d'une salve, l'ennemi touché une fois par l'anneau"), [this, Fed](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = ServerCaster->GetAbilitySystemComponent();
				ASSERT_THAT(AreEqual(1, AreaCount, TEXT("Une seule zone d'atterrissage")));
				ASSERT_THAT(AreEqual(Fed, RingProjectiles.Num(), TEXT("Une boule par flamme nourrie")));
				ASSERT_THAT(AreEqual(1, RingSalvos.Num(), TEXT("Toutes les boules partagent la même salve")));
				ASSERT_THAT(IsFalse(RingSalvos.Contains(nullptr), TEXT("Salve renseignée")));
				ASSERT_THAT(IsNear(EnemyHealthBefore - LandingDamage - RingDamage, GetAttribute(ServerEnemy->GetAbilitySystemComponent(), UGenAttributeSet::GetHealthAttribute()), 0.01f,
					TEXT("L'ennemi prend la zone d'atterrissage et UNE seule boule de l'anneau")));
				ASSERT_THAT(IsTrue(FlamesAtRing <= MaxFlames - Fed + 1.f + 0.01f && FlamesAtRing >= MaxFlames - Fed - 0.01f, TEXT("Flammes nourries dépensées au décollage (au plus une regagnée depuis)")));
				ASSERT_THAT(IsTrue(ASC->HasMatchingGameplayTag(LeapCooldownTag()), TEXT("Recharge payée")));
				ASSERT_THAT(IsFalse(ASC->HasMatchingGameplayTag(Tag(TEXT("State.CastLocked"))), TEXT("Serveur : plus de verrou de lancement")));
			})
			.UntilClient(TEXT("Client 0 : bond terminé, recharge, plus de verrou"), 0, [](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalASC(Client);
				return !IsAbilityActive(ASC, UGenNetTestGA_MeteorLeap::StaticClass()) && ASC->HasMatchingGameplayTag(LeapCooldownTag())
					&& !ASC->HasMatchingGameplayTag(Tag(TEXT("State.CastLocked")));
			}, DefaultWait());
	}

	/** 3 flammes : triangle de boules d'une seule salve ; l'ennemi qu'elles chevauchent toutes n'est touché qu'une fois. */
	TEST_METHOD(FedThree_RingHitsEachEnemyOnce)
	{
		QueueSetup();
		QueueFeed(1.1f);
		Network
			.UntilClient(TEXT("Client 1 : reçoit le point d'atterrissage du lanceur"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Client.World, CasterPlayerId);
				const AGenCharacterBase* Caster = PS ? PS->GetPawn<AGenCharacterBase>() : nullptr;
				if (Caster && Caster->GetLeapTarget().IsActive())
				{
					ObservedTarget = Caster->GetLeapTarget();
					bObservedDuringFlight = !HasLanded();
					return true;
				}
				return HasLanded();
			}, DefaultWait())
			.ThenClient(TEXT("Client 1 : point d'atterrissage reçu pendant le vol"), 1, [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(bObservedDuringFlight, TEXT("L'ennemi doit recevoir le point d'atterrissage avant l'atterrissage")));
				ASSERT_THAT(IsTrue(ObservedTarget.Ability == UGenNetTestGA_MeteorLeap::StaticClass()));
				ASSERT_THAT(IsNear(0.f, static_cast<float>(FVector::Dist2D(ObservedTarget.Location, FVector(500.f, 0.f, 0.f))), 5.f, TEXT("Point visé (500, 0)")));
				ASSERT_THAT(AreEqual(3, static_cast<int32>(ObservedTarget.Fed), TEXT("3 flammes : 3 amorces d'anneau")));
				ASSERT_THAT(IsNear(150.f, ObservedTarget.Radius, 0.01f, TEXT("Cercle = rayon de la zone d'atterrissage")));
				ASSERT_THAT(IsNear(1.f, static_cast<float>(ObservedTarget.Direction.X), 0.05f, TEXT("Direction du bond (+X)")));
			});
		QueueAssertLanding(3);
		Network.ThenServer(TEXT("Serveur : triangle régulier, première boule selon la visée"), [this](FBasePIENetworkComponentState&)
		{
			// Revue Plan 2 Tasks 7-8, M-10 : les boules partent selon GenAreaRules::GetRingDirections(3, direction du bond)
			ASSERT_THAT(AreEqual(3, RingYaws.Num()));
			const float Expected[] = { 0.f, 120.f, -120.f };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				ASSERT_THAT(IsNear(0.f, FMath::FindDeltaAngleDegrees(Expected[Index], RingYaws[Index]), 3.f,
					*FString::Printf(TEXT("Boule %d : lacet %.1f° (attendu %.0f°)"), Index, RingYaws[Index], Expected[Index])));
			}
		});
		Network.UntilClient(TEXT("Client 1 : point d'atterrissage effacé après l'atterrissage"), 1, [this](FBasePIENetworkComponentState& Client)
		{
			const AGenPlayerState* PS = FindPlayerStateById(Client.World, CasterPlayerId);
			const AGenCharacterBase* Caster = PS ? PS->GetPawn<AGenCharacterBase>() : nullptr;
			return Caster && !Caster->GetLeapTarget().IsActive();
		}, DefaultWait());
	}

	/**
	 * Revue Plan 2 Tasks 7-8, M-4 : le signal d'impact du bond joue chez le propriétaire à SON atterrissage (prédit, son bond
	 * encore actif), une seule fois (le serveur le diffuse sous sa clé d'activation, que lui seul ignore), et une fois chez
	 * l'autre client. Rien sur le serveur dédié.
	 */
	TEST_METHOD(ImpactCue_PredictedForOwner_OncePerClient)
	{
		UGenNetTestGA_MeteorLeap::TestImpactCueTag = Tag(TEXT("GameplayCue.FlameLeap.Impact"));
		CueHandle = UAbilitySystemGlobals::Get().GetGameplayCueManager()->OnGameplayCueRouted().AddLambda(
			[this](AActor* Target, FGameplayTag CueTag, EGameplayCueEvent::Type Event, const FGameplayCueParameters&, EGameplayCueExecutionOptions)
			{
				if (!Target || Event != EGameplayCueEvent::Executed || CueTag != UGenNetTestGA_MeteorLeap::TestImpactCueTag)
				{
					return;
				}
				++ImpactCues.FindOrAdd(Target->GetWorld());
				const APawn* Pawn = Cast<APawn>(Target);
				if (Pawn && Pawn->IsLocallyControlled() && !Pawn->HasAuthority())
				{
					const IAbilitySystemInterface* Owner = Cast<IAbilitySystemInterface>(Target);
					bOwnerCueDuringOwnLeap = Owner && IsAbilityActive(Owner->GetAbilitySystemComponent(), UGenNetTestGA_MeteorLeap::StaticClass());
				}
			});
		QueueSetup();
		QueueFeed(0.45f);
		Network
			.UntilServer(TEXT("Serveur : atterri"), [this](FBasePIENetworkComponentState&) { return HasLanded(); }, DefaultWait())
			.UntilClients(TEXT("Clients : signal d'impact joué"), [this](FBasePIENetworkComponentState& Client) { return ImpactCues.FindRef(Client.World) > 0; }, DefaultWait());
		QueueServerWait(TEXT("Serveur : 0.5 s pour un éventuel doublon"), 0.5f);
		Network
			.ThenServer(TEXT("Serveur dédié : aucun signal"), [this](FBasePIENetworkComponentState& Server)
			{
				ASSERT_THAT(AreEqual(0, ImpactCues.FindRef(Server.World), TEXT("Signal cosmétique : jamais sur le serveur dédié")));
			})
			.ThenClients(TEXT("Clients : un seul signal chacun"), [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(AreEqual(1, ImpactCues.FindRef(Client.World), TEXT("Un signal par client, sans doublon")));
			})
			.ThenClient(TEXT("Client 0 : signal joué à son propre atterrissage"), 0, [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(bOwnerCueDuringOwnLeap, TEXT("Prédit : joué pendant le bond local, pas à la diffusion du serveur")));
			});
	}

	/** Étourdi en plein vol : ignoré, le vol continue, la zone et l'anneau partent. */
	TEST_METHOD(StunnedInFlight_LandsAndBursts)
	{
		QueueSetup();
		QueueFeed(0.45f);
		Network
			.UntilServer(TEXT("Serveur : en vol"), [this](FBasePIENetworkComponentState&)
			{
				const UGenGA_Leap* Leap = GetInstance<UGenNetTestGA_MeteorLeap>(ServerCaster->GetAbilitySystemComponent());
				return Leap && Leap->IsAirborne();
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : étourdit le lanceur en plein vol"), [this](FBasePIENetworkComponentState&)
			{
				UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(ServerCaster->GetAbilitySystemComponent());
				ASSERT_THAT(IsTrue(ASC->HasMatchingGameplayTag(Tag(TEXT("State.CastLocked"))), TEXT("Verrou de lancement pendant le vol")));
				ASSERT_THAT(IsTrue(ASC->ApplyHardCC(Tag(TEXT("State.Stunned")), 2.f, ServerEnemy.Get()).IsValid()));
				const UGenGA_Leap* Leap = GetInstance<UGenNetTestGA_MeteorLeap>(ASC);
				ASSERT_THAT(IsTrue(Leap && Leap->IsActive() && Leap->IsAirborne(), TEXT("L'étourdissement ne coupe pas le vol")));
			});
		QueueAssertLanding(1);
	}

	/**
	 * Règle des murs de l'anneau (revue Plan 2 Tasks 7-8, M-8 et M-10) : boules à 2 m du point d'atterrissage, un mur
	 * (serveur seulement : l'anneau est du serveur) à ~1 m devant. La boule apparaît contre le mur, pas derrière.
	 */
	TEST_METHOD(RingSpawnsAgainstWall)
	{
		UGenNetTestGA_MeteorLeap::TestRingSpawnOffset = 200.f;
		QueueSetup();
		Network.ThenServer(TEXT("Serveur : mur devant le point d'atterrissage"), [this](FBasePIENetworkComponentState& Server)
		{
			// Bloc de 50 cm d'épaisseur, face avant à X = 605 (atterrissage en X = 500, capsule de 42 cm : jamais touché en vol)
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Wall = Server.World->SpawnActor<AStaticMeshActor>(FVector(630.f, 0.f, 150.f), FRotator::ZeroRotator, Params);
			ASSERT_THAT(IsNotNull(Wall));
			Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Wall->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Wall->SetActorScale3D(FVector(0.5f, 4.f, 3.f));
			ServerWall = Wall;
		});
		QueueFeed(0.45f);
		Network
			.UntilServer(TEXT("Serveur : atterrissage et anneau"), [this](FBasePIENetworkComponentState&) { return HasLanded() && RingOrigins.Num() > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : la boule apparaît contre le mur"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(AreEqual(1, RingOrigins.Num()));
			ASSERT_THAT(IsNear(600.f, static_cast<float>(RingOrigins[0].X), 3.f, TEXT("Contre la face du mur (X = 605, moins 5 cm), pas à 2 m (X = 700)")));
			ASSERT_THAT(IsNear(0.f, FMath::FindDeltaAngleDegrees(0.f, RingYaws[0]), 3.f, TEXT("Boule selon la visée (+X)")));
			if (ServerWall.IsValid())
			{
				ServerWall->Destroy();
			}
		});
	}

	/** Étourdi pendant le décollage (nourrissage) : rien ne part, rien n'est payé. */
	TEST_METHOD(StunnedDuringTakeOff_NoCost)
	{
		QueueSetup();
		Network.ThenClient(TEXT("Client 0 : appuie sur le bond"), 0, [](FBasePIENetworkComponentState& Client) { SendInput(Client, LeapInputTag(), true); });
		QueueClientWait(TEXT("Client 0 : en plein décollage (1 flamme)"), 0.45f);
		Network
			.ThenServer(TEXT("Serveur : étourdit le lanceur pendant le décollage"), [this](FBasePIENetworkComponentState&)
			{
				UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(ServerCaster->GetAbilitySystemComponent());
				const UGenGA_Leap* Leap = GetInstance<UGenNetTestGA_MeteorLeap>(ASC);
				ASSERT_THAT(IsTrue(Leap && Leap->IsCastPending() && !Leap->IsAirborne(), TEXT("Le serveur doit être en plein décollage")));
				ASSERT_THAT(IsTrue(ASC->ApplyHardCC(Tag(TEXT("State.Stunned")), 1.f, ServerEnemy.Get()).IsValid()));
			})
			.ThenClient(TEXT("Client 0 : relâche"), 0, [](FBasePIENetworkComponentState& Client) { SendInput(Client, LeapInputTag(), false); })
			.UntilServer(TEXT("Serveur : bond annulé"), [this](FBasePIENetworkComponentState&)
			{
				return !IsAbilityActive(ServerCaster->GetAbilitySystemComponent(), UGenNetTestGA_MeteorLeap::StaticClass());
			}, DefaultWait());
		QueueServerWait(TEXT("Serveur : au-delà de tout départ possible"), 1.5f);
		Network
			.ThenServer(TEXT("Serveur : ni bond, ni recharge, ni flamme dépensée"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = ServerCaster->GetAbilitySystemComponent();
				ASSERT_THAT(AreEqual(0, AreaCount, TEXT("Pas de zone d'atterrissage")));
				ASSERT_THAT(AreEqual(0, RingProjectiles.Num(), TEXT("Pas d'anneau")));
				ASSERT_THAT(IsFalse(ASC->HasMatchingGameplayTag(LeapCooldownTag()), TEXT("Pas de recharge")));
				ASSERT_THAT(IsNear(MaxFlames, GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()), 0.01f, TEXT("Aucune flamme dépensée")));
				ASSERT_THAT(IsFalse(ASC->HasMatchingGameplayTag(Tag(TEXT("State.CastLocked")))));
				ASSERT_THAT(AreEqual(0, ServerCaster->GetFedResource(), TEXT("Plus aucune flamme en cours de nourrissage")));
			})
			.UntilClient(TEXT("Client 0 : bond annulé, rien de payé (prédiction annulée)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalASC(Client);
				return !IsAbilityActive(ASC, UGenNetTestGA_MeteorLeap::StaticClass()) && !ASC->HasMatchingGameplayTag(LeapCooldownTag())
					&& !ASC->HasMatchingGameplayTag(Tag(TEXT("State.CastLocked")))
					&& FMath::IsNearlyEqual(GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()), MaxFlames, 0.01f);
			}, DefaultWait());
	}
};

// =====================================================================================================================

/**
 * Gen.Net.BackfireEnds (revue Plan 2 Tasks 7-8, I-3, I-4 et M-10) : comment la posture se termine.
 * - fin naturelle de la fenêtre : posture et ralenti retirés partout, chaque machine à sa propre fin ;
 * - un nouvel appui de clic gauche la termine (serveur et client) ; le clic gauche maintenu (répétition) ne la termine pas ;
 * - un sort passif ou déclenché par un événement ne la termine pas ;
 * - la mort la termine.
 * Client 0 contre (équipe 0), client 1 l'observe (équipe 1).
 */
NETWORK_TEST_CLASS(BackfireEnds, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<AGenPlayerCharacter> ServerOther;
	int32 CasterPlayerId = INDEX_NONE;
	float ClientMark = 0.f;
	float WindowMultiplier = 0.f;

	static FGameplayTag PrimaryInputTag() { return Tag(TEXT("InputTag.Ability.Primary")); }

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);

		// Sort déclenché : touche et événement de test (lus à l'activation et quand le sort est accordé)
		UGenNetTestGA_Triggered::TestInputTag = PrimaryInputTag();
		UGenNetTestGA_Triggered::SetTriggerEvent(Tag(TEXT("Event.Counter.Blocked")));
		UGenNetTestGA_Triggered::ActivationCount = 0;
		UGenNetTestGA_Passive::ActivationCount = 0;
		WindowMultiplier = UGenNetTestGA_Backfire::TestWindowMoveSpeedMultiplier;

		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	static bool IsCountering(const UAbilitySystemComponent* ASC)
	{
		return ASC && ASC->HasMatchingGameplayTag(Tag(TEXT("State.Countering")));
	}

	/** Pion du lanceur vu dans ce monde (serveur, propriétaire ou observateur). */
	AGenCharacterBase* GetCasterIn(const UWorld* World) const
	{
		const AGenPlayerState* PS = FindPlayerStateById(World, CasterPlayerId);
		return PS ? PS->GetPawn<AGenCharacterBase>() : nullptr;
	}

	void QueueClientWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenClient(TEXT("Client 0 : départ de l'attente"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(Description, 0, [this, Seconds](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + Seconds; }, DefaultWait());
	}

	void QueueSetup()
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [this](FBasePIENetworkComponentState& Server) { ASSERT_THAT(IsNotNull(SpawnTestFloor(Server.World))); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : contre, passif et sort déclenché accordés"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				ServerCaster = Caster;
				ServerOther = Other;
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				PlaceOnFloor(Caster, 0.f, 0.f);
				PlaceOnFloor(Other, 0.f, 600.f);
				Cast<UGenAbilitySystemComponent>(Caster->GetAbilitySystemComponent())->GrantAbilities(
					{ UGenNetTestGA_Backfire::StaticClass(), UGenNetTestGA_Passive::StaticClass(), UGenNetTestGA_Triggered::StaticClass() }, nullptr);
			})
			.UntilClient(TEXT("Client 0 : sorts répliqués"), 0, [](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalASC(Client);
				return FindAbilitySpec(ASC, UGenNetTestGA_Backfire::StaticClass()) && FindAbilitySpec(ASC, UGenNetTestGA_Triggered::StaticClass());
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : vise devant lui"), 0, [](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(500.f, 0.f, StandingHeight);
			});
	}

	/** Le client 0 lance le contre ; posture vue sur le serveur, chez lui et chez l'observateur. */
	void QueueStartStance()
	{
		Network
			.ThenClient(TEXT("Client 0 : lance le contre"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalASC(Client);
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, UGenNetTestGA_Backfire::StaticClass());
				ASSERT_THAT(IsTrue(Spec && ASC->TryActivateAbility(Spec->Handle, true), TEXT("Activation refusée côté client")));
			})
			.UntilServer(TEXT("Serveur : posture"), [this](FBasePIENetworkComponentState&) { return IsCountering(ServerCaster->GetAbilitySystemComponent()); }, DefaultWait())
			.UntilClients(TEXT("Clients : posture vue (propriétaire et observateur)"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				return Caster && IsCountering(Caster->GetAbilitySystemComponent());
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : ralenti de la fenêtre (local)"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsNear(WindowMultiplier, ServerCaster->GetLocalMoveSpeedMultiplier(), 0.001f, TEXT("Ralenti de la fenêtre seul (incantation finie)")));
			})
			.ThenClient(TEXT("Client 0 : ralenti de la fenêtre (local)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNear(WindowMultiplier, Caster->GetLocalMoveSpeedMultiplier(), 0.001f));
			});
	}

	/** Posture terminée partout : sort fini, tag retiré, plus de ralenti local. */
	void QueueAssertStanceEnded(const TCHAR* Why)
	{
		Network
			.UntilServer(*FString::Printf(TEXT("Serveur : posture terminée (%s)"), Why), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = ServerCaster->GetAbilitySystemComponent();
				return !IsAbilityActive(ASC, UGenNetTestGA_Backfire::StaticClass()) && !IsCountering(ASC);
			}, DefaultWait())
			// Plus de ralenti local (une boule de feu lancée par le nouvel appui a le sien le temps de son incantation)
			.UntilServer(TEXT("Serveur : plus de ralenti local"), [this](FBasePIENetworkComponentState&)
			{
				return FMath::IsNearlyEqual(ServerCaster->GetLocalMoveSpeedMultiplier(), 1.f);
			}, DefaultWait())
			.UntilClients(TEXT("Clients : posture terminée"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				UAbilitySystemComponent* ASC = Caster ? Caster->GetAbilitySystemComponent() : GetASC(FindPlayerStateById(Client.World, CasterPlayerId));
				const bool bOwnerDone = Client.ClientIndex != 0 || (!IsAbilityActive(ASC, UGenNetTestGA_Backfire::StaticClass())
					&& (!Caster || FMath::IsNearlyEqual(Caster->GetLocalMoveSpeedMultiplier(), 1.f)));
				return ASC && bOwnerDone && !IsCountering(ASC);
			}, DefaultWait());
	}

	/** Fin naturelle de la fenêtre (3 s) : la vitesse de marche du propriétaire revient à l'attribut, sans GE à retirer. */
	TEST_METHOD(WindowExpires_RemovesStanceAndSlowEverywhere)
	{
		QueueSetup();
		QueueStartStance();
		QueueAssertStanceEnded(TEXT("fin de la fenêtre"));
		Network.ThenClient(TEXT("Client 0 : vitesse de marche rendue"), 0, [this](FBasePIENetworkComponentState& Client)
		{
			const AGenCharacterBase* Caster = GetCasterIn(Client.World);
			ASSERT_THAT(IsNotNull(Caster));
			const float MoveSpeed = GetAttribute(Caster->GetAbilitySystemComponent(), UGenAttributeSet::GetMoveSpeedAttribute());
			ASSERT_THAT(IsNear(MoveSpeed, Caster->GetCharacterMovement()->MaxWalkSpeed, 0.01f));
		});
	}

	/** Nouvel appui du clic gauche : la posture se termine sur les deux machines. */
	TEST_METHOD(FreshPrimaryPress_EndsStance)
	{
		QueueSetup();
		QueueStartStance();
		Network.ThenClient(TEXT("Client 0 : appuie sur le clic gauche"), 0, [](FBasePIENetworkComponentState& Client)
		{
			SendInput(Client, PrimaryInputTag(), true);
			SendInput(Client, PrimaryInputTag(), false);
		});
		QueueAssertStanceEnded(TEXT("nouvel appui"));
	}

	/** Clic gauche maintenu avant et pendant la posture : la répétition automatique ne la termine pas. */
	TEST_METHOD(HeldPrimary_DoesNotEndStance)
	{
		QueueSetup();
		Network.ThenClient(TEXT("Client 0 : maintient le clic gauche"), 0, [](FBasePIENetworkComponentState& Client) { SendInput(Client, PrimaryInputTag(), true); });
		QueueClientWait(TEXT("Client 0 : clic tenu 0.5 s"), 0.5f);
		QueueStartStance();
		QueueClientWait(TEXT("Client 0 : clic toujours tenu 1 s dans la fenêtre"), 1.f);
		Network
			.ThenServer(TEXT("Serveur : posture toujours là"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = ServerCaster->GetAbilitySystemComponent();
				ASSERT_THAT(IsTrue(IsAbilityActive(ASC, UGenNetTestGA_Backfire::StaticClass()) && IsCountering(ASC)));
			})
			.ThenClient(TEXT("Client 0 : posture toujours là, relâche le clic"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalASC(Client);
				ASSERT_THAT(IsTrue(IsAbilityActive(ASC, UGenNetTestGA_Backfire::StaticClass()) && IsCountering(ASC)));
				SendInput(Client, PrimaryInputTag(), false);
			});
	}

	/** Un passif (serveur) et un sort déclenché par un événement (prédit, avec une touche) ne terminent pas la posture. */
	TEST_METHOD(PassiveOrTriggeredActivation_KeepsStance)
	{
		QueueSetup();
		QueueStartStance();
		Network
			.ThenServer(TEXT("Serveur : active le passif"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = ServerCaster->GetAbilitySystemComponent();
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, UGenNetTestGA_Passive::StaticClass());
				ASSERT_THAT(IsTrue(Spec && ASC->TryActivateAbility(Spec->Handle, true)));
				ASSERT_THAT(AreEqual(1, UGenNetTestGA_Passive::ActivationCount));
			})
			.ThenClient(TEXT("Client 0 : événement qui déclenche le sort (prédit, envoyé au serveur)"), 0, [](FBasePIENetworkComponentState& Client)
			{
				FGameplayEventData Payload;
				Payload.EventTag = Tag(TEXT("Event.Counter.Blocked"));
				GetLocalASC(Client)->HandleGameplayEvent(Payload.EventTag, &Payload);
			})
			.UntilServer(TEXT("Serveur : sort déclenché activé chez le client et le serveur"), [](FBasePIENetworkComponentState&)
			{
				return UGenNetTestGA_Triggered::ActivationCount >= 2;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : posture toujours là"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = ServerCaster->GetAbilitySystemComponent();
				ASSERT_THAT(IsTrue(IsAbilityActive(ASC, UGenNetTestGA_Backfire::StaticClass()) && IsCountering(ASC)));
			})
			.ThenClient(TEXT("Client 0 : posture toujours là"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = GetLocalASC(Client);
				ASSERT_THAT(IsTrue(IsAbilityActive(ASC, UGenNetTestGA_Backfire::StaticClass()) && IsCountering(ASC)));
			});
	}

	/** Mort pendant la fenêtre : la posture se termine partout. */
	TEST_METHOD(DeathDuringWindow_EndsStance)
	{
		QueueSetup();
		QueueStartStance();
		Network.ThenServer(TEXT("Serveur : le contreur meurt"), [this](FBasePIENetworkComponentState&)
		{
			ApplyDamage(ServerOther->GetAbilitySystemComponent(), ServerCaster->GetAbilitySystemComponent(), 100000.f);
			ASSERT_THAT(IsTrue(ServerCaster->IsDead()));
		});
		QueueAssertStanceEnded(TEXT("mort"));
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
