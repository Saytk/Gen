#include "Character/GenStatusVisualsComponent.h"

#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
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
		Systems.Add(nullptr);
		SystemWanted.Add(false);
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
	for (int32 Index = 0; Index < Systems.Num(); ++Index)
	{
		SetSystemActive(Index, false);
	}

	Shapes.Reset();
	ShapeMIDs.Reset();
	Systems.Reset();
	SystemWanted.Reset();
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

bool UGenStatusVisualsComponent::IsStatusSystemActive(FGameplayTag Tag) const
{
	for (int32 Index = 0; Index < Visuals.Num(); ++Index)
	{
		if (Visuals[Index].Tag == Tag && SystemWanted.IsValidIndex(Index) && SystemWanted[Index])
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
	if (bShown != bWasShown)
	{
		SetSystemActive(Index, bShown);
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

void UGenStatusVisualsComponent::SetSystemActive(int32 Index, bool bActive)
{
	if (!Systems.IsValidIndex(Index))
	{
		return;
	}

	SystemWanted[Index] = bActive && Visuals[Index].System != nullptr;
	if (!bActive)
	{
		// Désactivé, pas détruit : les particules finissent leur vie, puis le composant retourne au pool. Revue V6-V8, I-5 :
		// seulement un composant qu'on tient encore (un système fini a déjà vidé son emplacement, OnStatusSystemFinished)
		if (UNiagaraComponent* System = Systems[Index])
		{
			System->OnSystemFinished.RemoveDynamic(this, &ThisClass::OnStatusSystemFinished);
			if (System->PoolingMethod == ENCPoolMethod::ManualRelease)
			{
				System->ReleaseToPool(); // désactive, puis rend au pool à la fin des particules
			}
			else
			{
				// Pool désactivé (FX.NiagaraComponentPool.Enable 0) : composant à nous, détruit à la fin des particules
				System->SetAutoDestroy(true);
				System->Deactivate();
			}
		}
		Systems[Index] = nullptr;
		return;
	}

	UNiagaraSystem* Template = Visuals[Index].System;
	if (!Template || Systems[Index])
	{
		return;
	}

	// Attaché au corps (socket) s'il y en a un, sinon au composant (centre du personnage). Pool : Art Bible §7.8
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USceneComponent* AttachTo = Character && Character->GetMesh() ? static_cast<USceneComponent*>(Character->GetMesh()) : this;
	// Pas de pré-élimination au lancement : un état qui commence hors de l'écran doit s'afficher quand le personnage y
	// revient (l'Effect Type gère l'élimination en cours de vie)
	// Revue V6-V8, I-5 : ManualRelease, jamais AutoRelease (le pool reprendrait le composant à la fin du système, puis le
	// prêterait à un autre effet que SetSystemActive(false) désactiverait). Contrat des assets : les Effect Types des
	// systèmes d'état bouclés utilisent une réaction d'élimination « Resume » (pas Kill)
	UNiagaraComponent* System = UNiagaraFunctionLibrary::SpawnSystemAttached(Template, AttachTo, Visuals[Index].Socket, FVector::ZeroVector, FRotator::ZeroRotator,
		FVector::OneVector, EAttachLocation::SnapToTarget, /*bAutoDestroy*/ false, ENCPoolMethod::ManualRelease, /*bAutoActivate*/ true, /*bPreCullCheck*/ false);
	Systems[Index] = System;
	if (System)
	{
		System->OnSystemFinished.AddUniqueDynamic(this, &ThisClass::OnStatusSystemFinished);
	}
}

void UGenStatusVisualsComponent::OnStatusSystemFinished(UNiagaraComponent* System)
{
	const int32 Index = System ? Systems.IndexOfByKey(System) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return;
	}

	// Fini de lui-même : plus le nôtre. Rendu au pool par le moteur juste après cette diffusion (comme ReleaseToPool sur un
	// système encore actif : ManualRelease_OnComplete), ou détruit si le pool est désactivé
	System->OnSystemFinished.RemoveDynamic(this, &ThisClass::OnStatusSystemFinished);
	Systems[Index] = nullptr;
	if (System->PoolingMethod == ENCPoolMethod::ManualRelease)
	{
		System->PoolingMethod = ENCPoolMethod::ManualRelease_OnComplete;
	}
	else
	{
		System->SetAutoDestroy(true);
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
