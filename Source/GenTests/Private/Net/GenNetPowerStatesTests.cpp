#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Actors/GenProjectile.h"
#include "Character/GenPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

namespace GenNetPowerStates
{
	// GenGameplayTags::* n'est pas exporté par le module Gen
	inline FGameplayTag UntouchableTag() { return FGameplayTag::RequestGameplayTag(TEXT("State.Untouchable")); }
	inline FGameplayTag StunnedTag() { return FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")); }
	inline FGameplayTag CCImmuneTag() { return FGameplayTag::RequestGameplayTag(TEXT("State.CCImmune")); }

	inline bool HasTag(const AGenPlayerState* PlayerState, const FGameplayTag& Tag)
	{
		const UAbilitySystemComponent* ASC = GetASC(PlayerState);
		return ASC && ASC->HasMatchingGameplayTag(Tag);
	}
}

using namespace GenNetPowerStates;

/**
 * Gen.Net.Untouchable : état intouchable (Plan 3 Task 4), serveur dédié + 2 clients.
 * Le client 1 (équipe 1) devient intouchable (GE à durée UGenGE_TimedState, comme la forme de feu de Living Flame) ;
 * le client 0 (équipe 0) est l'attaquant. Pendant l'état, sur le serveur : contrôle dur, repoussement et dégâts sont
 * ignorés ; le tag est vu par les deux clients (propriétaire et observateur). L'intouchable n'empêche PAS de lancer
 * ses propres sorts (le blocage de Living Flame vient de State.CastLocked) : le client 1 tire une boule de feu.
 * À la fin de l'état, tout reprend. La traversée des projectiles est déjà couverte par
 * Gen.Net.ProjectileCounter.Untouchable_PassesThrough_NoExplosion.
 */
NETWORK_TEST_CLASS(Untouchable, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	/** Plus long que la forme de feu (0.5 s) pour laisser la réplication arriver ; la durée n'est pas le sujet ici. */
	static constexpr float FormDuration = 1.5f;
	static constexpr float Damage = 30.f;

	TSubclassOf<UGameplayAbility> FireballClass;
	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	TWeakObjectPtr<AGenPlayerCharacter> ServerTarget;
	TWeakObjectPtr<UGenAbilitySystemComponent> ServerTargetASC;
	TWeakObjectPtr<UAbilitySystemComponent> ServerAttackerASC;
	int32 TargetPlayerId = INDEX_NONE;
	float HealthBefore = 0.f;
	int32 TargetProjectiles = 0;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
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

	void ApplyServerDamage()
	{
		const FGameplayEffectSpecHandle Spec = ServerAttackerASC->MakeOutgoingSpec(UGenGE_Damage::StaticClass(), 1.f, ServerAttackerASC->MakeEffectContext());
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage")), Damage);
		ServerTargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	float GetServerHealth() const
	{
		return GetAttribute(ServerTargetASC.Get(), UGenAttributeSet::GetHealthAttribute());
	}

	TEST_METHOD(Untouchable_IgnoresCCKnockbackDamage_ButCanCast)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : la cible devient intouchable"), [this](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;
				AGenPlayerCharacter* Attacker = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Target = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Attacker));
				ASSERT_THAT(IsNotNull(Target));
				ASSERT_THAT(IsNotNull(FireballClass.Get(), TEXT("GA_Fireball introuvable")));
				ServerTarget = Target;
				ServerTargetASC = Cast<UGenAbilitySystemComponent>(Target->GetAbilitySystemComponent());
				ServerAttackerASC = Attacker->GetAbilitySystemComponent();
				ASSERT_THAT(IsNotNull(ServerTargetASC.Get()));
				ASSERT_THAT(IsNotNull(ServerAttackerASC.Get()));
				TargetPlayerId = Target->GetPlayerState()->GetPlayerId();
				Attacker->TeleportTo(Target->GetActorLocation() + FVector(0.f, 5000.f, 0.f), Attacker->GetActorRotation(), false, true);

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
				{
					const AGenProjectile* Projectile = Cast<AGenProjectile>(Actor);
					if (Projectile && ServerTarget.IsValid() && Projectile->GetInstigator() == ServerTarget.Get())
					{
						++TargetProjectiles;
					}
				}));

				const FGameplayEffectSpecHandle Form = ServerTargetASC->MakeOutgoingSpec(UGenGE_TimedState::StaticClass(), 1.f, ServerTargetASC->MakeEffectContext());
				UGenGE_TimedState::SetDuration(*Form.Data, FormDuration, FGameplayTagContainer(UntouchableTag()));
				ASSERT_THAT(IsTrue(ServerTargetASC->ApplyGameplayEffectSpecToSelf(*Form.Data).IsValid()));
				ASSERT_THAT(IsTrue(Target->IsUntouchable()));
				HealthBefore = GetServerHealth();
			})
			.UntilClients(TEXT("Clients : la cible est intouchable"), [this](FBasePIENetworkComponentState& Client)
			{
				return HasTag(FindPlayerStateById(Client.World, TargetPlayerId), UntouchableTag());
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : contrôle dur, repoussement et dégâts ignorés"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsFalse(ServerTargetASC->ApplyHardCC(StunnedTag(), 1.f, nullptr).IsValid(), TEXT("Étourdissement ignoré")));
				ASSERT_THAT(IsFalse(ServerTargetASC->HasMatchingGameplayTag(StunnedTag())));

				ServerTarget->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
				ServerTarget->ApplyKnockback(FVector::ForwardVector, 300.f);
				ASSERT_THAT(IsFalse(IsBeingLaunched(ServerTarget.Get()), TEXT("Repoussement ignoré")));

				ApplyServerDamage();
				ASSERT_THAT(IsNear(HealthBefore, GetServerHealth(), 0.01f, TEXT("Dégâts ignorés")));
			})
			.ThenClient(TEXT("Client 1 : lance une boule de feu en étant intouchable"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
				UAbilitySystemComponent* ASC = GetASC(PC->GetPlayerState<AGenPlayerState>());
				FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, FireballClass);
				ASSERT_THAT(IsNotNull(Spec));
				ASSERT_THAT(IsTrue(ASC->HasMatchingGameplayTag(UntouchableTag())));
				ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, true), TEXT("L'intouchable n'empêche pas de lancer")));
			})
			.UntilServer(TEXT("Serveur : boule de feu de la cible partie"), [this](FBasePIENetworkComponentState&) { return TargetProjectiles > 0; }, DefaultWait())
			.UntilClient(TEXT("Client 1 : sa vie n'a pas bougé"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const UAbilitySystemComponent* ASC = GetASC(FindPlayerStateById(Client.World, TargetPlayerId));
				return ASC && FMath::IsNearlyEqual(GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()), HealthBefore, 0.01f);
			}, DefaultWait())
			.UntilServer(TEXT("Serveur : fin de l'état"), [this](FBasePIENetworkComponentState&) { return !ServerTarget->IsUntouchable(); }, DefaultWait())
			.ThenServer(TEXT("Serveur : de nouveau touchable"), [this](FBasePIENetworkComponentState&)
			{
				ApplyServerDamage();
				ASSERT_THAT(IsNear(HealthBefore - Damage, GetServerHealth(), 0.01f, TEXT("Les dégâts reprennent")));
				ServerTarget->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
				ServerTarget->ApplyKnockback(FVector::ForwardVector, 300.f);
				ASSERT_THAT(IsTrue(IsBeingLaunched(ServerTarget.Get()), TEXT("Le repoussement reprend")));
				ASSERT_THAT(IsTrue(ServerTargetASC->ApplyHardCC(StunnedTag(), 0.5f, nullptr).IsValid(), TEXT("Les contrôles reprennent")));
			})
			.UntilClients(TEXT("Clients : plus intouchable, vie de la cible baissée"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Client.World, TargetPlayerId);
				const UAbilitySystemComponent* ASC = GetASC(PS);
				return ASC && !ASC->HasMatchingGameplayTag(UntouchableTag())
					&& FMath::IsNearlyEqual(GetAttribute(ASC, UGenAttributeSet::GetHealthAttribute()), HealthBefore - Damage, 0.01f);
			}, DefaultWait());
	}
};

