#include "Character/GenSpellIndicatorComponent.h"

#include "AbilitySystem/Abilities/GenGA_Leap.h"
#include "AbilitySystem/Abilities/GenGameplayAbility.h"
#include "Character/GenCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"
#include "UObject/ConstructorHelpers.h"

// Nommé (pas anonyme) : les builds unity regroupent plusieurs .cpp dans une même unité de traduction
namespace GenSpellIndicatorPrivate
{
	/** Le plan /Engine/BasicShapes/Plane mesure 100 cm : échelle = demi-taille / 50. */
	constexpr float PlaneHalfSize = 50.f;

	// Paramètres de M_VFX_Telegraph (Plan 2 Task 10, Task E2). Shape et BorderAlpha viennent de l'instance.
	const FName ParamSizeX(TEXT("SizeX"));
	const FName ParamSizeY(TEXT("SizeY"));
	const FName ParamRelationIndex(TEXT("RelationIndex"));
	const FName ParamFill(TEXT("Fill"));
	const FName ParamFillAlpha(TEXT("FillAlpha"));
	const FName ParamEnemyPattern(TEXT("EnemyPattern"));

	uint32 SlotBit(EGenIndicatorSlot Slot)
	{
		return 1u << static_cast<uint32>(Slot);
	}

	EGenIndicatorSlot StubSlot(int32 Index)
	{
		return static_cast<EGenIndicatorSlot>(static_cast<int32>(EGenIndicatorSlot::Stub0) + Index);
	}

	constexpr int32 MaxStubs = static_cast<int32>(EGenIndicatorSlot::Stub3) - static_cast<int32>(EGenIndicatorSlot::Stub0) + 1;

	/** Canalisation (Task V4) : fenêtre minutée, la barre se vide (contre, forme de Living Flame). */
	bool IsChannelCast(const FGenCastInfo& Info)
	{
		return Info.bChannel;
	}

	/** Temps écoulé / durée (Task V4) : grandit aussi pendant une canalisation, dont la barre se vide. */
	float GetTelegraphProgress(const AGenCharacterBase& Caster)
	{
		return Caster.GetCastElapsedFraction();
	}
}

using namespace GenSpellIndicatorPrivate;

UGenSpellIndicatorComponent::UGenSpellIndicatorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Après le mouvement de l'image : l'indicateur suit le personnage sans une image de retard
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	PlaneMesh = Plane.Object;
}

void UGenSpellIndicatorComponent::BeginPlay()
{
	Super::BeginPlay();

	// Purement cosmétique : rien sur un serveur dédié (GetNetMode, valable aussi en PIE, Art Bible §8.5)
	bDisabled = GetNetMode() == NM_DedicatedServer;
	SetComponentTickEnabled(!bDisabled);
}

void UGenSpellIndicatorComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// L'acteur détruit emporte les plans ; seul le composant retiré seul doit les détruire lui-même
	if (!bDestroyingHierarchy)
	{
		for (FGenIndicatorPart& Part : Parts)
		{
			if (IsValid(Part.Mesh) && !Part.Mesh->IsBeingDestroyed())
			{
				Part.Mesh->DestroyComponent();
			}
		}
	}
	Parts.Reset();
	VisibleParts = 0;
	AimAbility.Reset();

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UGenSpellIndicatorComponent::BeginAim(const UGenGameplayAbility* Ability)
{
	// La visée ne part jamais chez les autres joueurs (Review Focus 5) : seul le client qui contrôle le pion la dessine
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!bDisabled && Ability && Pawn && Pawn->IsLocallyControlled())
	{
		AimAbility = Ability;
	}
}

void UGenSpellIndicatorComponent::EndAim(const UGenGameplayAbility* Ability)
{
	if (!Ability || AimAbility.Get() == Ability)
	{
		AimAbility.Reset();
	}
}

void UGenSpellIndicatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ShownThisFrame = 0;
	if (const AGenCharacterBase* Caster = Cast<AGenCharacterBase>(GetOwner()); Caster && !Caster->IsDead())
	{
		if (AimAbility.IsValid())
		{
			DrawAim(*Caster);
		}
		DrawSelfTelegraph(*Caster);
		DrawLeapTarget(*Caster);
	}
	HideUnshownParts();
}

