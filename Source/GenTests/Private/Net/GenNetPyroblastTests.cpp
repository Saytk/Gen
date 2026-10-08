#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Actors/GenProjectile.h"
#include "Character/GenPlayerCharacter.h"
#include "Character/GenTrainingDummy.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"
#include "UObject/UnrealType.h"

using namespace GenNetTest;

/**
 * Gen.Net.Pyroblast : explosion sans nourrissage (Plan 3 Task 7, UGenGA_Projectile::BaseExplosionRadius), serveur
 * dédié + 1 client. Le client 0 lance une boule de feu NON nourrie (vrai chemin prédit : activation, visée, lancer) ;
 * l'instance du sort sur le serveur reçoit un rayon d'explosion de base, comme le futur GA_Pyroblast (donnée seulement).
 * Dès que le projectile apparaît, le serveur place deux mannequins (neutres = ennemis de tous, sans gravité) devant
 * lui : A sur sa trajectoire (coup direct), B à côté (hors de la sphère du projectile, dans l'explosion).
 * - Avec un rayon de base : A et B prennent des dégâts (éclaboussure sans nourrissage).
 * - Sans (boule de feu normale, contrôle) : seul A est touché.
 */
NETWORK_TEST_CLASS(Pyroblast, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	/** Plus large que le Pyroblast (120 cm) : la géométrie du test reste robuste aux écarts d'une image. */
	static constexpr float BaseRadius = 300.f;
	static constexpr float DirectDistance = 300.f;
	static constexpr float SideOffset = 180.f;

	TSubclassOf<UGameplayAbility> FireballClass;
	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	TWeakObjectPtr<AGenPlayerCharacter> ServerCaster;
	TWeakObjectPtr<AGenProjectile> Projectile;
	TWeakObjectPtr<AGenTrainingDummy> DirectDummy;
	TWeakObjectPtr<AGenTrainingDummy> SideDummy;
	float DirectHealthBefore = 0.f;
	float SideHealthBefore = 0.f;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FireballClass = LoadCurffeAbilityClass(TEXT("GA_Fireball"));
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(1)
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

	static float GetHealth(const AGenTrainingDummy* Dummy)
	{
		return Dummy ? GetAttribute(Dummy->GetAbilitySystemComponent(), UGenAttributeSet::GetHealthAttribute()) : -1.f;
	}

	static AGenTrainingDummy* SpawnFloatingDummy(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGenTrainingDummy* Dummy = World->SpawnActor<AGenTrainingDummy>(AGenTrainingDummy::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (Dummy)
		{
			// Carte vide, pas de sol : immobile à la hauteur du tir
			Dummy->GetCharacterMovement()->GravityScale = 0.f;
			Dummy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			Dummy->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		}
		return Dummy;
	}

	/** Boule de feu non nourrie du client 0, avec Radius comme rayon d'explosion de base sur l'instance du serveur. */
	void QueueShot(float Radius)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : rayon de base sur l'instance, suit le projectile"), [this, Radius](FBasePIENetworkComponentState& Server)
			{
				ServerWorld = Server.World;
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(FireballClass.Get(), TEXT("GA_Fireball introuvable")));
				ServerCaster = Caster;

				FGameplayAbilitySpec* Spec = FindAbilitySpec(Caster->GetAbilitySystemComponent(), FireballClass);
				UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
				ASSERT_THAT(IsNotNull(Instance, TEXT("Instance serveur de la boule de feu")));
				// Propriété protégée (réglée dans l'asset GA_Pyroblast) : par réflexion, sur l'instance seulement
				FFloatProperty* Property = FindFProperty<FFloatProperty>(UGenGA_Projectile::StaticClass(), TEXT("BaseExplosionRadius"));
				ASSERT_THAT(IsNotNull(Property, TEXT("UGenGA_Projectile::BaseExplosionRadius")));
				Property->SetPropertyValue_InContainer(Instance, Radius);

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
				{
					AGenProjectile* Spawned = Cast<AGenProjectile>(Actor);
					if (Spawned && ServerCaster.IsValid() && Spawned->GetInstigator() == ServerCaster.Get())
					{
						Projectile = Spawned;
					}
				}));
			})
			.ThenClient(TEXT("Client 0 : boule de feu non nourrie"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = PC->GetPawn()->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
				UAbilitySystemComponent* ASC = GetASC(PC->GetPlayerState<AGenPlayerState>());
				FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, FireballClass);
				ASSERT_THAT(IsNotNull(Spec));
				ASSERT_THAT(IsTrue(ASC->TryActivateAbility(Spec->Handle, true)));
			})
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return Projectile.IsValid(); }, DefaultWait())
			.ThenServer(TEXT("Serveur : mannequins devant le projectile"), [this](FBasePIENetworkComponentState& Server)
			{
				const FVector Location = Projectile->GetActorLocation();
				const FVector Forward = Projectile->GetActorForwardVector().GetSafeNormal2D();
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
				DirectDummy = SpawnFloatingDummy(Server.World, Location + Forward * DirectDistance);
				SideDummy = SpawnFloatingDummy(Server.World, Location + Forward * DirectDistance + Side * SideOffset);
				ASSERT_THAT(IsTrue(DirectDummy.IsValid() && SideDummy.IsValid(), TEXT("Mannequins créés")));
				DirectHealthBefore = GetHealth(DirectDummy.Get());
				SideHealthBefore = GetHealth(SideDummy.Get());
			})
			.UntilServer(TEXT("Serveur : impact"), [this](FBasePIENetworkComponentState&) { return !Projectile.IsValid() || Projectile->HasExploded(); }, DefaultWait());
	}

	TEST_METHOD(UnfedShot_WithBaseRadius_ExplodesAndSplashes)
	{
		QueueShot(BaseRadius);
		Network.ThenServer(TEXT("Serveur : coup direct et éclaboussure"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(GetHealth(DirectDummy.Get()) < DirectHealthBefore - 0.01f, TEXT("Coup direct sur A")));
			ASSERT_THAT(IsTrue(GetHealth(SideDummy.Get()) < SideHealthBefore - 0.01f, TEXT("Explosion sans nourrissage : B éclaboussé")));
		});
	}

	TEST_METHOD(UnfedShot_WithoutBaseRadius_DirectHitOnly)
	{
		QueueShot(0.f);
		Network.ThenServer(TEXT("Serveur : coup direct seulement"), [this](FBasePIENetworkComponentState&)
		{
			ASSERT_THAT(IsTrue(GetHealth(DirectDummy.Get()) < DirectHealthBefore - 0.01f, TEXT("Coup direct sur A")));
			ASSERT_THAT(IsNear(SideHealthBefore, GetHealth(SideDummy.Get()), 0.01f, TEXT("Boule de feu normale : pas d'explosion")));
		});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
