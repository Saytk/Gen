#include "AbilitySystem/Abilities/GenGA_Cast.h"

#include "Abilities/Tasks/AbilityTask_NetworkSyncPoint.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystem/Effects/GenGE_Gain.h"
#include "AbilitySystem/GenAbilitySystemComponent.h"
#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystem/GenFeeding.h"
#include "AbilitySystem/GenMontageTiming.h"
#include "AbilitySystem/GenTargetData.h"
#include "AbilitySystem/GenWorldQueries.h"
#include "AbilitySystem/Tasks/GenAbilityTask_TargetDataUnderCursor.h"
#include "AbilitySystemComponent.h"
#include "Actors/GenGroundArea.h"
#include "Actors/GenProjectile.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/GenCharacterBase.h"
#include "Champions/Curffe/CurffeTuning.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GenGameplayTags.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenCast, Log, All);

namespace GenCastPrivate
{
	/** Clé du ralenti d'incantation parmi les multiplicateurs locaux du personnage. */
	FName GenCastSlowReason()
	{
		static const FName Reason(TEXT("CastSlow"));
		return Reason;
	}
}
using namespace GenCastPrivate;

#define GEN_CAST_LOG(Verbosity, Format, ...) UE_LOG(LogGenCast, Verbosity, TEXT("[%s] %s: " Format), (CurrentActorInfo && CurrentActorInfo->IsNetAuthority()) ? TEXT("SERVEUR") : TEXT("CLIENT"), *GetName(), ##__VA_ARGS__)

UGenGA_Cast::UGenGA_Cast()
{
	// Curffe est le seul champion qui nourrit ses sorts pour l'instant : ses règles servent de défaut
	FeedInterval = CurffeTuning::FeedInterval;
	MaxFeed = CurffeTuning::MaxFeedPerSpell;

	ActivationOwnedTags.AddTag(GenGameplayTags::State_Casting);
}

void UGenGA_Cast::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Pas de CommitAbility ici : le cooldown et le coût ne sont appliqués qu'au lancer
	// (ReleaseCast). CanActivateAbility les a déjà vérifiés avant l'activation.
	GEN_CAST_LOG(Verbose, "Activé (clé %s), incantation %.2fs%s", *ActivationInfo.GetActivationPredictionKey().ToString(), CastTime, bFeedable ? TEXT(", nourrissable") : TEXT(""));

	FedCount = 0;
	FedVisualCount = 0;
	FeedSlotsAtPress = 0;
	ActiveFeedInterval = FeedInterval; // figé par StartFeeding (nourrissage rapide)
	ServerFeedElapsed = 0.f;
	ReportedFedCount = INDEX_NONE;
	bInterruptWatchStarted = false;
	bServerShotLocked = false;
	bReleased = false;
	// Une fin de sort sans ASC (ou un ClearCastLock à la mort) a pu laisser l'instance croire qu'elle tient le verrou
	bCastLockApplied = false;
	PendingAimData.Clear();
	DeferredAimKey = FPredictionKey();
	AimTask = nullptr;

	// Un seul sort incanté à la fois : celui-ci remplace l'incantation en cours (un sort déjà parti continue)
	CancelOtherPendingCasts();

	// Contrôles durs surveillés pendant TOUTE l'activation, quel que soit le déroulé (même sans nourrissage ni
	// incantation) : OnCastInterrupted décide selon la phase (IsInterruptedByHardCC)
	StartInterruptWatch();

	if (bFeedable)
	{
		StartFeeding();
	}
	else if (CastTime > 0.f)
	{
		StartCasting();
	}
	else
	{
		OnCastFinished();
	}
}

void UGenGA_Cast::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (IsActive())
	{
		GEN_CAST_LOG(Verbose, "Fin (annulé=%d, répliqué=%d)", bWasCancelled, bReplicateEndAbility);
	}

	// Annulation en plein nourrissage ou incantation (contrôle dur, mort, autre sort...) : on nettoie.
	// La ressource nourrie n'est dépensée qu'au lancer : rien à rendre.
	StopCasting();
	SetCastLock(false);

	// Départ différé abandonné (contrôle dur, mort) : on acquitte enfin la clé de la visée, le client retire
	// le cooldown et la dépense de flammes qu'il avait prédits (rien n'a été payé côté serveur)
	AcknowledgeDeferredAim();
	PendingAimData.Clear();
	bServerShotLocked = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UGenGA_Cast::CanBeCanceled() const
{
	// Serveur, visée du client reçue (départ différé si elle est arrivée en avance) : le client a déjà lancé.
	// La boule de feu relancée par son clic maintenu (ou tout autre sort lancé après) arrive juste derrière
	// la visée et ne doit pas annuler ce sort. Ce verrou ne protège que des autres sorts du joueur : un contrôle
	// dur annule quand même le départ différé (OnCastInterrupted le termine directement), la mort au départ.
	return !bServerShotLocked && Super::CanBeCanceled();
}

void UGenGA_Cast::AcknowledgeDeferredAim()
{
	if (!DeferredAimKey.IsValidKey())
	{
		return;
	}

	// Fenêtre vide sur la clé de la visée : en sortant, elle l'acquitte (réplication de la clé au client)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FScopedPredictionWindow Acknowledge(ASC, DeferredAimKey);
	}
	DeferredAimKey = FPredictionKey();
}

bool UGenGA_Cast::IsServerForRemoteClient() const
{
	return CurrentActorInfo && CurrentActorInfo->IsNetAuthority() && !IsLocallyControlled();
}

void UGenGA_Cast::CancelOtherPendingCasts()
{
	// Même règle que la touche d'annulation (un sort déjà parti n'est jamais annulé, ni un sort verrouillé côté serveur :
	// CanBeCanceled faux), sauf ce sort-ci
	if (UGenAbilitySystemComponent* ASC = Cast<UGenAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		ASC->CancelPendingCasts(this);
	}
}

