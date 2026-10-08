#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenHitRules.h"
#include "Character/GenTrainingDummy.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenGameplayTags.h"
#include "Tests/GenTestWorld.h"

using namespace GenTestWorld;

namespace GenPowerWorldTests
{
	void SetEnergy(UAbilitySystemComponent* ASC, float Energy)
	{
		ASC->SetNumericAttributeBase(UGenAttributeSet::GetEnergyAttribute(), Energy);
	}
}

using namespace GenPowerWorldTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenEnergyCostTest, "Gen.Energy.CostCheckAndApply",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenEnergyCostTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	// Un sort concret quelconque. Le coût est réglé sur l'instance (InstancedPerActor), jamais sur le CDO
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(FGameplayAbilitySpec(UGenGA_Projectile::StaticClass(), 1));
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	UGenGA_Projectile* Ability = Spec ? Cast<UGenGA_Projectile>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("instance du sort"), Ability))
	{
		return false;
	}
	Ability->EnergyCost = 25.f;
	const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();

	SetEnergy(ASC, 24.f);
	TestFalse(TEXT("24 d'énergie : coût refusé"), Ability->CheckCost(Handle, ActorInfo));
	TestFalse(TEXT("24 d'énergie : activation refusée"), Ability->CanActivateAbility(Handle, ActorInfo));
	SetEnergy(ASC, 24.99f);
	TestFalse(TEXT("24.99 ne suffit pas"), Ability->CheckCost(Handle, ActorInfo));
	SetEnergy(ASC, 25.f);
	TestTrue(TEXT("25 d'énergie : accepté (pile le coût)"), Ability->CheckCost(Handle, ActorInfo));
	TestTrue(TEXT("25 d'énergie : activation possible"), Ability->CanActivateAbility(Handle, ActorInfo));

	// CommitAbility (au lancer pour UGenGA_Cast) passe par ApplyCost
	Ability->ApplyCost(Handle, ActorInfo, FGameplayAbilityActivationInfo());
	TestEqual(TEXT("payé : 25 -> 0"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 0.f);
	TestFalse(TEXT("plus assez pour un second"), Ability->CheckCost(Handle, ActorInfo));

	// Sort gratuit : jamais refusé, rien de dépensé
	Ability->EnergyCost = 0.f;
	TestTrue(TEXT("gratuit à 0 d'énergie"), Ability->CheckCost(Handle, ActorInfo));
	SetEnergy(ASC, 10.f);
	Ability->ApplyCost(Handle, ActorInfo, FGameplayAbilityActivationInfo());
	TestEqual(TEXT("gratuit : rien dépensé"), Get(ASC, UGenAttributeSet::GetEnergyAttribute()), 10.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenUntouchableTest, "Gen.Combat.Untouchable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenUntouchableTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Target = TestWorld.SpawnDummy();
	AGenTrainingDummy* Attacker = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Target ? Target->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC de la cible"), ASC) || !TestNotNull(TEXT("attaquant"), Attacker))
	{
		return false;
	}

	int32 CounterEvents = 0;
	const FDelegateHandle CounterHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(GenGameplayTags::Event_Counter_Blocked).AddLambda([&CounterEvents](const FGameplayEventData*) { ++CounterEvents; });

	// Le sort de la cible : l'intouchable ne l'empêche pas de lancer (Living Flame pose State.CastLocked en plus)
	const FGameplayAbilitySpecHandle OwnSpell = ASC->GiveAbility(FGameplayAbilitySpec(UGenGA_Projectile::StaticClass(), 1));
	const FGameplayAbilitySpec* OwnSpec = ASC->FindAbilitySpecFromHandle(OwnSpell);
	const UGameplayAbility* OwnInstance = OwnSpec ? OwnSpec->GetPrimaryInstance() : nullptr;

	// Forme de feu : 0.5 s intouchable (et contre actif en même temps, pour vérifier la priorité)
	const FGameplayEffectSpecHandle Form = ASC->MakeOutgoingSpec(UGenGE_TimedState::StaticClass(), 1.f, ASC->MakeEffectContext());
	UGenGE_TimedState::SetDuration(*Form.Data, 0.5f, FGameplayTagContainer(GenGameplayTags::State_Untouchable));
	ASC->ApplyGameplayEffectSpecToSelf(*Form.Data);
	ASC->AddLooseGameplayTag(GenGameplayTags::State_Countering);

	TestTrue(TEXT("intouchable"), Target->IsUntouchable());
	TestTrue(TEXT("projectile ignoré"), Target->ResolveIncomingHit(Attacker, EGenHitKind::Projectile, nullptr) == EGenHitResponse::Ignored);
	TestTrue(TEXT("zone ignorée"), Target->ResolveIncomingHit(Attacker, EGenHitKind::Area, nullptr) == EGenHitResponse::Ignored);
	TestEqual(TEXT("aucun contre prévenu"), CounterEvents, 0);
	TestFalse(TEXT("étourdissement ignoré"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, Attacker).IsValid());
	TestFalse(TEXT("silence ignoré"), ASC->ApplyHardCC(GenGameplayTags::State_Silenced, 1.f, Attacker).IsValid());
	TestEqual(TEXT("pas étourdi"), ASC->GetTagCount(GenGameplayTags::State_Stunned), 0);

	UCharacterMovementComponent* Movement = Target->GetCharacterMovement();
	Movement->PendingLaunchVelocity = FVector::ZeroVector;
	Target->ApplyKnockback(FVector::ForwardVector, 300.f);
	TestTrue(TEXT("repoussement ignoré"), Movement->PendingLaunchVelocity.IsNearlyZero());

	const FGameplayEffectSpecHandle Hit = ASC->MakeOutgoingSpec(UGenGE_Damage::StaticClass(), 1.f, ASC->MakeEffectContext());
	Hit.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Damage, 50.f);
	ASC->ApplyGameplayEffectSpecToSelf(*Hit.Data);
	TestEqual(TEXT("dégâts ignorés (filet de sécurité de l'attribut)"), Get(ASC, UGenAttributeSet::GetHealthAttribute()), 200.f);

	if (TestNotNull(TEXT("instance du sort de la cible"), OwnInstance))
	{
		TestTrue(TEXT("intouchable : peut encore lancer ses sorts"), OwnInstance->CanActivateAbility(OwnSpell, ASC->AbilityActorInfo.Get()));
	}

	TestWorld.Advance(0.6f);
	ASC->RemoveLooseGameplayTag(GenGameplayTags::State_Countering);
	TestFalse(TEXT("fin de la forme"), Target->IsUntouchable());
	ASC->ApplyGameplayEffectSpecToSelf(*Hit.Data);
	TestEqual(TEXT("de nouveau touchable"), Get(ASC, UGenAttributeSet::GetHealthAttribute()), 150.f);
	TestTrue(TEXT("de nouveau contrôlable"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, Attacker).IsValid());
	Movement->PendingLaunchVelocity = FVector::ZeroVector;
	Target->ApplyKnockback(FVector::ForwardVector, 300.f);
	TestFalse(TEXT("de nouveau repoussable"), Movement->PendingLaunchVelocity.IsNearlyZero());

	ASC->GenericGameplayEventCallbacks.FindOrAdd(GenGameplayTags::Event_Counter_Blocked).Remove(CounterHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenResilienceTest, "Gen.Combat.Resilience",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenResilienceTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Target = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Target ? Target->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC de la cible"), ASC))
	{
		return false;
	}

	// Trois étourdissements de 1 s à 0, 1.2 et 2.4 s
	TestTrue(TEXT("1er étourdissement"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestWorld.Advance(1.2f);
	TestTrue(TEXT("2e"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestEqual(TEXT("pas encore immunisé (2 s)"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 0);
	TestWorld.Advance(1.2f);
	TestTrue(TEXT("3e (appliqué en entier)"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestEqual(TEXT("immunisé"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 1);
	TestEqual(TEXT("le 3e étourdit quand même"), ASC->GetTagCount(GenGameplayTags::State_Stunned), 1);

	TestFalse(TEXT("4e ignoré pendant l'immunité"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestFalse(TEXT("tout contrôle dur ignoré (silence)"), ASC->ApplyHardCC(GenGameplayTags::State_Silenced, 1.f, nullptr).IsValid());
	TestWorld.Advance(2.3f);
	TestEqual(TEXT("encore immunisé à 2.3 s (1 s + 1.5 s)"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 1);
	TestWorld.Advance(0.3f);
	TestEqual(TEXT("immunité finie"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 0);
	TestTrue(TEXT("de nouveau contrôlable"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());

	// Mort : immunité et historique effacés
	ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr);
	ASC->RemoveTimedStates();
	TestTrue(TEXT("historique remis à zéro (2 s de plus ne suffisent pas)"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 2.f, nullptr).IsValid());
	TestEqual(TEXT("pas d'immunité"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 0);

	// Mort pendant l'immunité : elle disparaît avec les autres états. Un contrôle qui chevauche le précédent ne compte
	// qu'une fois (union) : 1 s par-dessus les 2 s ne suffirait pas, 3 s d'un coup oui
	ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr);
	TestEqual(TEXT("chevauchement : 2 s seulement, pas d'immunité"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 0);
	ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 3.f, nullptr);
	TestEqual(TEXT("immunisé (3 s)"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 1);
	ASC->RemoveTimedStates();
	TestEqual(TEXT("mort : plus d'immunité"), ASC->GetTagCount(GenGameplayTags::State_CCImmune), 0);
	TestEqual(TEXT("mort : plus d'étourdissement"), ASC->GetTagCount(GenGameplayTags::State_Stunned), 0);
	return true;
}

#endif
