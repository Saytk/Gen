#include "Character/GenCharacterBase.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenCastBarRules.h"
#include "AbilitySystem/GenIndicatorRules.h"
#include "AbilitySystem/GenKnockback.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Game/GenGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayCueManager.h"
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

	StatusVisuals = CreateDefaultSubobject<UGenStatusVisualsComponent>(TEXT("StatusVisuals"));
	StatusVisuals->SetupAttachment(GetCapsuleComponent());
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

void AGenCharacterBase::MarkFeedEnded(UClass* Ability, int32 FedCount, bool bFinal)
{
	if (CastInfo.Ability != Ability)
	{
		return;
	}

	const uint8 Count = static_cast<uint8>(FMath::Clamp(FedCount, 0, static_cast<int32>(CastInfo.FeedSlots)));

	// Première annonce : début du repli
	if (CastInfo.FeedEndTime <= 0.f)
	{
		CastInfo.FeedEndTime = FMath::Max(GetCastClockSeconds(), KINDA_SMALL_NUMBER); // 0 = nourrissage en cours
		CastInfo.FedCount = Count;
		return;
	}

	// Correction (annonce du client arrivée après coup) : le repli garde son heure de départ et le compteur
	// ne recule pas ; seul le compte validé au lancer peut le faire descendre
	CastInfo.FedCount = bFinal ? Count : FMath::Max(CastInfo.FedCount, Count);
}

void AGenCharacterBase::StartChannel(UClass* Ability, float Duration)
{
	// Comme StartCast, sans effet ni SetFaceAim : la fenêtre ne fige pas l'orientation du personnage
	CastInfo = FGenCastInfo();
	CastInfo.Ability = Ability;
	CastInfo.Duration = Duration;
	CastInfo.StartTime = GetCastClockSeconds();
	CastInfo.bChannel = true;
	UpdateCastFX(); // éteint l'effet d'une incantation précédente
}

