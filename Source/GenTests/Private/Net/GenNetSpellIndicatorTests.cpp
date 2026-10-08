#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "Character/GenPlayerCharacter.h"
#include "Character/GenSpellIndicatorComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Materials/Material.h"
#include "Net/GenNetCurffeTestAbilities.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.SpellIndicator : la visée d'un sort ne part jamais chez les autres joueurs (Plan Visuals, Review Focus 5).
 * Le même composant est posé sur le pion du client 0 sur les trois machines : seul le client 0 ouvre la visée ;
 * le serveur dédié ne l'ouvre pas et ne fait même pas tourner le composant (celui du personnage, Task V6).
 *
 * Bond météore réel (décision du 2026-10-08) : pendant le décollage, la visée (arc, cercle, amorces) n'existe que chez le
 * lanceur ; pendant le vol, TOUS les clients dessinent le cercle d'atterrissage et une amorce par boule de l'anneau, puis
 * plus rien à l'atterrissage. Matériau de test sur les indicateurs (les MI_Telegraph_* ne sont assignés que dans
 * BP_Champion) : sans matériau, une partie n'est jamais affichée.
 */
NETWORK_TEST_CLASS(SpellIndicator, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	/** PlayerId du lanceur (client 0), identique sur toutes les machines. */
	int32 CasterPlayerId = INDEX_NONE;

	float ClientMark = 0.f;

	static FGameplayTag LeapInputTag() { return FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.3")); }

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		GetMutableDefault<UGenNetTestGA_MeteorLeap>()->InputTag = LeapInputTag();
		UGenNetTestGA_MeteorLeap::TestCooldownTags = FGameplayTagContainer();
		UGenNetTestGA_MeteorLeap::TestRingSpawnOffset = 0.f;
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	/** Indicateur porté par le personnage (Task V6). */
	static UGenSpellIndicatorComponent* GetIndicator(APawn* Pawn)
	{
		const AGenCharacterBase* Character = Cast<AGenCharacterBase>(Pawn);
		return Character ? Character->GetSpellIndicator() : nullptr;
	}

	/** Indicateur du pion du lanceur dans ce monde client. */
	UGenSpellIndicatorComponent* GetCasterIndicator(const FBasePIENetworkComponentState& Client) const
	{
		const AGenPlayerState* PS = FindPlayerStateById(Client.World, CasterPlayerId);
		return PS ? GetIndicator(PS->GetPawn()) : nullptr;
	}

	static void SendLeapInput(const FBasePIENetworkComponentState& Client, bool bPressed)
	{
		const APlayerController* PC = GetLocalController(Client);
		UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr));
		if (!ASC)
		{
			return;
		}
		if (bPressed)
		{
			ASC->AbilityInputTagPressed(LeapInputTag());
		}
		else
		{
			ASC->AbilityInputTagReleased(LeapInputTag());
		}
		ASC->ProcessAbilityInput(0.f, false);
	}

	TEST_METHOD(AimIndicator_OnlyOnTheCastersOwnClient)
	{
		// N'importe quel sort convient : seule l'identité compte pour BeginAim/EndAim
		const UGenGameplayAbility* Ability = GetDefault<UGenGameplayAbility>();
		const UGenGameplayAbility* OtherAbility = GetDefault<UGenGA_Projectile>();

		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server)
			{
				return AreAllServerPlayersReady(Server);
			}, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client)
			{
				return IsPlayerReady(GetLocalController(Client));
			}, DefaultWait())
			.ThenServer(TEXT("Serveur dédié : ni visée ni tick"), [this, Ability](FBasePIENetworkComponentState& Server)
			{
				APawn* Caster = GetServerController(Server, 0)->GetPawn();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Caster->GetPlayerState()));
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();

				UGenSpellIndicatorComponent* Indicator = GetIndicator(Caster);
				ASSERT_THAT(IsNotNull(Indicator, TEXT("Le personnage porte son indicateur")));
				Indicator->BeginAim(Ability);
				ASSERT_THAT(IsFalse(Indicator->IsAiming(), TEXT("Le serveur dédié ne dessine aucune visée")));
				ASSERT_THAT(IsFalse(Indicator->IsComponentTickEnabled(), TEXT("Le serveur dédié ne fait pas tourner les indicateurs")));
			})
			.UntilClients(TEXT("Clients : le pion du lanceur est connu"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenPlayerState* PS = FindPlayerStateById(Client.World, CasterPlayerId);
				return PS && PS->GetPawn();
			}, DefaultWait())
			.ThenClients(TEXT("Clients : seul le client du lanceur ouvre la visée"), [this, Ability, OtherAbility](FBasePIENetworkComponentState& Client)
			{
				APawn* Caster = FindPlayerStateById(Client.World, CasterPlayerId)->GetPawn();
				UGenSpellIndicatorComponent* Indicator = GetIndicator(Caster);
				ASSERT_THAT(IsNotNull(Indicator, TEXT("Le personnage porte son indicateur")));
				ASSERT_THAT(IsFalse(Indicator->IsComponentTickEnabled(), TEXT("Revue V6-V8, M-5 : rien à dessiner, pas de tick")));

				Indicator->BeginAim(Ability);
				const bool bOwnClient = Client.ClientIndex == 0;
				ASSERT_THAT(AreEqual(bOwnClient, Indicator->IsAiming(), TEXT("Visée ouverte seulement sur le client qui contrôle le pion")));
				ASSERT_THAT(AreEqual(bOwnClient, Indicator->IsComponentTickEnabled(), TEXT("Tick seulement pour une visée ouverte")));

				// La fin de visée d'un autre sort ne ferme pas celle-ci ; la sienne la ferme
				Indicator->EndAim(OtherAbility);
				ASSERT_THAT(AreEqual(bOwnClient, Indicator->IsAiming()));
				Indicator->EndAim(Ability);
				ASSERT_THAT(IsFalse(Indicator->IsAiming()));
				ASSERT_THAT(IsTrue(Indicator->GetShownSize(TEXT("Lane")) < 0.f, TEXT("Rien d'affiché après la fin de visée")));
			});
	}

	TEST_METHOD(MeteorLeap_AimOnOwnerOnly_LandingCircleForEveryone)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [this](FBasePIENetworkComponentState& Server) { ASSERT_THAT(IsNotNull(SpawnTestFloor(Server.World))); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : bond accordé, joueurs placés"), [this](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Observer = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Observer));
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				Caster->TeleportTo(FVector(0.f, 0.f, StandingHeight), FRotator::ZeroRotator, false, true);
				Observer->TeleportTo(FVector(0.f, 600.f, StandingHeight), FRotator::ZeroRotator, false, true);
				Cast<UGenAbilitySystemComponent>(Caster->GetAbilitySystemComponent())->GrantAbilities({ UGenNetTestGA_MeteorLeap::StaticClass() }, nullptr);
			})
			.UntilClient(TEXT("Client 0 : bond répliqué"), 0, [](FBasePIENetworkComponentState& Client)
			{
				const APlayerController* PC = GetLocalController(Client);
				return FindAbilitySpec(GetASC(PC->GetPlayerState<AGenPlayerState>()), UGenNetTestGA_MeteorLeap::StaticClass()) != nullptr;
			}, DefaultWait())
			.UntilClients(TEXT("Clients : pion du lanceur au sol, matériau de test"), [this](FBasePIENetworkComponentState& Client)
			{
				UGenSpellIndicatorComponent* Indicator = GetCasterIndicator(Client);
				const ACharacter* Caster = Indicator ? Cast<ACharacter>(Indicator->GetOwner()) : nullptr;
				if (!Caster || !Caster->GetCharacterMovement()->IsMovingOnGround() || FVector::Dist2D(Caster->GetActorLocation(), FVector::ZeroVector) > 20.f)
				{
					return false;
				}
				Indicator->SetAllMaterialsForTests(UMaterial::GetDefaultMaterial(MD_Surface));
				return true;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : vise (1500, 0) et appuie sur le bond"), 0, [](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(1500.f, 0.f, StandingHeight);
				SendLeapInput(Client, true);
			})
			.ThenClient(TEXT("Client 0 : départ de l'attente"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(TEXT("Client 0 : une flamme nourrie (0.45 s)"), 0, [this](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + 0.45f; }, DefaultWait())
			.ThenClient(TEXT("Client 0 : sa visée (arc 7 m, cercle 1.5 m borné à la portée, une amorce)"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const UGenSpellIndicatorComponent* Indicator = GetCasterIndicator(Client);
				ASSERT_THAT(IsNotNull(Indicator));
				ASSERT_THAT(IsTrue(Indicator->IsAiming(), TEXT("Visée ouverte chez le lanceur pendant le nourrissage")));
				ASSERT_THAT(IsNear(700.f, Indicator->GetShownSize(TEXT("Arc")), 0.01f));
				ASSERT_THAT(IsNear(150.f, Indicator->GetShownSize(TEXT("Target")), 0.01f));
				ASSERT_THAT(IsNear(1.f, Indicator->GetShownSize(TEXT("Stubs")), 0.01f, TEXT("Une amorce par flamme nourrie")));
			})
			.ThenClient(TEXT("Client 1 : rien de la visée du lanceur"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const UGenSpellIndicatorComponent* Indicator = GetCasterIndicator(Client);
				ASSERT_THAT(IsNotNull(Indicator));
				ASSERT_THAT(IsFalse(Indicator->IsAiming()));
				ASSERT_THAT(IsTrue(Indicator->GetShownSize(TEXT("Arc")) < 0.f && Indicator->GetShownSize(TEXT("Target")) < 0.f, TEXT("La visée ne part jamais chez les autres")));
			})
			.ThenClient(TEXT("Client 0 : relâche (décollage puis vol)"), 0, [](FBasePIENetworkComponentState& Client) { SendLeapInput(Client, false); })
			.UntilClient(TEXT("Client 1 : voit le cercle d'atterrissage et l'amorce pendant le vol"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const UGenSpellIndicatorComponent* Indicator = GetCasterIndicator(Client);
				return Indicator && FMath::IsNearlyEqual(Indicator->GetShownSize(TEXT("Target")), 150.f, 0.01f)
					&& FMath::IsNearlyEqual(Indicator->GetShownSize(TEXT("Stubs")), 1.f, 0.01f);
			}, DefaultWait())
			.ThenClient(TEXT("Client 1 : ni arc ni visée pendant le vol"), 1, [this](FBasePIENetworkComponentState& Client)
			{
				const UGenSpellIndicatorComponent* Indicator = GetCasterIndicator(Client);
				ASSERT_THAT(IsFalse(Indicator->IsAiming()));
				ASSERT_THAT(IsTrue(Indicator->GetShownSize(TEXT("Arc")) < 0.f, TEXT("L'arc de portée reste privé")));
			})
			.UntilClient(TEXT("Client 0 : visée fermée, cercle d'atterrissage du vol"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				const UGenSpellIndicatorComponent* Indicator = GetCasterIndicator(Client);
				return Indicator && !Indicator->IsAiming() && Indicator->GetShownSize(TEXT("Arc")) < 0.f
					&& FMath::IsNearlyEqual(Indicator->GetShownSize(TEXT("Target")), 150.f, 0.01f);
			}, DefaultWait())
			.UntilClients(TEXT("Clients : plus rien après l'atterrissage"), [this](FBasePIENetworkComponentState& Client)
			{
				const UGenSpellIndicatorComponent* Indicator = GetCasterIndicator(Client);
				return Indicator && Indicator->GetShownSize(TEXT("Target")) < 0.f && Indicator->GetShownSize(TEXT("Stubs")) < 0.5f;
			}, DefaultWait());
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
