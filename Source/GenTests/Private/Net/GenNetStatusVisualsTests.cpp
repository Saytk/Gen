#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "Character/GenStatusVisualsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Materials/MaterialInterface.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.StatusVisuals : la forme d'état d'un joueur suit le tag répliqué sur chaque client, celui du
 * joueur (client 0, ASC complet) comme la copie simulée chez l'autre (client 1, tags minimaux), et rien
 * n'est dessiné sur le serveur dédié. Le tag vient d'un contrôle dur appliqué par le serveur (ApplyHardCC).
 * La configuration est posée à la main : celle de BP_Champion arrive avec les assets (Task 10 Step 9).
 */
NETWORK_TEST_CLASS(StatusVisuals, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	static constexpr float StunDuration = 1.f;

	/** PlayerId du joueur étourdi (client 0), identique sur toutes les machines. */
	int32 TargetPlayerId = INDEX_NONE;

	/** Composant de formes d'état du joueur ciblé, par client. */
	TMap<int32, TWeakObjectPtr<UGenStatusVisualsComponent>> ClientVisuals;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	static FGameplayTag StunnedTag()
	{
		// GenGameplayTags::* n'est pas exporté par le module Gen
		return FGameplayTag::RequestGameplayTag(TEXT("State.Stunned"));
	}

	static TArray<FGenStatusVisual> MakeConfig()
	{
		FGenStatusVisual Stunned;
		Stunned.Tag = StunnedTag();
		return { Stunned };
	}

	/** Matériau du corps imposé par l'état (n'importe lequel : seule la substitution compte ici). */
	static UMaterialInterface* LoadSwapMaterial()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
	}

	static TArray<FGenStatusVisual> MakeSwapConfig()
	{
		FGenStatusVisual Stunned;
		Stunned.Tag = StunnedTag();
		Stunned.OwnerMeshMaterial = LoadSwapMaterial();
		return { Stunned };
	}

	static USkeletalMeshComponent* FindBody(const AGenPlayerState* PlayerState)
	{
		const ACharacter* Character = PlayerState ? PlayerState->GetPawn<ACharacter>() : nullptr;
		return Character ? Character->GetMesh() : nullptr;
	}

	/** Relation écrite dans la Custom Primitive Data du corps (-1 si absente). */
	static float GetBodyRelation(const AGenPlayerState* PlayerState)
	{
		const USkeletalMeshComponent* Body = FindBody(PlayerState);
		const TArray<float>& Data = Body ? Body->GetCustomPrimitiveData().Data : TArray<float>();
		return Data.IsValidIndex(GenOwnerMeshRelationDataIndex) ? Data[GenOwnerMeshRelationDataIndex] : -1.f;
	}

	static UGenStatusVisualsComponent* FindVisuals(const AGenPlayerState* PlayerState)
	{
		const APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UGenStatusVisualsComponent>() : nullptr;
	}

	TEST_METHOD(HardCC_ShowsShapeOnEveryClient_NothingOnDedicatedServer)
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
			.ThenServer(TEXT("Serveur : cible = joueur du client 0"), [this](FBasePIENetworkComponentState& Server)
			{
				const AGenPlayerState* PS = GetServerController(Server, 0)->GetPlayerState<AGenPlayerState>();
				ASSERT_THAT(IsNotNull(PS));
				TargetPlayerId = PS->GetPlayerId();
			})
			.UntilClients(TEXT("Clients : formes d'état branchées sur le joueur ciblé"), [this](FBasePIENetworkComponentState& Client)
			{
				// Client 0 : son propre pion ; client 1 : la copie simulée du pion du client 0
				const AGenPlayerState* PS = FindPlayerStateById(Client.World, TargetPlayerId);
				UAbilitySystemComponent* ASC = GetASC(PS);
				UGenStatusVisualsComponent* Visuals = FindVisuals(PS);
				// Après l'initialisation de l'ASC sur ce pion (OnAbilitySystemInitialized rebranche une configuration vide)
				if (!ASC || !Visuals || ASC->GetAvatarActor() != Visuals->GetOwner())
				{
					return false;
				}
				Visuals->Bind(ASC, MakeConfig());
				ClientVisuals.Add(Client.ClientIndex, Visuals);
				return true;
			}, DefaultWait())
			.ThenClients(TEXT("Clients : caché au départ"), [this](FBasePIENetworkComponentState& Client)
			{
				const TWeakObjectPtr<UGenStatusVisualsComponent>* Visuals = ClientVisuals.Find(Client.ClientIndex);
				ASSERT_THAT(IsTrue(Visuals && Visuals->IsValid()));
				ASSERT_THAT(IsFalse((*Visuals)->IsStatusShown(StunnedTag())));
			})
			.ThenServer(TEXT("Serveur : étourdit le joueur ciblé ; aucune forme sur le serveur dédié"), [this](FBasePIENetworkComponentState& Server)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Server.World, TargetPlayerId);
				UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(GetASC(PS));
				ASSERT_THAT(IsNotNull(ASC, TEXT("ASC Gen attendu")));
				ASSERT_THAT(IsTrue(ASC->ApplyHardCC(StunnedTag(), StunDuration, nullptr).IsValid()));

				UGenStatusVisualsComponent* Visuals = FindVisuals(PS);
				ASSERT_THAT(IsNotNull(Visuals));
				Visuals->Bind(ASC, MakeConfig());
				ASSERT_THAT(IsFalse(Visuals->IsStatusShown(StunnedTag()), TEXT("Serveur dédié : Bind ne dessine rien")));
			})
			.UntilClients(TEXT("Clients : la forme apparaît avec le tag répliqué"), [this](FBasePIENetworkComponentState& Client)
			{
				const TWeakObjectPtr<UGenStatusVisualsComponent>* Visuals = ClientVisuals.Find(Client.ClientIndex);
				return Visuals && Visuals->IsValid() && (*Visuals)->IsStatusShown(StunnedTag());
			}, DefaultWait())
			.UntilClients(TEXT("Clients : la forme disparaît à la fin de l'étourdissement"), [this](FBasePIENetworkComponentState& Client)
			{
				const TWeakObjectPtr<UGenStatusVisualsComponent>* Visuals = ClientVisuals.Find(Client.ClientIndex);
				return Visuals && Visuals->IsValid() && !(*Visuals)->IsStatusShown(StunnedTag());
			}, DefaultWait());
	}

	/**
	 * Corps fantôme (M_VFX_GhostDither) : la relation au joueur local part dans la Custom Primitive Data 0 du corps
	 * quand le matériau est imposé. Le joueur ciblé (client 0, équipe 0) se voit « soi » (1), le client 1 (équipe 1)
	 * le voit « ennemi » (3). Le serveur dédié n'écrit rien ; à la fin de l'état le corps retrouve son matériau.
	 */
	TEST_METHOD(OwnerMeshSwap_WritesViewerRelation_SelfOnOwner_EnemyOnOtherClient)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : cible = joueur du client 0"), [this](FBasePIENetworkComponentState& Server)
			{
				ASSERT_THAT(IsNotNull(LoadSwapMaterial(), TEXT("Matériau de substitution")));
				const AGenPlayerState* PS = GetServerController(Server, 0)->GetPlayerState<AGenPlayerState>();
				ASSERT_THAT(IsNotNull(PS));
				TargetPlayerId = PS->GetPlayerId();
			})
			.UntilClients(TEXT("Clients : formes d'état (avec substitution du corps) branchées sur le joueur ciblé"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Client.World, TargetPlayerId);
				UAbilitySystemComponent* ASC = GetASC(PS);
				UGenStatusVisualsComponent* Visuals = FindVisuals(PS);
				if (!ASC || !Visuals || ASC->GetAvatarActor() != Visuals->GetOwner())
				{
					return false;
				}
				Visuals->Bind(ASC, MakeSwapConfig());
				ClientVisuals.Add(Client.ClientIndex, Visuals);
				return true;
			}, DefaultWait())
			.ThenServer(TEXT("Serveur : étourdit le joueur ciblé"), [this](FBasePIENetworkComponentState& Server)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Server.World, TargetPlayerId);
				UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(GetASC(PS));
				ASSERT_THAT(IsNotNull(ASC, TEXT("ASC Gen attendu")));
				ASSERT_THAT(IsTrue(ASC->ApplyHardCC(StunnedTag(), StunDuration, nullptr).IsValid()));

				UGenStatusVisualsComponent* Visuals = FindVisuals(PS);
				ASSERT_THAT(IsNotNull(Visuals));
				Visuals->Bind(ASC, MakeSwapConfig());
				Visuals->RefreshViewerRelation();
				ASSERT_THAT(IsNear(-1.f, GetBodyRelation(PS), 0.001f, TEXT("Serveur dédié : rien d'écrit sur le corps")));
				const USkeletalMeshComponent* Body = FindBody(PS);
				ASSERT_THAT(IsTrue(Body && Body->GetNumMaterials() > 0 && Body->GetMaterial(0) != LoadSwapMaterial(), TEXT("Serveur dédié : corps inchangé")));
			})
			.UntilClients(TEXT("Clients : corps substitué avec le tag répliqué"), [this](FBasePIENetworkComponentState& Client)
			{
				const USkeletalMeshComponent* Body = FindBody(FindPlayerStateById(Client.World, TargetPlayerId));
				return Body && Body->GetNumMaterials() > 0 && Body->GetMaterial(0) == LoadSwapMaterial();
			}, DefaultWait())
			.ThenClients(TEXT("Clients : relation au joueur local dans la Custom Primitive Data 0"), [this](FBasePIENetworkComponentState& Client)
			{
				const float Expected = Client.ClientIndex == 0 ? 1.f : 3.f;
				ASSERT_THAT(IsNear(Expected, GetBodyRelation(FindPlayerStateById(Client.World, TargetPlayerId)), 0.001f, TEXT("Soi chez le joueur ciblé, ennemi chez l'autre client")));
			})
			.UntilClients(TEXT("Clients : le corps retrouve son matériau à la fin de l'état"), [this](FBasePIENetworkComponentState& Client)
			{
				const USkeletalMeshComponent* Body = FindBody(FindPlayerStateById(Client.World, TargetPlayerId));
				return Body && Body->GetNumMaterials() > 0 && Body->GetMaterial(0) != LoadSwapMaterial();
			}, DefaultWait());
	}
};


#endif // ENABLE_PIE_NETWORK_TEST