void UGenGA_Cast::ApplyReportedFedCount(int32 Reported)
{
	// Visée déjà reçue (ou sort lancé) : le compte validé au lancer fait foi, une annonce en retard ne le change plus
	if (!bFeedable || !IsServerForRemoteClient() || bServerShotLocked || bReleased)
	{
		return;
	}

	// On garde l'annonce brute : tant que le nourrissage continue, sa borne de temps monte à chaque tick
	ReportedFedCount = Reported;
	const int32 Shown = ReconcileFedVisual();
	if (Shown != Reported)
	{
		GEN_CAST_LOG(Verbose, "Compte annoncé par le client : %d, affiché %d (estimation %d)", Reported, Shown, FedCount);
	}

	// L'annonce (RPC du personnage) et le signal de fin (RPC de l'ASC) n'ont pas d'ordre garanti :
	// arrivée après la fin du nourrissage, elle corrige la barre vue par les autres joueurs (sans la faire reculer)
	if (!bIsFeeding)
	{
		MarkFeedEnded(Shown);
	}
}

int32 UGenGA_Cast::ReconcileFedVisual()
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float Available = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetResourceAttribute()) : 0.f;

	// Même borne de temps que le lancer : pendant le nourrissage le temps écoulé, ensuite celui mesuré à la synchro
	const float Elapsed = bIsFeeding ? GetWorld()->GetTimeSeconds() - FeedStartTime : ServerFeedElapsed;
	const int32 Shown = GenFeeding::ReconcileDisplayedFed(FedVisualCount, FedCount, ReportedFedCount,
		FMath::Min(MaxFeed, FeedSlotsAtPress), Available, Elapsed, ActiveFeedInterval);
	SetFedVisual(Shown);
	return Shown;
}

int32 UGenGA_Cast::GetAvailableFeed() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float Available = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetResourceAttribute()) : 0.f;
	return GenFeeding::GetFeedLimit(MaxFeed, Available);
}

void UGenGA_Cast::StartFeeding()
{
	bIsFeeding = true;
	FeedStartTime = GetWorld()->GetTimeSeconds();

	// Intervalle figé pour tout ce nourrissage (Combustion qui commence ou finit pendant l'appui n'y change rien).
	// Chaque machine prend le sien à son activation. Revue V2-V4, I1 : la tolérance de ValidateFedCount n'absorbe PAS
	// un désaccord à la fin de l'embrasement (le client s'arrête seul au plafond, 0.45 s, et le serveur validerait au
	// rythme normal : 3 -> 2). Le serveur d'un client distant prend donc aussi l'intervalle rapide si le tag a été
	// retiré juste avant son activation (fenêtre de grâce, GenFeeding::ServerTagGrace). Cas symétrique au début de
	// l'embrasement (tag prédit par le client, pas encore posé sur le serveur) : traité à la fin du nourrissage
	// (OnFeedSynced), si le tag est posé juste après l'activation.
	const UAbilitySystemComponent* FeedASC = GetAbilitySystemComponentFromActorInfo();
	const bool bFastFeeding = (FeedASC && FeedASC->HasMatchingGameplayTag(GenGameplayTags::State_FastFeeding)) || IsFastFeedingInGrace(/*bAddedAfterActivation*/ false);
	ActiveFeedInterval = GenFeeding::GetFeedInterval(FeedInterval, bFastFeeding);

	// Unités disponibles à l'appui : autant de crans sur la barre, et jamais plus d'unités nourries
	// (une flamme régénérée pendant l'appui ne s'ajoute pas)
	FeedSlotsAtPress = GetAvailableFeed();

	ApplyCastSlow();
	StartInterruptWatch();

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		// Une seule barre de l'appui au lancer : un cran par unité disponible, repliée à la fin du
		// nourrissage (OnFeedSynced) puis prolongée par l'incantation sans redémarrer
		Character->StartFeedCast(GetClass(), FeedSlotsAtPress, ActiveFeedInterval, CastTime, CastFX, CastFXSocket);
	}

	// V3 : geste de nourrissage dès l'appui, sur le client du lanceur (prédit) ET sur le serveur (copie répliquée aux
	// autres joueurs, Art Bible §12 Q41). Une section par seuil : Feed_1 dure un intervalle de base, x2 si rapide.
	if (FeedMontage && FeedSlotsAtPress > 0)
	{
		if (IsServerForRemoteClient())
		{
			// Revue V2-V4, I2 : les autres joueurs voient les seuils sur l'estimation du serveur, en retard de
			// ServerEstimateLag (ScheduleFeedTick) : le geste part avec le même retard, pose et pop restent alignés
			FeedMontageDelayTask = UAbilityTask_WaitDelay::WaitDelay(this, GenFeeding::ServerEstimateLag);
			FeedMontageDelayTask->OnFinish.AddDynamic(this, &ThisClass::OnFeedMontageDelayFinished);
			FeedMontageDelayTask->ReadyForActivation();
		}
		else
		{
			PlayFeedMontage();
		}
	}

	if (IsLocallyControlled())
	{
		if (FeedSlotsAtPress == 0)
		{
			StopFeedingLocal(); // rien à nourrir : on incante directement
			return;
		}

		FeedReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, /*bTestAlreadyReleased*/ true);
		FeedReleaseTask->OnRelease.AddDynamic(this, &ThisClass::OnFeedInputReleased);
		FeedReleaseTask->ReadyForActivation();

		// Un simple appui (déjà relâché) a pu terminer le nourrissage pendant ReadyForActivation
		if (bIsFeeding)
		{
			ScheduleFeedTick();
		}
	}
	else
	{
		// Serveur pour un client distant : c'est le client qui annonce la fin du nourrissage
		UAbilityTask_NetworkSyncPoint* SyncTask = UAbilityTask_NetworkSyncPoint::WaitNetSync(this, EAbilityTaskNetSyncType::OnlyServerWait);
		SyncTask->OnSync.AddDynamic(this, &ThisClass::OnFeedSynced);
		SyncTask->ReadyForActivation();

		// Estimation cosmétique pour les autres joueurs (les flammes quittent l'orbite)
		ScheduleFeedTick();
	}
}

void UGenGA_Cast::PlayFeedMontage()
{
	if (!FeedMontage)
	{
		return;
	}

	// Revue V2-V4, M6 : Feed_1 par son nom (l'ordre des sections dépend du script qui a créé le montage)
	const int32 FirstSection = FeedMontage->GetSectionIndex(TEXT("Feed_1"));
	const float StepLength = FeedMontage->GetSectionLength(FirstSection != INDEX_NONE ? FirstSection : 0);
	const float Rate = GetPhaseRate(FeedMontage, StepLength, ActiveFeedInterval, GenMontageTiming::GetExpectedFeedRate(FeedInterval, ActiveFeedInterval));
	PlayPhaseMontage(FeedMontage, Rate, /*bStopWhenAbilityEnds*/ true);
}

