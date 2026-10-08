#include "Character/GenCharacterBase.h"
#include "Character/GenCharacterMovementComponent.h"
#include "Character/GenSpellIndicatorComponent.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/Abilities/GenGA_Cast.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenCastBarRules.h"
#include "AbilitySystem/GenIndicatorRules.h"
#include "AbilitySystem/GenKnockback.h"
#include "Actors/GenGroundArea.h"
#include "Actors/GenProjectile.h"
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
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGenCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
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

	SpellIndicator = CreateDefaultSubobject<UGenSpellIndicatorComponent>(TEXT("SpellIndicator"));
	SpellIndicator->SetupAttachment(GetCapsuleComponent());
}

void AGenCharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGenCharacterBase, bIsDead);
	DOREPLIFETIME_CONDITION(AGenCharacterBase, CastInfo, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AGenCharacterBase, FedResource, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AGenCharacterBase, FedSpentCount, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AGenCharacterBase, LeapTarget, COND_SkipOwner);
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
	WakeSpellIndicator();
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
	// Comme StartCast, sans effet : la fenêtre ne fige pas l'orientation du personnage. Remis à faux même si
	// l'appelant n'a pas retiré l'incantation avant (revue V2-V4, M9)
	SetFaceAim(false);
	CastInfo = FGenCastInfo();
	CastInfo.Ability = Ability;
	CastInfo.Duration = Duration;
	CastInfo.StartTime = GetCastClockSeconds();
	CastInfo.bChannel = true;
	UpdateCastFX(); // éteint l'effet d'une incantation précédente
	WakeSpellIndicator();
}

float AGenCharacterBase::GetCastElapsedFraction() const
{
	return CastInfo.IsCasting() ? GenCastBar::GetElapsedFraction(CastInfo.StartTime, CastInfo.Duration, GetCastClockSeconds()) : 0.f;
}

void AGenCharacterBase::SetLeapTarget(const FGenLeapTarget& Target)
{
	LeapTarget = Target;
	LeapTarget.StartTime = GetCastClockSeconds();
	WakeSpellIndicator();
}

void AGenCharacterBase::OnRep_LeapTarget()
{
	WakeSpellIndicator();
}

void AGenCharacterBase::WakeSpellIndicator()
{
	if (SpellIndicator)
	{
		SpellIndicator->Wake();
	}
}

void AGenCharacterBase::ClearLeapTarget(UClass* Ability)
{
	if (LeapTarget.Ability == Ability)
	{
		LeapTarget = FGenLeapTarget();
	}
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

void AGenCharacterBase::OnRep_CastInfo(const FGenCastInfo& OldCastInfo)
{
	// Revue P3 T3-7, I1 : seulement pour une NOUVELLE incantation (ou la première réception). La fin du nourrissage
	// (MarkFeedEnded : FeedEndTime, FedCount) change aussi CastInfo, souvent dans la même image que le dernier seuil :
	// la marquer supprimerait le pop du 3e seuil chez les autres joueurs.
	if (OldCastInfo.StartTime != CastInfo.StartTime)
	{
		CastInfoRepFrame = GFrameCounter;
	}

	// Autres clients : la rotation arrive déjà par le mouvement répliqué, seul l'effet est à gérer
	UpdateCastFX();
	WakeSpellIndicator();
}

void AGenCharacterBase::UpdateCastFX()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	UNiagaraSystem* WantedFX = CastInfo.IsCasting() ? CastInfo.FX.Get() : nullptr;
	const bool bKeepFX = CastFXComponent && CastFXComponent->GetAsset() == WantedFX && CastFXComponent->IsActive();

	// Fin (ou changement) d'incantation : on laisse les particules s'éteindre d'elles-mêmes
	if (!bKeepFX && CastFXComponent)
	{
		CastFXComponent->Deactivate();
		CastFXComponent = nullptr;
	}

	if (!bKeepFX && WantedFX)
	{
		CastFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(WantedFX, GetMesh(), CastInfo.FXSocket,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, /*bAutoDestroy*/ true);
	}

	// Taille du compte nourri de l'incantation EN COURS (V2) : un proxy arrivé en plein nourrissage la prend tout de suite,
	// et un sort qui reprend le même effet ne garde pas la taille du précédent (revue V2-V4, M1)
	ApplyCastFXScale(GetFedCountForCurrentCast());
}