void UGenSpellIndicatorComponent::DrawAim(const AGenCharacterBase& Caster)
{
	const UGenGameplayAbility* Ability = AimAbility.Get();
	const AGenPlayerController* PC = Cast<AGenPlayerController>(Caster.GetController());
	FVector Cursor;
	if (!Ability || !PC || !PC->GetCursorLocationOnPlane(Caster.GetActorLocation().Z, Cursor))
	{
		return;
	}

	// Le sort calcule sa géométrie depuis SES valeurs de jeu : le dessin ne peut pas dériver de la hitbox
	FGenAimGeometry G;
	if (!Ability->GetAimGeometry(Caster, Caster.GetFedResource(), Cursor, G))
	{
		return;
	}

	const float Z = GetFloorZ(Caster);
	const float Yaw = G.Direction.Rotation().Yaw;
	auto OnFloor = [Z](const FVector& Point) { return FVector(Point.X, Point.Y, Z); };
	constexpr EGenViewerRelation Self = EGenViewerRelation::Self;

	if (G.RangeArcRadius > 0.f)
	{
		ShowPart(EGenIndicatorSlot::Arc, ArcMaterial, OnFloor(G.Origin), Yaw, G.RangeArcRadius, G.RangeArcRadius, Self, 1.f, G.RangeArcRadius, true);
	}
	if (G.LineLength > 0.f && G.LineWidth > 0.f)
	{
		const FVector Centre = G.LineStart + G.Direction * (G.LineLength * 0.5f);
		ShowPart(EGenIndicatorSlot::Lane, LaneMaterial, OnFloor(Centre), Yaw, G.LineLength * 0.5f, G.LineWidth * 0.5f, Self, 1.f, G.LineWidth, false, G.LineLength);
	}
	if (G.CapRadius > 0.f)
	{
		ShowPart(EGenIndicatorSlot::Cap, DiscMaterial, OnFloor(G.CapCenter), 0.f, G.CapRadius, G.CapRadius, Self, 1.f, G.CapRadius, true);
		if (G.bCapSpokes)
		{
			ShowPart(EGenIndicatorSlot::Spokes, SpokesMaterial, OnFloor(G.CapCenter), 0.f, G.CapRadius, G.CapRadius, Self, 1.f, G.CapRadius, true);
		}
	}
	ShowTargetAndStubs(G, Z, Self, 1.f);
}

void UGenSpellIndicatorComponent::ShowTargetAndStubs(const FGenAimGeometry& G, float Z, EGenViewerRelation Relation, float Fill)
{
	auto OnFloor = [Z](const FVector& Point) { return FVector(Point.X, Point.Y, Z); };

	if (G.TargetRadius > 0.f)
	{
		ShowPart(EGenIndicatorSlot::Target, DiscMaterial, OnFloor(G.TargetCenter), 0.f, G.TargetRadius, G.TargetRadius, Relation, Fill, G.TargetRadius, true);
	}

	const int32 StubCount = FMath::Min(G.StubDirections.Num(), MaxStubs);
	for (int32 Index = 0; Index < StubCount; ++Index)
	{
		// L'amorce part du bord du cercle d'atterrissage, dans la direction exacte du projectile de l'anneau
		const FVector& Dir = G.StubDirections[Index];
		const FVector Start = G.TargetCenter + Dir * G.TargetRadius;
		ShowPart(StubSlot(Index), StubMaterial, OnFloor(Start + Dir * (G.StubLength * 0.5f)), Dir.Rotation().Yaw,
			G.StubLength * 0.5f, G.StubWidth * 0.5f, Relation, 1.f, G.StubWidth, false, G.StubLength);
	}
}

void UGenSpellIndicatorComponent::DrawLeapTarget(const AGenCharacterBase& Caster)
{
	// Décision du 2026-10-08 : tous les clients voient le cercle d'atterrissage pendant le vol (le propriétaire le pose
	// lui-même, les autres le reçoivent), avec une amorce par boule de l'anneau à venir
	const FGenLeapTarget& Target = Caster.GetLeapTarget();
	if (!Target.IsActive())
	{
		return;
	}

	const UGenGA_Leap* Leap = Cast<UGenGA_Leap>(Target.Ability->GetDefaultObject());
	if (!Leap)
	{
		return;
	}

	FGenAimGeometry G;
	Leap->GetFlightGeometry(Target, G);

	// Remplissage du centre vers le bord sur la durée du vol (horloge serveur, comme les télégraphes)
	const float Duration = Leap->GetLeapDuration();
	const float Fill = Duration > 0.f ? FMath::Clamp((Caster.GetCastClockSeconds() - Target.StartTime) / Duration, 0.f, 1.f) : 1.f;
	ShowTargetAndStubs(G, GetFloorZ(Caster, Target.Location.Z), GetLocalRelation(Caster), Fill);
}

