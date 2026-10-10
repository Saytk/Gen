#include "AbilitySystem/Abilities/GenGA_Dash.h"

#include "Abilities/Tasks/AbilityTask_ApplyRootMotionMoveToForce.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAbilityTooltipData.h"
#include "AbilitySystem/GenAreaRules.h"
#include "AbilitySystem/GenIndicatorRules.h"
#include "AbilitySystem/GenTargetData.h"
#include "Character/GenCharacterBase.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenDash, Log, All);

namespace GenDashPrivate
{
	/** Segment plus court que ça après un mur : abandonné (le trajet s'arrête au point précédent). */
	constexpr float MinSegmentLength = 10.f;
	/** Recul devant le mur (cm) : le mouvement ne part pas en pénétration. */
	constexpr float WallBackOff = 2.f;
	/** Marge du filet de sécurité au-delà de la durée prévue (s). */
	constexpr float SafetyMargin = 0.5f;
}

UGenGA_Dash::UGenGA_Dash()
{
	CastTime = 0.1f;
	bFeedable = true;
	// Le déplacement vient des Root Motion Sources, jamais du clip (Art Bible §8.4)
	CastMontageRootMotionScale = 0.f;
}

GenDashRules::FZigzagParams UGenGA_Dash::GetZigzagParams() const
{
	GenDashRules::FZigzagParams Params;
	Params.BaseDistance = BaseDistance;
	Params.SegmentDistance = SegmentDistance;
	Params.ZigzagAngle = ZigzagAngle;
	Params.MaxSegments = 1 + (bFeedable ? FMath::Max(MaxFeed, 0) : 0);
	return Params;
}

float UGenGA_Dash::GetDashDuration(int32 Fed) const
{
	return SegmentDuration * GenDashRules::GetSegmentCount(Fed, GetZigzagParams().MaxSegments);
}

void UGenGA_Dash::FillAimData(FGenTargetData_Aim& Data) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}

	// Même calcul que le serveur ferait depuis SA position : le client l'envoie, il devient celui des deux machines.
	// Distance = le premier segment (seul ce qui est vérifié par AcceptClientLeap), lacet = la visée
	const FVector Start = Avatar->GetActorLocation();
	Data.LeapDistance = BaseDistance;
	Data.LeapYaw = GenIndicatorRules::FlatDirection(Data.HitResult.Location - Start, Avatar->GetActorForwardVector()).Rotation().Yaw;
}

float UGenGA_Dash::ResolveDashYaw(const FGenCastRelease& Release, const FVector& Start) const
{
	// AimDirection n'est jamais nulle (avant du lanceur si le curseur est sur lui) : même repli que le client
	const float OwnYaw = GenIndicatorRules::FlatDirection(Release.AimDirection, Release.AimDirection).Rotation().Yaw;
	if (Release.ClientLeapDistance < 0.f)
	{
		return OwnYaw; // pas d'annonce (IA, visée d'un autre type)
	}

	// Client ou hôte : sa propre valeur, celle qu'il vient d'envoyer
	if (!IsServerForRemoteClient())
	{
		return Release.ClientLeapYaw;
	}

	// Serveur : lacet du client si son premier segment, refait depuis la position du serveur, arrive près du sien
	const FVector OwnFirst = Start + FRotator(0.f, OwnYaw, 0.f).Vector() * BaseDistance;
	if (GenAreaRules::AcceptClientLeap(Start, Release.ClientLeapDistance, Release.ClientLeapYaw, OwnFirst, BaseDistance))
	{
		return Release.ClientLeapYaw;
	}

	UE_LOG(LogGenDash, Warning, TEXT("[SERVEUR] %s : ruée du client refusée (%.0f cm, lacet %.1f°), lacet du serveur (%.1f°)"),
		*GetName(), Release.ClientLeapDistance, Release.ClientLeapYaw, OwnYaw);
	return OwnYaw;
}