uint8 AGenCharacterBase::GetFedCountForCurrentCast() const
{
	if (!CastInfo.IsCasting())
	{
		return 0;
	}

	// Serveur et client propriétaire : l'affichage connaît le sort qui le possède
	if (FedDisplay.Source != FObjectKey())
	{
		const UObject* DisplayOwner = FedDisplay.Source.ResolveObjectPtr();
		return DisplayOwner && DisplayOwner->GetClass() == CastInfo.Ability ? FedDisplay.Count : 0;
	}

	// Autres clients : le compte répliqué appartient à l'incantation vue à sa dernière réception
	return CastInfo.StartTime == FedRepCastStartTime ? FedResource : 0;
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

void AGenCharacterBase::SetFedResource(const UObject* Source, uint8 Count, bool bSpent)
{
	const uint8 Old = FedResource;
	// Revue V6-V8, I-3 : un lancer (baisse dépensée) fait tourner le compteur répliqué avec le compte
	const bool bSpentDrop = bSpent && Count < Old && FedDisplay.Source == FObjectKey(Source);
	if (bSpentDrop)
	{
		++FedSpentCount;
	}
	bLastFedDropSpent = bSpentDrop;
	// Revue V2-V4, M1 : un autre sort prend l'affichage (ex : B nourrit pendant le départ différé de A, qui garde ses
	// flammes affichées) => son compte part de 0, son premier seuil fait son pop au lieu d'un "2 -> 1" muet
	const bool bNewSource = Count > 0 && FedDisplay.Source != FObjectKey(Source);
	FedDisplay.Set(FObjectKey(Source), Count);
	FedResource = FedDisplay.Count;
	if (FedResource != Old || bNewSource)
	{
		NotifyFedResourceChanged(bNewSource ? 0 : Old, FedResource);
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
	bLastFedDropSpent = false;
	if (Old != 0)
	{
		NotifyFedResourceChanged(Old, 0);
	}
}

void AGenCharacterBase::OnRep_FedResource(uint8 OldValue)
{
	// Revue V2-V4, M1 : compte (non nul) d'une autre incantation que la dernière fois (un sort a pris l'affichage d'un
	// sort encore affiché) => il repart de 0, le premier seuil du nouveau sort fait son pop. Un retour à 0 reçu avec la
	// nouvelle incantation (annulation puis nouvel appui dans la même image du serveur) reste un retour à 0.
	const bool bCasting = CastInfo.IsCasting();
	const bool bNewCast = bCasting && CastInfo.StartTime != FedRepCastStartTime;
	const int32 Old = bNewCast && FedResource > 0 ? 0 : OldValue;
	FedRepCastStartTime = bCasting ? CastInfo.StartTime : -1.f;

	// Revue V2-V4, M2 : pop seulement pour une hausse vue APRÈS l'incantation (reçue dans une image précédente). Reçus
	// ensemble (personnage devenu pertinent en plein nourrissage), le compte s'affiche sans pop.
	const bool bAllowPop = bCasting && CastInfoRepFrame != GFrameCounter;

	// Revue V6-V8, I-3 : le compteur de lancers, reçu avec le compte, dit si une baisse est un lancer
	bLastFedDropSpent = FedSpentCount != LastSeenFedSpentCount;
	LastSeenFedSpentCount = FedSpentCount;

	if (FedResource != Old)
	{
		NotifyFedResourceChanged(Old, FedResource, bAllowPop);
	}
}

void AGenCharacterBase::NotifyFedResourceChanged(int32 Old, int32 New, bool bAllowPop)
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
	if (bAllowPop && GenIndicatorRules::IsThresholdPop(Old, New))
	{
		// Contrat du cue (revue V2-V4, M3) : RawMagnitude = compte ABSOLU atteint. Une réplication regroupée (0 -> 2) ne
		// donne qu'un pop : GCN_Curffe_FeedThreshold se dimensionne sur ce compte, jamais sur "+1"
		FGameplayCueParameters Params;
		Params.RawMagnitude = New;
		Params.Location = GetFeedCueLocation();
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

FVector AGenCharacterBase::GetFeedCueLocation() const
{
	// Contrat de GCN_Curffe_FeedThreshold : au socket du sort (main, pieds pour le bond), là où les flammes arrivent
	const USkeletalMeshComponent* MeshComponent = GetMesh();
	if (MeshComponent && !CastInfo.FXSocket.IsNone() && MeshComponent->DoesSocketExist(CastInfo.FXSocket))
	{
		return MeshComponent->GetSocketLocation(CastInfo.FXSocket);
	}
	return CastFXComponent ? CastFXComponent->GetComponentLocation() : GetActorLocation();
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

	// Seul un ennemi déclenche le contre. Attaquant nul (instigateur détruit) : l'équipe retenue par la source (projectile,
	// zone) décide (revue Plan 2 Tasks 7-8, M-6) ; sans source connue, il reste un ennemi
	bool bFromEnemy = true;
	if (Attacker)
	{
		bFromEnemy = AreEnemies(Attacker, this);
	}
	else if (const AGenProjectile* Projectile = Cast<AGenProjectile>(Source))
	{
		bFromEnemy = AreTeamsEnemies(Projectile->GetSourceTeam(), GetTeamId());
	}
	else if (const AGenGroundArea* Area = Cast<AGenGroundArea>(Source))
	{
		bFromEnemy = AreTeamsEnemies(Area->GetSourceTeam(), GetTeamId());
	}
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
	// Plan 3 Task 4 : intouchable (forme de feu...), le repoussement est ignoré comme le reste du coup
	if (!HasAuthority() || bIsDead || Distance <= 0.f || IsUntouchable())
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
	RefreshMaxWalkSpeed();
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
	RefreshMaxWalkSpeed();
}

void AGenCharacterBase::SetLocalMoveSpeedMultiplier(const UObject* Source, FName Reason, float Multiplier)
{
	const FObjectKey Key(Source);
	FLocalMoveSpeedMultiplier* Entry = LocalMoveSpeedMultipliers.FindByPredicate([&Key, Reason](const FLocalMoveSpeedMultiplier& Item)
	{
		return Item.Source == Key && Item.Reason == Reason;
	});
	if (!Entry)
	{
		Entry = &LocalMoveSpeedMultipliers.AddDefaulted_GetRef();
		Entry->Source = Key;
		Entry->Reason = Reason;
	}
	const float NewMultiplier = FMath::Max(Multiplier, 0.f);
	const bool bChanged = Entry->Multiplier != NewMultiplier;
	Entry->Multiplier = NewMultiplier;
	RefreshMaxWalkSpeed();
	if (bChanged)
	{
		NoteLocalSpeedChange();
	}
}

void AGenCharacterBase::ClearLocalMoveSpeedMultiplier(const UObject* Source, FName Reason)
{
	const FObjectKey Key(Source);
	if (LocalMoveSpeedMultipliers.RemoveAll([&Key, Reason](const FLocalMoveSpeedMultiplier& Item) { return Item.Source == Key && Item.Reason == Reason; }) > 0)
	{
		RefreshMaxWalkSpeed();
		NoteLocalSpeedChange();
	}
}

void AGenCharacterBase::NoteLocalSpeedChange()
{
	// Revue V6-V8, I-2 : serveur d'un client distant, grâce des corrections autour de la borne du ralenti
	if (UGenCharacterMovementComponent* Movement = Cast<UGenCharacterMovementComponent>(GetCharacterMovement()))
	{
		Movement->NoteLocalSpeedChange();
	}
}

void AGenCharacterBase::FlushMovesToServer()
{
	// Revue V6-V8, I-2 : client propriétaire (pas l'hôte) : le mouvement en attente part AVANT la RPC qui suit (activation,
	// visée), pour que le serveur le simule avec le même ralenti que le client
	if (GetLocalRole() == ROLE_AutonomousProxy && IsLocallyControlled())
	{
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->FlushServerMoves();
		}
	}
}

float AGenCharacterBase::GetLocalMoveSpeedMultiplier() const
{
	float Product = 1.f;
	for (const FLocalMoveSpeedMultiplier& Item : LocalMoveSpeedMultipliers)
	{
		Product *= Item.Multiplier;
	}
	return Product;
}

void AGenCharacterBase::RefreshMaxWalkSpeed()
{
	// Sans ASC (pas encore initialisé) : la vitesse par défaut du CMC reste la base
	if (AttributeSet)
	{
		GetCharacterMovement()->MaxWalkSpeed = AttributeSet->GetMoveSpeed() * GetLocalMoveSpeedMultiplier();
	}
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
