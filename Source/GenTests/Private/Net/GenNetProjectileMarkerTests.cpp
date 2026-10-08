#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenFeeding.h"
#include "Actors/GenProjectile.h"
#include "Character/GenPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"
#include "UObject/UnrealType.h"

using namespace GenNetTest;

/**
 * Gen.Net.ProjectileMarker : marqueur au sol des projectiles et rayon d'éclaboussure répliqué (Plan Visuals V7),
 * serveur dédié + 2 clients (client 0 équipe 0, client 1 équipe 1). Le client 0 tient la grande boule de feu
 * (0, 2 ou 3 flammes) puis la lance ; le client 1 observe.
 * - Serveur : rayon d'éclaboussure = règle du sort (GenFeeding::GetShotExplosionRadius sur les valeurs de l'asset),
 *   et AUCUN plan de marqueur (pas même un composant) sur le serveur dédié.
 * - Chaque client : rayon d'éclaboussure, échelle du tir et rayon du marqueur = ceux du serveur ; le plan est visible,
 *   au sol (sous le centre de la capsule du lanceur), trié au-dessus des télégraphes, aux couleurs du point de vue :
 *   soi (1) chez le lanceur, ennemi (3, chevrons) chez l'observateur.
 */
NETWORK_TEST_CLASS(ProjectileMarker, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TSubclassOf<UGameplayAbility> GreatFireballClass;
	int32 CasterPlayerId = INDEX_NONE;
	float ClientMark = 0.f;
	/** Compte nourri le plus haut vu par le client du lanceur. */
	int32 MaxFed = 0;

	TWeakObjectPtr<UWorld> ServerWorld;
	FDelegateHandle SpawnHandle;
	TWeakObjectPtr<AGenProjectile> ServerProjectile;
	float ServerExplosionRadius = -1.f;
	float ServerShotScale = -1.f;
	float ServerMarkerRadius = -1.f;

	/** Projectile vu par chaque client. */
	TMap<int32, TWeakObjectPtr<AGenProjectile>> ClientProjectiles;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		GreatFireballClass = LoadCurffeAbilityClass(TEXT("GA_GreatFireball"));
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

	// --- Outils -----------------------------------------------------------------------------------------

	/** Appui (bPressed) ou relâché d'une touche de sort sur le client, traité dans la foulée comme le ferait le PC. */
	static void SendInput(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass, bool bPressed)
	{
		const APlayerController* PC = GetLocalController(Client);
		UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr));
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

	/** Valeur d'une propriété protégée de l'asset du sort (réglée dans GA_GreatFireball), par réflexion. */
	template <typename TProperty, typename TValue>
	static TValue ReadAbilityValue(const UObject* CDO, const TCHAR* Name, TValue Default)
	{
		const TProperty* Property = FindFProperty<TProperty>(UGenGA_Projectile::StaticClass(), Name);
		return Property && CDO ? Property->GetPropertyValue_InContainer(CDO) : Default;
	}

	AGenProjectile* FindCasterProjectile(UWorld* World) const
	{
		for (TActorIterator<AGenProjectile> It(World); It; ++It)
		{
			const APawn* Instigator = It->GetInstigator();
			if (Instigator && Instigator->GetPlayerState() && Instigator->GetPlayerState()->GetPlayerId() == CasterPlayerId)
			{
				return *It;
			}
		}
		return nullptr;
	}

	// --- Scénario ----------------------------------------------------------------------------------------

	/** Le client 0 tient la grande boule de feu HoldSeconds (ExpectedFed flammes), la lance ; vérifications sur toutes les machines. */
	void QueueShotAndCheck(float HoldSeconds, int32 ExpectedFed)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : écarte l'observateur, suit les projectiles du lanceur"), [this](FBasePIENetworkComponentState& Server)
			{
				ASSERT_THAT(IsNotNull(GreatFireballClass.Get(), TEXT("GA_GreatFireball introuvable")));
				ServerWorld = Server.World;
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Other = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Other));
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				// Hors de la trajectoire, toujours pertinent pour la réplication
				Other->TeleportTo(Caster->GetActorLocation() + FVector(0.f, 3000.f, 0.f), Other->GetActorRotation(), false, true);

				SpawnHandle = Server.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateWeakLambda(Caster, [this, Caster](AActor* Actor)
				{
					AGenProjectile* Spawned = Cast<AGenProjectile>(Actor);
					if (Spawned && Spawned->GetInstigator() == Caster)
					{
						ServerProjectile = Spawned;
					}
				}));
			})
			.ThenClient(TEXT("Client 0 : visée déterministe, appuie sur la grande boule de feu"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				ASSERT_THAT(IsNotNull(PC));
				AGenCharacterBase* Caster = PC->GetPawn<AGenCharacterBase>();
				ASSERT_THAT(IsNotNull(Caster));
				Caster->OnFedResourceChanged.AddLambda([this](AGenCharacterBase*, int32, int32 New) { MaxFed = FMath::Max(MaxFed, New); });
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = Caster->GetActorLocation() + FVector(1000.f, 0.f, 0.f);
				SendInput(Client, GreatFireballClass, true);
				ClientMark = Client.World->GetTimeSeconds();
			})
			.UntilClient(TEXT("Client 0 : touche tenue"), 0, [this, HoldSeconds](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + HoldSeconds; }, DefaultWait())
			.ThenClient(TEXT("Client 0 : relâche"), 0, [this](FBasePIENetworkComponentState& Client) { SendInput(Client, GreatFireballClass, false); })
			.UntilServer(TEXT("Serveur : projectile apparu"), [this](FBasePIENetworkComponentState&) { return ServerProjectile.IsValid(); }, DefaultWait())
			.ThenServer(TEXT("Serveur : rayon d'éclaboussure du sort, aucun marqueur sur le serveur dédié"), [this, ExpectedFed](FBasePIENetworkComponentState&)
			{
				AGenProjectile* Projectile = ServerProjectile.Get();
				ASSERT_THAT(IsNotNull(Projectile));
				ServerExplosionRadius = Projectile->GetExplosionRadius();
				ServerShotScale = Projectile->GetShotScale();
				ServerMarkerRadius = Projectile->GetGroundMarkerRadius();

				const UObject* CDO = GreatFireballClass->GetDefaultObject();
				const float Expected = GenFeeding::GetShotExplosionRadius(ExpectedFed,
					ReadAbilityValue<FIntProperty>(CDO, TEXT("ExplosionMinFeed"), 0),
					ReadAbilityValue<FFloatProperty>(CDO, TEXT("ExplosionRadius"), 0.f),
					ReadAbilityValue<FFloatProperty>(CDO, TEXT("BaseExplosionRadius"), 0.f));
				ASSERT_THAT(IsNear(Expected, ServerExplosionRadius, 0.01f, *FString::Printf(TEXT("Éclaboussure du serveur à %d flamme(s)"), ExpectedFed)));
				ASSERT_THAT(IsNear(Projectile->GetCollisionRadius() * ServerShotScale * 1.2f, ServerMarkerRadius, 0.01f, TEXT("Marqueur = collision × échelle × 1.2")));

				ASSERT_THAT(IsNull(Projectile->GetGroundMarker(), TEXT("Serveur dédié : pas de marqueur")));
				TArray<UStaticMeshComponent*> Meshes;
				Projectile->GetComponents(Meshes);
				ASSERT_THAT(AreEqual(0, Meshes.Num(), TEXT("Serveur dédié : aucun plan créé")));
			})
			.UntilClients(TEXT("Clients : projectile du lanceur reçu, en vol"), [this](FBasePIENetworkComponentState& Client)
			{
				AGenProjectile* Projectile = FindCasterProjectile(Client.World);
				if (!Projectile || Projectile->HasExploded())
				{
					return false;
				}
				ClientProjectiles.Add(Client.ClientIndex, Projectile);
				return true;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : compte nourri attendu"), 0, [this, ExpectedFed](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(AreEqual(ExpectedFed, MaxFed, TEXT("Flammes nourries par le lanceur")));
			})
			.ThenClients(TEXT("Clients : éclaboussure et marqueur = serveur, couleur du point de vue"), [this](FBasePIENetworkComponentState& Client)
			{
				const TWeakObjectPtr<AGenProjectile>* Found = ClientProjectiles.Find(Client.ClientIndex);
				AGenProjectile* Projectile = Found ? Found->Get() : nullptr;
				ASSERT_THAT(IsNotNull(Projectile, TEXT("Projectile du client")));

				ASSERT_THAT(IsNear(ServerExplosionRadius, Projectile->GetExplosionRadius(), 0.01f, TEXT("Éclaboussure répliquée")));
				ASSERT_THAT(IsNear(ServerShotScale, Projectile->GetShotScale(), 0.001f, TEXT("Échelle du tir répliquée")));
				ASSERT_THAT(IsNear(ServerMarkerRadius, Projectile->GetGroundMarkerRadius(), 0.01f, TEXT("Rayon du marqueur = serveur")));

				UStaticMeshComponent* Marker = Projectile->GetGroundMarker();
				ASSERT_THAT(IsNotNull(Marker, TEXT("Marqueur créé sur le client")));
				ASSERT_THAT(IsTrue(Marker->IsVisible(), TEXT("Marqueur visible en vol")));
				// Plan de 100 cm : échelle monde = rayon / 50
				ASSERT_THAT(IsNear(ServerMarkerRadius, static_cast<float>(Marker->GetComponentScale().X) * 50.f, 0.01f, TEXT("Rayon dessiné = rayon du serveur")));
				ASSERT_THAT(AreEqual(4, Marker->TranslucencySortPriority, TEXT("Trié au-dessus des télégraphes")));

				// Au sol : sous le centre de la capsule du lanceur (le tir en part), quelle que soit l'échelle du tir
				const ACharacter* Caster = Cast<ACharacter>(Projectile->GetInstigator());
				ASSERT_THAT(IsNotNull(Caster, TEXT("Lanceur répliqué")));
				const float HalfHeight = Caster->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				const float Drop = static_cast<float>(Projectile->GetActorLocation().Z - Marker->GetComponentLocation().Z);
				ASSERT_THAT(IsNear(HalfHeight - 2.f, Drop, 0.5f, TEXT("Marqueur au sol")));

				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Marker->GetMaterial(0));
				ASSERT_THAT(IsNotNull(MID, TEXT("MID du marqueur")));
				const bool bOwner = Client.ClientIndex == 0;
				ASSERT_THAT(IsNear(bOwner ? 1.f : 3.f, MID->K2_GetScalarParameterValue(TEXT("RelationIndex")), 0.001f, TEXT("Soi chez le lanceur, ennemi chez l'observateur")));
				ASSERT_THAT(IsNear(bOwner ? 0.f : 1.f, MID->K2_GetScalarParameterValue(TEXT("EnemyPattern")), 0.001f, TEXT("Chevrons pour l'ennemi seulement")));
				ASSERT_THAT(IsNear(1.f, MID->K2_GetScalarParameterValue(TEXT("Fill")), 0.001f, TEXT("Disque plein")));
			})
			// Revue finale, M-2 : rien sur la trajectoire, le tir atteint sa portée maximale et s'éteint sans exploser
			.UntilServer(TEXT("Serveur : fin de course"), [this](FBasePIENetworkComponentState&)
			{
				return !ServerProjectile.IsValid() || ServerProjectile->HasExploded();
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : bout de portée = extinction, pas d'explosion"), [this](FBasePIENetworkComponentState&)
			{
				ASSERT_THAT(IsTrue(ServerProjectile.IsValid(), TEXT("Projectile encore là à sa fin de course")));
				ASSERT_THAT(IsTrue(ServerProjectile->HasFizzled(), TEXT("Bout de portée : extinction")));
			})
			.UntilClients(TEXT("Clients : extinction reçue"), [this](FBasePIENetworkComponentState& Client)
			{
				const TWeakObjectPtr<AGenProjectile>* Found = ClientProjectiles.Find(Client.ClientIndex);
				return Found && Found->IsValid() && (*Found)->HasExploded();
			}, DefaultWait())
			.ThenClients(TEXT("Clients : ni explosion ni marqueur"), [this](FBasePIENetworkComponentState& Client)
			{
				AGenProjectile* Projectile = ClientProjectiles.FindRef(Client.ClientIndex).Get();
				ASSERT_THAT(IsNotNull(Projectile));
				ASSERT_THAT(IsTrue(Projectile->HasFizzled(), TEXT("Extinction répliquée (pas d'éclat d'impact)")));
				ASSERT_THAT(IsFalse(Projectile->GetGroundMarker() && Projectile->GetGroundMarker()->IsVisible(), TEXT("Marqueur caché")));
			});
	}

	/** Relâché aussitôt (avant le 1er seuil, 0.3 s) : pas d'éclaboussure. */
	TEST_METHOD(Fed0_ObserverSeesServerSplashAndMarker)
	{
		QueueShotAndCheck(0.1f, 0);
	}

	/** Seuils à 0.3 et 0.6 s : 2 flammes. */
	TEST_METHOD(Fed2_ObserverSeesServerSplashAndMarker)
	{
		QueueShotAndCheck(0.75f, 2);
	}

	/** Plafond (3 flammes, 0.9 s) : le client s'arrête seul. */
	TEST_METHOD(Fed3_ObserverSeesServerSplashAndMarker)
	{
		QueueShotAndCheck(1.2f, 3);
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
