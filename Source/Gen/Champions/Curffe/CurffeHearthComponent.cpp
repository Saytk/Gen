#include "Champions/Curffe/CurffeHearthComponent.h"

#include "AbilitySystemComponent.h"
#include "Champions/Curffe/CurffeGameplayTags.h"
#include "Champions/Curffe/CurffeTuning.h"
#include "Character/GenCharacterBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GenGameplayTags.h"
#include "UObject/ConstructorHelpers.h"

UCurffeHearthComponent::UCurffeHearthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// L'orbite ne tourne pas avec le personnage quand il se retourne
	SetUsingAbsoluteRotation(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	FlameMesh = SphereMesh.Object;
}

void UCurffeHearthComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}

	for (int32 Index = 0; Index < CurffeTuning::MaxFlames; ++Index)
	{
		UStaticMeshComponent* Flame = NewObject<UStaticMeshComponent>(GetOwner());
		Flame->SetStaticMesh(FlameMesh);
		if (FlameMaterial)
		{
			Flame->SetMaterial(0, FlameMaterial);
		}
		Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Flame->SetCastShadow(false);
		Flame->SetupAttachment(this);
		Flame->SetRelativeScale3D(FVector(FlameScale));
		Flame->SetVisibility(false);
		Flame->RegisterComponent();
		FlameComponents.Add(Flame);
	}
}

void UCurffeHearthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AGenCharacterBase* Character = Cast<AGenCharacterBase>(GetOwner());
	int32 Flames = (Character && !Character->IsDead())
		? FMath::Clamp(FMath::FloorToInt32(Character->GetResource()) - Character->GetFedResource(), 0, FlameComponents.Num())
		: 0;

	// Revue P3 T8-10, I2 : la flamme vivante remplit le Foyer dès son départ (prédit), mais les flammes ne reviennent
	// qu'à la fin de la forme (Curffe-Visuals §3.6, convergence) : pendant State.Curffe.LivingFlame, pas de hausse.
	// Le propriétaire garde le tag ~1 RTT après SA fin de forme (revue P3 T8-10, M1) : chez lui, la forme finit avec
	// son verrou de lancement local (State.CastLocked), pour que les flammes reviennent quand il peut les nourrir
	const UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
	const bool bFormShown = ASC && ASC->HasMatchingGameplayTag(CurffeGameplayTags::State_LivingFlame)
		&& (!Character->IsLocallyControlled() || ASC->HasMatchingGameplayTag(GenGameplayTags::State_CastLocked));
	if (bFormShown)
	{
		Flames = FMath::Min(Flames, VisibleFlames);
	}
	VisibleFlames = Flames;

	OrbitAngle = FMath::Fmod(OrbitAngle + OrbitSpeedDegrees * DeltaTime, 360.f);

	for (int32 Index = 0; Index < FlameComponents.Num(); ++Index)
	{
		UStaticMeshComponent* Flame = FlameComponents[Index];
		const bool bFlameVisible = Index < VisibleFlames;
		Flame->SetVisibility(bFlameVisible);
		if (bFlameVisible)
		{
			const float Angle = FMath::DegreesToRadians(OrbitAngle + 360.f * Index / VisibleFlames);
			Flame->SetRelativeLocation(FVector(FMath::Cos(Angle) * OrbitRadius, FMath::Sin(Angle) * OrbitRadius, OrbitHeight));
		}
	}
}