/**
 * Gen.Net.Resilience : immunité aux contrôles durs (Plan 3 Task 5, guidelines §3.3), serveur dédié + 2 clients.
 * Le serveur étourdit le client 1 trois fois 1 s (à 0, 1.2 et 2.4 s de son horloge) : le 3e s'applique en entier et
 * pose State.CCImmune (1 s restante + 1.5 s). Le tag est vu par le propriétaire et l'observateur (effet visible) ; un
 * 4e contrôle est refusé pendant l'immunité ; à sa fin, le tag disparaît partout et les contrôles reprennent.
 */
NETWORK_TEST_CLASS(Resilience, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TWeakObjectPtr<UGenAbilitySystemComponent> ServerTargetASC;
	int32 TargetPlayerId = INDEX_NONE;
	float ServerMark = 0.f;
	float ImmuneSince = -1.f;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	void QueueServerWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenServer(TEXT("Serveur : départ de l'attente"), [this](FBasePIENetworkComponentState& Server) { ServerMark = Server.World->GetTimeSeconds(); })
			.UntilServer(Description, [this, Seconds](FBasePIENetworkComponentState& Server) { return Server.World->GetTimeSeconds() >= ServerMark + Seconds; }, DefaultWait());
	}

	void QueueStun(const TCHAR* Description)
	{
		Network.ThenServer(Description, [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(ServerTargetASC->ApplyHardCC(StunnedTag(), 1.f, nullptr).IsValid(), TEXT("Étourdissement appliqué")));
		});
	}

	TEST_METHOD(ThreeStunsInFiveSeconds_GrantImmunity_VisibleToClients)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : cible = client 1"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Target = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Target));
				ServerTargetASC = Cast<UGenAbilitySystemComponent>(Target->GetAbilitySystemComponent());
				ASSERT_THAT(IsNotNull(ServerTargetASC.Get()));
				TargetPlayerId = Target->GetPlayerState()->GetPlayerId();
			});
		QueueStun(TEXT("Serveur : 1er étourdissement (1 s)"));
		QueueServerWait(TEXT("Serveur : 1.2 s"), 1.2f);
		QueueStun(TEXT("Serveur : 2e étourdissement"));
		Network.ThenServer(TEXT("Serveur : 2 s cumulées, pas encore immunisé"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsFalse(ServerTargetASC->HasMatchingGameplayTag(CCImmuneTag())));
		});
		QueueServerWait(TEXT("Serveur : 1.2 s"), 1.2f);
		QueueStun(TEXT("Serveur : 3e étourdissement (atteint 2.5 s)"));
		Network
			.ThenServer(TEXT("Serveur : immunisé, 4e refusé"), [this](FBasePIENetworkComponentState& Server)
			{
				ASSERT_THAT(IsTrue(ServerTargetASC->HasMatchingGameplayTag(CCImmuneTag()), TEXT("Résilience : immunité")));
				ASSERT_THAT(IsTrue(ServerTargetASC->HasMatchingGameplayTag(StunnedTag()), TEXT("Le 3e étourdit quand même")));
				ASSERT_THAT(IsFalse(ServerTargetASC->ApplyHardCC(StunnedTag(), 1.f, nullptr).IsValid(), TEXT("4e refusé")));
				ImmuneSince = Server.World->GetTimeSeconds();
			})
			.UntilClients(TEXT("Clients : immunité de la cible visible"), [this](FBasePIENetworkComponentState& Client)
			{
				return HasTag(FindPlayerStateById(Client.World, TargetPlayerId), CCImmuneTag());
			}, DefaultWait())
			.UntilServer(TEXT("Serveur : fin de l'immunité"), [this](FBasePIENetworkComponentState&) { return !ServerTargetASC->HasMatchingGameplayTag(CCImmuneTag()); }, DefaultWait())
			.ThenServer(TEXT("Serveur : immunité de 2.5 s, contrôlable de nouveau"), [this](FBasePIENetworkComponentState& Server)
			{
				const float Lasted = Server.World->GetTimeSeconds() - ImmuneSince;
				ASSERT_THAT(IsTrue(Lasted >= 2.4f && Lasted <= 2.7f, *FString::Printf(TEXT("Immunité de 1 s + 1.5 s (mesuré : %.2f s)"), Lasted)));
				ASSERT_THAT(IsTrue(ServerTargetASC->ApplyHardCC(StunnedTag(), 0.5f, nullptr).IsValid(), TEXT("De nouveau contrôlable")));
			})
			.UntilClients(TEXT("Clients : immunité finie"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Client.World, TargetPlayerId);
				return GetASC(PS) && !HasTag(PS, CCImmuneTag());
			}, DefaultWait());
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
