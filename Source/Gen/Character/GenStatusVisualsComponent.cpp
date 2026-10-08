#include "Character/GenStatusVisualsComponent.h"

#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

UGenStatusVisualsComponent::UGenStatusVisualsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGenStatusVisualsComponent::Bind(UAbilitySystemComponent* InASC, const TArray<FGenStatusVisual>& InVisuals)
{
	Unbind();

	if (!InASC || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	BoundASC = InASC;
	Visuals = InVisuals;

	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		const FGenStatusVisual& Visual = Visuals[Index];

		UStaticMeshComponent* Shape = nullptr;
		UMaterialInstanceDynamic* MID = nullptr;
		if (Visual.Mesh)
		{
			Shape = NewObject<UStaticMeshComponent>(GetOwner());
			Shape->SetStaticMesh(Visual.Mesh);
			Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Shape->SetCastShadow(false);
			Shape->SetupAttachment(this);
			Shape->SetRelativeLocation(Visual.Offset);
			Shape->SetRelativeScale3D(Visual.Scale);
			Shape->SetVisibility(false);
			Shape->RegisterComponent();

			if (Visual.Material)
			{
				MID = Shape->CreateDynamicMaterialInstance(0, Visual.Material);
				MID->SetVectorParameterValue(TEXT("Colour"), Visual.Colour);
				MID->SetScalarParameterValue(TEXT("Opacity"), Visual.Opacity);
				MID->SetScalarParameterValue(TEXT("RimOnly"), Visual.bRimOnly ? 1.f : 0.f);
				MID->SetScalarParameterValue(TEXT("RimPower"), Visual.RimPower);
				MID->SetScalarParameterValue(TEXT("Flash"), 0.f);
			}
		}

		Shapes.Add(Shape);
		ShapeMIDs.Add(MID);
		Shown.Add(false);
		Flashing.Add(false);
		FlashTimers.AddDefaulted();
		TagHandles.Add(Visual.Tag.IsValid()
			? InASC->RegisterGameplayTagEvent(Visual.Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::OnTagChanged)
			: FDelegateHandle());

		SetShown(Index, Visual.Tag.IsValid() && InASC->GetTagCount(Visual.Tag) > 0);
	}

	RefreshOwnerMesh();
}

void UGenStatusVisualsComponent::Unbind()
{
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		for (int32 Index = 0; Index < TagHandles.Num(); ++Index)
		{
			if (TagHandles[Index].IsValid())
			{
				ASC->RegisterGameplayTagEvent(Visuals[Index].Tag, EGameplayTagEventType::NewOrRemoved).Remove(TagHandles[Index]);
			}
		}
	}

	if (UWorld* World = GetWorld())
	{
		for (FTimerHandle& Timer : FlashTimers)
		{
			World->GetTimerManager().ClearTimer(Timer);
		}
	}

	for (UStaticMeshComponent* Shape : Shapes)
	{
		if (Shape)
		{
			Shape->DestroyComponent();
		}
	}

	Shapes.Reset();
	ShapeMIDs.Reset();
	Shown.Reset();
	Flashing.Reset();
	FlashTimers.Reset();
	TagHandles.Reset();
	Visuals.Reset();
	BoundASC.Reset();

	// Plus aucun état : le corps retrouve ses matériaux
	RefreshOwnerMesh();
}

void UGenStatusVisualsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Unbind();
	Super::EndPlay(EndPlayReason);
}

bool UGenStatusVisualsComponent::IsStatusShown(FGameplayTag Tag) const
{
	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		if (Visuals[Index].Tag == Tag && Shown[Index])
		{
			return true;
		}
	}
	return false;
}

bool UGenStatusVisualsComponent::IsStatusFlashing(FGameplayTag Tag) const
{
	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		if (Visuals[Index].Tag == Tag && Flashing[Index])
		{
			return true;
		}
	}
	return false;
}

void UGenStatusVisualsComponent::OnTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		if (Visuals[Index].Tag == Tag)
		{
			SetShown(Index, NewCount > 0);
		}
	}
	RefreshOwnerMesh();
}

void UGenStatusVisualsComponent::SetShown(int32 Index, bool bShown)
{
	const bool bWasShown = Shown[Index];
	Shown[Index] = bShown;
	if (Shapes[Index])
	{
		Shapes[Index]->SetVisibility(bShown);
	}

	UWorld* World = GetWorld();
	if (bShown && !bWasShown && Visuals[Index].AppearFlashDuration > 0.f)
	{
		// Flash d'apparition : seulement sur le front montant, puis la forme reste à son opacité normale
		SetFlash(Index, true);
		if (World)
		{
			World->GetTimerManager().SetTimer(FlashTimers[Index], FTimerDelegate::CreateWeakLambda(this, [this, Index]()
			{
				if (Flashing.IsValidIndex(Index))
				{
					SetFlash(Index, false);
				}
			}), Visuals[Index].AppearFlashDuration, false);
		}
	}
	else if (!bShown)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(FlashTimers[Index]);
		}
		SetFlash(Index, false);
	}
}

void UGenStatusVisualsComponent::SetFlash(int32 Index, bool bFlash)
{
	Flashing[Index] = bFlash;
	if (ShapeMIDs[Index])
	{
		ShapeMIDs[Index]->SetScalarParameterValue(TEXT("Flash"), bFlash ? 1.f : 0.f);
	}
}

void UGenStatusVisualsComponent::RefreshOwnerMesh()
{
	// Le dernier état affiché qui impose un matériau gagne (en pratique un seul : intouchable)
	UMaterialInterface* Override = nullptr;
	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		if (Shown[Index] && Visuals[Index].OwnerMeshMaterial)
		{
			Override = Visuals[Index].OwnerMeshMaterial;
		}
	}

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh || Override == AppliedOwnerMaterial)
	{
		return;
	}

	// Première substitution : on garde les matériaux d'origine pour les rendre à la fin de l'état
	if (!AppliedOwnerMaterial)
	{
		OriginalOwnerMaterials.Reset();
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			OriginalOwnerMaterials.Add(Mesh->GetMaterial(Slot));
		}
	}

	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Material = Override ? Override : (OriginalOwnerMaterials.IsValidIndex(Slot) ? OriginalOwnerMaterials[Slot].Get() : nullptr);
		Mesh->SetMaterial(Slot, Material);
	}

	AppliedOwnerMaterial = Override;
	if (!Override)
	{
		OriginalOwnerMaterials.Reset();
	}
}
