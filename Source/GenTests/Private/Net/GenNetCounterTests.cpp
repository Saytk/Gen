#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystemComponent.h"
#include "Actors/GenProjectile.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.ProjectileCounter : résolution d'un projectile par le serveur face à un contre
 * (AGenProjectile::Explode -> AGenCharacterBase::ResolveIncomingHit), Review Focus #2 du plan 2.
 *
 * Trois clients : 0 (équipe 0) contre, 1 (équipe 1) attaque, 2 (équipe 0) est l'allié du contreur.
 * Le serveur fait apparaître un projectile à éclaboussure du client 1 directement dans le contreur :
 * l'impact est traité dès BeginPlay (overlaps initiaux), donc dans la même image, sans dépendre du
 * mouvement ni de la chute des pions sur la carte vide. Le sort de contre (Backfire) arrive en Task 7 :
 * la posture est simulée par le tag State.Countering posé sur le serveur.
 *
 * Attendu :
 * - coup direct bloqué : le contreur ne prend rien (ni coup direct, ni éclaboussure, ni repoussement) et son
 *   contre est prévenu ; le projectile est consommé (il explose sur lui, rien ne passe derrière) ;
 * - l'éclaboussure touche (et repousse) quand même son allié ;
 * - l'attaquant ne gagne que si quelqu'un a vraiment été touché ;
 * - cible intouchable (tag State.Untouchable) : le projectile la traverse sans exploser, sans éclabousser
 *   son allié et sans prévenir de contre (revue des tâches 3-4 du plan 2).
 */
