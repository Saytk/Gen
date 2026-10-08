#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenTargetData.h"
#include "AbilitySystemComponent.h"
#include "Actors/GenGroundArea.h"
#include "Actors/GenProjectile.h"
#include "Champions/Curffe/CurffeGA_Combustion.h"
#include "Champions/Curffe/CurffeGA_LivingFlame.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayPrediction.h"
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
	/** Vitesse de marche réelle de cette machine : attribut × multiplicateurs locaux (hâte de la flamme vivante). */
	float Speed(const UAbilitySystemComponent* ASC)
	{
		const ACharacter* Character = ASC ? Cast<ACharacter>(ASC->GetAvatarActor()) : nullptr;
		return Character && Character->GetCharacterMovement() ? Character->GetCharacterMovement()->MaxWalkSpeed : -1.f;
	}

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

	/** Appui (bPressed) ou relâché d'une touche de sort sur le client, traité dans la foulée comme le ferait le PC. */
	void SendInput(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass, bool bPressed)
	{
		UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(LocalASC(Client));
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

	/** Le client 0 active le sort AbilityClass (prédit) : faux si le client le refuse. */
	bool ClientActivate(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		UAbilitySystemComponent* ASC = LocalASC(Client);
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		return Spec && ASC->TryActivateAbility(Spec->Handle, true);
	}

	/**
	 * Client : suit le refus du serveur de l'activation en cours du sort AbilityClass (ClientActivateAbilityFailed ->
	 * délégué de refus de sa clé de prédiction). Faux si le sort n'a pas d'activation prédite en cours.
	 */
	bool WatchRejection(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass, const TSharedRef<bool>& bRejected)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(LocalASC(Client), AbilityClass);
		const UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
		if (!Instance)
		{
			return false;
		}
		FPredictionKey Key = Instance->GetCurrentActivationInfo().GetActivationPredictionKey();
		if (!Key.IsValidKey())
		{
			return false;
		}
		Key.NewRejectedDelegate().BindLambda([bRejected]() { *bRejected = true; });
		return true;
	}

	/** Client : visée déterministe (1000 cm devant le lanceur, sur +X), sans curseur. */
	void SetDebugAim(const FBasePIENetworkComponentState& Client)
	{
		if (AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client)))
		{
			PC->bDebugAimOverride = true;
			PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
		}
	}

	/** Client modifié : envoie tout de suite la visée du sort AbilityClass en cours (visée en avance : départ différé). */
	void SendAimNow(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		UAbilitySystemComponent* ASC = LocalASC(Client);
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
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
	
	/** StartMaxEnergy > 0 : énergie max imposée avant l'énergie (revue P3 T8-10, M6 : 150 sur 200, une double dépense se voit). */
	void QueueSetup(const TArray<TSubclassOf<UGenGameplayAbility>>& Abilities, float StartEnergy, float StartFlames, float EnemyX, float AllyY, float StartMaxEnergy = 0.f)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [this](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [](FBasePIENetworkComponentState& Server) { SpawnTestFloor(Server.World); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [this](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : joueurs placés, sorts accordés, énergie et flammes"), [this, Abilities, StartEnergy, StartFlames, EnemyX, AllyY, StartMaxEnergy](FBasePIENetworkComponentState& Server)
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
				if (StartMaxEnergy > 0.f)
				{
					CasterASC->SetNumericAttributeBase(UGenAttributeSet::GetMaxEnergyAttribute(), StartMaxEnergy);
				}
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

	AFTER_EACH()
	{
		if (ServerWorld.IsValid() && SpawnHandle.IsValid())
		{
			ServerWorld->RemoveOnActorSpawnedHandler(SpawnHandle);
		}
		SpawnHandle.Reset();
	}

	// --- Projectiles du lanceur (serveur) -------------------------------------------------------------------

	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	int32 ProjectileCount = 0;
	float FlamesAtSpawn = -1.f;
	/** Refus du serveur vu par le client 0 (WatchRejection). */
	TSharedRef<bool> bClientRejected = MakeShared<bool>(false);

	/** Serveur : compte les projectiles du lanceur et relève ses flammes à leur apparition (dépense déjà faite). */
	void TrackCasterProjectiles(FBasePIENetworkComponentState& Server)
	{
		ServerWorld = Server.World;
		SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
		{
			const AGenProjectile* Projectile = Cast<AGenProjectile>(Actor);
			if (Projectile && ServerCaster.IsValid() && Projectile->GetInstigator() == ServerCaster.Get())
			{
				++ProjectileCount;
				FlamesAtSpawn = Flames(CasterASC.Get());
			}
		}));
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
			.UntilClient(TEXT("Client 0 : forme de feu"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				if (!HasTag(LocalASC(Client), TEXT("State.Untouchable")))
				{
					return false;
				}
				ClientMark = Client.World->GetTimeSeconds();
				return true;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : sans sort pendant la forme"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("State.CastLocked")), TEXT("Verrou de lancement")));
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("State.Curffe.LivingFlame")), TEXT("Tag de la forme (visuel de Curffe)")));
				// N'importe quel autre sort du kit (BP_Curffe) est refusé par le verrou
				bool bAnyOtherActivated = false;
				int32 Tried = 0;
				for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
				{
					if (!Spec.IsActive() && Spec.Ability && !Spec.Ability->IsA<UCurffeGA_LivingFlame>())
					{
						++Tried;
						bAnyOtherActivated |= ASC->TryActivateAbility(Spec.Handle, true);
					}
				}
				// Revue P3 T8-10, M7 : sans autre sort, l'étape ne prouverait rien
				ASSERT_THAT(IsTrue(Tried > 0, TEXT("Au moins un autre sort essayé (kit de BP_Curffe)")));
				ASSERT_THAT(IsFalse(bAnyOtherActivated, TEXT("Aucun autre sort pendant la forme")));
			})
			.UntilServer(TEXT("Serveur : forme de feu"), [this](FBasePIENetworkComponentState&) { return HasTag(CasterASC.Get(), TEXT("State.Untouchable")); }, DefaultWait())
			.ThenServer(TEXT("Serveur : intouchable, payé une fois, recharge, canalisation, Foyer plein dès le départ"), [this](FBasePIENetworkComponentState&)
			{
				UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(CasterASC.Get());
				ASSERT_THAT(IsFalse(ASC->ApplyHardCC(Tag(TEXT("State.Stunned")), 1.f, nullptr).IsValid(), TEXT("Contrôle dur refusé pendant la forme")));
				ASSERT_THAT(IsNear(35.f, Energy(ASC), 0.01f, TEXT("60 - 25 = 35, payé une fois")));
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("Cooldown.Ability.LivingFlame")), TEXT("Recharge de 16 s")));
				ASSERT_THAT(IsTrue(ServerCaster->GetCastInfo().bChannel, TEXT("La forme se lit comme une canalisation")));
				ASSERT_THAT(IsNear(EnemyHealthBefore, Health(ServerEnemy->GetAbilitySystemComponent()), 0.01f, TEXT("Pas encore d'anneau")));
				// Revue P3 T8-10, I2 : Foyer rempli au départ (prédit chez le client)
				ASSERT_THAT(IsNear(5.f, Flames(ASC), 0.01f, TEXT("Foyer plein dès le départ de la forme")));
			})
			.UntilClient(TEXT("Client 0 : la forme se termine (tags de la forme retirés)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const UAbilitySystemComponent* ASC = LocalASC(Client);
				return !HasTag(ASC, TEXT("State.Untouchable")) && !HasTag(ASC, TEXT("State.Curffe.LivingFlame"));
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : tags de la forme retirés dans la forme + 1 RTT"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				// Revue P3 T8-10, M1 : le GE prédit est remplacé par celui du serveur, dont le retrait arrive ~1 RTT après la
				// fin de la forme du client. Budget : forme (0.5 s) + 0.25 s de RTT
				const float Elapsed = Client.World->GetTimeSeconds() - ClientMark;
				ASSERT_THAT(IsTrue(Elapsed <= GetDefault<UCurffeGA_LivingFlame>()->GetFormDuration() + 0.25f, *FString::Printf(TEXT("Forme vue %.2f s"), Elapsed)));
			})
			.UntilServer(TEXT("Serveur : fin de la forme"), [this](FBasePIENetworkComponentState&) { return !IsActive(CasterASC.Get(), UCurffeGA_LivingFlame::StaticClass()); }, DefaultWait())
			.ThenServer(TEXT("Serveur : anneau sur l'ennemi seul, Foyer plein, hâte"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsNear(EnemyHealthBefore - 8.f, Health(ServerEnemy->GetAbilitySystemComponent()), 0.01f, TEXT("Ennemi : 8 dégâts")));
				ASSERT_THAT(IsNear(AllyHealthBefore, Health(ServerAlly->GetAbilitySystemComponent()), 0.01f, TEXT("Allié épargné")));
				ASSERT_THAT(IsNear(CasterHealthBefore, Health(CasterASC.Get()), 0.01f, TEXT("Lanceur épargné")));
				ASSERT_THAT(IsNear(5.f, Flames(CasterASC.Get()), 0.01f, TEXT("Foyer rempli à 5")));
				ASSERT_THAT(IsNear(BaseSpeed * 1.3f, Speed(CasterASC.Get()), 0.5f, TEXT("Hâte +30 %")));
				ASSERT_THAT(IsNear(1.3f, ServerCaster->GetLocalMoveSpeedMultiplier(), 0.001f, TEXT("Hâte : multiplicateur local du serveur (grâce de mouvement)")));
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
			.UntilClient(TEXT("Client 0 : Foyer plein, hâte (posée par le client), énergie 35, recharge (répliqués)"), 0, [this](FBasePIENetworkComponentState& Client)
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

	TSubclassOf<UGameplayAbility> GreatFireballClass;
	float ClientFlamesAtPress = -1.f;
	int32 ClientFeedSlotsAtPress = -1;

	/**
	 * Revue P3 T8-10, I2 et M7 : flamme vivante depuis un Foyer vide, puis clic droit (grande boule de feu du kit) sur la
	 * PREMIÈRE image où le client n'a plus son verrou. Le serveur accepte (fenêtre du verrou, aucun refus), le Foyer
	 * est déjà plein chez le client (remplissage prédit au départ de la forme) : 3 crans, 3 flammes nourries.
	 */
	TEST_METHOD(LivingFlame_GreatFireballOnFirstFrameAfterForm_AcceptedAndFedThree)
	{
		GreatFireballClass = LoadCurffeAbilityClass(TEXT("GA_GreatFireball"));
		// Ennemi et allié loin : ni l'anneau ni la boule de feu ne les touchent
		QueueSetup({ UCurffeGA_LivingFlame::StaticClass() }, 60.f, 0.f, 1500.f, 1500.f);
		Network
			.ThenServer(TEXT("Serveur : grande boule de feu dans le kit, projectiles suivis"), [this](FBasePIENetworkComponentState& Server)
			{
				ASSERT_THAT(IsNotNull(GreatFireballClass.Get(), TEXT("GA_GreatFireball introuvable")));
				ASSERT_THAT(IsNotNull(FindAbilitySpec(CasterASC.Get(), GreatFireballClass), TEXT("Grande boule de feu dans le kit de BP_Curffe")));
				TrackCasterProjectiles(Server);
			})
			.ThenClient(TEXT("Client 0 : visée déterministe, lance la flamme vivante"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				SetDebugAim(Client);
				ASSERT_THAT(IsTrue(ClientActivate(Client, UCurffeGA_LivingFlame::StaticClass()), TEXT("Activation refusée côté client")));
			})
			.UntilClient(TEXT("Client 0 : forme de feu (verrou local)"), 0, [this](FBasePIENetworkComponentState& Client) { return HasTag(LocalASC(Client), TEXT("State.CastLocked")); }, DefaultWait())
			.UntilClient(TEXT("Client 0 : première image après SA forme, appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				if (HasTag(ASC, TEXT("State.CastLocked")))
				{
					return false;
				}
				ClientFlamesAtPress = Flames(ASC);
				SendInput(Client, GreatFireballClass, true);
				const AGenCharacterBase* Pawn = Cast<AGenCharacterBase>(GetLocalController(Client)->GetPawn());
				ClientFeedSlotsAtPress = Pawn ? Pawn->GetCastInfo().FeedSlots : -1;
				WatchRejection(Client, GreatFireballClass, bClientRejected);
				return true;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : Foyer plein à l'appui, 3 crans"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsNear(5.f, ClientFlamesAtPress, 0.01f, TEXT("Foyer plein dès la fin de SA forme (remplissage prédit)")));
				ASSERT_THAT(IsTrue(IsActive(LocalASC(Client), GreatFireballClass), TEXT("Grande boule de feu activée par le client")));
				ASSERT_THAT(IsTrue(ClientFeedSlotsAtPress >= 3, *FString::Printf(TEXT("Crans de nourrissage : %d"), ClientFeedSlotsAtPress)));
			});
		QueueClientWait(TEXT("Client 0 : nourrit 3 flammes"), 1.2f);
		Network
			.ThenClient(TEXT("Client 0 : relâche"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilServer(TEXT("Serveur : grande boule de feu partie"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait())
			.ThenServer(TEXT("Serveur : une boule de feu, 3 flammes nourries"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount));
				ASSERT_THAT(IsNear(2.f, FlamesAtSpawn, 0.01f, TEXT("5 - 3 flammes nourries")));
			})
			.ThenClient(TEXT("Client 0 : aucun refus du serveur"), 0, [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsFalse(*bClientRejected, TEXT("Activation refusée par le serveur (verrou du serveur)")));
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

	/**
	 * Énergie vue par machine (clé : -1 serveur, 0 client 0) : nombre de baisses, total des baisses, solde (baisses moins
	 * hausses), plus basse valeur et suite des changements (message d'échec).
	 */
	TMap<int32, int32> EnergyDrops;
	TMap<int32, float> EnergySpent;
	TMap<int32, float> EnergyNet;
	TMap<int32, float> EnergyMin;
	TMap<int32, FString> EnergyTrace;

	void CountEnergyDrops(UAbilitySystemComponent* ASC, int32 Key)
	{
		EnergyDrops.FindOrAdd(Key) = 0;
		EnergySpent.FindOrAdd(Key) = 0.f;
		EnergyNet.FindOrAdd(Key) = 0.f;
		EnergyMin.FindOrAdd(Key) = Energy(ASC);
		EnergyTrace.FindOrAdd(Key).Reset();
		ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).AddLambda([this, Key](const FOnAttributeChangeData& Data)
		{
			EnergyTrace.FindOrAdd(Key) += FString::Printf(TEXT(" %.1f->%.1f"), Data.OldValue, Data.NewValue);
			EnergyNet.FindOrAdd(Key) += Data.OldValue - Data.NewValue;
			EnergyMin.FindOrAdd(Key) = FMath::Min(EnergyMin.FindOrAdd(Key), Data.NewValue);
			if (Data.NewValue < Data.OldValue)
			{
				++EnergyDrops.FindOrAdd(Key);
				EnergySpent.FindOrAdd(Key) += Data.OldValue - Data.NewValue;
			}
		});
	}

	/** Revue P3 T8-10, M6 : 100 dépensés une seule fois sur le serveur et sur le client 0 (pas « 100 -> 0 », borné). */
	void QueueCountEnergyDrops()
	{
		Network
			.ThenServer(TEXT("Serveur : compte les baisses d'énergie"), [this](FBasePIENetworkComponentState&) { CountEnergyDrops(CasterASC.Get(), -1); })
			.ThenClient(TEXT("Client 0 : compte les baisses d'énergie"), 0, [this](FBasePIENetworkComponentState& Client) { CountEnergyDrops(LocalASC(Client), 0); });
	}

	/**
	 * Serveur : exactement une baisse de 100. Client 0 : jamais sous le coût payé une fois (pas de double dépense, même
	 * transitoire), solde de 100. Le client peut voir un rebond (dépense prédite, retirée au rattrapage de la clé avant
	 * l'arrivée de la base du serveur, puis la base) : comportement générique de GAS, noté dans les points ouverts.
	 */
	void QueueAssertPaidOnce(float StartEnergy)
	{
		Network
			.ThenServer(TEXT("Serveur : une seule dépense de 100"), [this](FBasePIENetworkComponentState&)
			{
				const FString Trace = EnergyTrace.FindOrAdd(-1);
				ASSERT_THAT(AreEqual(1, EnergyDrops.FindOrAdd(-1), *(TEXT("Une baisse d'énergie sur le serveur :") + Trace)));
				ASSERT_THAT(IsNear(100.f, EnergySpent.FindOrAdd(-1), 0.01f, TEXT("100 dépensés sur le serveur")));
			})
			.ThenClient(TEXT("Client 0 : 100 dépensés une fois"), 0, [this, StartEnergy](FBasePIENetworkComponentState&)
			{
				const FString Trace = EnergyTrace.FindOrAdd(0);
				ASSERT_THAT(IsTrue(EnergyMin.FindOrAdd(0) >= StartEnergy - 100.f - 0.01f, *(TEXT("Jamais sous le coût payé une fois :") + Trace)));
				ASSERT_THAT(IsNear(100.f, EnergyNet.FindOrAdd(0), 0.01f, *(TEXT("Solde de 100 chez le client :") + Trace)));
			});
	}

	TEST_METHOD(Combustion_CostAtCastEnd_Nova_Ablaze5s)
	{
		// Ennemi à 2 m, allié à 2 m : dans la nova de 3 m. 150 d'énergie sur 200 (revue P3 T8-10, M6)
		QueueSetup({ UCurffeGA_Combustion::StaticClass(), UGenNetTestGA_Pyroblast::StaticClass() }, 150.f, 1.f, 200.f, 200.f, 200.f);
		Network.ThenServer(TEXT("Serveur : Pyroblast refusé hors embrasement"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsFalse(CanServerActivatePyroblast()));
		});
		QueueCountEnergyDrops();
		QueueCast();
		QueueServerWait(TEXT("Serveur : 0.25 s d'incantation"), 0.25f);
		Network
			.ThenServer(TEXT("Serveur : rien de payé pendant l'incantation"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(IsActive(CasterASC.Get(), UCurffeGA_Combustion::StaticClass())));
				ASSERT_THAT(IsNear(150.f, Energy(CasterASC.Get()), 0.01f));
				ASSERT_THAT(IsFalse(HasTag(CasterASC.Get(), TEXT("State.Curffe.Ablaze"))));
				ASSERT_THAT(IsTrue(ServerCaster->GetCastInfo().IsCasting() && !ServerCaster->GetCastInfo().bChannel, TEXT("Barre d'incantation (télégraphe de 3 m)")));
			})
			.UntilServer(TEXT("Serveur : éruption"), [this](FBasePIENetworkComponentState&) { return HasTag(CasterASC.Get(), TEXT("State.Curffe.Ablaze")); }, DefaultWait())
			.ThenServer(TEXT("Serveur : payé au lancer, nova sur l'ennemi seul, embrasé"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = CasterASC.Get();
				ASSERT_THAT(IsNear(50.f, Energy(ASC), 0.01f, TEXT("100 payés à la fin de l'incantation : 150 -> 50")));
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("State.FastFeeding")), TEXT("Nourrissage rapide")));
				ASSERT_THAT(IsTrue(HasTag(ASC, TEXT("State.FreeResource")), TEXT("Flammes illimitées")));
				ASSERT_THAT(IsNear(5.f, Flames(ASC), 0.01f, TEXT("Foyer plein")));
				ASSERT_THAT(IsTrue(CanServerActivatePyroblast(), TEXT("Pyroblast lançable pendant l'embrasement")));
				ASSERT_THAT(IsNear(EnemyHealthBefore - 20.f, Health(ServerEnemy->GetAbilitySystemComponent()), 0.01f, TEXT("Nova : 20 à l'ennemi")));
				ASSERT_THAT(IsNear(AllyHealthBefore, Health(ServerAlly->GetAbilitySystemComponent()), 0.01f, TEXT("Allié épargné")));
				ASSERT_THAT(IsNear(CasterHealthBefore, Health(ASC), 0.01f, TEXT("Lanceur épargné")));
			})
			.UntilClient(TEXT("Client 0 : embrasé, nourrissage rapide, flammes illimitées, énergie 50"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return HasTag(ASC, TEXT("State.Curffe.Ablaze")) && HasTag(ASC, TEXT("State.FastFeeding")) && HasTag(ASC, TEXT("State.FreeResource"))
					&& FMath::IsNearlyEqual(Energy(ASC), 50.f, 0.01f) && !IsActive(ASC, UCurffeGA_Combustion::StaticClass());
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
		QueueAssertPaidOnce(150.f);
		QueueServerWait(TEXT("Serveur : fin de l'embrasement (5 s)"), 4.8f);
		Network
			.ThenServer(TEXT("Serveur : embrasement fini proprement"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = CasterASC.Get();
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.Curffe.Ablaze"))));
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.FastFeeding"))));
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.FreeResource"))));
			});
		// Revue P3 T8-10, I1 : la grâce du serveur (GenFeeding::ServerTagGrace) passée, le Pyroblast est refusé
		QueueServerWait(TEXT("Serveur : au-delà de la grâce"), GenFeeding::ServerTagGrace + 0.1f);
		Network
			.ThenServer(TEXT("Serveur : Pyroblast refusé après l'embrasement"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsFalse(CanServerActivatePyroblast(), TEXT("Pyroblast refusé après l'embrasement et sa grâce")));
			})
			.UntilClient(TEXT("Client 0 : embrasement fini"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return !HasTag(ASC, TEXT("State.Curffe.Ablaze")) && !HasTag(ASC, TEXT("State.FastFeeding")) && !HasTag(ASC, TEXT("State.FreeResource"));
			}, DefaultWait());
	}

	/**
	 * Revue P3 T8-10, I1 : le client garde l'embrasement ~1 RTT de plus que le serveur. Latence simulée : le client
	 * garde le tag (tag local) quand le serveur finit l'embrasement, puis lance un Pyroblast aussitôt. Le serveur
	 * l'accepte (tag requis retiré il y a moins de GenFeeding::ServerTagGrace) : un tir, aucun refus (pas de sort fantôme).
	 */
	TEST_METHOD(Pyroblast_InClientsLastMomentOfAblaze_AcceptedWithinServerGrace)
	{
		QueueSetup({ UCurffeGA_Combustion::StaticClass(), UGenNetTestGA_Pyroblast::StaticClass() }, 100.f, 1.f, 1500.f, 1500.f);
		Network
			.ThenServer(TEXT("Serveur : projectiles suivis"), [this](FBasePIENetworkComponentState& Server) { TrackCasterProjectiles(Server); })
			.ThenClient(TEXT("Client 0 : visée déterministe"), 0, [this](FBasePIENetworkComponentState& Client) { SetDebugAim(Client); });
		QueueCast();
		Network
			.UntilServer(TEXT("Serveur : embrasé, combustion finie"), [this](FBasePIENetworkComponentState&)
			{
				return HasTag(CasterASC.Get(), TEXT("State.Curffe.Ablaze")) && !IsActive(CasterASC.Get(), UCurffeGA_Combustion::StaticClass());
			}, DefaultWait())
			.UntilClient(TEXT("Client 0 : embrasé, combustion finie"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return HasTag(ASC, TEXT("State.Curffe.Ablaze")) && !IsActive(ASC, UCurffeGA_Combustion::StaticClass());
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : le retrait du serveur n'est pas encore arrivé (latence simulée)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				LocalASC(Client)->AddLooseGameplayTag(Tag(TEXT("State.Curffe.Ablaze")));
			})
			.ThenServer(TEXT("Serveur : fin de l'embrasement"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = CasterASC.Get();
				ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Tag(TEXT("State.Curffe.Ablaze"))));
				ASSERT_THAT(IsFalse(HasTag(ASC, TEXT("State.Curffe.Ablaze")), TEXT("Plus embrasé sur le serveur")));
				ASSERT_THAT(IsTrue(CanServerActivatePyroblast(), TEXT("Client distant : Pyroblast accepté dans la grâce")));
				ProjectileCount = 0;
			})
			.ThenClient(TEXT("Client 0 : Pyroblast (il se voit encore embrasé)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(ClientActivate(Client, UGenNetTestGA_Pyroblast::StaticClass()), TEXT("Activation refusée côté client")));
				ASSERT_THAT(IsTrue(WatchRejection(Client, UGenNetTestGA_Pyroblast::StaticClass(), bClientRejected), TEXT("Activation prédite")));
			})
			.UntilServer(TEXT("Serveur : Pyroblast parti"), [this](FBasePIENetworkComponentState&) { return ProjectileCount > 0; }, DefaultWait());
		QueueServerWait(TEXT("Serveur : 0.5 s après"), 0.5f);
		Network
			.ThenServer(TEXT("Serveur : un seul tir"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(1, ProjectileCount, TEXT("Un Pyroblast")));
				ASSERT_THAT(IsFalse(IsActive(CasterASC.Get(), UGenNetTestGA_Pyroblast::StaticClass())));
			})
			.ThenClient(TEXT("Client 0 : aucun refus, fin de la latence simulée"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsFalse(*bClientRejected, TEXT("Pyroblast refusé par le serveur (sort fantôme)")));
				LocalASC(Client)->RemoveLooseGameplayTag(Tag(TEXT("State.Curffe.Ablaze")));
			})
			.UntilClient(TEXT("Client 0 : plus embrasé, Pyroblast fini"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return !HasTag(ASC, TEXT("State.Curffe.Ablaze")) && !IsActive(ASC, UGenNetTestGA_Pyroblast::StaticClass());
			}, DefaultWait());
	}

	/**
	 * Revue P3 T8-10, M7 : visée de la combustion envoyée tout de suite (client modifié) : le serveur garde toute son
	 * incantation, puis paie 100 une fois à SA fin, embrase, remplit le Foyer et pose la nova une seule fois, même quand
	 * la vraie visée du client arrive ensuite.
	 */
	TEST_METHOD(Combustion_EarlyAim_DeferredLaunch_PaidOnce_EffectsOnce)
	{
		TestRunner->AddExpectedMessage(TEXT("Visée très en avance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
		// Ennemi à 2 m (dans la nova), allié loin. 150 d'énergie sur 200 : une double dépense se voit
		QueueSetup({ UCurffeGA_Combustion::StaticClass() }, 150.f, 1.f, 200.f, 1500.f, 200.f);
		QueueCountEnergyDrops();
		Network
			.ThenClient(TEXT("Client 0 : active la combustion puis vise tout de suite"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(ClientActivate(Client, UCurffeGA_Combustion::StaticClass()), TEXT("Activation refusée côté client")));
				SendAimNow(Client, UCurffeGA_Combustion::StaticClass());
			})
			.UntilServer(TEXT("Serveur : visée reçue en avance, départ en attente"), [this](FBasePIENetworkComponentState&)
			{
				const FGameplayAbilitySpec* Spec = FindAbilitySpec(CasterASC.Get(), UCurffeGA_Combustion::StaticClass());
				const UGenGA_Cast* Instance = Spec ? Cast<UGenGA_Cast>(Spec->GetPrimaryInstance()) : nullptr;
				return Instance && Instance->IsWaitingForDeferredLaunch();
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : rien de payé pendant l'attente"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsNear(150.f, Energy(CasterASC.Get()), 0.01f));
				ASSERT_THAT(IsFalse(HasTag(CasterASC.Get(), TEXT("State.Curffe.Ablaze"))));
			})
			.UntilServer(TEXT("Serveur : départ différé"), [this](FBasePIENetworkComponentState&) { return HasTag(CasterASC.Get(), TEXT("State.Curffe.Ablaze")); }, DefaultWait())
			.ThenServer(TEXT("Serveur : payé au départ, Foyer plein, nova"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = CasterASC.Get();
				ASSERT_THAT(IsNear(50.f, Energy(ASC), 0.01f, TEXT("150 -> 50")));
				ASSERT_THAT(IsNear(5.f, Flames(ASC), 0.01f, TEXT("Foyer plein")));
				ASSERT_THAT(IsNear(EnemyHealthBefore - 20.f, Health(ServerEnemy->GetAbilitySystemComponent()), 0.01f, TEXT("Nova : 20 à l'ennemi")));
			});
		QueueServerWait(TEXT("Serveur : la vraie visée du client arrive"), 0.8f);
		Network
			.ThenServer(TEXT("Serveur : tout une seule fois"), [this](FBasePIENetworkComponentState&)
			{
				UAbilitySystemComponent* ASC = CasterASC.Get();
				ASSERT_THAT(AreEqual(1, EnergyDrops.FindOrAdd(-1), TEXT("Une baisse d'énergie sur le serveur")));
				ASSERT_THAT(IsNear(100.f, EnergySpent.FindOrAdd(-1), 0.01f, TEXT("100 dépensés")));
				ASSERT_THAT(IsNear(50.f, Energy(ASC), 0.01f));
				ASSERT_THAT(AreEqual(1, ASC->GetGameplayTagCount(Tag(TEXT("State.Curffe.Ablaze"))), TEXT("Un seul embrasement")));
				ASSERT_THAT(IsNear(EnemyHealthBefore - 20.f, Health(ServerEnemy->GetAbilitySystemComponent()), 0.01f, TEXT("Une seule nova")));
				ASSERT_THAT(IsFalse(IsActive(ASC, UCurffeGA_Combustion::StaticClass())));
			})
			.UntilClient(TEXT("Client 0 : embrasé, énergie 50, combustion finie"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				return HasTag(ASC, TEXT("State.Curffe.Ablaze")) && FMath::IsNearlyEqual(Energy(ASC), 50.f, 0.01f) && !IsActive(ASC, UCurffeGA_Combustion::StaticClass());
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