void UGenGA_Cast::OnFeedMontageDelayFinished()
{
	FeedMontageDelayTask = nullptr;
	if (bIsFeeding)
	{
		PlayFeedMontage();
	}
}

void UGenGA_Cast::StopFeedMontage()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!FeedMontage || !ASC)
	{
		return;
	}

	if (ASC->GetCurrentMontage() == FeedMontage)
	{
		ASC->CurrentMontageStop(0.1f); // répliqué aux autres joueurs
		return;
	}

	// Plus le montage courant de l'ASC (un autre montage l'a remplacé dans un autre groupe de slots) : arrêt local,
	// sur le serveur et le client du lanceur. Les autres clients ne reçoivent qu'un montage : d'où l'avertissement
	// de PlayPhaseMontage et la vérification des assets (même groupe pour toutes les phases d'un sort).
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = Info ? Info->GetAnimInstance() : nullptr;
	if (AnimInstance && AnimInstance->Montage_IsPlaying(FeedMontage))
	{
		AnimInstance->Montage_Stop(0.1f, FeedMontage);
	}
}

bool UGenGA_Cast::IsFastFeedingInGrace(bool bAddedAfterActivation) const
{
	// Hôte, client, IA : c'est le côté qui prédit, ses tags font foi
	if (!IsServerForRemoteClient())
	{
		return false;
	}
	const UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
	return GenASC && GenASC->WasGraceTagChangedNear(GenGameplayTags::State_FastFeeding, bAddedAfterActivation, FeedStartTime);
}

void UGenGA_Cast::ScheduleFeedTick()
{
	// Calé sur le début du nourrissage : les retards des ticks ne s'additionnent pas.
	// Serveur pour un client distant : estimation volontairement en retard (le client la corrige vers le haut).
	const float EstimateStart = FeedStartTime + (IsServerForRemoteClient() ? GenFeeding::ServerEstimateLag : 0.f);
	const float Delay = GenFeeding::GetNextFeedTickDelay(EstimateStart, FedCount, ActiveFeedInterval, GetWorld()->GetTimeSeconds());
	FeedTickTask = UAbilityTask_WaitDelay::WaitDelay(this, Delay);
	FeedTickTask->OnFinish.AddDynamic(this, &ThisClass::OnFeedTick);
	FeedTickTask->ReadyForActivation();
}

void UGenGA_Cast::OnFeedTick()
{
	if (!bIsFeeding)
	{
		return;
	}

	const int32 Limit = FMath::Min(GetAvailableFeed(), FeedSlotsAtPress);
	if (FedCount < Limit)
	{
		++FedCount;

		// Serveur pour un client distant : estimation, ou annonce du client si elle est déjà arrivée
		// (sa borne de temps a monté), sans jamais faire reculer l'affichage
		if (IsServerForRemoteClient())
		{
			ReconcileFedVisual();
		}
		else
		{
			SetFedVisual(FedCount);
		}
	}

	if (FedCount >= Limit)
	{
		if (IsLocallyControlled())
		{
			StopFeedingLocal(); // plus rien à absorber : l'incantation enchaîne même si la touche reste enfoncée
		}
		return; // serveur : attend le signal du client
	}

	ScheduleFeedTick();
}

void UGenGA_Cast::OnFeedInputReleased(float TimeHeld)
{
	if (bIsFeeding)
	{
		StopFeedingLocal();
	}
}

void UGenGA_Cast::StopFeedingLocal()
{
	if (!bIsFeeding)
	{
		return;
	}

	// Seuil déjà franchi mais tick pas encore traité (le relâché est lu avant le minuteur de cette image) :
	// l'unité est acquise, la barre l'a déjà montrée. Le serveur l'accepte (borne de temps + 1).
	const int32 Limit = FMath::Min(GetAvailableFeed(), FeedSlotsAtPress);
	if (FedCount < Limit && ActiveFeedInterval > 0.f && GetWorld()->GetTimeSeconds() >= FeedStartTime + (FedCount + 1) * ActiveFeedInterval)
	{
		++FedCount;
		SetFedVisual(FedCount);
	}

	EndFeedTasks();

	// Client distant : le serveur n'a qu'une estimation des unités nourries (son minuteur peut avoir un
	// tick de retard, surtout quand le client s'arrête pile au maximum). On lui annonce le compte exact
	// pour que les autres joueurs voient le bon nombre de flammes quitter l'orbite pendant l'incantation.
	// Envoyé même à 0 : l'estimation du serveur peut déjà en être à 1.
	if (CurrentActorInfo && !CurrentActorInfo->IsNetAuthority())
	{
		if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
		{
			Character->ServerReportFedResource(GetClass(), static_cast<uint8>(FMath::Clamp(FedCount, 0, 255)));
		}
	}

	// Client : envoie le signal au serveur et continue sans attendre. Hôte : se termine aussitôt.
	UAbilityTask_NetworkSyncPoint* SyncTask = UAbilityTask_NetworkSyncPoint::WaitNetSync(this, EAbilityTaskNetSyncType::OnlyServerWait);
	SyncTask->OnSync.AddDynamic(this, &ThisClass::OnFeedSynced);
	SyncTask->ReadyForActivation();
}