void UGenGA_Dash::ComputePath(const ACharacter* Character, const FVector& Start, float AimYaw, int32 Fed, TArray<FVector>& OutPoints) const
{
	OutPoints = GenDashRules::ComputeZigzag(Start, AimYaw, Fed, GetZigzagParams());

	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!World || !Capsule)
	{
		return;
	}

	// Capsule du personnage, rétrécie et relevée au-dessus des marches (son bas à MaxStepHeight des pieds) : une marche ou
	// une pente douce n'est pas un mur, le mouvement les franchit. Les pions ne sont pas des murs (positions différentes
	// sur chaque machine) : un pion sur le trajet bloque le mouvement, et le segment qui n'arrive pas arrête la ruée.
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const float StepHeight = Movement ? Movement->MaxStepHeight : 45.f;
	const float Radius = FMath::Max(Capsule->GetScaledCapsuleRadius() - 2.f, 1.f);
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float ShrunkHalfHeight = FMath::Max(HalfHeight - StepHeight * 0.5f, Radius);
	const FVector Lift(0.f, 0.f, HalfHeight - ShrunkHalfHeight);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, ShrunkHalfHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GenDashPath), false, Character);
	FCollisionResponseParams Response;
	Capsule->InitSweepCollisionParams(Params, Response);
	Params.AddIgnoredActor(Character);
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);

	FVector From = Start;
	for (int32 Index = 0; Index < OutPoints.Num(); ++Index)
	{
		const FVector To = OutPoints[Index];
		FHitResult Hit;
		if (!World->SweepSingleByChannel(Hit, From + Lift, To + Lift, FQuat::Identity, Capsule->GetCollisionObjectType(), Shape, Params, Response))
		{
			From = To;
			continue;
		}

		// Mur : ce segment s'arrête devant, les suivants sont abandonnés
		const FVector Direction = (To - From).GetSafeNormal2D();
		FVector Stop = Hit.bStartPenetrating ? From : FVector(Hit.Location) - Lift - Direction * GenDashPrivate::WallBackOff;
		Stop.Z = Start.Z;
		if (FVector::Dist2D(From, Stop) < GenDashPrivate::MinSegmentLength || FVector::DotProduct(Stop - From, Direction) <= 0.f)
		{
			OutPoints.SetNum(Index);
		}
		else
		{
			OutPoints[Index] = Stop;
			OutPoints.SetNum(Index + 1);
		}
		break;
	}
}

bool UGenGA_Dash::GetAimGeometry(const AGenCharacterBase& Caster, int32 Fed, const FVector& Cursor, FGenAimGeometry& Out) const
{
	// Même trajet que le sort (ComputePath, même lacet que FillAimData) : le dessin = le mouvement
	const FVector Start = Caster.GetActorLocation();
	Out = FGenAimGeometry();
	Out.Origin = Start;
	Out.Direction = GenIndicatorRules::FlatDirection(Cursor - Start, Caster.GetActorForwardVector());
	Out.Fed = Fed;

	TArray<FVector> Points;
	ComputePath(&Caster, Start, Out.Direction.Rotation().Yaw, Fed, Points);
	Out.PathPoints.Add(Start);
	Out.PathPoints.Append(Points);

	const float CapsuleRadius = Caster.GetCapsuleComponent() ? Caster.GetCapsuleComponent()->GetScaledCapsuleRadius() : 42.f;
	Out.PathWidth = IndicatorWidth > 0.f ? IndicatorWidth : 2.f * CapsuleRadius;
	return true;
}

