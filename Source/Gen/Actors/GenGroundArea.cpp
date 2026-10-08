#include "Actors/GenGroundArea.h"

#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenHitRules.h"
#include "AbilitySystem/GenWorldQueries.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Character/GenCharacterBase.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GenGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "Player/GenPlayerController.h"
#include "Player/GenPlayerState.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenGroundArea, Log, All);

namespace
{
	/** Forme de la zone : capsule verticale (rayon = Radius) qui couvre du sol à ~2.5 m. */
	constexpr float ShapeCenterHeight = 100.f;
	constexpr float ShapeExtraHalfHeight = 150.f;
	/** Les lignes de vue partent un peu au-dessus du sol. */
	constexpr float LineOfSightHeight = 50.f;
	/** Le télégraphe flotte juste au-dessus du sol (pas de scintillement). */
	constexpr float TelegraphHeight = 2.f;
	/** Le plan /Engine/BasicShapes/Plane mesure 100 cm : échelle = rayon / 50. */
	constexpr float PlaneHalfSize = 50.f;
}

AGenGroundArea::AGenGroundArea()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	bReplicates = true;

	AreaRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AreaRoot"));
	SetRootComponent(AreaRoot);

	TelegraphMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TelegraphMesh"));
	TelegraphMesh->SetupAttachment(AreaRoot);
	TelegraphMesh->SetRelativeLocation(FVector(0.f, 0.f, TelegraphHeight));
	TelegraphMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TelegraphMesh->SetCastShadow(false);
	TelegraphMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	TelegraphMesh->SetStaticMesh(PlaneMesh.Object);
}

void AGenGroundArea::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AGenGroundArea, Radius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AGenGroundArea, Delay, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AGenGroundArea, StartServerTime, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AGenGroundArea, SourceTeam, COND_InitialOnly);
	DOREPLIFETIME(AGenGroundArea, bDetonated);
}

void AGenGroundArea::InitializeArea(const FGenAreaParams& Params, uint8 InSourceTeam)
{
	Radius = FMath::Max(Params.Radius, 1.f);
	Delay = FMath::Max(Params.Delay, 0.f);
	StunDuration = FMath::Max(Params.StunDuration, 0.f);
	KnockbackDistance = FMath::Max(Params.KnockbackDistance, 0.f);
	SourceTeam = InSourceTeam;

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	StartServerTime = GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
}

void AGenGroundArea::StartPreview(AGenPlayerController* InController, float InRange, float InBaseRadius, float InRadiusAtMaxFeed, int32 InMaxFeed)
{
	// Exemplaire local : jamais répliqué, même sur un hôte (listen server)
	bIsPreview = true;
	SetReplicates(false);
	PreviewController = InController;
	PreviewRange = InRange;
	PreviewBaseRadius = InBaseRadius;
	PreviewRadiusAtMaxFeed = InRadiusAtMaxFeed;
	PreviewMaxFeed = InMaxFeed;
	Radius = InBaseRadius;
}

void AGenGroundArea::BeginPlay()
{
	Super::BeginPlay();

	SetTelegraphRadius(Radius);

	const bool bShowsTelegraph = GetNetMode() != NM_DedicatedServer && !bDetonated && (bIsPreview || Delay > 0.f);
	if (bShowsTelegraph && TelegraphMaterial)
	{
		TelegraphMID = TelegraphMesh->CreateDynamicMaterialInstance(0, TelegraphMaterial);

		const EGenViewerRelation Relation = GetLocalViewerRelation();
		const bool bEnemy = Relation == EGenViewerRelation::Enemy;
		TelegraphMID->SetScalarParameterValue(TEXT("RelationIndex"), static_cast<float>(Relation));
		TelegraphMID->SetScalarParameterValue(TEXT("FillAlpha"), bEnemy ? EnemyFillAlpha : FillAlpha);
		TelegraphMID->SetScalarParameterValue(TEXT("BorderAlpha"), BorderAlpha);
		TelegraphMID->SetScalarParameterValue(TEXT("EnemyPattern"), bEnemy ? 1.f : 0.f);
		TelegraphMID->SetScalarParameterValue(TEXT("Fill"), 0.f);

		TelegraphMesh->SetVisibility(true);
		SetActorTickEnabled(true);
	}

	if (HasAuthority() && !bIsPreview)
	{
		if (Delay > 0.f)
		{
			FTimerHandle ImpactTimer;
			GetWorldTimerManager().SetTimer(ImpactTimer, this, &ThisClass::Detonate, Delay, false);
		}
		else
		{
			Detonate();
		}
	}
}

void AGenGroundArea::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsPreview)
	{
		UpdatePreview();
	}
	else
	{
		UpdateTelegraphFill();
	}
}

EGenViewerRelation AGenGroundArea::GetLocalViewerRelation() const
{
	if (bIsPreview)
	{
		return EGenViewerRelation::Self;
	}

	// Équipe du joueur local lue sur son PlayerState : elle reste juste quand il est mort (pas de pion)
	// ou entre deux possessions. Même chose pour la source : comparée par PlayerState, pas par pion.
	const APlayerController* LocalController = GetWorld()->GetFirstPlayerController();
	const AGenPlayerState* ViewerState = LocalController ? LocalController->GetPlayerState<AGenPlayerState>() : nullptr;
	const APawn* SourcePawn = GetInstigator();
	const bool bViewerIsSource = ViewerState && SourcePawn && SourcePawn->GetPlayerState() == ViewerState;
	const uint8 ViewerTeam = ViewerState ? ViewerState->GetTeamId() : GenNoTeam;
	return GenAreaRules::GetViewerRelation(bViewerIsSource, ViewerTeam, SourceTeam, GenNoTeam);
}

