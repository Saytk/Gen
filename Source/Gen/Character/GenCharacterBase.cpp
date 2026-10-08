#include "Character/GenCharacterBase.h"

#include "AbilitySystem/Abilities/GenGA_Projectile.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenKnockback.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Game/GenGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GenGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

AGenCharacterBase::AGenCharacterBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	// Le personnage s'oriente dans la direction du déplacement (pendant une incantation : vers la visée, cf. SetFaceAim)
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 1080.f, 0.f);
	Movement->MaxWalkSpeed = 550.f;
	Movement->MaxAcceleration = 4096.f;          // déplacements nerveux type arène
	Movement->BrakingDecelerationWalking = 4096.f;
	Movement->GroundFriction = 8.f;
	Movement->bCanWalkOffLedges = true;
	Movement->GetNavAgentPropertiesRef().bCanJump = false;

	// Mesh orienté comme le mannequin UE5 (face à +X)
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -96.f), FRotator(0.f, -90.f, 0.f));
}

void AGenCharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGenCharacterBase, bIsDead);
	DOREPLIFETIME_CONDITION(AGenCharacterBase, CastInfo, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AGenCharacterBase, FedResource, COND_SkipOwner);
}

float AGenCharacterBase::GetCastClockSeconds() const
{
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void AGenCharacterBase::StartCast(UClass* Ability, float Duration, UNiagaraSystem* FX, FName FXSocket)
{
	// Repart de zéro : aucun reste d'un nourrissage précédent
	CastInfo = FGenCastInfo();
	CastInfo.Ability = Ability;
	CastInfo.Duration = Duration;
	CastInfo.StartTime = GetCastClockSeconds();
	CastInfo.FX = FX;
	CastInfo.FXSocket = FXSocket;

	SetFaceAim(true);
	UpdateCastFX();
}

void AGenCharacterBase::StartFeedCast(UClass* Ability, int32 FeedSlots, float FeedInterval, float CastTime, UNiagaraSystem* FX, FName FXSocket)
{
	const int32 Slots = FMath::Clamp(FeedSlots, 0, 255);
	StartCast(Ability, Slots * FMath::Max(FeedInterval, 0.f) + FMath::Max(CastTime, 0.f), FX, FXSocket);
	CastInfo.FeedSlots = static_cast<uint8>(Slots);
	CastInfo.FeedInterval = FeedInterval;
}

void AGenCharacterBase::MarkFeedEnded(UClass* Ability, int32 FedCount)
{
	if (CastInfo.Ability != Ability)
	{
		return;
	}

	// Seconde annonce (compte corrigé par le serveur) : le repli garde son heure de départ
	if (CastInfo.FeedEndTime <= 0.f)
	{
		CastInfo.FeedEndTime = FMath::Max(GetCastClockSeconds(), KINDA_SMALL_NUMBER); // 0 = nourrissage en cours
	}
	CastInfo.FedCount = static_cast<uint8>(FMath::Clamp(FedCount, 0, static_cast<int32>(CastInfo.FeedSlots)));
}

void AGenCharacterBase::StopCast(UClass* Ability)
{
	// Ne pas effacer l'incantation d'un autre sort lancé entre-temps
	if (CastInfo.Ability == Ability)
	{
		CastInfo = FGenCastInfo();
		SetFaceAim(false);
		UpdateCastFX();
	}
}

void AGenCharacterBase::ServerReportFedResource_Implementation(UClass* Ability, uint8 Count)
{
	// Message en retard (incantation finie, ou d'un autre sort) : ignoré
	if (!CastInfo.IsCasting() || CastInfo.Ability != Ability || !AbilitySystemComponent)
	{
		return;
	}

	// Le sort actif borne l'annonce : à ±1 de sa propre estimation, à la ressource et à son maximum
	for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->GetClass() == Ability)
		{
			if (UGenGA_Projectile* Projectile = Cast<UGenGA_Projectile>(Spec.GetPrimaryInstance()))
			{
				Projectile->ApplyReportedFedCount(Count);
			}
			return;
		}
	}
}

void AGenCharacterBase::OnRep_CastInfo()
{
	// Autres clients : la rotation arrive déjà par le mouvement répliqué, seul l'effet est à gérer
	UpdateCastFX();
}

void AGenCharacterBase::UpdateCastFX()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	UNiagaraSystem* WantedFX = CastInfo.IsCasting() ? CastInfo.FX.Get() : nullptr;
	if (CastFXComponent && CastFXComponent->GetAsset() == WantedFX && CastFXComponent->IsActive())
	{
		return;
	}

	// Fin (ou changement) d'incantation : on laisse les particules s'éteindre d'elles-mêmes
	if (CastFXComponent)
	{
		CastFXComponent->Deactivate();
		CastFXComponent = nullptr;
	}

	if (WantedFX)
	{
		CastFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(WantedFX, GetMesh(), CastInfo.FXSocket,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, /*bAutoDestroy*/ true);
	}
}