void UGenGA_Dash::OnCastLaunched(const FGenCastRelease& Release)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		FinishAbility();
		return;
	}

	// Trajet décidé ici, une fois (visée verrouillée), depuis la position de chaque machine avec le même lacet
	const FVector Start = Character->GetActorLocation();
	const float Yaw = ResolveDashYaw(Release, Start);
	ComputePath(Character, Start, Yaw, Release.Fed, PathPoints);

	const GenDashRules::FZigzagParams Params = GetZigzagParams();
	SegmentDurations.Reset(PathPoints.Num());
	float Planned = 0.f;
	FVector From = Start;
	for (int32 Index = 0; Index < PathPoints.Num(); ++Index)
	{
		const float Length = static_cast<float>(FVector::Dist2D(From, PathPoints[Index]));
		const float Duration = GenDashRules::GetSegmentDuration(Length, GenDashRules::GetSegmentLength(Index, Params), SegmentDuration);
		SegmentDurations.Add(Duration);
		Planned += Duration;
		From = PathPoints[Index];
	}

	UE_LOG(LogGenDash, Verbose, TEXT("[%s] %s : ruée de %d segment(s), lacet %.1f° (nourri %d), %.2fs"),
		CurrentActorInfo && CurrentActorInfo->IsNetAuthority() ? TEXT("SERVEUR") : TEXT("CLIENT"), *GetName(), PathPoints.Num(), Yaw, Release.Fed, Planned);

	if (PathPoints.Num() == 0)
	{
		FinishAbility(); // collé à un mur dans la direction visée : rien à parcourir
		return;
	}

	bDashing = true;
	// Le serveur ne refuse les sorts du client que pendant la ruée la plus courte possible (le premier segment : un segment
	// qui n'arrive pas arrête la ruée à sa fin) ; SetCastLock appelle NoteCastLock (jamais de tag posé à la main)
	SetCastLock(true, SegmentDurations[0]);
	// Fin prévue de la ruée (tampon des appuis du client : un appui dans ses derniers instants attend)
	if (UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		GenASC->NoteLocalCastLockDuration(Planned);
	}

	if (TrailCueTag.IsValid())
	{
		K2_AddGameplayCue(TrailCueTag, MakeEffectContext(CurrentSpecHandle, CurrentActorInfo), /*bRemoveOnAbilityEnd*/ true);
	}

	UAbilityTask_WaitDelay* SafetyTask = UAbilityTask_WaitDelay::WaitDelay(this, Planned + GenDashPrivate::SafetyMargin);
	SafetyTask->OnFinish.AddDynamic(this, &ThisClass::OnSafetyNet);
	SafetyTask->ReadyForActivation();

	CurrentSegment = INDEX_NONE;
	StartNextSegment();
}

void UGenGA_Dash::StartNextSegment()
{
	++CurrentSegment;
	if (!bDashing || !PathPoints.IsValidIndex(CurrentSegment))
	{
		EndDash();
		return;
	}

	// Mouvement racine prédit (client et serveur), mêmes paramètres des deux côtés (ResolveDashYaw). Vitesse bornée à celle
	// prévue (bRestrictSpeedToExpected : pas de rattrapage brutal après un obstacle), vitesse nulle à la fin du segment :
	// le suivant repart de là, la rotation n'est jamais touchée (gen.AlwaysFaceAim)
	SegmentTask = UAbilityTask_ApplyRootMotionMoveToForce::ApplyRootMotionMoveToForce(
		this, NAME_None, PathPoints[CurrentSegment], SegmentDurations[CurrentSegment],
		/*bSetNewMovementMode*/ false, MOVE_Walking, /*bRestrictSpeedToExpected*/ true, /*PathOffsetCurve*/ nullptr,
		ERootMotionFinishVelocityMode::SetVelocity, FVector::ZeroVector, 0.f);
	SegmentTask->OnTimedOutAndDestinationReached.AddDynamic(this, &ThisClass::OnSegmentReached);
	SegmentTask->OnTimedOut.AddDynamic(this, &ThisClass::OnSegmentBlocked);
	SegmentTask->ReadyForActivation();
}

void UGenGA_Dash::OnSegmentReached()
{
	SegmentTask = nullptr; // la tâche se termine d'elle-même après ce signal
	StartNextSegment();
}

void UGenGA_Dash::OnSegmentBlocked()
{
	SegmentTask = nullptr;
	UE_LOG(LogGenDash, Verbose, TEXT("%s : segment %d bloqué, fin de la ruée"), *GetName(), CurrentSegment);
	EndDash();
}

void UGenGA_Dash::OnSafetyNet()
{
	if (bDashing)
	{
		const AActor* Avatar = GetAvatarActorFromActorInfo();
		UE_LOG(LogGenDash, Warning, TEXT("[%s] %s : filet de sécurité de la ruée atteint (segment %d/%d)"),
			Avatar && Avatar->HasAuthority() ? TEXT("SERVEUR") : TEXT("CLIENT"), *GetName(), CurrentSegment + 1, PathPoints.Num());
		EndDash();
	}
}

