#include "Actors/GenProjectile.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/GenCharacterBase.h"
#include "CollisionQueryParams.h"
#include "Components/SphereComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenProjectile, Log, All);

AGenProjectile::AGenProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicatingMovement(true);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(20.f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);
	SetRootComponent(CollisionSphere);

	ProjectileFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ProjectileFX"));
	ProjectileFX->SetupAttachment(CollisionSphere);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->InitialSpeed = Speed;
	ProjectileMovement->MaxSpeed = Speed;
	ProjectileMovement->ProjectileGravityScale = 0.f;
	ProjectileMovement->bRotationFollowsVelocity = true;
}

void AGenProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGenProjectile, bExploded);
	DOREPLIFETIME(AGenProjectile, ImpactLocation);
	DOREPLIFETIME_CONDITION(AGenProjectile, Speed, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AGenProjectile, ShotScale, COND_InitialOnly);
}

void AGenProjectile::InitializeShot(const FGenProjectileShotParams& Params)
{
	if (Params.Speed > 0.f)
	{
		Speed = Params.Speed;
	}
	ShotScale = FMath::Max(Params.Scale, 0.1f);
	ExplosionRadius = FMath::Max(Params.ExplosionRadius, 0.f);
	KnockbackDistance = FMath::Max(Params.KnockbackDistance, 0.f);
	SetActorScale3D(FVector(ShotScale));
}

void AGenProjectile::BeginPlay()
{
	Super::BeginPlay();

	// Clients : l'échelle n'est pas répliquée par le mouvement, on l'applique depuis ShotScale
	SetActorScale3D(FVector(ShotScale));

	// La vitesse éditée dans le Blueprint prime sur la valeur du constructeur
	ProjectileMovement->InitialSpeed = Speed;
	ProjectileMovement->MaxSpeed = Speed;
	ProjectileMovement->Velocity = GetActorForwardVector() * Speed;

	if (APawn* InstigatorPawn = GetInstigator())
	{
		CollisionSphere->IgnoreActorWhenMoving(InstigatorPawn, true);
	}

	CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnSphereOverlap);

	if (HasAuthority())
	{
		// En bout de portée, le projectile explose dans le vide (FX d'impact, sans dégâts)
		// plutôt que de disparaître d'un coup
		if (Speed > 0.f)
		{
			FTimerHandle RangeTimer;
			GetWorldTimerManager().SetTimer(RangeTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (!bExploded)
				{
					Explode(nullptr, GetActorLocation());
				}
			}), MaxRange / Speed, false);
		}

		// Les overlaps initiaux (spawn à bout portant dans un ennemi) sont calculés avant
		// BeginPlay, donc avant que le delegate soit branché : on les traite ici
		TArray<UPrimitiveComponent*> OverlappingComponents;
		CollisionSphere->GetOverlappingComponents(OverlappingComponents);
		for (UPrimitiveComponent* Component : OverlappingComponents)
		{
			OnSphereOverlap(CollisionSphere, Component ? Component->GetOwner() : nullptr, Component, INDEX_NONE, false, FHitResult());
			if (bExploded)
			{
				break;
			}
		}
	}
}

void AGenProjectile::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Seul le serveur décide des impacts ; les clients reçoivent bExploded
	if (!HasAuthority() || bExploded || !OtherActor || OtherActor == this || OtherActor == GetInstigator())
	{
		return;
	}

	// Les projectiles ne se percutent pas entre eux
	if (OtherActor->IsA<AGenProjectile>())
	{
		return;
	}

	if (const AGenCharacterBase* HitCharacter = Cast<AGenCharacterBase>(OtherActor))
	{
		// On traverse les alliés et les morts
		if (HitCharacter->IsDead() || !AGenCharacterBase::AreEnemies(GetInstigator(), HitCharacter))
		{
			return;
		}
	}
	else if (OtherActor->IsA<APawn>())
	{
		return;
	}

	Explode(OtherActor, bFromSweep ? FVector(SweepResult.ImpactPoint) : GetActorLocation());
}

bool AGenProjectile::IsValidTarget(const AGenCharacterBase* Character) const
{
	return Character && !Character->IsDead() && AGenCharacterBase::AreEnemies(GetInstigator(), Character);
}

