#include "Champions/Curffe/CurffeGA_LivingFlame.h"

#include "AbilitySystem/GenAbilityTooltipData.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/Effects/GenGE_Damage.h"
#include "AbilitySystem/Effects/GenGE_TimedState.h"
#include "Actors/GenGroundArea.h"
#include "Champions/Curffe/CurffeEffects.h"
#include "Champions/Curffe/CurffeGameplayTags.h"
#include "Character/GenCharacterBase.h"
#include "Engine/World.h"
#include "GenGameplayTags.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCurffeLivingFlame, Log, All);

UCurffeGA_LivingFlame::UCurffeGA_LivingFlame()
{
	// Valeurs de la spec (Curffe.md §3 R) : l'asset GA_LivingFlame peut les reprendre telles quelles
	InputTag = GenGameplayTags::InputTag_Ability_3;
	EnergyCost = 25.f;
	CooldownDuration = FScalableFloat(16.f);
	CooldownTags.AddTag(CurffeGameplayTags::Cooldown_Ability_LivingFlame);
	SetAssetTags(FGameplayTagContainer(CurffeGameplayTags::Ability_LivingFlame));

	CastTime = 0.1f;
	bTurnToAim = false;
	BurstAreaClass = AGenGroundArea::StaticClass();
	RefillEffect = UCurffeGE_HearthFill::StaticClass();
}

namespace CurffeLivingFlamePrivate
{
	const FName HasteReason(TEXT("LivingFlameHaste"));
}

void UCurffeGA_LivingFlame::OnCastLaunched(const FGenCastRelease& Release)
{
	// Revue finale, M-5 : client propriétaire, lancer prédit dans la fenêtre de la visée. Quand sa clé rattrape le serveur,
	// on vérifie que le serveur l'a accepté (OnLaunchCaughtUp) : sinon la hâte, qui ne dépend pas du sort, survivrait au
	// refus si la forme du client finit avant que le refus n'arrive (RTT > forme)
	bLaunchRejected = false;
	UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (OwnerASC && CurrentActorInfo && !CurrentActorInfo->IsNetAuthority() && OwnerASC->ScopedPredictionKey.IsLocalClientKey())
	{
		OwnerASC->ScopedPredictionKey.NewCaughtUpDelegate().BindWeakLambda(this, [this]() { OnLaunchCaughtUp(); });
	}

	// Forme de feu prédite chez le client : intouchable, vue par tous (État + tag propre à Curffe pour son visuel).
	// Revue P3 T8-10, M1 : le GE prédit est remplacé par celui du serveur (commencé ~½ RTT plus tard, retrait reçu ~½ RTT
	// après sa fin) : le propriétaire garde les tags de la forme ~1 RTT après SA fin de forme, alors qu'il peut déjà agir
	// (verrou local levé) et être touché (tout est décidé par le serveur). Affichage seulement ; son Foyer suit son verrou
	FGameplayEffectSpecHandle FormSpec = MakeOutgoingGameplayEffectSpec(UGenGE_TimedState::StaticClass(), GetAbilityLevel());
	if (FormSpec.IsValid())
	{
		FGameplayTagContainer FormTags;
		FormTags.AddTag(GenGameplayTags::State_Untouchable);
		FormTags.AddTag(CurffeGameplayTags::State_LivingFlame);
		UGenGE_TimedState::SetDuration(*FormSpec.Data, FormDuration, FormTags);
		FormEffectHandle = ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, FormSpec);
	}

	// Revue P3 T8-10, I2 : Foyer plein dès le départ, prédit chez le client (fenêtre de la visée) et annulé si le serveur
	// refuse le lancer. Rien ne peut le dépenser pendant la forme (verrou de lancement) : même résultat qu'à la fin de la
	// forme, sans le trou d'un RTT où « flamme vivante -> grande boule de feu à 3 flammes » partait sans flamme. Le Foyer
	// n'affiche les flammes qu'à la fin de la forme (UCurffeHearthComponent).
	if (RefillEffect)
	{
		ApplyGameplayEffectToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, RefillEffect->GetDefaultObject<UGameplayEffect>(), GetAbilityLevel());
	}

	// Sans sort pendant la forme : verrou de lancement, seul point d'entrée du tag (State.CastLocked local + fenêtre
	// du serveur : la forme du serveur commence ~½ RTT après celle du client, il ne refuse ses sorts qu'au début)
	SetCastLock(true, FormDuration);

	// Plan Visuals V4 : la forme se lit comme une canalisation (barre qui se vide, télégraphe de l'anneau)
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StartChannel(GetClass(), FormDuration);
	}

	UAbilityTask_WaitDelay* FormTask = UAbilityTask_WaitDelay::WaitDelay(this, FormDuration);
	FormTask->OnFinish.AddDynamic(this, &ThisClass::OnFormEnded);
	FormTask->ReadyForActivation();

	UE_LOG(LogCurffeLivingFlame, Verbose, TEXT("[%s] %s : forme de feu (%.2fs)"), CurrentActorInfo && CurrentActorInfo->IsNetAuthority() ? TEXT("SERVEUR") : TEXT("CLIENT"),
		*GetName(), FormDuration);
}