void AGenGroundArea::SetTelegraphRadius(float InRadius)
{
	const float Scale = FMath::Max(InRadius, 1.f) / PlaneHalfSize;
	TelegraphMesh->SetRelativeScale3D(FVector(Scale, Scale, 1.f));
}

void AGenGroundArea::UpdateTelegraphFill()
{
	if (!TelegraphMID)
	{
		return;
	}

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const float Now = GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
	TelegraphMID->SetScalarParameterValue(TEXT("Fill"), GenAreaRules::GetTelegraphFill(Now - StartServerTime, Delay));
}

void AGenGroundArea::UpdatePreview()
{
	AGenPlayerController* Controller = PreviewController.Get();
	const AGenCharacterBase* Caster = Controller ? Cast<AGenCharacterBase>(Controller->GetPawn()) : nullptr;
	if (!Caster)
	{
		TelegraphMesh->SetVisibility(false);
		return;
	}

	FVector Cursor;
	if (Controller->GetCursorLocationOnPlane(Caster->GetActorLocation().Z, Cursor))
	{
		const FVector Center = GenAreaRules::ClampToRange(Caster->GetActorLocation(), Cursor, PreviewRange);
		SetActorLocation(GenWorldQueries::FindFloor(GetWorld(), Center, { this, Caster }));
	}

	// Le télégraphe grandit avec le nourrissage (affichage prédit du lanceur)
	SetTelegraphRadius(GenFeeding::ScaleByFeed(PreviewBaseRadius, PreviewRadiusAtMaxFeed, Caster->GetFedResource(), PreviewMaxFeed));
}

bool AGenGroundArea::IsValidTarget(const AGenCharacterBase* Character) const
{
	// L'équipe retenue à l'apparition décide : la zone reste juste si le lanceur est mort entre-temps
	return Character && !Character->IsDead() && Character != GetInstigator() && AGenCharacterBase::AreTeamsEnemies(SourceTeam, Character->GetTeamId());
}

void AGenGroundArea::Detonate()
{
	if (bDetonated)
	{
		return;
	}

	const FVector Floor = GetActorLocation();
	const FVector LineOrigin = Floor + FVector(0.f, 0.f, LineOfSightHeight);

	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GenGroundArea), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Floor + FVector(0.f, 0.f, ShapeCenterHeight), FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeCapsule(Radius, Radius + ShapeExtraHalfHeight), QueryParams);

	TArray<AGenCharacterBase*> Targets;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AGenCharacterBase* Character = Cast<AGenCharacterBase>(Overlap.GetActor());
		if (!Character || Targets.Contains(Character) || !IsValidTarget(Character))
		{
			continue;
		}

		if (!GenWorldQueries::HasLineOfSight(GetWorld(), LineOrigin, Character, { this, Character }))
		{
			continue; // un mur protège la cible
		}

		// Zone au sol : traverse les contres (la cible décide quand même, ex. intouchable)
		if (Character->ResolveIncomingHit(GetInstigator(), EGenHitKind::Area, this) != EGenHitResponse::Hit)
		{
			continue;
		}

		Targets.Add(Character);
	}

	for (AGenCharacterBase* Target : Targets)
	{
		ApplyHit(Target);
	}

	UE_LOG(LogGenGroundArea, Verbose, TEXT("%s : impact en %s, rayon %.0f, %d cible(s) touchée(s)"), *GetName(), *Floor.ToCompactString(), Radius, Targets.Num());

	if (Targets.Num() > 0 && InstigatorOnHitSpecHandle.IsValid())
	{
		if (UAbilitySystemComponent* InstigatorASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetInstigator()))
		{
			InstigatorASC->ApplyGameplayEffectSpecToSelf(*InstigatorOnHitSpecHandle.Data.Get());
		}
	}

	bDetonated = true;
	OnRep_Detonated(); // Les RepNotify ne s'exécutent pas sur le serveur : appel manuel (listen server)
	SetLifeSpan(LingerAfterImpact);
}

void AGenGroundArea::ApplyHit(AGenCharacterBase* Target)
{
	if (DamageEffectSpecHandle.IsValid())
	{
		if (UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent())
		{
			const FHitResult HitResult(Target, nullptr, GetActorLocation(), FVector::UpVector);
			DamageEffectSpecHandle.Data->GetContext().AddHitResult(HitResult, true);
			TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
		}
	}

	if (Target->IsDead())
	{
		return; // tué par l'impact : ni étourdissement ni repoussement sur un corps
	}

	if (StunDuration > 0.f)
	{
		if (UGenAbilitySystemComponent* TargetASC = Target->GetGenAbilitySystemComponent())
		{
			TargetASC->ApplyHardCC(GenGameplayTags::State_Stunned, StunDuration, GetInstigator());
		}
	}

	if (KnockbackDistance > 0.f)
	{
		Target->ApplyKnockback(Target->GetActorLocation() - GetActorLocation(), KnockbackDistance);
	}
}

void AGenGroundArea::OnRep_Detonated()
{
	if (bDetonated)
	{
		PlayImpactEffects();
	}
}

void AGenGroundArea::PlayImpactEffects()
{
	if (bImpactEffectsPlayed)
	{
		return;
	}
	bImpactEffectsPlayed = true;

	SetActorTickEnabled(false);
	TelegraphMesh->SetVisibility(false);

	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (ImpactFX)
	{
		// Image 1 de l'impact = zone pleine taille au rayon exact (Art Bible §7.3)
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactFX, GetActorLocation(), FRotator::ZeroRotator, FVector(Radius / ImpactFXReferenceRadius));
	}
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, GetActorLocation());
	}

	K2_OnImpact();
}