void UGenGA_Cast::OnFeedSynced()
{
	if (!bIsFeeding)
	{
		return;
	}

	bIsFeeding = false;
	EndFeedTasks();

	if (CurrentActorInfo && CurrentActorInfo->IsNetAuthority())
	{
		ServerFeedElapsed = GetWorld()->GetTimeSeconds() - FeedStartTime;

		// Revue V2-V4, I1, cas symétrique : State.FastFeeding posé sur le serveur juste APRÈS son activation (début de
		// l'embrasement : le client l'avait déjà, prédit). La validation se fait sur l'intervalle rapide.
		const float FastInterval = GenFeeding::GetFeedInterval(FeedInterval, true);
		if (ActiveFeedInterval > FastInterval && IsFastFeedingInGrace(/*bAddedAfterActivation*/ true))
		{
			GEN_CAST_LOG(Verbose, "Nourrissage rapide posé juste après l'activation : validation à %.2fs par unité", FastInterval);
			ActiveFeedInterval = FastInterval;
		}
	}

	// Serveur pour un client distant : FedCount n'est que son estimation, le compte validé est
	// journalisé au lancer (ResolveFedCount)
	GEN_CAST_LOG(Verbose, "Nourrissage terminé%s : %d (%.2fs)", IsServerForRemoteClient() ? TEXT(" (estimation du serveur)") : TEXT(""),
		FedCount, GetWorld()->GetTimeSeconds() - FeedStartTime);

	// Barre : les segments inutilisés se replient. Compte affiché = unités qui quittent l'orbite
	// (serveur pour un client distant : son estimation, ou l'annonce du client si elle est déjà arrivée,
	// rebornée au temps mesuré). L'annonce du client arrivée après coup corrige la barre (ApplyReportedFedCount).
	if (IsServerForRemoteClient())
	{
		ReconcileFedVisual();
	}
	MarkFeedEnded(FedVisualCount);

	// V3 : la dernière section du geste de nourrissage boucle (sécurité). Seul un montage du MÊME groupe de slots le
	// remplace en partant : sans ChargeMontage à suivre, ou avec une charge dans un autre groupe (revue V2-V4, I3),
	// on l'arrête explicitement, sinon il continuerait jusqu'au lancer ou à la fin du sort
	if (FeedMontage)
	{
		const UAnimMontage* NextPhase = CastTime > 0.f ? ChargeMontage.Get() : nullptr;
		if (!NextPhase || NextPhase->GetGroupName() != FeedMontage->GetGroupName())
		{
			StopFeedMontage();
		}
	}

	if (CastTime > 0.f)
	{
		StartCasting();
	}
	else
	{
		OnCastFinished();
	}
}

void UGenGA_Cast::EndFeedTasks()
{
	if (FeedTickTask)
	{
		FeedTickTask->EndTask();
		FeedTickTask = nullptr;
	}
	if (FeedReleaseTask)
	{
		FeedReleaseTask->EndTask();
		FeedReleaseTask = nullptr;
	}
	if (FeedMontageDelayTask)
	{
		FeedMontageDelayTask->EndTask();
		FeedMontageDelayTask = nullptr;
	}
}

void UGenGA_Cast::EndAimTask()
{
	if (AimTask)
	{
		UGenAbilityTask_TargetDataUnderCursor* Task = AimTask;
		AimTask = nullptr;
		Task->EndTask();
	}
}

void UGenGA_Cast::MarkFeedEnded(int32 Count, bool bFinal)
{
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->MarkFeedEnded(GetClass(), Count, bFinal); // sans effet si la barre n'est plus la nôtre
	}
}

void UGenGA_Cast::SetFedVisual(int32 Count)
{
	FedVisualCount = Count;
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->SetFedResource(this, static_cast<uint8>(FMath::Clamp(Count, 0, 255)));
	}
}

void UGenGA_Cast::StartCasting()
{
	// Sort nourri : la barre du nourrissage continue (StartFeedCast), jamais de seconde barre
	if (!bFeedable)
	{
		if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
		{
			Character->StartCast(GetClass(), CastTime, CastFX, CastFXSocket);
		}
	}

	ApplyCastSlow(); // sans effet s'il est déjà actif depuis le nourrissage
	StartInterruptWatch();

	// Le geste continue après la fin normale du sort (le lancer tombe à la fin de l'incantation).
	// Une annulation (contrôle dur, mort) le coupe quand même : la tâche écoute OnGameplayAbilityCancelled.
	// V3 : calé sur CastTime quand le lancer est un CastMontage à part ; montage unique du Plan 1 : vitesse 1.
	if (ChargeMontage)
	{
		const float Rate = GenMontageTiming::ShouldScaleChargeToCastTime(bScaleChargeMontageToCastTime, CastMontage != nullptr, CastTime)
			? GetPhaseRate(ChargeMontage, ChargeMontage->GetPlayLength(), CastTime, 1.f)
			: 1.f;
		PlayPhaseMontage(ChargeMontage, Rate, /*bStopWhenAbilityEnds*/ false);
	}

	CastStartTime = GetWorld()->GetTimeSeconds();

	if (IsServerForRemoteClient())
	{
		// Serveur pour un client distant : pas de minuteur séparé. Le serveur démarre l'incantation une
		// latence après le client, donc un minuteur finissait à peu près quand la visée arrivait ; la boule
		// de feu relancée par le clic maintenu, envoyée juste après, annulait alors un sort déjà lancé côté
		// client. On écoute la visée dès maintenant : c'est elle qui termine l'incantation (OnServerAimReceived).
		AimTask = UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(this);
		AimTask->ValidData.AddDynamic(this, &ThisClass::OnServerAimReceived);
		AimTask->ReadyForActivation();
		return;
	}

	UAbilityTask_WaitDelay* CastTask = UAbilityTask_WaitDelay::WaitDelay(this, CastTime);
	CastTask->OnFinish.AddDynamic(this, &ThisClass::OnCastFinished);
	CastTask->ReadyForActivation();
}

