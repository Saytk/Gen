#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Champions/Curffe/CurffeGA_Combustion.h"
#include "Champions/Curffe/CurffeGA_LivingFlame.h"
#include "Champions/Curffe/CurffeHearthComponent.h"
#include "Character/GenPlayerCharacter.h"
#include "Character/GenSpellIndicatorComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Net/GenNetCurffeTestAbilities.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.PowerVisuals (Plan Visuals V6 et V8, Curffe-Visuals.md §3.6, §3.7, §4, §5) : ce que tout le monde voit des
 * sorts d'énergie de Curffe, lancés par le client 0 (équipe 0) ; client 1 ennemi, client 2 allié.
 * - Télégraphe centré, vu par TOUS les clients (matrice §4) : la nova de 3 m pendant l'incantation de Combustion,
 *   l'anneau de 2.5 m pendant la forme de Living Flame ; plus rien ensuite.
 * - Foyer : les flammes convergent pendant la forme (et l'incantation de Combustion), puis la recharge rallume les cinq
 *   emplacements ; embrasé, flammes × 1.4 ; flammes illimitées : une flamme nourrie part et son emplacement se rallume
 *   aussitôt, rien ne revient à l'annulation.
 * Matériau de test sur les indicateurs (les MI_Telegraph_* ne sont assignés que dans BP_Champion).
 */
NETWORK_TEST_CLASS(PowerVisuals, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	TWeakObjectPtr<UAbilitySystemComponent> CasterASC;
	int32 CasterPlayerId = INDEX_NONE;
	float ClientMark = 0.f;
	/** Relevés par client (index = client). */
	float BaseFlameScale[3] = { 0.f, 0.f, 0.f };
	int32 FlightsBefore[3] = { 0, 0, 0 };
	int32 ReturnsBefore[3] = { 0, 0, 0 };

	static FGameplayTag LeapInputTag() { return FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.3")); }

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		GetMutableDefault<UGenNetTestGA_MeteorLeap>()->InputTag = LeapInputTag();
		UGenNetTestGA_MeteorLeap::TestCooldownTags = FGameplayTagContainer();
		UGenNetTestGA_MeteorLeap::TestRingSpawnOffset = 0.f;
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(3)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	AGenCharacterBase* GetCasterIn(const UWorld* World) const
	{
		const AGenPlayerState* PS = FindPlayerStateById(World, CasterPlayerId);
		return PS ? PS->GetPawn<AGenCharacterBase>() : nullptr;
	}

	UGenSpellIndicatorComponent* GetIndicatorIn(const UWorld* World) const
	{
		const AGenCharacterBase* Caster = GetCasterIn(World);
		return Caster ? Caster->GetSpellIndicator() : nullptr;
	}

	UCurffeHearthComponent* GetHearthIn(const UWorld* World) const
	{
		const AGenCharacterBase* Caster = GetCasterIn(World);
		return Caster ? Caster->FindComponentByClass<UCurffeHearthComponent>() : nullptr;
	}

	static UAbilitySystemComponent* LocalASC(const FBasePIENetworkComponentState& Client)
	{
		const APlayerController* PC = GetLocalController(Client);
		return GetASC(PC ? PC->GetPlayerState<AGenPlayerState>() : nullptr);
	}

	static bool ClientActivate(const FBasePIENetworkComponentState& Client, TSubclassOf<UGameplayAbility> AbilityClass)
	{
		UAbilitySystemComponent* ASC = LocalASC(Client);
		const FGameplayAbilitySpec* Spec = FindAbilitySpec(ASC, AbilityClass);
		return Spec && ASC->TryActivateAbility(Spec->Handle, true);
	}

	static void SendLeapInput(const FBasePIENetworkComponentState& Client, bool bPressed)
	{
		UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(LocalASC(Client));
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

	/** Caster (client 0) au centre, ennemi et allié loin (hors des zones), sorts accordés, énergie et flammes réglées. */
	void QueueSetup(const TArray<TSubclassOf<UGenGameplayAbility>>& Abilities, float StartEnergy, float StartFlames)
	{
		Network
			.UntilServer(TEXT("Serveur : joueurs prêts"), [](FBasePIENetworkComponentState& Server) { return AreAllServerPlayersReady(Server); }, DefaultWait())
			.UntilClients(TEXT("Clients : joueurs prêts"), [](FBasePIENetworkComponentState& Client) { return IsPlayerReady(GetLocalController(Client)); }, DefaultWait())
			.ThenServer(TEXT("Serveur : sol de test"), [](FBasePIENetworkComponentState& Server) { SpawnTestFloor(Server.World); })
			.UntilClients(TEXT("Clients : sol de test reçu"), [](FBasePIENetworkComponentState& Client) { return HasTestFloor(Client.World); }, DefaultWait())
			.ThenServer(TEXT("Serveur : joueurs placés, sorts accordés, énergie et flammes"), [this, Abilities, StartEnergy, StartFlames](FBasePIENetworkComponentState& Server)
			{
				AGenPlayerCharacter* Caster = GetServerController(Server, 0)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Enemy = GetServerController(Server, 1)->GetPawn<AGenPlayerCharacter>();
				AGenPlayerCharacter* Ally = GetServerController(Server, 2)->GetPawn<AGenPlayerCharacter>();
				ASSERT_THAT(IsNotNull(Caster));
				ASSERT_THAT(IsNotNull(Enemy));
				ASSERT_THAT(IsNotNull(Ally));
				ASSERT_THAT(IsTrue(Ally->GetTeamId() == Caster->GetTeamId() && Enemy->GetTeamId() != Caster->GetTeamId(), TEXT("Clients 0 et 2 alliés, client 1 ennemi")));
				CasterPlayerId = Caster->GetPlayerState()->GetPlayerId();
				CasterASC = Caster->GetAbilitySystemComponent();
				Caster->TeleportTo(FVector(0.f, 0.f, StandingHeight), FRotator::ZeroRotator, false, true);
				Enemy->TeleportTo(FVector(1000.f, 0.f, StandingHeight), FRotator::ZeroRotator, false, true);
				Ally->TeleportTo(FVector(0.f, 1000.f, StandingHeight), FRotator::ZeroRotator, false, true);
				Cast<UGenAbilitySystemComponent>(CasterASC.Get())->GrantAbilities(Abilities, nullptr);
				CasterASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), StartEnergy);
				CasterASC->SetNumericAttributeBase(UGenAttributeSet::GetResourceAttribute(), StartFlames);
				ASSERT_THAT(IsNotNull(Caster->FindComponentByClass<UCurffeHearthComponent>(), TEXT("BP_Curffe porte le Foyer")));
			})
			.UntilClient(TEXT("Client 0 : sorts, énergie et flammes répliqués"), 0, [this, Abilities, StartEnergy, StartFlames](FBasePIENetworkComponentState& Client)
			{
				UAbilitySystemComponent* ASC = LocalASC(Client);
				for (const TSubclassOf<UGenGameplayAbility>& Ability : Abilities)
				{
					if (!FindAbilitySpec(ASC, Ability))
					{
						return false;
					}
				}
				return FMath::IsNearlyEqual(GetAttribute(ASC, UGenAttributeSet::GetEnergyAttribute()), StartEnergy, 0.01f)
					&& FMath::IsNearlyEqual(GetAttribute(ASC, UGenAttributeSet::GetResourceAttribute()), StartFlames, 0.01f);
			}, DefaultWait())
			.UntilClients(TEXT("Clients : lanceur posé, Foyer à jour, matériau de test sur ses indicateurs"), [this, StartFlames](FBasePIENetworkComponentState& Client)
			{
				AGenCharacterBase* Caster = GetCasterIn(Client.World);
				UGenSpellIndicatorComponent* Indicator = GetIndicatorIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				if (!Caster || !Indicator || !Hearth || !Caster->GetCharacterMovement()->IsMovingOnGround()
					|| FVector::Dist2D(Caster->GetActorLocation(), FVector::ZeroVector) > 20.f
					|| Hearth->GetVisibleFlameCount() != FMath::FloorToInt32(StartFlames) || Hearth->GetLitFlameScale() <= 0.f)
				{
					return false;
				}
				Indicator->SetAllMaterialsForTests(UMaterial::GetDefaultMaterial(MD_Surface));
				BaseFlameScale[Client.ClientIndex] = Hearth->GetLitFlameScale();
				return true;
			}, DefaultWait());
	}

	void QueueClientWait(const TCHAR* Description, float Seconds)
	{
		Network
			.ThenClient(TEXT("Client 0 : départ de l'attente"), 0, [this](FBasePIENetworkComponentState& Client) { ClientMark = Client.World->GetTimeSeconds(); })
			.UntilClient(Description, 0, [this, Seconds](FBasePIENetworkComponentState& Client) { return Client.World->GetTimeSeconds() >= ClientMark + Seconds; }, DefaultWait());
	}

	TEST_METHOD(LivingFlame_SelfTelegraphForEveryone_HearthConvergesThenRefills)
	{
		QueueSetup({ UCurffeGA_LivingFlame::StaticClass() }, 60.f, 0.f);
		Network
			.ThenClients(TEXT("Clients : pas de télégraphe avant le sort"), [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(GetIndicatorIn(Client.World)->GetShownSize(TEXT("Self")) < 0.f));
				ASSERT_THAT(IsFalse(GetHearthIn(Client.World)->IsConverging()));
			})
			.ThenClient(TEXT("Client 0 : lance la flamme vivante"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(ClientActivate(Client, UCurffeGA_LivingFlame::StaticClass()), TEXT("Activation refusée côté client")));
			})
			.UntilClients(TEXT("Clients (lanceur, ennemi, allié) : anneau de 2.5 m autour du lanceur, Foyer qui converge"), [this](FBasePIENetworkComponentState& Client)
			{
				const UGenSpellIndicatorComponent* Indicator = GetIndicatorIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				return Indicator && Hearth && FMath::IsNearlyEqual(Indicator->GetShownSize(TEXT("Self")), 250.f, 0.01f) && Hearth->IsConverging();
			}, DefaultWait())
			.ThenClients(TEXT("Clients : le télégraphe n'est pas une visée"), [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsFalse(GetIndicatorIn(Client.World)->IsAiming()));
			})
			.UntilClients(TEXT("Clients : après la forme, plus de télégraphe ; Foyer rechargé à 5, orbite normale"), [this](FBasePIENetworkComponentState& Client)
			{
				const UGenSpellIndicatorComponent* Indicator = GetIndicatorIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				return Indicator && Hearth && Indicator->GetShownSize(TEXT("Self")) < 0.f && !Hearth->IsConverging()
					&& Hearth->GetVisibleFlameCount() == 5 && FMath::IsNearlyEqual(Hearth->GetCurrentOrbitRadius(), 70.f, 0.01f);
			}, DefaultWait());
	}

	TEST_METHOD(Combustion_SelfTelegraphForEveryone_HearthAblaze_UnlimitedFlames)
	{
		QueueSetup({ UCurffeGA_Combustion::StaticClass(), UGenNetTestGA_MeteorLeap::StaticClass() }, 100.f, 1.f);
		Network
			.ThenClient(TEXT("Client 0 : lance la combustion"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(IsTrue(ClientActivate(Client, UCurffeGA_Combustion::StaticClass()), TEXT("Activation refusée côté client")));
			})
			.UntilClients(TEXT("Clients (lanceur, ennemi, allié) : nova de 3 m autour du lanceur pendant l'incantation, Foyer resserré"), [this](FBasePIENetworkComponentState& Client)
			{
				const UGenSpellIndicatorComponent* Indicator = GetIndicatorIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				return Indicator && Hearth && FMath::IsNearlyEqual(Indicator->GetShownSize(TEXT("Self")), 300.f, 0.01f) && Hearth->IsConverging();
			}, DefaultWait())
			.UntilClients(TEXT("Clients : embrasé, plus de télégraphe, flammes du Foyer × 1.4, Foyer plein"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				const UGenSpellIndicatorComponent* Indicator = GetIndicatorIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				const UAbilitySystemComponent* ASC = Caster ? Caster->GetAbilitySystemComponent() : nullptr;
				return ASC && Indicator && Hearth && ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Curffe.Ablaze")))
					&& ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.FreeResource")))
					&& Indicator->GetShownSize(TEXT("Self")) < 0.f && !Hearth->IsConverging() && Hearth->GetVisibleFlameCount() == 5
					&& FMath::IsNearlyEqual(Hearth->GetLitFlameScale(), BaseFlameScale[Client.ClientIndex] * 1.4f, 0.001f);
			}, DefaultWait())
			.ThenClients(TEXT("Clients : relevé des vols"), [this](FBasePIENetworkComponentState& Client)
			{
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				FlightsBefore[Client.ClientIndex] = Hearth->GetStartedFlightCount();
				ReturnsBefore[Client.ClientIndex] = Hearth->GetStartedReturnFlightCount();
			})
			.ThenClient(TEXT("Client 0 : vise et nourrit le bond (nourrissage rapide)"), 0, [](FBasePIENetworkComponentState& Client)
			{
				AGenPlayerController* PC = Cast<AGenPlayerController>(GetLocalController(Client));
				PC->bDebugAimOverride = true;
				PC->DebugAimLocation = FVector(500.f, 0.f, StandingHeight);
				SendLeapInput(Client, true);
			})
			.UntilClients(TEXT("Clients : une flamme nourrie s'envole, son emplacement reste allumé (5)"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
				return Caster && Hearth && Caster->GetFedResource() >= 1 && Hearth->GetStartedFlightCount() > FlightsBefore[Client.ClientIndex]
					&& Hearth->GetVisibleFlameCount() == 5;
			}, DefaultWait())
			.ThenClient(TEXT("Client 0 : annule le bond"), 0, [this](FBasePIENetworkComponentState& Client)
			{
				ASSERT_THAT(AreEqual(1, Cast<UGenAbilitySystemComponent>(LocalASC(Client))->CancelPendingCasts(), TEXT("Le bond en nourrissage est annulé")));
				SendLeapInput(Client, false);
			})
			.UntilClients(TEXT("Clients : plus rien de nourri"), [this](FBasePIENetworkComponentState& Client)
			{
				const AGenCharacterBase* Caster = GetCasterIn(Client.World);
				return Caster && Caster->GetFedResource() == 0;
			}, DefaultWait());
		// Laisse le temps à un vol de retour de partir chez chaque client (il ne doit pas y en avoir)
		QueueClientWait(TEXT("Client 0 : 0.4 s"), 0.4f);
		Network.ThenClients(TEXT("Clients : flammes illimitées, rien ne revient, Foyer toujours plein"), [this](FBasePIENetworkComponentState& Client)
		{
			const UCurffeHearthComponent* Hearth = GetHearthIn(Client.World);
			ASSERT_THAT(AreEqual(ReturnsBefore[Client.ClientIndex], Hearth->GetStartedReturnFlightCount(), TEXT("Aucun vol de retour")));
			ASSERT_THAT(AreEqual(5, Hearth->GetVisibleFlameCount()));
		});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Net/GenNetCurffeTestAbilities.h"

/**
 * Pyroblast (Curffe-Visuals.md §3.1, ⚑ F11) : comme la boule de feu, une attaque de base (non nourrissable, incantation
 * de 0.35 s) : pas de ligne de visée par défaut (gen.ShowBasicAttackAimLine). L'éclat de 1.2 m de la ligne optionnelle
 * est épinglé par Gen.Visuals.BaseExplosionAim.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenPyroblastAimLineTest, "Gen.Visuals.PyroblastAimLine",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenPyroblastAimLineTest::RunTest(const FString& Parameters)
{
	const UGenNetTestGA_Pyroblast* Pyroblast = GetDefault<UGenNetTestGA_Pyroblast>();
	TestTrue(TEXT("Pyroblast : attaque de base, ligne de visée seulement sur option"), Pyroblast->IsBasicAttack());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