#if WITH_DEV_AUTOMATION_TESTS
void UGenSpellIndicatorComponent::SetAllMaterialsForTests(UMaterialInterface* Material)
{
	DiscMaterial = Material;
	LaneMaterial = Material;
	ArcMaterial = Material;
	StubMaterial = Material;
	SpokesMaterial = Material;
}
#endif

void UGenSpellIndicatorComponent::DrawSelfTelegraph(const AGenCharacterBase& Caster)
{
	const FGenCastInfo& Info = Caster.GetCastInfo();
	if (!Info.IsCasting())
	{
		return;
	}

	// Une seule lecture du CDO par image ; le rayon vient des valeurs de jeu du sort (NovaRadius, BurstRadius)
	const UGenGameplayAbility* CDO = Cast<UGenGameplayAbility>(Info.Ability->GetDefaultObject());
	const float Radius = CDO ? CDO->GetSelfTelegraphRadius(IsChannelCast(Info)) : 0.f;
	if (Radius <= 0.f)
	{
		return;
	}

	// Remplissage = temps écoulé / durée (temps serveur), qui grandit même pour une canalisation (dont la barre se vide)
	const FVector Location = Caster.GetActorLocation();
	ShowPart(EGenIndicatorSlot::Self, DiscMaterial, FVector(Location.X, Location.Y, GetFloorZ(Caster)), 0.f, Radius, Radius,
		GetLocalRelation(Caster), GetTelegraphProgress(Caster), Radius, true);
}

void UGenSpellIndicatorComponent::ShowPart(EGenIndicatorSlot Slot, UMaterialInterface* Material, const FVector& Centre, float Yaw, float HalfX, float HalfY,
	EGenViewerRelation Relation, float Fill, float ShownSize, bool bUnitSize, float ShownLength)
{
	// Sans matériau (assets pas encore créés, ou forme refusée : ⚑ F8), la partie n'est jamais affichée
	if (!Material || !PlaneMesh || HalfX <= 0.f || HalfY <= 0.f)
	{
		return;
	}

	const int32 Index = static_cast<int32>(Slot);
	if (Parts.Num() < static_cast<int32>(EGenIndicatorSlot::Count))
	{
		Parts.SetNum(static_cast<int32>(EGenIndicatorSlot::Count));
	}
	FGenIndicatorPart& Part = Parts[Index];

	// Réservé une fois : aucune allocation ensuite
	if (!Part.Mesh)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
		Mesh->SetStaticMesh(PlaneMesh);
		Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->CastShadow = false;
		Mesh->bReceivesDecals = false;
		Mesh->bVisibleInReflectionCaptures = false;
		Mesh->bVisibleInRealTimeSkyCaptures = false;
		Mesh->bVisibleInRayTracing = false;
		Mesh->SetUsingAbsoluteLocation(true);
		Mesh->SetUsingAbsoluteRotation(true);
		Mesh->SetUsingAbsoluteScale(true);
		Mesh->SetupAttachment(this);
		Mesh->RegisterComponent();

		Part.Mesh = Mesh;
		Part.MID = UMaterialInstanceDynamic::Create(Material, this);
		Part.MID->GetScalarParameterValue(ParamFillAlpha, Part.DefaultFillAlpha);
		Mesh->SetMaterial(0, Part.MID);
	}

	// Transformée : seulement si elle a changé (une visée immobile ne touche pas au rendu)
	const FTransform Wanted(FRotator(0.f, Yaw, 0.f), Centre, FVector(HalfX / PlaneHalfSize, HalfY / PlaneHalfSize, 1.f));
	if (!Part.Mesh->GetComponentTransform().Equals(Wanted, 0.01))
	{
		Part.Mesh->SetWorldTransform(Wanted);
	}

	// Art Bible §7.5 : ennemi au-dessus de tout, sa propre visée au-dessus des alliés
	const int32 Priority = GenIndicatorRules::GetSortPriority(Relation);
	if (Part.Mesh->TranslucencySortPriority != Priority)
	{
		Part.Mesh->SetTranslucentSortPriority(Priority);
	}

	// Paramètres du MID : seulement ceux qui ont changé. Les tailles sautent aux seuils, jamais interpolées.
	auto SetScalar = [&Part](float& Cached, const FName& Name, float Value)
	{
		if (Cached != Value)
		{
			Cached = Value;
			Part.MID->SetScalarParameterValue(Name, Value);
		}
	};
	SetScalar(Part.SizeX, ParamSizeX, bUnitSize ? 1.f : HalfX);
	SetScalar(Part.SizeY, ParamSizeY, bUnitSize ? 1.f : HalfY);
	SetScalar(Part.Fill, ParamFill, Fill);
	const float RelationIndex = static_cast<float>(static_cast<uint8>(Relation));
	if (Part.RelationIndex != RelationIndex)
	{
		Part.RelationIndex = RelationIndex;
		const bool bEnemy = Relation == EGenViewerRelation::Enemy;
		Part.MID->SetScalarParameterValue(ParamRelationIndex, RelationIndex);
		Part.MID->SetScalarParameterValue(ParamEnemyPattern, bEnemy ? 1.f : 0.f);
		Part.MID->SetScalarParameterValue(ParamFillAlpha, bEnemy ? EnemyFillAlpha : Part.DefaultFillAlpha);
	}

	if (!Part.Mesh->IsVisible())
	{
		Part.Mesh->SetVisibility(true);
	}
	Part.ShownSize = ShownSize;
	Part.ShownLength = ShownLength;
	ShownThisFrame |= SlotBit(Slot);
	VisibleParts |= SlotBit(Slot);
}

