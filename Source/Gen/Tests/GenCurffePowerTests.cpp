#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "Champions/Curffe/CurffeGA_Combustion.h"
#include "Champions/Curffe/CurffeGA_LivingFlame.h"
#include "Champions/Curffe/CurffeGameplayTags.h"
#include "Actors/GenGroundArea.h"
#include "Character/GenTrainingDummy.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenGameplayTags.h"
#include "Tests/GenTestWorld.h"

using namespace GenTestWorld;

/**
 * Living Flame (R) et Combustion (F) joués de bout en bout sur un mannequin qui a l'autorité (comme un hôte : incantation,
 * visée locale, lancer, forme / éruption). La cible, un second mannequin neutre (hostile à tous), est à 1.5 m.
 * Les règles réseau (prédiction, client distant, allié épargné) sont dans Gen.Net.LivingFlame / Gen.Net.Combustion.
 */
namespace GenCurffePowerTests
{
	struct FPowerFixture
	{
		FScopedTestWorld TestWorld;
		AGenTrainingDummy* Caster = nullptr;
		AGenTrainingDummy* Target = nullptr;
		UGenAbilitySystemComponent* ASC = nullptr;
		UGenAbilitySystemComponent* TargetASC = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			Caster = TestWorld.SpawnDummy();
			Target = TestWorld.SpawnDummy();
			ASC = Caster ? Caster->GetGenAbilitySystemComponent() : nullptr;
			TargetASC = Target ? Target->GetGenAbilitySystemComponent() : nullptr;
			if (!Test.TestNotNull(TEXT("ASC du lanceur"), ASC) || !Test.TestNotNull(TEXT("ASC de la cible"), TargetASC))
			{
				return false;
			}
			Caster->SetActorLocation(FVector::ZeroVector);
			Target->SetActorLocation(FVector(150.f, 0.f, 0.f));

			// Foyer de Curffe (5 flammes max), vide au départ
			ApplyClass(ASC, UCurffeGE_HearthSetup::StaticClass());
			ASC->SetNumericAttributeBase(UGenAttributeSet::GetResourceAttribute(), 0.f);
			return true;
		}

		float Attr(const UAbilitySystemComponent* InASC, const FGameplayAttribute& Attribute) const { return Get(InASC, Attribute); }
	};

	/** Repoussé vers +X : lancé (vitesse) ou lancement en attente du prochain pas du mouvement. */
	bool IsPushedAlongX(const ACharacter* Character)
	{
		return Character->GetVelocity().X > 100.f || Character->GetCharacterMovement()->PendingLaunchVelocity.X > 100.f;
	}

	/**
	 * Le monde de test n'a pas de mode de jeu : il n'a jamais « commencé » et un acteur apparu n'y reçoit pas BeginPlay
	 * (le mannequin le reçoit de SpawnDummy). Les zones posées par le sort (anneau, nova) le reçoivent ici : impact
	 * immédiat (sans délai), comme en jeu. Renvoie le nombre de zones déclenchées.
	 */
	int32 BeginPendingAreas(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AGenGroundArea> It(World); It; ++It)
		{
			if (!It->HasActorBegunPlay())
			{
				It->DispatchBeginPlay();
				++Count;
			}
		}
		return Count;
	}

	bool IsActive(UAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Handle)
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		return Spec && Spec->IsActive();
	}
}