void AGenCharacterBase::SetFaceAim(bool bFaceAim)
{
	// Serveur et client propriétaire : le serveur reçoit la visée du client avec ses mouvements
	// (rotation de contrôle), puis la rotation du personnage est répliquée aux autres
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = !bFaceAim;
	Movement->bUseControllerDesiredRotation = bFaceAim;
}

float AGenCharacterBase::GetCastProgress() const
{
	GenCastBar::FLayout Layout;
	return GetCastBarLayout(Layout) ? Layout.Fill : -1.f;
}

bool AGenCharacterBase::GetCastBarLayout(GenCastBar::FLayout& OutLayout) const
{
	if (!CastInfo.IsCasting())
	{
		return false;
	}

	GenCastBar::FParams Params;
	Params.StartTime = CastInfo.StartTime;
	if (CastInfo.FeedSlots > 0)
	{
		// Sort nourri : Duration est la longueur du nourrissage, l'incantation en est le reste
		Params.FeedSlots = CastInfo.FeedSlots;
		Params.FeedInterval = CastInfo.FeedInterval;
		Params.CastTime = FMath::Max(CastInfo.Duration - CastInfo.FeedSlots * CastInfo.FeedInterval, 0.f);
		Params.FeedEndTime = CastInfo.FeedEndTime;
		Params.FedCount = CastInfo.FedCount;
	}
	else
	{
		Params.CastTime = CastInfo.Duration;
	}

	OutLayout = GenCastBar::ComputeLayout(Params, GetCastClockSeconds());
	return true;
}

UAbilitySystemComponent* AGenCharacterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

bool AGenCharacterBase::AreTeamsEnemies(uint8 TeamA, uint8 TeamB)
{
	return TeamA == GenNoTeam || TeamB == GenNoTeam || TeamA != TeamB;
}

bool AGenCharacterBase::AreEnemies(const AActor* A, const AActor* B)
{
	const AGenCharacterBase* CharA = Cast<AGenCharacterBase>(A);
	const AGenCharacterBase* CharB = Cast<AGenCharacterBase>(B);

	if (!CharA || !CharB)
	{
		return CharA != CharB;
	}
	if (CharA == CharB)
	{
		return false;
	}
	return AreTeamsEnemies(CharA->GetTeamId(), CharB->GetTeamId());
}

float AGenCharacterBase::GetHealth() const
{
	return AttributeSet ? AttributeSet->GetHealth() : 0.f;
}

float AGenCharacterBase::GetMaxHealth() const
{
	return AttributeSet ? AttributeSet->GetMaxHealth() : 0.f;
}

float AGenCharacterBase::GetEnergy() const
{
	return AttributeSet ? AttributeSet->GetEnergy() : 0.f;
}

float AGenCharacterBase::GetMaxEnergy() const
{
	return AttributeSet ? AttributeSet->GetMaxEnergy() : 0.f;
}

float AGenCharacterBase::GetResource() const
{
	return AttributeSet ? AttributeSet->GetResource() : 0.f;
}

float AGenCharacterBase::GetMaxResource() const
{
	return AttributeSet ? AttributeSet->GetMaxResource() : 0.f;
}

void AGenCharacterBase::ApplyKnockback(const FVector& Direction, float Distance)
{
	if (!HasAuthority() || bIsDead || Distance <= 0.f)
	{
		return;
	}

	FVector Direction2D = Direction.GetSafeNormal2D();
	if (Direction2D.IsNearlyZero())
	{
		Direction2D = -GetActorForwardVector().GetSafeNormal2D();
	}

	const FVector LaunchVelocity = GenKnockback::ComputeLaunchVelocity(Direction2D, Distance, GetCharacterMovement()->GetGravityZ());
	LaunchCharacter(LaunchVelocity, true, true);

	if (IsPlayerControlled() && !IsLocallyControlled())
	{
		ClientApplyKnockback(LaunchVelocity);
	}
}

void AGenCharacterBase::ClientApplyKnockback_Implementation(FVector_NetQuantize10 LaunchVelocity)
{
	LaunchCharacter(LaunchVelocity, true, true);
}