void UGenGA_Dash::StopSegmentTask()
{
	if (SegmentTask)
	{
		UAbilityTask_ApplyRootMotionMoveToForce* Task = SegmentTask;
		SegmentTask = nullptr;
		Task->EndTask(); // retire la source de mouvement racine
	}
}

void UGenGA_Dash::EndDash()
{
	if (!bDashing)
	{
		return; // segments, blocage et filet : une seule fois
	}
	bDashing = false;
	StopSegmentTask();
	SetCastLock(false);
	FinishAbility();
}

void UGenGA_Dash::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Fin en pleine ruée (mort) : la force s'arrête, le verrou est retiré par la base
	bDashing = false;
	StopSegmentTask();
	PathPoints.Reset();
	SegmentDurations.Reset();
	CurrentSegment = INDEX_NONE;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

#define LOCTEXT_NAMESPACE "GenGA_Dash"

void UGenGA_Dash::GetTooltipArgs(FFormatNamedArguments& Args) const
{
	Super::GetTooltipArgs(Args);
	Args.Add(TEXT("Range"), GenAbilityTooltip::Meters(BaseDistance));
	Args.Add(TEXT("Segment"), GenAbilityTooltip::Meters(SegmentDistance));
	Args.Add(TEXT("Angle"), GenAbilityTooltip::Number(ZigzagAngle));
	Args.Add(TEXT("DashTime"), GenAbilityTooltip::Seconds(SegmentDuration));
}

FText UGenGA_Dash::GetFeedTooltipLines(int32 Fed) const
{
	// Le décollage dure le nourrissage (Fed intervalles) puis l'incantation ; un segment de plus par unité
	const GenDashRules::FZigzagParams Params = GetZigzagParams();
	const int32 Segments = GenDashRules::GetSegmentCount(Fed, Params.MaxSegments);
	return FText::Format(LOCTEXT("TakeOffPath", "décollage {0}, {1} {1}|plural(one=segment,other=segments) ({2})"),
		GenAbilityTooltip::Seconds(Fed * FeedInterval + CastTime), Segments, GenAbilityTooltip::Meters(GenDashRules::GetPathLength(Fed, Params)));
}

FText UGenGA_Dash::GetCompactTooltipLine() const
{
	const int32 Top = GetTooltipMaxFeed();
	if (Top <= 0)
	{
		return Super::GetCompactTooltipLine();
	}
	// Mêmes valeurs que GetFeedTooltipLines : décollage puis longueur du trajet, par seuil
	const GenDashRules::FZigzagParams Params = GetZigzagParams();
	TArray<float> TakeOff;
	TArray<float> Length;
	for (int32 Fed = 0; Fed <= Top; ++Fed)
	{
		TakeOff.Add(Fed * FeedInterval + CastTime);
		Length.Add(GenDashRules::GetPathLength(Fed, Params));
	}
	return GenAbilityTooltip::FeedSummary(Top, {
		FText::Format(LOCTEXT("TakeOffSeries", "décollage {0}"), GenAbilityTooltip::SecondsSeries(TakeOff)),
		FText::Format(LOCTEXT("PathSeries", "trajet {0}"), GenAbilityTooltip::MetersSeries(Length)) });
}

void UGenGA_Dash::GetTooltipEffectLines(TArray<FText>& OutLines) const
{
	OutLines.Add(FText::Format(LOCTEXT("Dash", "Ruée de {0}, puis un segment de {1} par flamme en zigzag (±{2}°)"),
		GenAbilityTooltip::Meters(BaseDistance), GenAbilityTooltip::Meters(SegmentDistance), GenAbilityTooltip::Number(ZigzagAngle)));
	OutLines.Add(LOCTEXT("DashRules", "Sans sort pendant la ruée ; un contrôle dur ne l'arrête pas ; s'arrête au premier mur"));
}

#undef LOCTEXT_NAMESPACE