void AGenProjectile::AddExplosionTargets(const FVector& Origin, TArray<AGenCharacterBase*>& InOutTargets) const
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GenProjectileExplosion), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(ExplosionRadius), QueryParams);

	FCollisionObjectQueryParams BlockingObjects;
	BlockingObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	BlockingObjects.AddObjectTypesToQuery(ECC_WorldDynamic);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AGenCharacterBase* Character = Cast<AGenCharacterBase>(Overlap.GetActor());
		if (!Character || InOutTargets.Contains(Character) || !IsValidTarget(Character))
		{
			continue;
		}

		FCollisionQueryParams LineParams(SCENE_QUERY_STAT(GenProjectileExplosionLOS), false, this);
		LineParams.AddIgnoredActor(Character);
		if (GetWorld()->LineTraceTestByObjectType(Origin, Character->GetActorLocation(), BlockingObjects, LineParams))
		{
			continue; // un mur protège la cible
		}

		InOutTargets.Add(Character);
	}
}

void AGenProjectile::ApplyHit(AGenCharacterBase* Target, const FVector& Origin, bool bDirectHit)
{
	if (DamageEffectSpecHandle.IsValid())
	{
		if (UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent())
		{
			const FHitResult HitResult(Target, nullptr, Origin, -GetActorForwardVector());
			DamageEffectSpecHandle.Data->GetContext().AddHitResult(HitResult, true);
			TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
		}
	}

	if (KnockbackDistance > 0.f)
	{
		// Coup direct : dans le sens du tir ; éclaboussure : en s'éloignant du centre de l'explosion
		const FVector Direction = bDirectHit ? GetActorForwardVector() : Target->GetActorLocation() - Origin;
		Target->ApplyKnockback(Direction, KnockbackDistance);
	}
}

void AGenProjectile::Explode(AActor* HitActor, const FVector& Location)
{
	UE_LOG(LogGenProjectile, Verbose, TEXT("%s explose sur %s en %s (%.2fs après spawn)"), *GetName(), *GetNameSafe(HitActor), *Location.ToCompactString(), GetGameTimeSinceCreation());

	// Centre de l'explosion légèrement en retrait de la surface touchée : les tests de ligne
	// de vue partent ainsi du bon côté d'un mur
	const FVector Origin = Location - GetActorForwardVector() * CollisionSphere->GetScaledSphereRadius();

	TArray<AGenCharacterBase*> Targets;
	AGenCharacterBase* DirectTarget = Cast<AGenCharacterBase>(HitActor);
	if (IsValidTarget(DirectTarget))
	{
		Targets.Add(DirectTarget);
	}
	if (ExplosionRadius > 0.f && HitActor)
	{
		AddExplosionTargets(Origin, Targets);
	}

	for (AGenCharacterBase* Target : Targets)
	{
		ApplyHit(Target, Origin, Target == DirectTarget);
	}

	UE_LOG(LogGenProjectile, Verbose, TEXT("%s : %d cible(s) touchée(s)"), *GetName(), Targets.Num());

	if (Targets.Num() > 0 && InstigatorOnHitSpecHandle.IsValid())
	{
		if (UAbilitySystemComponent* InstigatorASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetInstigator()))
		{
			InstigatorASC->ApplyGameplayEffectSpecToSelf(*InstigatorOnHitSpecHandle.Data.Get());
		}
	}

	ImpactLocation = Location;
	bExploded = true;
	OnRep_Exploded(); // Les RepNotify ne s'exécutent pas sur le serveur : appel manuel (listen server)

	ProjectileMovement->StopMovementImmediately();
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Laisse le temps à bExploded d'être répliqué avant la destruction
	SetLifeSpan(0.5f);
}

void AGenProjectile::OnRep_Exploded()
{
	if (bExploded)
	{
		PlayImpactEffects();
	}
}

void AGenProjectile::PlayImpactEffects()
{
	if (bImpactEffectsPlayed)
	{
		return;
	}
	bImpactEffectsPlayed = true;

	const FVector Location = ImpactLocation;

	if (ImpactFX)
	{
		// Suit l'échelle du projectile (grosse boule de feu => grosse explosion)
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactFX, Location, GetActorRotation(), GetActorScale3D());
	}
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Location);
	}

	// On masque les composants plutôt que l'acteur : un acteur caché sans collision cesse
	// d'être répliqué, et bExploded n'arriverait jamais aux clients
	ProjectileFX->Deactivate();
	CollisionSphere->SetVisibility(false, true);

	K2_OnImpact(Location);
}