float AGenCharacterBase::GetCastElapsedFraction() const
{
	return CastInfo.IsCasting() ? GenCastBar::GetElapsedFraction(CastInfo.StartTime, CastInfo.Duration, GetCastClockSeconds()) : 0.f;
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

	// Le sort actif borne l'annonce : par le temps qu'il a mesuré, la ressource et son maximum
	for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->GetClass() == Ability)
		{
			if (UGenGA_Cast* CastAbility = Cast<UGenGA_Cast>(Spec.GetPrimaryInstance()))
			{
				CastAbility->ApplyReportedFedCount(Count);
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
		// Proxy arrivé en cours de nourrissage : l'effet prend tout de suite la taille du compte nourri (V2)
		ApplyCastFXScale(FedResource);
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
	Params.bChannel = CastInfo.bChannel;
	if (CastInfo.FeedSlots > 0)
	{
		// Sort nourri : Duration est la longueur du nourrissage (cf. StartFeedCast), l'incantation en est le reste
		const float Interval = FMath::Max(CastInfo.FeedInterval, 0.f);
		Params.FeedSlots = CastInfo.FeedSlots;
		Params.FeedInterval = Interval;
		Params.CastTime = FMath::Max(CastInfo.Duration - CastInfo.FeedSlots * Interval, 0.f);
		Params.FeedEndTime = CastInfo.FeedEndTime;
		// Pendant le nourrissage : flammes nourries en direct (celles qui quittent l'orbite), sinon le compte final
		Params.FedCount = CastInfo.FeedEndTime > 0.f ? CastInfo.FedCount : FedResource;
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

void AGenCharacterBase::SetFedResource(const UObject* Source, uint8 Count)
{
	const uint8 Old = FedResource;
	FedDisplay.Set(FObjectKey(Source), Count);
	FedResource = FedDisplay.Count;
	if (FedResource != Old)
	{
		NotifyFedResourceChanged(Old, FedResource);
	}
}

void AGenCharacterBase::ClearFedResourceFrom(const UObject* Source)
{
	if (Source && FedDisplay.Source == FObjectKey(Source))
	{
		ResetFedResource();
	}
}

void AGenCharacterBase::ResetFedResource()
{
	const uint8 Old = FedResource;
	FedDisplay = GenFeeding::FFedDisplay();
	FedResource = 0;
	if (Old != 0)
	{
		NotifyFedResourceChanged(Old, 0);
	}
}

void AGenCharacterBase::OnRep_FedResource(uint8 OldValue)
{
	if (FedResource != OldValue)
	{
		NotifyFedResourceChanged(OldValue, FedResource);
	}
}

void AGenCharacterBase::NotifyFedResourceChanged(int32 Old, int32 New)
{
	// Purement cosmétique : jamais sur le serveur dédié (GetNetMode, valable aussi en PIE, Art Bible §8.5)
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	ApplyCastFXScale(New);
	OnFedResourceChanged.Broadcast(this, Old, New);

	// Pop du seuil : exécuté localement sur chaque client (pas de RPC ni de réplication du cue), seulement quand le
	// compte augmente. Le client propriétaire le joue sur sa prédiction, les autres sur le compte répliqué.
	if (GenIndicatorRules::IsThresholdPop(Old, New))
	{
		FGameplayCueParameters Params;
		Params.RawMagnitude = New;
		Params.Location = CastFXComponent ? CastFXComponent->GetComponentLocation() : GetActorLocation();
		Params.SourceObject = CastInfo.Ability ? CastInfo.Ability->GetDefaultObject() : nullptr;
		Params.Instigator = this;
		Params.EffectCauser = this;

		// HandleGameplayCue est local ; UGameplayCueFunctionLibrary::ExecuteGameplayCueOnActor ne joue que sur
		// l'autorité quand l'acteur a un ASC (il serait muet sur les clients)
		if (UGameplayCueManager* CueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager())
		{
			CueManager->HandleGameplayCue(this, GenGameplayTags::GameplayCue_Feed_Threshold, EGameplayCueEvent::Executed, Params);
		}
		OnFedThresholdReached.Broadcast(this, New);
	}
}

void AGenCharacterBase::ApplyCastFXScale(int32 Count)
{
	if (CastFXComponent)
	{
		CastFXComponent->SetRelativeScale3D(FVector(1.f + CastFXScalePerFed * FMath::Max(Count, 0)));
	}
}

bool AGenCharacterBase::IsUntouchable() const
{
	return AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(GenGameplayTags::State_Untouchable);
}

EGenHitResponse AGenCharacterBase::ResolveIncomingHit(AActor* Attacker, EGenHitKind Kind, const UObject* Source)
{
	if (!HasAuthority() || !AbilitySystemComponent)
	{
		return EGenHitResponse::Hit;
	}

	// Seul un ennemi déclenche le contre ; un attaquant nul (instigateur détruit, ex. zone d'un lanceur mort) reste un ennemi
	const bool bFromEnemy = !Attacker || AreEnemies(Attacker, this);
	const bool bCountering = bFromEnemy && AbilitySystemComponent->HasMatchingGameplayTag(GenGameplayTags::State_Countering);
	const EGenHitResponse Response = GenHitRules::Resolve(bCountering, IsUntouchable(), Kind);

	if (Response == EGenHitResponse::Countered)
	{
		// Le sort de contre écoute cet événement (récompense, repoussement de la mêlée, effet)
		FGameplayEventData Payload;
		Payload.EventTag = GenGameplayTags::Event_Counter_Blocked;
		Payload.Instigator = Attacker;
		Payload.Target = this;
		Payload.OptionalObject = Source;
		Payload.EventMagnitude = GenHitRules::ToEventMagnitude(Kind);
		AbilitySystemComponent->HandleGameplayEvent(Payload.EventTag, &Payload);
	}

	return Response;
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

void AGenCharacterBase::ClientStopCastMontage_Implementation(UAnimMontage* Montage)
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (Montage && AnimInstance && AnimInstance->Montage_IsPlaying(Montage))
	{
		AnimInstance->Montage_Stop(0.25f, Montage);
	}
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

	// Formes d'état sur toutes les machines (Bind ignore le serveur dédié)
	if (StatusVisuals)
	{
		StatusVisuals->Bind(AbilitySystemComponent, StatusVisualConfig);
	}
}

void AGenCharacterBase::UninitializeAbilitySystem()
{
	// IsValid : en fin de partie le PlayerState (et son ASC) peut être détruit avant le pawn
	if (!IsValid(AbilitySystemComponent))
	{
		return;
	}

	if (StatusVisuals)
	{
		StatusVisuals->Unbind();
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
		// Un mort ne garde ni contre, ni étourdissement, ni état temporaire (l'ASC survit au respawn)
		AbilitySystemComponent->RemoveTimedStates();
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
	// Toutes les machines : un verrou de lancement (tag local) oublié par un sort survivrait au respawn (l'ASC est
	// sur le PlayerState) et bloquerait tous les sorts
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->ClearCastLock();
	}
	ResetFedResource();

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