void AGenCharacterBase::OnAbilitySystemInitialized()
{
	check(AbilitySystemComponent && AttributeSet);

	// Peut être rappelé côté client (OnRep_PlayerState multiples) : on évite les doubles bindings
	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetMoveSpeedAttribute()).Remove(MoveSpeedChangedHandle);
	AttributeSet->OnOutOfHealth.Remove(OutOfHealthHandle);

	// Vitesse de déplacement pilotée par l'attribut MoveSpeed (slows / boosts via GameplayEffects)
	GetCharacterMovement()->MaxWalkSpeed = AttributeSet->GetMoveSpeed();
	MoveSpeedChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetMoveSpeedAttribute())
		.AddUObject(this, &ThisClass::OnMoveSpeedChanged);

	if (HasAuthority())
	{
		OutOfHealthHandle = AttributeSet->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);

		GrantStartupAbilitiesAndEffects();

		// (Ré)apparition : plus mort, vie pleine
		AbilitySystemComponent->SetLooseGameplayTagCount(GenGameplayTags::State_Dead, 0, EGameplayTagReplicationState::TagOnly);
		AbilitySystemComponent->SetNumericAttributeBase(UGenAttributeSet::GetHealthAttribute(), AttributeSet->GetMaxHealth());
	}
}

void AGenCharacterBase::UninitializeAbilitySystem()
{
	// IsValid : en fin de partie le PlayerState (et son ASC) peut être détruit avant le pawn
	if (!IsValid(AbilitySystemComponent))
	{
		return;
	}

	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UGenAttributeSet::GetMoveSpeedAttribute()).Remove(MoveSpeedChangedHandle);
	MoveSpeedChangedHandle.Reset();

	if (AttributeSet)
	{
		AttributeSet->OnOutOfHealth.Remove(OutOfHealthHandle);
		OutOfHealthHandle.Reset();
	}

	if (HasAuthority())
	{
		RemoveStartupAbilitiesAndEffects();
	}

	// Si on est encore l'avatar de l'ASC, on le libère proprement
	if (AbilitySystemComponent->GetAvatarActor() == this)
	{
		AbilitySystemComponent->CancelAbilities();
		AbilitySystemComponent->ClearAbilityInput();
		AbilitySystemComponent->RemoveAllGameplayCues();

		if (AbilitySystemComponent->GetOwnerActor() && AbilitySystemComponent->GetOwnerActor() != this)
		{
			AbilitySystemComponent->SetAvatarActor(nullptr);
		}
		else
		{
			AbilitySystemComponent->ClearActorInfo();
		}
	}
}

void AGenCharacterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeAbilitySystem();

	Super::EndPlay(EndPlayReason);
}

void AGenCharacterBase::GrantStartupAbilitiesAndEffects()
{
	if (!HasAuthority() || !AbilitySystemComponent || GrantedAbilityHandles.Num() > 0)
	{
		return;
	}

	GrantedEffectHandles = AbilitySystemComponent->ApplyEffectsToSelf(StartupEffects, this);
	GrantedAbilityHandles = AbilitySystemComponent->GrantAbilities(StartupAbilities, this);
}

void AGenCharacterBase::RemoveStartupAbilitiesAndEffects()
{
	if (!HasAuthority() || !AbilitySystemComponent)
	{
		return;
	}

	for (const FGameplayAbilitySpecHandle& Handle : GrantedAbilityHandles)
	{
		AbilitySystemComponent->ClearAbility(Handle);
	}
	for (const FActiveGameplayEffectHandle& Handle : GrantedEffectHandles)
	{
		AbilitySystemComponent->RemoveActiveGameplayEffect(Handle);
	}

	GrantedAbilityHandles.Reset();
	GrantedEffectHandles.Reset();
}

void AGenCharacterBase::OnMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	GetCharacterMovement()->MaxWalkSpeed = Data.NewValue;
}

void AGenCharacterBase::HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->CancelAllAbilities();
		AbilitySystemComponent->SetLooseGameplayTagCount(GenGameplayTags::State_Dead, 1, EGameplayTagReplicationState::TagOnly);
	}

	OnDeathStarted(); // Les RepNotify ne s'exécutent pas sur le serveur

	if (AGenGameMode* GameMode = GetWorld()->GetAuthGameMode<AGenGameMode>())
	{
		GameMode->OnCharacterDied(this, DamageInstigator);
	}
}

void AGenCharacterBase::OnRep_IsDead()
{
	if (bIsDead)
	{
		OnDeathStarted();
	}
}

void AGenCharacterBase::OnDeathStarted()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->DisableMovement();

	// On peut traverser les cadavres
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	if (bRagdollOnDeath && GetMesh()->GetPhysicsAsset())
	{
		GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
		GetMesh()->SetSimulatePhysics(true);
	}

	K2_OnDeath();
}
