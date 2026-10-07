#include "Champions/Curffe/CurffeHearthComponent.h"

#include "Champions/Curffe/CurffeTuning.h"
#include "Character/GenCharacterBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
	VisibleFlames = (Character && !Character->IsDead())
		? FMath::Clamp(FMath::FloorToInt32(Character->GetResource()) - Character->GetFedResource(), 0, FlameComponents.Num())
		: 0;

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