void UGenGA_Cast::OnServerAimReceived(const FGameplayAbilityTargetDataHandle& DataHandle)
{
	// Une seule visée par activation : une seconde visée (client modifié, renvoi) ne lance jamais deux fois
	if (bServerShotLocked || bReleased)
	{
		GEN_CAST_LOG(Verbose, "Visée en double ignorée");
		return;
	}

	// Le client a lancé à la fin de SON incantation. Les RPC du joueur arrivent dans l'ordre : un nouvel
	// appui qui annule ce sort côté client arrive avant toute visée, un sort lancé après le lancer arrive
	// après. À partir d'ici, un autre sort du joueur ne peut donc plus annuler ce lancer.
	bServerShotLocked = true;
	EndAimTask(); // les visées suivantes ne sont plus écoutées

	const float Elapsed = GetWorld()->GetTimeSeconds() - CastStartTime;
	const float Wait = GenFeeding::GetServerCastWait(CastTime, Elapsed, GenFeeding::CastTimeTolerance);
	if (Wait <= 0.f)
	{
		// Cas normal : le client a fini d'incanter, ralenti et barre de cast s'arrêtent maintenant ; le sort part
		// pendant le RPC de la visée, avant tout sort envoyé après
		GEN_CAST_LOG(Verbose, "Visée reçue après %.3fs d'incantation (%.2fs demandées)", Elapsed, CastTime);
		EndCastPresentation();
		OnTargetDataReady(DataHandle);
		return;
	}

	// Visée trop tôt (triche, ou activation retardée par une perte de paquet). Le serveur garde toute l'incantation
	// jusqu'à SA fin (CastTime - tolérance) : barre de cast et effet de main vus par les autres, ralenti, interruption
	// par un contrôle dur. Cooldown et ressource ne sont payés qu'au départ (OnServerLaunchDelayFinished).
	LogEarlyAim(Elapsed, Wait);
	PendingAimData = DataHandle;

	// Compte validé du lancer, connu dès maintenant : c'est le compte final de la barre vue par les autres
	if (bFeedable)
	{
		const int32 Fed = ResolveFedCount(DataHandle.Get(0), /*bLog*/ false);
		SetFedVisual(Fed);
		MarkFeedEnded(Fed, /*bFinal*/ true);
	}

	// Pas d'acquittement de la clé de la visée tant que le sort n'est pas parti : le cooldown et la dépense prédits
	// par le client restent en place jusqu'au commit du serveur (acquitté avec lui), au lieu d'être retirés puis
	// réappliqués. Seulement dans la fenêtre du RPC qui porte cette visée (elle l'acquitterait en sortant).
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(ASC);
	const FPredictionKey AimKey = GenASC ? GenASC->GetReplicatedTargetDataKey(CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey()) : FPredictionKey();
	if (ASC && AimKey.IsValidKey() && ASC->ScopedPredictionKey == AimKey)
	{
		DeferredAimKey = AimKey;
		ASC->ScopedPredictionKey = FPredictionKey(); // la fenêtre du RPC restaure sa clé à la sortie, sans acquitter
	}

	UAbilityTask_WaitDelay* WaitTask = UAbilityTask_WaitDelay::WaitDelay(this, Wait);
	WaitTask->OnFinish.AddDynamic(this, &ThisClass::OnServerLaunchDelayFinished);
	WaitTask->ReadyForActivation();
}

void UGenGA_Cast::LogEarlyAim(float Elapsed, float Wait) const
{
	if (!GenFeeding::IsAimSuspiciouslyEarly(CastTime, Elapsed, GenFeeding::CastTimeTolerance))
	{
		GEN_CAST_LOG(Log, "Visée en avance : %.3fs d'incantation sur %.2fs, départ différé de %.3fs", Elapsed, CastTime, Wait);
		return;
	}

	// Triche ou lien très dégradé : Warning, au plus un toutes les 5 s (tous sorts confondus) avec le nombre de visées tues
	static double LastWarningTime = -1.e9;
	static int32 Suppressed = 0;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastWarningTime < 5.0)
	{
		++Suppressed;
		return;
	}
	GEN_CAST_LOG(Warning, "Visée très en avance : %.3fs d'incantation sur %.2fs (tolérance %.2fs), départ différé de %.3fs (%d autres depuis le dernier avertissement)",
		Elapsed, CastTime, GenFeeding::CastTimeTolerance, Wait, Suppressed);
	LastWarningTime = Now;
	Suppressed = 0;
}

void UGenGA_Cast::OnServerLaunchDelayFinished()
{
	// Mort pendant l'attente : rien ne part, rien n'est payé (le verrou a empêché CancelAllAbilities de couper le sort)
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || ASC->HasMatchingGameplayTag(GenGameplayTags::State_Dead))
	{
		GEN_CAST_LOG(Verbose, "Départ différé abandonné (mort)");
		AbortCastMontages();
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// Fin de l'incantation du serveur : le sort est lancé maintenant, dans la fenêtre de prédiction de la visée.
	// Cooldown et dépense sont répliqués avec l'acquittement de la clé : le client garde ses valeurs prédites
	// jusqu'aux valeurs du serveur, sans trou.
	FScopedPredictionWindow AimWindow(ASC, DeferredAimKey);
	DeferredAimKey = FPredictionKey();

	const FGameplayAbilityTargetDataHandle AimData = PendingAimData;
	PendingAimData.Clear();

	EndCastPresentation();
	OnTargetDataReady(AimData);
}

void UGenGA_Cast::ApplyCastSlow()
{
	if (bCastSlowApplied || CastMoveSpeedMultiplier >= 1.f)
	{
		return;
	}

	// Revue Plan 2 Tasks 7-8, I-4 : multiplicateur local posé par le serveur et le client propriétaire, chacun au début de
	// SON incantation et retiré à SA fin. Un GE prédit restait chez le client jusqu'au retrait du serveur (~1 RTT) :
	// correction du mouvement à chaque fin d'incantation.
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->SetLocalMoveSpeedMultiplier(this, GenCastSlowReason(), CastMoveSpeedMultiplier);
		bCastSlowApplied = true;
	}
}

void UGenGA_Cast::StartInterruptWatch()
{
	if (bInterruptWatchStarted)
	{
		return;
	}
	bInterruptWatchStarted = true;

	// Un contrôle dur (étourdi, silence, peur, neutralisé) interrompt le nourrissage, l'incantation et les phases
	// interruptibles après le départ (la mort annule déjà tous les sorts). Une tâche par tag, jamais terminée par un
	// premier déclenchement : un contrôle ignoré (bond en vol) ne doit pas rendre aveugle aux suivants.
	for (const FGameplayTag& HardCCTag : GenGameplayTags::GetHardCCTags())
	{
		UAbilityTask_WaitGameplayTagAdded* HardCCTask = UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(this, HardCCTag, nullptr, /*OnlyTriggerOnce*/ false);
		HardCCTask->Added.AddDynamic(this, &ThisClass::OnCastInterrupted);
		HardCCTask->ReadyForActivation();
	}
}

void UGenGA_Cast::EndCastPresentation()
{
	if (bCastSlowApplied)
	{
		bCastSlowApplied = false;
		if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
		{
			Character->ClearLocalMoveSpeedMultiplier(this, GenCastSlowReason());
		}
	}

	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StopCast(GetClass()); // sans effet si un autre sort a pris la barre
	}
}

