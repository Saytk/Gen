#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/Effects/GenGE_MoveSpeedMultiplier.h"
#include "NiagaraSystem.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "Character/GenStatusVisualsComponent.h"
#include "Character/GenTrainingDummy.h"
#include "GenGameplayTags.h"
#include "Tests/GenTestWorld.h"

using namespace GenTestWorld;

namespace GenCombatWorldTests
{
	void ApplySlow(UAbilitySystemComponent* ASC, float Multiplier)
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGenGE_MoveSpeedMultiplier::StaticClass(), 1.f, ASC->MakeEffectContext());
		Spec.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_MoveSpeedMultiplier, Multiplier);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	FActiveGameplayEffectHandle ApplyTimedState(UAbilitySystemComponent* ASC, float Duration, const FGameplayTag& Tag)
	{
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UGenGE_TimedState::StaticClass(), 1.f, ASC->MakeEffectContext());
		UGenGE_TimedState::SetDuration(*Spec.Data, Duration, FGameplayTagContainer(Tag));
		return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
}

using namespace GenCombatWorldTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenStunTest, "Gen.Combat.StunTimedState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenStunTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Dummy = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Dummy ? Dummy->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	TestTrue(TEXT("étourdissement appliqué"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr).IsValid());
	TestEqual(TEXT("tag State.Stunned"), ASC->GetTagCount(GenGameplayTags::State_Stunned), 1);
	TestEqual(TEXT("immobile"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 0.f);
	TestFalse(TEXT("durée nulle ignorée"), ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 0.f, nullptr).IsValid());

	TestWorld.Advance(1.1f);
	TestEqual(TEXT("fin de l'étourdissement"), ASC->GetTagCount(GenGameplayTags::State_Stunned), 0);
	TestEqual(TEXT("vitesse rendue"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 550.f);

	// Les autres contrôles durs passent par le même point d'entrée
	TestEqual(TEXT("4 contrôles durs"), GenGameplayTags::GetHardCCTags().Num(), 4);
	TestTrue(TEXT("silence appliqué"), ASC->ApplyHardCC(GenGameplayTags::State_Silenced, 1.f, nullptr).IsValid());
	TestEqual(TEXT("réduit au silence : bouge encore"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 550.f);
	TestWorld.Advance(1.1f);
	TestTrue(TEXT("neutralisé appliqué"), ASC->ApplyHardCC(GenGameplayTags::State_Incapacitated, 1.f, nullptr).IsValid());
	TestEqual(TEXT("neutralisé : immobile"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 0.f);
	TestWorld.Advance(1.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenSlowsCompoundTest, "Gen.Combat.SlowsCompound",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenSlowsCompoundTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	UAbilitySystemComponent* ASC = TestWorld.SpawnDummyASC();
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	ApplySlow(ASC, 0.5f);
	ApplySlow(ASC, 0.5f);
	TestEqual(TEXT("deux ralentis de moitié : 550 x 0.5 x 0.5"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 137.5f, 0.01f);

	const FGameplayEffectSpecHandle Haste = ASC->MakeOutgoingSpec(UGenGE_TimedMoveSpeed::StaticClass(), 1.f, ASC->MakeEffectContext());
	UGenGE_TimedMoveSpeed::SetMagnitudes(*Haste.Data, 2.f, 1.3f, FGameplayTagContainer());
	ASC->ApplyGameplayEffectSpecToSelf(*Haste.Data);
	TestEqual(TEXT("hâte x1.3 par-dessus"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 178.75f, 0.01f);

	TestWorld.Advance(2.1f);
	TestEqual(TEXT("fin de la hâte"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 137.5f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenRemoveTimedStatesTest, "Gen.Combat.RemoveTimedStates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenRemoveTimedStatesTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Dummy = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Dummy ? Dummy->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	ApplyClass(ASC, UCurffeGE_HearthSetup::StaticClass());
	ApplyTimedState(ASC, 5.f, GenGameplayTags::State_Countering);
	ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 5.f, nullptr);

	ASC->RemoveTimedStates();
	TestEqual(TEXT("contre retiré"), ASC->GetTagCount(GenGameplayTags::State_Countering), 0);
	TestEqual(TEXT("étourdissement retiré"), ASC->GetTagCount(GenGameplayTags::State_Stunned), 0);
	TestEqual(TEXT("vitesse rendue"), Get(ASC, UGenAttributeSet::GetMoveSpeedAttribute()), 550.f);
	TestEqual(TEXT("le Foyer (effet infini) reste"), Get(ASC, UGenAttributeSet::GetMaxResourceAttribute()), 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenCounterResolveTest, "Gen.Combat.CounterResolve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenCounterResolveTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Defender = TestWorld.SpawnDummy();
	AGenTrainingDummy* Attacker = TestWorld.SpawnDummy();
	UAbilitySystemComponent* ASC = Defender ? Defender->GetAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du défenseur"), ASC) || !TestNotNull(TEXT("attaquant"), Attacker))
	{
		return false;
	}

	int32 Received = 0;
	const AActor* ReceivedInstigator = nullptr;
	float ReceivedKind = -1.f;
	ASC->GenericGameplayEventCallbacks.FindOrAdd(GenGameplayTags::Event_Counter_Blocked).AddLambda([&](const FGameplayEventData* Payload)
	{
		++Received;
		ReceivedInstigator = Payload->Instigator;
		ReceivedKind = Payload->EventMagnitude;
	});

	TestTrue(TEXT("sans contre : touché"), Defender->ResolveIncomingHit(Attacker, EGenHitKind::Projectile, nullptr) == EGenHitResponse::Hit);

	ASC->AddLooseGameplayTag(GenGameplayTags::State_Countering);
	TestTrue(TEXT("projectile bloqué"), Defender->ResolveIncomingHit(Attacker, EGenHitKind::Projectile, nullptr) == EGenHitResponse::Countered);
	TestTrue(TEXT("zone : traverse le contre"), Defender->ResolveIncomingHit(Attacker, EGenHitKind::Area, nullptr) == EGenHitResponse::Hit);
	TestTrue(TEXT("mêlée bloquée"), Defender->ResolveIncomingHit(Attacker, EGenHitKind::Melee, nullptr) == EGenHitResponse::Countered);

	TestEqual(TEXT("le contre est prévenu de chaque blocage"), Received, 2);
	TestTrue(TEXT("instigateur transmis"), ReceivedInstigator == static_cast<const AActor*>(Attacker));
	TestEqual(TEXT("nature du dernier coup bloqué"), ReceivedKind, GenHitRules::ToEventMagnitude(EGenHitKind::Melee));

	// Seul un ennemi déclenche le contre ; un attaquant détruit (nul) compte comme ennemi
	TestTrue(TEXT("son propre coup (non ennemi) : pas de contre"), Defender->ResolveIncomingHit(Defender, EGenHitKind::Projectile, nullptr) == EGenHitResponse::Hit);
	TestTrue(TEXT("attaquant nul : bloqué"), Defender->ResolveIncomingHit(nullptr, EGenHitKind::Projectile, nullptr) == EGenHitResponse::Countered);
	TestEqual(TEXT("aucun événement pour le coup non ennemi"), Received, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFedDisplayOwnerRemovedTest, "Gen.Feeding.FedDisplayOwnerRemoved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFedDisplayOwnerRemovedTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Dummy = TestWorld.SpawnDummy();
	UAbilitySystemComponent* ASC = Dummy ? Dummy->GetAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	// Un sort instancié par acteur possède l'affichage, puis il est retiré sans l'avoir effacé
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(FGameplayAbilitySpec(UGenGA_Projectile::StaticClass()));
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
	if (!TestNotNull(TEXT("instance du sort"), Instance))
	{
		return false;
	}

	Dummy->SetFedResource(Instance, 3);
	TestEqual(TEXT("3 affichées"), Dummy->GetFedResource(), 3);
	Dummy->ClearFedResourceFrom(Dummy);
	TestEqual(TEXT("un autre objet n'efface rien"), Dummy->GetFedResource(), 3);

	ASC->ClearAbility(Handle);
	TestEqual(TEXT("sort retiré : affichage effacé"), Dummy->GetFedResource(), 0);

	Dummy->SetFedResource(Dummy, 2);
	Dummy->ResetFedResource();
	TestEqual(TEXT("remise à zéro (mort)"), Dummy->GetFedResource(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenStatusVisualTest, "Gen.Status.VisualFollowsTag",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenStatusVisualTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Dummy = TestWorld.SpawnDummy();
	UGenAbilitySystemComponent* ASC = Dummy ? Dummy->GetGenAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ASC du mannequin"), ASC))
	{
		return false;
	}

	UGenStatusVisualsComponent* Visuals = NewObject<UGenStatusVisualsComponent>(Dummy);
	Visuals->SetupAttachment(Dummy->GetRootComponent());
	Visuals->RegisterComponent();

	FGenStatusVisual Stunned;
	Stunned.Tag = GenGameplayTags::State_Stunned;
	Stunned.AppearFlashDuration = 0.2f;
	Visuals->Bind(ASC, { Stunned });

	TestFalse(TEXT("caché au départ"), Visuals->IsStatusShown(GenGameplayTags::State_Stunned));
	ASC->ApplyHardCC(GenGameplayTags::State_Stunned, 1.f, nullptr);
	TestTrue(TEXT("affiché pendant l'étourdissement"), Visuals->IsStatusShown(GenGameplayTags::State_Stunned));
	TestTrue(TEXT("flash d'apparition"), Visuals->IsStatusFlashing(GenGameplayTags::State_Stunned));

	TestWorld.Advance(0.3f);
	TestFalse(TEXT("flash terminé"), Visuals->IsStatusFlashing(GenGameplayTags::State_Stunned));
	TestTrue(TEXT("forme toujours là après le flash"), Visuals->IsStatusShown(GenGameplayTags::State_Stunned));

	TestWorld.Advance(0.8f);
	TestFalse(TEXT("caché à la fin"), Visuals->IsStatusShown(GenGameplayTags::State_Stunned));

	// Plan Visuals V9 : un état avec seulement un système Niagara (ni forme ni matériau) suit aussi son tag
	FGenStatusVisual Countering;
	Countering.Tag = GenGameplayTags::State_Countering;
	Countering.System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Gen/Champions/Curffe/VFX/NS_Curffe_AblazeBody.NS_Curffe_AblazeBody"));
	TestNotNull(TEXT("système de test"), Countering.System.Get());
	Visuals->Bind(ASC, { Stunned, Countering });
	TestFalse(TEXT("système : caché au départ"), Visuals->IsStatusShown(GenGameplayTags::State_Countering) || Visuals->IsStatusSystemActive(GenGameplayTags::State_Countering));
	ASC->AddLooseGameplayTag(GenGameplayTags::State_Countering);
	TestTrue(TEXT("système : affiché avec le tag"), Visuals->IsStatusShown(GenGameplayTags::State_Countering));
	TestTrue(TEXT("système : lancé avec le tag"), Visuals->IsStatusSystemActive(GenGameplayTags::State_Countering));
	TestFalse(TEXT("l'autre état reste caché"), Visuals->IsStatusShown(GenGameplayTags::State_Stunned));
	ASC->RemoveLooseGameplayTag(GenGameplayTags::State_Countering);
	TestFalse(TEXT("système : caché sans le tag"), Visuals->IsStatusShown(GenGameplayTags::State_Countering));
	TestFalse(TEXT("système : désactivé sans le tag"), Visuals->IsStatusSystemActive(GenGameplayTags::State_Countering));

	Visuals->Unbind();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenFedResourceEventTest, "Gen.Visuals.FedResourceEvent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGenFedResourceEventTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	AGenTrainingDummy* Dummy = TestWorld.SpawnDummy();
	if (!TestNotNull(TEXT("mannequin"), Dummy))
	{
		return false;
	}

	TArray<TPair<int32, int32>> Calls;
	TArray<int32> Pops;
	Dummy->OnFedResourceChanged.AddLambda([&Calls](AGenCharacterBase*, int32 Old, int32 New) { Calls.Emplace(Old, New); });
	Dummy->OnFedThresholdReached.AddLambda([&Pops](AGenCharacterBase*, int32 New) { Pops.Add(New); });

	// Le monde de test n'est pas un serveur dédié : les événements partent (sur un serveur dédié jamais, Gen.Net.FeedThreshold)
	const UObject* Source = Dummy; // n'importe quelle source stable
	Dummy->SetFedResource(Source, 1);
	Dummy->SetFedResource(Source, 1); // inchangé : pas d'appel
	Dummy->SetFedResource(Source, 3);
	Dummy->SetFedResource(Source, 0);
	Dummy->SetFedResource(Source, 2);
	Dummy->ResetFedResource(); // mort : retour à 0 sans pop

	TestEqual(TEXT("5 changements"), Calls.Num(), 5);
	TestTrue(TEXT("0 -> 1"), Calls.IsValidIndex(0) && Calls[0] == TPair<int32, int32>(0, 1));
	TestTrue(TEXT("1 -> 3"), Calls.IsValidIndex(1) && Calls[1] == TPair<int32, int32>(1, 3));
	TestTrue(TEXT("3 -> 0"), Calls.IsValidIndex(2) && Calls[2] == TPair<int32, int32>(3, 0));
	TestTrue(TEXT("0 -> 2"), Calls.IsValidIndex(3) && Calls[3] == TPair<int32, int32>(0, 2));
	TestTrue(TEXT("2 -> 0 (remise à zéro)"), Calls.IsValidIndex(4) && Calls[4] == TPair<int32, int32>(2, 0));

	// Pop seulement quand le compte augmente : jamais au lancer (3 -> 0) ni à la remise à zéro
	TestEqual(TEXT("3 pops"), Pops.Num(), 3);
	TestTrue(TEXT("pops 1, 3, 2"), Pops == TArray<int32>({ 1, 3, 2 }));
	return true;
}

#endif
