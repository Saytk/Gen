#include "Game/GenDevTuning.h"

#include "AbilitySystem/GenAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/GenCharacterBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Net/UnrealNetwork.h"

FGenDevTuning FGenDevTuning::Clamped() const
{
	FGenDevTuning Out = *this;
	Out.CooldownScale = FMath::Clamp(CooldownScale, GenDevTuning::MinCooldownScale, GenDevTuning::MaxCooldownScale);
	Out.CastTimeScale = FMath::Clamp(CastTimeScale, GenDevTuning::MinCastTimeScale, GenDevTuning::MaxCastTimeScale);
	Out.GameSpeed = FMath::Clamp(GameSpeed, GenDevTuning::MinGameSpeed, GenDevTuning::MaxGameSpeed);
	return Out;
}

const FGenDevTuning& GenDevTuning::Get(const UObject* WorldContext)
{
	static const FGenDevTuning Defaults;
	const UGenDevTuningSubsystem* Subsystem = UGenDevTuningSubsystem::Get(WorldContext);
	const AGenDevTuningActor* Actor = Subsystem ? Subsystem->GetActor() : nullptr;
	return Actor ? Actor->GetTuning() : Defaults;
}

float GenDevTuning::ScaleCooldown(const FGenDevTuning& Tuning, float Duration)
{
	return Tuning.bNoCooldowns ? 0.f : FMath::Max(Duration * Tuning.CooldownScale, 0.f);
}

AGenDevTuningActor::AGenDevTuningActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	// Le serveur ne tique que si l'énergie ou la ressource doivent rester pleines (OnTuningChanged)
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 0.2f;
}

void AGenDevTuningActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGenDevTuningActor, Tuning);
}

void AGenDevTuningActor::BeginPlay()
{
	Super::BeginPlay();
	if (UGenDevTuningSubsystem* Subsystem = UGenDevTuningSubsystem::Get(this))
	{
		Subsystem->Register(this);
	}
	OnTuningChanged();
}

void AGenDevTuningActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGenDevTuningSubsystem* Subsystem = UGenDevTuningSubsystem::Get(this))
	{
		Subsystem->Unregister(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AGenDevTuningActor::SetTuning(const FGenDevTuning& NewTuning)
{
	if (!HasAuthority())
	{
		return;
	}
	Tuning = NewTuning.Clamped();
	if (AWorldSettings* WorldSettings = GetWorldSettings())
	{
		// Comme la commande slomo : la dilatation du temps du monde est répliquée aux clients
		WorldSettings->SetTimeDilation(Tuning.GameSpeed);
	}
	ForceNetUpdate();
	OnTuningChanged();
}

void AGenDevTuningActor::OnRep_Tuning()
{
	OnTuningChanged();
}

void AGenDevTuningActor::OnTuningChanged()
{
	SetActorTickEnabled(HasAuthority() && (Tuning.bInfiniteEnergy || Tuning.bInfiniteResource));
	if (UGenDevTuningSubsystem* Subsystem = UGenDevTuningSubsystem::Get(this))
	{
		Subsystem->OnChanged.Broadcast();
	}
}

void AGenDevTuningActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	for (TActorIterator<AGenCharacterBase> It(GetWorld()); It; ++It)
	{
		if (It->IsPlayerControlled())
		{
			RefillCharacter(**It, /*bHealth*/ false, Tuning.bInfiniteEnergy, Tuning.bInfiniteResource, /*bCooldowns*/ false);
		}
	}
}

void AGenDevTuningActor::RefillAll()
{
	if (!HasAuthority())
	{
		return;
	}
	for (TActorIterator<AGenCharacterBase> It(GetWorld()); It; ++It)
	{
		RefillCharacter(**It, true, true, true, true);
	}
}

void AGenDevTuningActor::RefillCharacter(AGenCharacterBase& Character, bool bHealth, bool bEnergy, bool bResource, bool bCooldowns) const
{
	UAbilitySystemComponent* ASC = Character.GetAbilitySystemComponent();
	if (!ASC || Character.IsDead())
	{
		return;
	}
	const auto Fill = [ASC](const FGameplayAttribute& Value, const FGameplayAttribute& Max)
	{
		const float Target = ASC->GetNumericAttribute(Max);
		if (ASC->GetNumericAttributeBase(Value) < Target)
		{
			ASC->SetNumericAttributeBase(Value, Target);
		}
	};
	if (bHealth)
	{
		Fill(UGenAttributeSet::GetHealthAttribute(), UGenAttributeSet::GetMaxHealthAttribute());
	}
	if (bEnergy)
	{
		Fill(UGenAttributeSet::GetEnergyAttribute(), UGenAttributeSet::GetMaxEnergyAttribute());
	}
	if (bResource)
	{
		Fill(UGenAttributeSet::GetResourceAttribute(), UGenAttributeSet::GetMaxResourceAttribute());
	}
	if (bCooldowns)
	{
		static const FGameplayTag CooldownRoot = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability"), /*ErrorIfNotFound*/ false);
		if (CooldownRoot.IsValid())
		{
			ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(CooldownRoot));
		}
	}
}

UGenDevTuningSubsystem* UGenDevTuningSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UGenDevTuningSubsystem>() : nullptr;
}

void UGenDevTuningSubsystem::Register(AGenDevTuningActor* InActor)
{
	Actor = InActor;
}

void UGenDevTuningSubsystem::Unregister(AGenDevTuningActor* InActor)
{
	if (Actor.Get() == InActor)
	{
		Actor.Reset();
		OnChanged.Broadcast();
	}
}