void UGenGA_Cast::StopCasting()
{
	bIsFeeding = false;
	EndFeedTasks();

	// Toutes les écritures de l'affichage (estimation, compte annoncé par le client) passent par SetFedVisual
	if (FedVisualCount > 0)
	{
		SetFedVisual(0);
	}

	EndCastPresentation();
}

void UGenGA_Cast::OnCastInterrupted()
{
	if (!IsActive())
	{
		return;
	}

	if (IsWaitingForDeferredLaunch())
	{
		// Serveur : visée reçue en avance, l'incantation du serveur n'est pas finie. Un contrôle dur l'interrompt
		// comme n'importe quelle incantation : rien ne part, rien n'est payé. CanBeCanceled est faux (verrou du
		// lancer contre les autres sorts du joueur) : on termine le sort directement.
		GEN_CAST_LOG(Log, "Contrôle dur pendant l'incantation (visée reçue en avance) : lancer annulé, sans coût");
		AbortCastMontages();
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	if (!IsInterruptedByHardCC())
	{
		GEN_CAST_LOG(Verbose, "Contrôle dur pendant une phase non interruptible : ignoré");
		return;
	}

	GEN_CAST_LOG(Verbose, "Incantation interrompue (contrôle dur)");
	CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}

void UGenGA_Cast::OnCastFinished()
{
	GEN_CAST_LOG(Verbose, "Incantation terminée, attente de la visée (nourri : %d)", FedCount);
	EndCastPresentation();

	// Visée lue maintenant : le joueur peut ajuster pendant toute l'incantation
	// Client : la visée est lue et diffusée pendant ReadyForActivation (OnTargetDataReady termine la tâche)
	UGenAbilityTask_TargetDataUnderCursor* TargetTask = UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(this, static_cast<uint8>(FMath::Clamp(FedCount, 0, 255)));
	AimTask = TargetTask;
	TargetTask->ValidData.AddDynamic(this, &ThisClass::OnTargetDataReady);
	TargetTask->ReadyForActivation();
}

int32 UGenGA_Cast::ResolveFedCount(const FGameplayAbilityTargetData* Data, bool bLog) const
{
	if (!bFeedable)
	{
		return 0;
	}

	const FGenTargetData_Aim* AimData = (Data && Data->GetScriptStruct() == FGenTargetData_Aim::StaticStruct())
		? static_cast<const FGenTargetData_Aim*>(Data)
		: nullptr;
	const int32 Reported = AimData ? AimData->FedCount : 0;

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float Available = ASC ? ASC->GetNumericAttribute(UGenAttributeSet::GetResourceAttribute()) : 0.f;

	// Jamais plus d'unités que de crans sur la barre (unités disponibles à l'appui)
	const int32 FeedCap = FMath::Min(MaxFeed, FeedSlotsAtPress);

	// Serveur pour un client distant : le client ne peut annoncer ni plus que ce qu'il a, ni plus que le temps écoulé
	if (IsServerForRemoteClient())
	{
		const int32 Validated = GenFeeding::ValidateFedCount(Reported, FeedCap, Available, ServerFeedElapsed, ActiveFeedInterval);
		if (!bLog)
		{
			return Validated;
		}
		if (Validated != Reported)
		{
			GEN_CAST_LOG(Warning, "Nourrissage corrigé par le serveur : %d -> %d (ressource %.0f, %.2fs)", Reported, Validated, Available, ServerFeedElapsed);
		}
		else
		{
			GEN_CAST_LOG(Verbose, "Nourrissage validé : %d (estimation du serveur %d, %.2fs)", Validated, FedCount, ServerFeedElapsed);
		}
		return Validated;
	}

	return FMath::Min(Reported, GenFeeding::GetFeedLimit(FeedCap, Available));
}

void UGenGA_Cast::SpendResource(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}

	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UGenGE_Gain::StaticClass(), GetAbilityLevel());
	if (Spec.IsValid())
	{
		UGenGE_Gain::SetMagnitudes(*Spec.Data, 0.f, -static_cast<float>(Amount));
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, Spec);
	}
}

void UGenGA_Cast::OnTargetDataReady(const FGameplayAbilityTargetDataHandle& DataHandle)
{
	// Une seule visée par activation, quel que soit le chemin (incantation 0 comprise) : un sort déjà lancé ne
	// repasse jamais par le commit (double effet, ou annulation d'une phase déjà partie)
	if (bReleased)
	{
		GEN_CAST_LOG(Verbose, "Visée en double ignorée");
		return;
	}
	EndAimTask(); // les visées suivantes ne sont plus écoutées

	FGenCastRelease Release;
	if (ReleaseCast(DataHandle, Release))
	{
		LaunchCast(Release);
	}
}