void UCurffeGA_LivingFlame::OnFormEnded()
{
	// Il peut de nouveau lancer (pendant la hâte) ; la forme elle-même expire avec sa durée
	SetCastLock(false);
	FormEffectHandle.Invalidate();
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->StopCast(GetClass());
	}

	// Plan Visuals V3 : geste de l'anneau, à vitesse 1, jusqu'au bout même après la fin du sort
	PlayPhaseMontage(FinishMontage, 1.f, /*bStopWhenAbilityEnds*/ false);

	// Hâte (revue P3 T8-10, M3) : multiplicateur local posé par CETTE machine (serveur, client propriétaire) à SA fin de
	// forme, retiré HasteDuration plus tard. Prédite chez le client sans le RTT d'un GE du serveur ; le serveur ouvre sa
	// grâce de mouvement aux deux bornes (NoteLocalSpeedChange). Clé : la classe du sort (une flamme vivante par personnage)
	AGenCharacterBase* HasteCharacter = GetGenCharacterFromActorInfo();
	if (HasteCharacter && HasteMultiplier > 1.f && HasteDuration > 0.f && GetWorld() && !bLaunchRejected)
	{
		const FName HasteReason = CurffeLivingFlamePrivate::HasteReason;
		const UClass* HasteSource = GetClass();
		HasteCharacter->SetLocalMoveSpeedMultiplier(HasteSource, HasteReason, HasteMultiplier);
		GetWorld()->GetTimerManager().SetTimer(HasteTimer, FTimerDelegate::CreateWeakLambda(HasteCharacter, [HasteCharacter, HasteSource, HasteReason]()
		{
			HasteCharacter->ClearLocalMoveSpeedMultiplier(HasteSource, HasteReason);
		}), HasteDuration, false);
		UE_LOG(LogCurffeLivingFlame, Verbose, TEXT("[%s] %s : hâte x%.2f %.1fs"), HasteCharacter->HasAuthority() ? TEXT("SERVEUR") : TEXT("CLIENT"),
			*GetName(), HasteMultiplier, HasteDuration);
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (Avatar && Avatar->HasAuthority())
	{
		// Anneau (zone : traverse les contres)
		FGenAreaParams Params;
		Params.Radius = BurstRadius;
		Params.KnockbackDistance = BurstKnockback;
		SpawnGroundArea(BurstAreaClass, Avatar->GetActorLocation(), Params, UGenGE_Damage::StaticClass(), BurstDamage, 0.f);

		UE_LOG(LogCurffeLivingFlame, Verbose, TEXT("[SERVEUR] %s : anneau (%.0f cm)"), *GetName(), BurstRadius);
	}

	FinishAbility();
}