using namespace GenCurffePowerTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenLivingFlameTest, "Gen.Curffe.LivingFlame",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenLivingFlameTest::RunTest(const FString& Parameters)
{
	FPowerFixture F;
	if (!F.Init(*this))
	{
		return false;
	}

	const UCurffeGA_LivingFlame* CDO = GetDefault<UCurffeGA_LivingFlame>();
	TestEqual(TEXT("coût 25"), CDO->EnergyCost, 25.f);
	TestEqual(TEXT("touche R"), CDO->InputTag, FGameplayTag(GenGameplayTags::InputTag_Ability_3));
	TestEqual(TEXT("télégraphe : l'anneau pendant la forme"), CDO->GetSelfTelegraphRadius(true), 250.f);
	TestEqual(TEXT("pas de télégraphe pendant l'incantation"), CDO->GetSelfTelegraphRadius(false), 0.f);

	const FGameplayAbilitySpecHandle Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UCurffeGA_LivingFlame::StaticClass(), 1));
	const FGameplayAbilitySpecHandle OtherHandle = F.ASC->GiveAbility(FGameplayAbilitySpec(UGenGA_Projectile::StaticClass(), 1));
	const FGameplayAbilityActorInfo* ActorInfo = F.ASC->AbilityActorInfo.Get();
	const UGameplayAbility* Other = F.ASC->FindAbilitySpecFromHandle(OtherHandle)->GetPrimaryInstance();

	// 60 d'énergie : payé une fois => 35 (l'énergie est bornée à 0, 25 cacherait une double dépense)
	F.ASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), 60.f);
	const float BaseSpeed = F.Attr(F.ASC, UGenAttributeSet::GetMoveSpeedAttribute());
	const float TargetHealth = F.Attr(F.TargetASC, UGenAttributeSet::GetHealthAttribute());
	const float CasterHealth = F.Attr(F.ASC, UGenAttributeSet::GetHealthAttribute());

	if (!TestTrue(TEXT("activation"), F.ASC->TryActivateAbility(Handle)))
	{
		return false;
	}
	TestEqual(TEXT("incantation : rien de payé"), F.Attr(F.ASC, UGenAttributeSet::GetEnergyAttribute()), 60.f);
	TestFalse(TEXT("pas encore intouchable"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable));

	// Lancer à 0.1 s : forme de feu (0.25 s : bien après le lancer, bien avant la fin de la forme)
	F.TestWorld.Advance(0.25f);
	TestTrue(TEXT("forme : intouchable"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable));
	TestTrue(TEXT("forme : tag de Curffe (visuel)"), F.ASC->HasMatchingGameplayTag(CurffeGameplayTags::State_LivingFlame));
	TestTrue(TEXT("forme : sans sort (verrou)"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_CastLocked));
	TestFalse(TEXT("forme : un autre sort est refusé"), Other->CanActivateAbility(OtherHandle, ActorInfo));
	TestEqual(TEXT("payé au lancer : 60 -> 35"), F.Attr(F.ASC, UGenAttributeSet::GetEnergyAttribute()), 35.f);
	TestTrue(TEXT("recharge de 16 s posée"), F.ASC->HasMatchingGameplayTag(CurffeGameplayTags::Cooldown_Ability_LivingFlame));
	// Revue P3 T8-10, I2 : Foyer plein dès le départ (prédit chez le client), affiché à la fin de la forme
	TestEqual(TEXT("forme : Foyer déjà rempli à 5"), F.Attr(F.ASC, UGenAttributeSet::GetResourceAttribute()), 5.f);
	TestFalse(TEXT("forme : contrôle dur ignoré"), F.ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestTrue(TEXT("forme : canalisation (barre qui se vide)"), F.Caster->GetCastInfo().IsCasting() && F.Caster->GetCastInfo().bChannel);
	TestEqual(TEXT("canalisation de la durée de la forme"), F.Caster->GetCastInfo().Duration, 0.5f, 0.001f);
	TestEqual(TEXT("cible pas encore touchée"), F.Attr(F.TargetASC, UGenAttributeSet::GetHealthAttribute()), TargetHealth);

	// Fin de la forme (0.6 s) : anneau, Foyer plein, hâte
	F.TestWorld.Advance(0.45f);
	TestEqual(TEXT("un anneau posé"), BeginPendingAreas(F.TestWorld.World), 1);
	TestFalse(TEXT("sort terminé"), IsActive(F.ASC, Handle));
	TestFalse(TEXT("plus intouchable"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable));
	TestFalse(TEXT("plus de tag de forme"), F.ASC->HasMatchingGameplayTag(CurffeGameplayTags::State_LivingFlame));
	TestFalse(TEXT("peut de nouveau lancer"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_CastLocked));
	TestTrue(TEXT("un autre sort est de nouveau possible"), Other->CanActivateAbility(OtherHandle, ActorInfo));
	TestFalse(TEXT("plus de canalisation"), F.Caster->GetCastInfo().IsCasting());
	TestEqual(TEXT("anneau : 8 dégâts à la cible"), F.Attr(F.TargetASC, UGenAttributeSet::GetHealthAttribute()), TargetHealth - 8.f);
	TestEqual(TEXT("anneau : le lanceur n'est pas touché"), F.Attr(F.ASC, UGenAttributeSet::GetHealthAttribute()), CasterHealth);
	TestTrue(TEXT("anneau : la cible est repoussée (vers +X)"), IsPushedAlongX(F.Target));
	TestTrue(TEXT("anneau : le lanceur n'est pas repoussé"), FMath::Abs(F.Caster->GetVelocity().X) < 1.f && F.Caster->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
	TestEqual(TEXT("Foyer rempli à 5"), F.Attr(F.ASC, UGenAttributeSet::GetResourceAttribute()), 5.f);
	TestEqual(TEXT("hâte +30 %"), F.Attr(F.ASC, UGenAttributeSet::GetMoveSpeedAttribute()), BaseSpeed * 1.3f, 0.5f);

	F.TestWorld.Advance(2.1f);
	TestEqual(TEXT("hâte finie après 2 s"), F.Attr(F.ASC, UGenAttributeSet::GetMoveSpeedAttribute()), BaseSpeed, 0.5f);
	TestTrue(TEXT("recharge encore là (16 s)"), F.ASC->HasMatchingGameplayTag(CurffeGameplayTags::Cooldown_Ability_LivingFlame));
	TestFalse(TEXT("recharge : relance refusée"), F.ASC->TryActivateAbility(Handle));

	// Mort pendant la forme : la forme et le verrou disparaissent (RemoveTimedStates + annulation, comme HandleOutOfHealth)
	F.ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(CurffeGameplayTags::Cooldown_Ability_LivingFlame));
	F.ASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), 25.f);
	TestTrue(TEXT("seconde activation"), F.ASC->TryActivateAbility(Handle));
	F.TestWorld.Advance(0.25f);
	TestTrue(TEXT("seconde forme"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable));
	F.ASC->CancelAllAbilities();
	F.ASC->RemoveTimedStates();
	TestFalse(TEXT("mort : plus intouchable"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable));
	TestFalse(TEXT("mort : plus de verrou"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_CastLocked));
	TestFalse(TEXT("mort : plus de canalisation"), F.Caster->GetCastInfo().IsCasting());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCombustionTest, "Gen.Curffe.Combustion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCombustionTest::RunTest(const FString& Parameters)
{
	FPowerFixture F;
	if (!F.Init(*this))
	{
		return false;
	}

	const UCurffeGA_Combustion* CDO = GetDefault<UCurffeGA_Combustion>();
	TestEqual(TEXT("coût 100"), CDO->EnergyCost, 100.f);
	TestEqual(TEXT("touche F"), CDO->InputTag, FGameplayTag(GenGameplayTags::InputTag_Ability_Ultimate));
	TestEqual(TEXT("télégraphe : la nova pendant l'incantation"), CDO->GetSelfTelegraphRadius(false), 300.f);
	TestEqual(TEXT("pas de canalisation"), CDO->GetSelfTelegraphRadius(true), 0.f);

	const FGameplayAbilitySpecHandle Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UCurffeGA_Combustion::StaticClass(), 1));
	auto SetEnergy = [&F](float Energy) { F.ASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), Energy); };
	auto HasAblaze = [&F]() { return F.ASC->HasMatchingGameplayTag(CurffeGameplayTags::State_Ablaze); };

	// --- Contrôle dur pendant l'incantation : rien n'est payé, pas d'embrasement
	SetEnergy(100.f);
	TestTrue(TEXT("activation"), F.ASC->TryActivateAbility(Handle));
	F.TestWorld.Advance(0.25f);
	TestTrue(TEXT("étourdi pendant l'incantation"), F.ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 0.3f, nullptr).IsValid());
	TestFalse(TEXT("incantation interrompue"), IsActive(F.ASC, Handle));
	F.TestWorld.Advance(0.6f);
	TestEqual(TEXT("interrompu : 100 d'énergie gardés"), F.Attr(F.ASC, UGenAttributeSet::GetEnergyAttribute()), 100.f);
	TestFalse(TEXT("interrompu : pas embrasé"), HasAblaze());
	TestEqual(TEXT("interrompu : pas de nova"), BeginPendingAreas(F.TestWorld.World), 0);

	// --- Incantation complète
	const float TargetHealth = F.Attr(F.TargetASC, UGenAttributeSet::GetHealthAttribute());
	F.ASC->SetNumericAttributeBase(UGenAttributeSet::GetResourceAttribute(), 1.f);
	TestFalse(TEXT("99 d'énergie : refusé"), (SetEnergy(99.f), F.ASC->TryActivateAbility(Handle)));
	// Revue P3 T8-10, M6 : énergie max 200, départ à 150 (l'énergie est bornée à 0 : 100 -> 0 cacherait une double
	// dépense) et chaque baisse d'énergie comptée : une seule dépense
	F.ASC->SetNumericAttributeBase(UGenAttributeSet::GetMaxEnergyAttribute(), 200.f);
	SetEnergy(150.f);
	int32 EnergyDrops = 0;
	float EnergySpent = 0.f;
	const FDelegateHandle EnergyHandle = F.ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).AddLambda(
		[&EnergyDrops, &EnergySpent](const FOnAttributeChangeData& Data)
		{
			if (Data.NewValue < Data.OldValue)
			{
				++EnergyDrops;
				EnergySpent += Data.OldValue - Data.NewValue;
			}
		});
	TestTrue(TEXT("150 d'énergie : activation"), F.ASC->TryActivateAbility(Handle));
	F.TestWorld.Advance(0.3f);
	TestTrue(TEXT("0.3 s : toujours en incantation"), IsActive(F.ASC, Handle));
	TestEqual(TEXT("0.3 s : rien de payé"), F.Attr(F.ASC, UGenAttributeSet::GetEnergyAttribute()), 150.f);
	TestFalse(TEXT("0.3 s : pas encore embrasé"), HasAblaze());

	F.TestWorld.Advance(0.3f);
	TestEqual(TEXT("une nova posée"), BeginPendingAreas(F.TestWorld.World), 1);
	TestFalse(TEXT("lancé : sort terminé"), IsActive(F.ASC, Handle));
	TestEqual(TEXT("100 payés à la fin de l'incantation : 150 -> 50"), F.Attr(F.ASC, UGenAttributeSet::GetEnergyAttribute()), 50.f);
	TestEqual(TEXT("une seule baisse d'énergie"), EnergyDrops, 1);
	TestEqual(TEXT("100 dépensés en tout"), EnergySpent, 100.f, 0.01f);
	F.ASC->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetEnergyAttribute()).Remove(EnergyHandle);
	TestTrue(TEXT("embrasé"), HasAblaze());
	TestTrue(TEXT("nourrissage rapide"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_FastFeeding));
	TestTrue(TEXT("flammes illimitées"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_FreeResource));
	TestEqual(TEXT("Foyer plein"), F.Attr(F.ASC, UGenAttributeSet::GetResourceAttribute()), 5.f);
	TestEqual(TEXT("nova : 20 dégâts"), F.Attr(F.TargetASC, UGenAttributeSet::GetHealthAttribute()), TargetHealth - 20.f);
	TestTrue(TEXT("nova : la cible est repoussée"), IsPushedAlongX(F.Target));
	TestFalse(TEXT("pas d'immunité aux contrôles"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_CCImmune) || F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable));

	// Embrasé 5 s
	F.TestWorld.Advance(4.7f);
	TestTrue(TEXT("encore embrasé à 4.8 s"), HasAblaze());
	F.TestWorld.Advance(0.4f);
	TestFalse(TEXT("embrasement fini après 5 s"), HasAblaze());
	TestFalse(TEXT("nourrissage normal"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_FastFeeding));
	TestFalse(TEXT("flammes normales"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_FreeResource));

	// Mort pendant l'embrasement : tout part avec les états temporaires
	SetEnergy(100.f);
	TestTrue(TEXT("troisième activation"), F.ASC->TryActivateAbility(Handle));
	F.TestWorld.Advance(0.6f);
	TestTrue(TEXT("embrasé de nouveau"), HasAblaze());
	F.ASC->CancelAllAbilities();
	F.ASC->RemoveTimedStates();
	TestFalse(TEXT("mort : plus embrasé"), HasAblaze());
	TestFalse(TEXT("mort : plus de nourrissage rapide"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_FastFeeding));
	TestFalse(TEXT("mort : plus de flammes illimitées"), F.ASC->HasMatchingGameplayTag(GenGameplayTags::State_FreeResource));
	return true;
}

#endif