bool UGenGA_Cast::ReleaseCast(const FGameplayAbilityTargetDataHandle& DataHandle, FGenCastRelease& OutRelease)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const FGameplayAbilityTargetData* Data = DataHandle.Get(0);
	const FHitResult* Hit = Data ? Data->GetHitResult() : nullptr;

	if (!Avatar || !Hit)
	{
		GEN_CAST_LOG(Warning, "Visée invalide (avatar=%d, hit=%d)", Avatar != nullptr, Hit != nullptr);
		AbortCastMontages();
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return false;
	}

	const int32 Fed = ResolveFedCount(Data);
	const FGenTargetData_Aim* AimData = (Data && Data->GetScriptStruct() == FGenTargetData_Aim::StaticStruct())
		? static_cast<const FGenTargetData_Aim*>(Data)
		: nullptr;

	// Le sort part : on applique cooldown, coût et dépense de la ressource maintenant, pour qu'une
	// incantation interrompue (annulée, contrôle dur, mort) ne coûte rien. Le client est dans la fenêtre
	// de prédiction ouverte par la tâche de visée, le serveur dans celle de la clé reçue.
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		GEN_CAST_LOG(Verbose, "CommitAbility a échoué au lancer");
		AbortCastMontages();
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return false;
	}

	// Flammes illimitées (State.FreeResource, ex. Combustion) : les unités nourries reviennent au Foyer.
	// Revue V2-V4, I1 : serveur d'un client distant, tag retiré à moins de ServerTagGrace de CE lancer (fin de
	// l'embrasement) => le client, qui ne le perd que ~½ RTT plus tard, a lancé avec et n'a rien dépensé : gratuit aussi.
	// Référence = le lancer (moment où chaque machine décide), pas l'activation : après un long nourrissage, le client
	// a perdu le tag bien avant de lancer et dépense lui aussi.
	const UGenAbilitySystemComponent* OwnerASC = Cast<UGenAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
	const bool bFreeResource = OwnerASC && (OwnerASC->HasMatchingGameplayTag(GenGameplayTags::State_FreeResource)
		|| (IsServerForRemoteClient() && OwnerASC->WasGraceTagChangedNear(GenGameplayTags::State_FreeResource, /*bAdded*/ false, GetWorld()->GetTimeSeconds())));
	if (!bFreeResource)
	{
		SpendResource(Fed);
	}
	SetFedVisual(0);
	bReleased = true;

	GEN_CAST_LOG(Verbose, "Visée reçue : %s, nourri : %d", *Hit->Location.ToCompactString(), Fed);

	// Revue Plan 2 Tasks 7-8, M-1 : le client honnête vise sur le plan du lanceur (GetCursorLocationOnPlane) ; un Z modifié
	// ne choisit ni l'étage d'une carte à niveaux ni une zone sous le sol (FindFloor part de ce point)
	OutRelease.AimLocation = FVector(Hit->Location.X, Hit->Location.Y, Avatar->GetActorLocation().Z);
	OutRelease.AimDirection = (Hit->Location - Avatar->GetActorLocation()).GetSafeNormal2D();
	if (OutRelease.AimDirection.IsNearlyZero())
	{
		OutRelease.AimDirection = Avatar->GetActorForwardVector().GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
	}
	OutRelease.Fed = Fed;
	if (AimData)
	{
		OutRelease.ClientLeapDistance = AimData->LeapDistance;
		OutRelease.ClientLeapYaw = AimData->LeapYaw;
	}

	// Se tourner vers la cible (client et serveur, pour que la prédiction concorde)
	if (bTurnToAim)
	{
		Avatar->SetActorRotation(OutRelease.AimDirection.Rotation());
	}

	// V3 : vitesse 1 (geste au lancer puis suivi), sauf si la phase lancée a une durée de jeu (vol, fenêtre, forme)
	if (CastMontage)
	{
		const float Target = GetCastMontageTargetDuration();
		const float Rate = Target > 0.f ? GetPhaseRate(CastMontage, CastMontage->GetPlayLength(), Target, 1.f) : 1.f;
		PlayPhaseMontage(CastMontage, Rate, bStopCastMontageWithAbility);
	}

	return true;
}

float UGenGA_Cast::GetPhaseRate(const UAnimMontage* Montage, float AuthoredLength, float TargetDuration, float ExpectedRate) const
{
	const float Rate = GenMontageTiming::GetPlayRate(AuthoredLength, TargetDuration);
#if !UE_BUILD_SHIPPING
	if (TargetDuration > 0.f && GenMontageTiming::ShouldWarn(Rate, ExpectedRate))
	{
		GEN_CAST_LOG(Warning, "%s joué à x%.2f pour %.2fs (attendu x%.2f) : recaler le clip (Art Bible §8.2)",
			*GetNameSafe(Montage), Rate, TargetDuration, ExpectedRate);
	}
#endif
	return Rate;
}

UAbilityTask_PlayMontageAndWait* UGenGA_Cast::PlayPhaseMontage(UAnimMontage* Montage, float Rate, bool bStopWhenAbilityEnds)
{
	if (!Montage)
	{
		return nullptr;
	}

#if !UE_BUILD_SHIPPING
	// Revue V2-V4, I3 : une phase n'en remplace une autre (et n'arrête la boucle du nourrissage chez les autres joueurs)
	// que dans le même groupe de slots. Les assets doivent le respecter (vérifié aussi par le script des montages).
	if (!bWarnedPhaseSlotGroups)
	{
		for (const UAnimMontage* Other : { FeedMontage.Get(), ChargeMontage.Get(), CastMontage.Get() })
		{
			if (Other && Other != Montage && Other->GetGroupName() != Montage->GetGroupName())
			{
				GEN_CAST_LOG(Warning, "%s (groupe de slots %s) et %s (groupe %s) : les phases d'un sort doivent partager un groupe de slots",
					*GetNameSafe(Montage), *Montage->GetGroupName().ToString(), *GetNameSafe(Other), *Other->GetGroupName().ToString());
				bWarnedPhaseSlotGroups = true;
				break;
			}
		}
	}
#endif

	UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Montage, Rate, NAME_None, bStopWhenAbilityEnds, CastMontageRootMotionScale);
	Task->ReadyForActivation();
	return Task;
}

void UGenGA_Cast::StopClientCastMontages()
{
	// Seul le serveur d'un client distant est concerné : un hôte ou une IA n'a rien joué d'avance
	AGenCharacterBase* Character = IsServerForRemoteClient() ? GetGenCharacterFromActorInfo() : nullptr;
	if (!Character)
	{
		return;
	}

	for (UAnimMontage* Montage : { FeedMontage.Get(), ChargeMontage.Get(), CastMontage.Get() })
	{
		if (Montage)
		{
			Character->ClientStopCastMontage(Montage);
		}
	}
}

void UGenGA_Cast::AbortCastMontages()
{
	// Une fin directe (EndAbility annulé, sans CancelAbility) n'arrête pas les montages joués avec bStopWhenAbilityEnds = faux
	StopClientCastMontages();

	// Serveur : le geste répliqué aux autres joueurs (montage du GAS) s'arrête aussi
	if (CurrentActorInfo && CurrentActorInfo->IsNetAuthority())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			for (const UAnimMontage* Montage : { ChargeMontage.Get(), CastMontage.Get() })
			{
				if (Montage)
				{
					ASC->StopMontageIfCurrent(*Montage);
				}
			}
		}
	}
}

void UGenGA_Cast::LaunchCast(const FGenCastRelease& Release)
{
	OnCastLaunched(Release);

	// Le sort est parti : s'il reste actif (fenêtre de contre, bond...), la mort et les autres sorts
	// peuvent de nouveau l'annuler côté serveur
	if (IsActive())
	{
		bServerShotLocked = false;
	}
}

void UGenGA_Cast::OnCastLaunched(const FGenCastRelease& Release)
{
	FinishAbility();
}