void UCurffeGA_LivingFlame::OnLaunchCaughtUp()
{
	// Le temps de recharge est posé au lancer (CommitAbility). Le GE prédit vient d'être retiré (rattrapage, délégués
	// appelés dans l'ordre d'enregistrement) ; celui du serveur est déjà là (ses GE arrivent avec la clé ou avant) s'il a
	// accepté le lancer. Sans temps de recharge réglé, rien à vérifier
	const UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	const FGameplayTagContainer* Cooldown = GetCooldownTags();
	if (!OwnerASC || !Cooldown || Cooldown->IsEmpty() || OwnerASC->HasAnyMatchingGameplayTags(*Cooldown))
	{
		return;
	}
	UE_LOG(LogCurffeLivingFlame, Verbose, TEXT("[CLIENT] %s : lancer refusé par le serveur, hâte retirée"), *GetName());
	bLaunchRejected = true;
	ClearHaste();
}

void UCurffeGA_LivingFlame::ClearHaste()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HasteTimer);
	}
	if (AGenCharacterBase* Character = GetGenCharacterFromActorInfo())
	{
		Character->ClearLocalMoveSpeedMultiplier(GetClass(), CurffeLivingFlamePrivate::HasteReason);
	}
}

void UCurffeGA_LivingFlame::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Fin anticipée (mort) : la forme s'arrête avec le sort. Le verrou et la barre sont retirés par la base.
	if (FormEffectHandle.IsValid())
	{
		BP_RemoveGameplayEffectFromOwnerWithHandle(FormEffectHandle);
		FormEffectHandle.Invalidate();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

#define LOCTEXT_NAMESPACE "CurffeGA_LivingFlame"

void UCurffeGA_LivingFlame::GetTooltipArgs(FFormatNamedArguments& Args) const
{
	Super::GetTooltipArgs(Args);
	Args.Add(TEXT("Duration"), GenAbilityTooltip::Seconds(FormDuration));
	Args.Add(TEXT("Radius"), GenAbilityTooltip::Meters(BurstRadius));
	Args.Add(TEXT("Damage"), GenAbilityTooltip::Number(BurstDamage));
	Args.Add(TEXT("Knockback"), GenAbilityTooltip::Meters(BurstKnockback));
	Args.Add(TEXT("Haste"), GenAbilityTooltip::Percent(HasteMultiplier - 1.f));
	Args.Add(TEXT("HasteDuration"), GenAbilityTooltip::Seconds(HasteDuration));
}

void UCurffeGA_LivingFlame::GetTooltipEffectLines(TArray<FText>& OutLines) const
{
	OutLines.Add(FText::Format(LOCTEXT("Form", "Forme de feu {0} : intouchable, sans sort"), GenAbilityTooltip::Seconds(FormDuration)));
	TArray<FText> Burst;
	Burst.Add(FText::Format(LOCTEXT("Burst", "Fin de la forme : anneau {0}, {1} dégâts"), GenAbilityTooltip::Meters(BurstRadius), GenAbilityTooltip::Number(BurstDamage)));
	if (BurstKnockback > 0.f)
	{
		Burst.Add(FText::Format(LOCTEXT("BurstKnockback", "recul {0}"), GenAbilityTooltip::Meters(BurstKnockback)));
	}
	OutLines.Add(GenAbilityTooltip::Join(Burst, LOCTEXT("Comma", ", ")));
	if (RefillEffect)
	{
		OutLines.Add(LOCTEXT("Refill", "Foyer rempli"));
	}
	if (HasteMultiplier > 1.f && HasteDuration > 0.f)
	{
		OutLines.Add(FText::Format(LOCTEXT("Haste", "Vitesse +{0} pendant {1}"), GenAbilityTooltip::Percent(HasteMultiplier - 1.f), GenAbilityTooltip::Seconds(HasteDuration)));
	}
}

#undef LOCTEXT_NAMESPACE
