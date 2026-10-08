#include "AbilitySystem/Tasks/GenAbilityTask_TargetDataUnderCursor.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AbilitySystem/GenTargetData.h"
#include "GameFramework/PlayerController.h"
#include "Player/GenPlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogGenTargetData, Log, All);

UGenAbilityTask_TargetDataUnderCursor* UGenAbilityTask_TargetDataUnderCursor::CreateTargetDataUnderCursor(UGameplayAbility* OwningAbility, uint8 FedCount)
{
	UGenAbilityTask_TargetDataUnderCursor* Task = NewAbilityTask<UGenAbilityTask_TargetDataUnderCursor>(OwningAbility);
	Task->FedCount = FedCount;
	return Task;
}

void UGenAbilityTask_TargetDataUnderCursor::Activate()
{
	if (IsLocallyControlled())
	{
		SendCursorData();
		return;
	}

	// Serveur (pour un client distant) : on attend les données envoyées par le client
	UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	const FGameplayAbilitySpecHandle SpecHandle = GetAbilitySpecHandle();
	const FPredictionKey ActivationPredictionKey = GetActivationPredictionKey();

	UE_LOG(LogGenTargetData, Verbose, TEXT("[SERVEUR] Attente de la visée du client (clé %s)"), *ActivationPredictionKey.ToString());
	ASC->AbilityTargetDataSetDelegate(SpecHandle, ActivationPredictionKey).AddUObject(this, &ThisClass::OnTargetDataReplicatedCallback);

	// Les données ont peut-être déjà été reçues avant l'activation
	const bool bCalledDelegate = ASC->CallReplicatedTargetDataDelegatesIfSet(SpecHandle, ActivationPredictionKey);
	if (!bCalledDelegate)
	{
		SetWaitingOnRemotePlayerData();
	}
}

void UGenAbilityTask_TargetDataUnderCursor::SendCursorData()
{
	UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	FScopedPredictionWindow ScopedPrediction(ASC);

	const FGameplayAbilityActorInfo* ActorInfo = Ability->GetCurrentActorInfo();
	const AActor* Avatar = ActorInfo->AvatarActor.Get();
	const APlayerController* PC = ActorInfo->PlayerController.Get();

	FHitResult CursorHit;
	FVector CursorLocation = Avatar ? Avatar->GetActorLocation() + Avatar->GetActorForwardVector() * 100.f : FVector::ZeroVector;

	if (const AGenPlayerController* GenPC = Cast<AGenPlayerController>(PC))
	{
		GenPC->GetCursorLocationOnPlane(Avatar ? Avatar->GetActorLocation().Z : 0.f, CursorLocation);
	}
	else if (PC && PC->GetHitResultUnderCursor(ECC_Visibility, false, CursorHit))
	{
		CursorLocation = CursorHit.ImpactPoint;
	}

	CursorHit.bBlockingHit = true;
	CursorHit.Location = CursorLocation;
	CursorHit.ImpactPoint = CursorLocation;
	CursorHit.TraceStart = Avatar ? Avatar->GetActorLocation() : CursorLocation;
	CursorHit.TraceEnd = CursorLocation;

	FGenTargetData_Aim* Data = new FGenTargetData_Aim();
	Data->HitResult = CursorHit;
	Data->FedCount = FedCount;

	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(Data);

	// Un serveur-hôte (listen server) n'a pas besoin de s'envoyer les données à lui-même
	if (!ActorInfo->IsNetAuthority())
	{
		UE_LOG(LogGenTargetData, Verbose, TEXT("[CLIENT] Envoi de la visée au serveur (clé %s)"), *GetActivationPredictionKey().ToString());
		ASC->CallServerSetReplicatedTargetData(GetAbilitySpecHandle(), GetActivationPredictionKey(), DataHandle, FGameplayTag(), ASC->ScopedPredictionKey);
	}

	if (ShouldBroadcastAbilityTaskDelegates())
	{
		ValidData.Broadcast(DataHandle);
	}
}

void UGenAbilityTask_TargetDataUnderCursor::OnTargetDataReplicatedCallback(const FGameplayAbilityTargetDataHandle& DataHandle, FGameplayTag ActivationTag)
{
	UE_LOG(LogGenTargetData, Verbose, TEXT("[SERVEUR] Visée du client reçue"));

	// Copie AVANT le Consume : si les données sont arrivées avant que la tâche n'écoute
	// (cas d'un sort avec incantation), DataHandle référence le cache de l'ASC, que
	// Consume vide => on diffuserait un handle vide.
	const FGameplayAbilityTargetDataHandle DataHandleCopy = DataHandle;
	AbilitySystemComponent->ConsumeClientReplicatedTargetData(GetAbilitySpecHandle(), GetActivationPredictionKey());

	if (ShouldBroadcastAbilityTaskDelegates())
	{
		ValidData.Broadcast(DataHandleCopy);
	}
}

void UGenAbilityTask_TargetDataUnderCursor::OnDestroy(bool bInOwnerFinished)
{
	UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	if (ASC && !IsLocallyControlled())
	{
		ASC->AbilityTargetDataSetDelegate(GetAbilitySpecHandle(), GetActivationPredictionKey()).RemoveAll(this);
	}

	Super::OnDestroy(bInOwnerFinished);
}