void UGenGA_Cast::FinishAbility()
{
	if (!IsActive())
	{
		return;
	}

	// Le client ne réplique PAS la fin du sort : un EndAbility répliqué pourrait arriver au serveur
	// avant qu'il ait traité la visée et fait partir le sort. C'est le serveur qui termine (à la réception
	// de la visée, ou au départ différé) et prévient le client.
	const bool bReplicateEnd = CurrentActorInfo && CurrentActorInfo->IsNetAuthority();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, bReplicateEnd, false);
}

void UGenGA_Cast::SetCastLock(bool bLocked, float MinLockDuration)
{
	if (bCastLockApplied == bLocked)
	{
		return;
	}

	// Tag local (non répliqué) : le serveur et le client exécutent tous deux le sort et le posent chacun
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (bLocked)
		{
			ASC->AddLooseGameplayTag(GenGameplayTags::State_CastLocked);

			// Serveur : fenêtre où le verrou refuse les sorts d'un client distant (GenFeeding::GetCastLockEnforcedUntil)
			if (UGenAbilitySystemComponent* GenASC = Cast<UGenAbilitySystemComponent>(ASC))
			{
				GenASC->NoteCastLock(MinLockDuration);
			}
		}
		else
		{
			ASC->RemoveLooseGameplayTag(GenGameplayTags::State_CastLocked);
		}
		bCastLockApplied = bLocked;
	}
}

FGameplayEffectSpecHandle UGenGA_Cast::MakeDamageSpec(TSubclassOf<UGameplayEffect> EffectClass, float Amount, UObject* SourceObject) const
{
	// Toujours attaché dès qu'un effet est configuré, même à 0 dégât : ses tags et cues s'appliquent quand même
	if (!EffectClass)
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(EffectClass, GetAbilityLevel());
	if (Spec.IsValid())
	{
		if (SourceObject)
		{
			Spec.Data->GetContext().AddSourceObject(SourceObject);
		}
		Spec.Data->SetSetByCallerMagnitude(GenGameplayTags::SetByCaller_Damage, Amount);
	}
	return Spec;
}

FGameplayEffectSpecHandle UGenGA_Cast::MakeGainSpec(float Energy, float Resource) const
{
	if (Energy <= 0.f && Resource <= 0.f)
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UGenGE_Gain::StaticClass(), GetAbilityLevel());
	if (Spec.IsValid())
	{
		UGenGE_Gain::SetMagnitudes(*Spec.Data, Energy, Resource);
	}
	return Spec;
}

AGenProjectile* UGenGA_Cast::SpawnProjectileShot(TSubclassOf<AGenProjectile> ShotClass, const FVector& Origin, const FVector& Direction, const FGenProjectileShotParams& ShotParams,
	TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain, float ResourceGain, const TSharedPtr<FGenProjectileSalvo>& Salvo)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || !ShotClass)
	{
		return nullptr;
	}

	FVector Forward = Direction.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = Avatar->GetActorForwardVector();
	}

	const FTransform SpawnTransform(Forward.Rotation(), Origin);
	AGenProjectile* Projectile = GetWorld()->SpawnActorDeferred<AGenProjectile>(
		ShotClass, SpawnTransform, Avatar, Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Projectile)
	{
		GEN_CAST_LOG(Warning, "Échec du spawn de %s", *GetNameSafe(ShotClass));
		return nullptr;
	}

	Projectile->InitializeShot(ShotParams);
	// Équipe retenue au tir : les cibles restent justes si le lanceur disparaît pendant le vol (revue Plan 2 Tasks 7-8, M-6)
	const AGenCharacterBase* Character = GetGenCharacterFromActorInfo();
	Projectile->SetSourceTeam(Character ? Character->GetTeamId() : GenNoTeam);
	Projectile->Salvo = Salvo;
	Projectile->DamageEffectSpecHandle = MakeDamageSpec(DamageClass, DamageAmount, Projectile);
	Projectile->InstigatorOnHitSpecHandle = MakeGainSpec(EnergyGain, ResourceGain);

	GEN_CAST_LOG(Verbose, "Projectile %s créé en %s (vitesse %.0f, échelle %.2f, zone %.0f, repoussement %.0f, dégâts %.0f%s)",
		*Projectile->GetName(), *Origin.ToCompactString(), ShotParams.Speed, ShotParams.Scale, ShotParams.ExplosionRadius, ShotParams.KnockbackDistance, DamageAmount,
		Salvo ? TEXT(", salve") : TEXT(""));

	Projectile->FinishSpawning(SpawnTransform);
	return Projectile;
}

AGenGroundArea* UGenGA_Cast::SpawnGroundArea(TSubclassOf<AGenGroundArea> AreaClass, const FVector& Center, const FGenAreaParams& Params,
	TSubclassOf<UGameplayEffect> DamageClass, float DamageAmount, float EnergyGain)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->HasAuthority() || !AreaClass)
	{
		return nullptr;
	}

	const FTransform SpawnTransform(FRotator::ZeroRotator, GenWorldQueries::FindFloor(GetWorld(), Center, { Avatar }));
	AGenGroundArea* Area = GetWorld()->SpawnActorDeferred<AGenGroundArea>(
		AreaClass, SpawnTransform, Avatar, Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Area)
	{
		GEN_CAST_LOG(Warning, "Échec du spawn de %s", *GetNameSafe(AreaClass));
		return nullptr;
	}

	// Équipe retenue à l'apparition : la zone reste juste si le lanceur meurt ou réapparaît avant l'impact
	const AGenCharacterBase* Character = GetGenCharacterFromActorInfo();
	Area->InitializeArea(Params, Character ? Character->GetTeamId() : GenNoTeam);
	Area->DamageEffectSpecHandle = MakeDamageSpec(DamageClass, DamageAmount, Area);
	Area->InstigatorOnHitSpecHandle = MakeGainSpec(EnergyGain, 0.f);

	GEN_CAST_LOG(Verbose, "Zone %s en %s (rayon %.0f, délai %.2fs, dégâts %.0f, étourdit %.2fs, repousse %.0f)",
		*Area->GetName(), *SpawnTransform.GetLocation().ToCompactString(), Params.Radius, Params.Delay, DamageAmount, Params.StunDuration, Params.KnockbackDistance);

	Area->FinishSpawning(SpawnTransform);
	return Area;
}
