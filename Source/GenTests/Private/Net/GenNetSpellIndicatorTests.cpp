#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "Character/GenSpellIndicatorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/GenNetTestHelpers.h"
#include "Player/GenPlayerState.h"

using namespace GenNetTest;

/**
 * Gen.Net.SpellIndicator : la visée d'un sort ne part jamais chez les autres joueurs (Plan Visuals, Review Focus 5).
 * Le même composant est posé sur le pion du client 0 sur les trois machines : seul le client 0 ouvre la visée ;
 * le serveur dédié ne l'ouvre pas et ne fait même pas tourner le composant. Le composant est ajouté par le test
 * (le personnage ne le porte qu'à partir de la Task V6).
 */
NETWORK_TEST_CLASS(SpellIndicator, "Gen.Net")
{
	FPIENetworkComponent<FBasePIENetworkComponentState> Network{ TestRunner, TestCommandBuilder, bInitializing };

	/** PlayerId du lanceur (client 0), identique sur toutes les machines. */
	int32 CasterPlayerId = INDEX_NONE;

	BEFORE_EACH()
	{
		IgnoreUntitledMapNetWarnings(*TestRunner);
		FNetworkComponentBuilder<FBasePIENetworkComponentState>()
			.WithClients(2)
			.AsDedicatedServer()
			.WithGameMode(LoadGameModeClass())
			.Build(Network);
	}

	static UGenSpellIndicatorComponent* AddIndicator(APawn* Pawn)
	{
		UGenSpellIndicatorComponent* Indicator = NewObject<UGenSpellIndicatorComponent>(Pawn, TEXT("TestSpellIndicator"));
		Indicator->SetupAttachment(Pawn->GetRootComponent());
		Indicator->RegisterComponent(); // le pion a déjà commencé à jouer : BeginPlay est appelé ici
		return Indicator;
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

				UGenSpellIndicatorComponent* Indicator = AddIndicator(Caster);
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
				UGenSpellIndicatorComponent* Indicator = AddIndicator(Caster);
				ASSERT_THAT(IsTrue(Indicator->IsComponentTickEnabled(), TEXT("Les clients font tourner les télégraphes centrés, vus par tous")));

				Indicator->BeginAim(Ability);
				const bool bOwnClient = Client.ClientIndex == 0;
				ASSERT_THAT(AreEqual(bOwnClient, Indicator->IsAiming(), TEXT("Visée ouverte seulement sur le client qui contrôle le pion")));

				// La fin de visée d'un autre sort ne ferme pas celle-ci ; la sienne la ferme
				Indicator->EndAim(OtherAbility);
				ASSERT_THAT(AreEqual(bOwnClient, Indicator->IsAiming()));
				Indicator->EndAim(Ability);
				ASSERT_THAT(IsFalse(Indicator->IsAiming()));
				ASSERT_THAT(AreEqual(-1.f, Indicator->GetShownSize(TEXT("Lane"))));
			});
	}
};

#endif // ENABLE_PIE_NETWORK_TEST