NETWORK_TEST_CLASS(ProjectileCounter, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	static constexpr float Damage = 10.f;
	static constexpr float EnergyOnHit = 10.f;
	static constexpr float ExplosionRadius = 300.f;
	static constexpr float KnockbackDistance = 300.f;

	/** Défense de la cible directe (client 0). */
	enum class EDefence : uint8 { None, Countering, Untouchable };

	TWeakObjectPtr<UAbilitySystemComponent> ServerCountererASC;
	FDelegateHandle BlockedHandle;

	int32 CountererPlayerId = INDEX_NONE;
	int32 AllyPlayerId = INDEX_NONE;
	float ExpectedCountererHealth = 0.f;
	float ExpectedAllyHealth = 0.f;

	int32 BlockedEvents = 0;
	const AActor* BlockedInstigator = nullptr;
	float BlockedKind = -1.f;

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
		RemoveBlockedHandler();
	}

	static FGameplayTag CounteringTag()
	{
		// GenGameplayTags::* n'est pas exporté par le module Gen
		return FGameplayTag::RequestGameplayTag(TEXT("State.Countering"));
	}

	static FGameplayTag UntouchableTag()
	{
		return FGameplayTag::RequestGameplayTag(TEXT("State.Untouchable"));
	}

	static FGameplayTag DefenceTag(EDefence Defence)
	{
		return Defence == EDefence::Countering ? CounteringTag() : UntouchableTag();
	}

	/** Le serveur a lancé le personnage (repoussement) dans cette image. */
	static bool IsBeingLaunched(const ACharacter* Character)
	{
		return !Character->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero();
	}

	void RemoveBlockedHandler()
	{
		if (ServerCountererASC.IsValid() && BlockedHandle.IsValid())
		{
			if (FGameplayEventMulticastDelegate* Delegate = ServerCountererASC->GenericGameplayEventCallbacks.Find(FGameplayTag::RequestGameplayTag(TEXT("Event.Counter.Blocked"))))
			{
				Delegate->Remove(BlockedHandle);
			}
		}
		BlockedHandle.Reset();
	}

	/**
	 * Defence : posture de contre ou intouchable du client 0 (ou rien). bAllyInSplash : son allié (client 2) est à 2 m,
	 * dans le rayon de l'éclaboussure, sinon à 30 m. Le projectile repousse (KnockbackDistance).
	 */
	void QueueScenario(EDefence Defence, bool bAllyInSplash)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server)
			{
				return AreAllServerPlayersReady(Server);
			}, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client)
			{
				return IsPlayerReady(GetLocalController(Client));
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : projectile de l'attaquant dans le contreur"), [this, Defence, bAllyInSplash](FBasePIENetworkComponentState& Server)
			{
				const bool bCountering = Defence == EDefence::Countering;
				const bool bUntouchable = Defence == EDefence::Untouchable;
				AGenPlayerCharacter* Counterer = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Attacker = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Ally = GetServerController(Server, 2)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Counterer));
				ASSERT_THAT(IsNotNull(Attacker));
				ASSERT_THAT(IsNotNull(Ally));

				const AGenPlayerState* CountererPS = Counterer->GetPlayerState<AGenPlayerState>();
				const AGenPlayerState* AttackerPS = Attacker->GetPlayerState<AGenPlayerState>();
				const AGenPlayerState* AllyPS = Ally->GetPlayerState<AGenPlayerState>();
				ASSERT_THAT(IsTrue(CountererPS->GetTeamId() == AllyPS->GetTeamId(), TEXT("Clients 0 et 2 doivent être alliés")));
				ASSERT_THAT(IsTrue(CountererPS->GetTeamId() != AttackerPS->GetTeamId(), TEXT("Le client 1 doit être ennemi")));
				CountererPlayerId = CountererPS->GetPlayerId();
				AllyPlayerId = AllyPS->GetPlayerId();

				UAbilitySystemComponent* CountererASC = GetASC(CountererPS);
				UAbilitySystemComponent* AttackerASC = GetASC(AttackerPS);
				UAbilitySystemComponent* AllyASC = GetASC(AllyPS);
				ASSERT_THAT(IsNotNull(CountererASC));
				ASSERT_THAT(IsNotNull(AttackerASC));
				ASSERT_THAT(IsNotNull(AllyASC));
				ServerCountererASC = CountererASC;

				// Carte vide : tout le monde apparaît à l'origine. L'attaquant est écarté, l'allié placé
				// à côté du contreur (dans l'éclaboussure, hors de la sphère du projectile) ou loin.
				const FVector Center = Counterer->GetActorLocation();
				Attacker->TeleportTo(Center + FVector(0.f, 5000.f, 0.f), Attacker->GetActorRotation(), false, true);
				Ally->TeleportTo(Center + (bAllyInSplash ? FVector(0.f, 200.f, 0.f) : FVector(0.f, -3000.f, 0.f)), Ally->GetActorRotation(), false, true);

				if (Defence != EDefence::None)
				{
					CountererASC->AddLooseGameplayTag(DefenceTag(Defence));
				}
				BlockedHandle = CountererASC->GenericGameplayEventCallbacks.FindOrAdd(FGameplayTag::RequestGameplayTag(TEXT("Event.Counter.Blocked")))
					.AddLambda([this](const FGameplayEventData* Payload)
					{
						++BlockedEvents;
						BlockedInstigator = Payload->Instigator;
						BlockedKind = Payload->EventMagnitude;
					});

				const float CountererHealthBefore = GetAttribute(CountererASC, UGenAttributeSet::GetHealthAttribute());
				const float AllyHealthBefore = GetAttribute(AllyASC, UGenAttributeSet::GetHealthAttribute());
				const float AttackerEnergyBefore = GetAttribute(AttackerASC, UGenAttributeSet::GetEnergyAttribute());
				ASSERT_THAT(IsTrue(AttackerEnergyBefore + EnergyOnHit <= GetAttribute(AttackerASC, UGenAttributeSet::GetMaxEnergyAttribute()), TEXT("Le gain ne doit pas être borné par le max")));

				// Tir comme UGenGA_Cast::SpawnProjectileShot : spec de dégâts et gains du lanceur fixés avant FinishSpawning
				const FTransform SpawnTransform(FRotator::ZeroRotator, Center);
				AGenProjectile* Projectile = Server.World->SpawnActorDeferred<AGenProjectile>(AGenProjectile::StaticClass(), SpawnTransform,
					Attacker, Attacker, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				ASSERT_THAT(IsNotNull(Projectile));

				FGenProjectileShotParams ShotParams;
				ShotParams.ExplosionRadius = ExplosionRadius;
				ShotParams.KnockbackDistance = KnockbackDistance;
				Projectile->InitializeShot(ShotParams);

				FGameplayEffectContextHandle Context = AttackerASC->MakeEffectContext();
				Context.AddSourceObject(Projectile);
				const FGameplayEffectSpecHandle DamageSpec = AttackerASC->MakeOutgoingSpec(UGenGE_Damage::StaticClass(), 1.f, Context);
				DamageSpec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage")), Damage);
				Projectile->DamageEffectSpecHandle = DamageSpec;

				const FGameplayEffectSpecHandle GainSpec = AttackerASC->MakeOutgoingSpec(UGenGE_Gain::StaticClass(), 1.f, AttackerASC->MakeEffectContext());
				UGenGE_Gain::SetMagnitudes(*GainSpec.Data, EnergyOnHit, 0.f);
				Projectile->InstigatorOnHitSpecHandle = GainSpec;

				Projectile->FinishSpawning(SpawnTransform);

				// L'impact a eu lieu pendant FinishSpawning (BeginPlay, overlaps initiaux)
				const bool bDirectHit = Defence == EDefence::None;
				const bool bAllySplashed = bAllyInSplash && !bUntouchable;
				ExpectedCountererHealth = CountererHealthBefore - (bDirectHit ? Damage : 0.f);
				ExpectedAllyHealth = AllyHealthBefore - (bAllySplashed ? Damage : 0.f);
				const bool bAnyoneHit = bDirectHit || bAllySplashed;

				ASSERT_THAT(IsTrue(Projectile->HasExploded() == !bUntouchable, bUntouchable
					? TEXT("Une cible intouchable est traversée : le projectile n'explose pas")
					: TEXT("Le projectile est consommé par l'impact (même bloqué par un contre : rien ne passe derrière)")));
				ASSERT_THAT(IsNear(ExpectedCountererHealth, GetAttribute(CountererASC, UGenAttributeSet::GetHealthAttribute()), 0.01f,
					bDirectHit ? TEXT("Sans défense, le coup direct touche (une seule fois)") : TEXT("Le contreur / l'intouchable ne doit rien prendre (ni coup direct, ni éclaboussure)")));
				ASSERT_THAT(IsNear(ExpectedAllyHealth, GetAttribute(AllyASC, UGenAttributeSet::GetHealthAttribute()), 0.01f,
					bAllySplashed ? TEXT("L'allié dans le rayon prend l'éclaboussure") : TEXT("L'allié ne prend rien (hors du rayon, ou pas d'explosion)")));
				ASSERT_THAT(IsTrue(IsBeingLaunched(Counterer) == bDirectHit, bDirectHit
					? TEXT("Sans défense, le coup direct repousse")
					: TEXT("Le contreur / l'intouchable n'est pas repoussé")));
				ASSERT_THAT(IsTrue(IsBeingLaunched(Ally) == bAllySplashed, bAllySplashed
					? TEXT("L'allié éclaboussé est repoussé")
					: TEXT("L'allié non touché n'est pas repoussé")));
				ASSERT_THAT(IsNear(AttackerEnergyBefore + (bAnyoneHit ? EnergyOnHit : 0.f), GetAttribute(AttackerASC, UGenAttributeSet::GetEnergyAttribute()), 0.01f,
					bAnyoneHit ? TEXT("L'attaquant gagne une fois par explosion qui touche") : TEXT("Un coup bloqué qui ne touche personne ne rapporte rien")));

				ASSERT_THAT(AreEqual(bCountering ? 1 : 0, BlockedEvents, TEXT("Le contre est prévenu de chaque coup bloqué (et seulement en posture)")));
				if (bCountering)
				{
					ASSERT_THAT(IsTrue(BlockedInstigator == Attacker, TEXT("Instigateur du blocage = attaquant")));
					ASSERT_THAT(IsNear(GenHitRules::ToEventMagnitude(EGenHitKind::Projectile), BlockedKind, 0.01f, TEXT("Nature du coup bloqué = Projectile (ToEventMagnitude, jamais 0)")));
				}
				if (Defence != EDefence::None)
				{
					CountererASC->RemoveLooseGameplayTag(DefenceTag(Defence));
				}
				RemoveBlockedHandler();
			})
			.UntilClients(TEXT("Clients : vies du contreur et de l'allié répliquées"), [this](FBasePIENetworkComponentState& Client)
			{
				const UAbilitySystemComponent* CountererASC = GetASC(FindPlayerStateById(Client.World, CountererPlayerId));
				const UAbilitySystemComponent* AllyASC = GetASC(FindPlayerStateById(Client.World, AllyPlayerId));
				return CountererASC && AllyASC
					&& FMath::IsNearlyEqual(ExpectedCountererHealth, GetAttribute(CountererASC, UGenAttributeSet::GetHealthAttribute()))
					&& FMath::IsNearlyEqual(ExpectedAllyHealth, GetAttribute(AllyASC, UGenAttributeSet::GetHealthAttribute()));
			}, DefaultWait());
	}

	/** Review Focus #2 : bloqué pour le contreur, l'éclaboussure touche son allié, l'attaquant gagne (l'allié est touché). */
	TEST_METHOD(Countered_SplashStillHitsCountererAlly)
	{
		QueueScenario(EDefence::Countering, /*bAllyInSplash*/ true);
	}

	/** Bloqué et personne d'autre dans le rayon : aucun dégât, aucun gain pour l'attaquant. */
	TEST_METHOD(Countered_NobodyHit_NoGainForAttacker)
	{
		QueueScenario(EDefence::Countering, /*bAllyInSplash*/ false);
	}

	/** Témoin sans contre : coup direct et éclaboussure (sans double dégât sur la cible directe), un seul gain. */
	TEST_METHOD(NotCountering_DirectHitAndSplash)
	{
		QueueScenario(EDefence::None, /*bAllyInSplash*/ true);
	}

	/** Intouchable : le projectile le traverse (pas d'explosion, l'allié à côté n'est pas éclaboussé), aucun contre prévenu, aucun gain. */
	TEST_METHOD(Untouchable_PassesThrough_NoExplosion)
	{
		QueueScenario(EDefence::Untouchable, /*bAllyInSplash*/ true);
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