void UGenSpellIndicatorComponent::HideUnshownParts()
{
	const uint32 ToHide = VisibleParts & ~ShownThisFrame;
	if (ToHide == 0)
	{
		return;
	}

	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		if (ToHide & (1u << Index))
		{
			FGenIndicatorPart& Part = Parts[Index];
			if (Part.Mesh)
			{
				Part.Mesh->SetVisibility(false);
			}
			Part.ShownSize = -1.f;
			Part.ShownLength = -1.f;
		}
	}
	VisibleParts = ShownThisFrame;
}

float UGenSpellIndicatorComponent::GetShownSize(FName Part) const
{
	auto Shown = [this](EGenIndicatorSlot Slot, bool bLength = false)
	{
		const int32 Index = static_cast<int32>(Slot);
		if (!(VisibleParts & SlotBit(Slot)) || !Parts.IsValidIndex(Index))
		{
			return -1.f;
		}
		return bLength ? Parts[Index].ShownLength : Parts[Index].ShownSize;
	};

	static const FName NameLane(TEXT("Lane")), NameLaneLength(TEXT("LaneLength")), NameCap(TEXT("Cap")), NameSpokes(TEXT("Spokes")),
		NameTarget(TEXT("Target")), NameArc(TEXT("Arc")), NameSelf(TEXT("Self")), NameStubs(TEXT("Stubs"));

	if (Part == NameLane) { return Shown(EGenIndicatorSlot::Lane); }
	if (Part == NameLaneLength) { return Shown(EGenIndicatorSlot::Lane, true); }
	if (Part == NameCap) { return Shown(EGenIndicatorSlot::Cap); }
	if (Part == NameSpokes) { return Shown(EGenIndicatorSlot::Spokes); }
	if (Part == NameTarget) { return Shown(EGenIndicatorSlot::Target); }
	if (Part == NameArc) { return Shown(EGenIndicatorSlot::Arc); }
	if (Part == NameSelf) { return Shown(EGenIndicatorSlot::Self); }
	if (Part == NameStubs)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < MaxStubs; ++Index)
		{
			Count += (VisibleParts & SlotBit(StubSlot(Index))) ? 1 : 0;
		}
		return static_cast<float>(Count);
	}
	return -1.f;
}

float UGenSpellIndicatorComponent::GetFloorZ(const AGenCharacterBase& Caster) const
{
	return GetFloorZ(Caster, Caster.GetActorLocation().Z);
}

float UGenSpellIndicatorComponent::GetFloorZ(const AGenCharacterBase& Caster, float CapsuleCentreZ) const
{
	// Arènes plates (Art Bible §6.1) : le bas de la capsule est le sol. En vol, le centre de capsule du point visé
	// (au niveau du départ), pas la position courante en l'air
	const UCapsuleComponent* Capsule = Caster.GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
	return CapsuleCentreZ - HalfHeight + FloorOffset;
}

EGenViewerRelation UGenSpellIndicatorComponent::GetLocalRelation(const AGenCharacterBase& Caster) const
{
	// Le joueur local de cette machine (un seul par client ; le premier en écran partagé)
	const APlayerController* Viewer = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	const bool bViewerIsSource = Viewer && Viewer->GetPawn() == &Caster;
	// Équipe lue sur le PlayerState : reste connue quand le pion du joueur est mort
	const AGenPlayerState* ViewerState = Viewer ? Viewer->GetPlayerState<AGenPlayerState>() : nullptr;
	const uint8 ViewerTeam = ViewerState ? ViewerState->GetTeamId() : GenNoTeam;
	return GenAreaRules::GetViewerRelation(bViewerIsSource, ViewerTeam, Caster.GetTeamId(), GenNoTeam);
}
