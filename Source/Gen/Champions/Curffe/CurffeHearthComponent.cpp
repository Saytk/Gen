#include "Champions/Curffe/CurffeHearthComponent.h"

#include "AbilitySystemComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Champions/Curffe/CurffeGA_Combustion.h"
#include "Champions/Curffe/CurffeGameplayTags.h"
#include "Champions/Curffe/CurffeHearthRules.h"
#include "Champions/Curffe/CurffeTuning.h"
#include "Character/GenCharacterBase.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GenGameplayTags.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace CurffeHearthPrivate
{
	/** Le Foyer a toujours 5 emplacements (Curffe-Visuals.md §5). */
	constexpr int32 SocketCount = CurffeTuning::MaxFlames;
	constexpr int32 FlightCount = 3;
	constexpr int32 InstanceCount = SocketCount + FlightCount;
	/** Données par instance (contrat de MI_Hearth_Flame) : Lit, Pop. */
	constexpr int32 CustomDataCount = 2;
	/** Les systèmes de vol sont écrits pour 100 cm à l'échelle 1. */
	constexpr float FlightPathLength = 100.f;

	uint8 SocketBit(int32 Socket)
	{
		return static_cast<uint8>(1u << Socket);
	}
}

using namespace CurffeHearthPrivate;

UCurffeHearthComponent::UCurffeHearthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Revue V6-V8, M-6 : après l'animation (sockets du sort à jour) et le mouvement, comme les indicateurs
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	// L'orbite ne tourne pas avec le personnage quand il se retourne
	SetUsingAbsoluteRotation(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	FlameMesh = SphereMesh.Object;
}

bool UCurffeHearthComponent::UsesFallback() const
{
	return !HearthFlameMesh || !HearthFlameMaterial;
}

double UCurffeHearthComponent::GetNow() const
{
	return GetWorld()->GetTimeSeconds();
}

void UCurffeHearthComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}

	AblazeTag = CurffeGameplayTags::State_Ablaze;
	LivingFlameTag = CurffeGameplayTags::State_LivingFlame;
	CurrentOrbitRadius = OrbitRadius;

	if (AGenCharacterBase* Character = Cast<AGenCharacterBase>(GetOwner()))
	{
		OwnerCharacter = Character;
		FedChangedHandle = Character->OnFedResourceChanged.AddUObject(this, &ThisClass::OnFedResourceChanged);
		ThresholdHandle = Character->OnFedThresholdReached.AddUObject(this, &ThisClass::OnFedThresholdReached);
	}

	// Un seul composant instancié : un appel de dessin par Curffe (5 emplacements + 3 vols)
	Flames = NewObject<UInstancedStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
	Flames->SetStaticMesh(UsesFallback() ? FlameMesh.Get() : HearthFlameMesh.Get());
	if (UMaterialInterface* Material = UsesFallback() ? FlameMaterial.Get() : HearthFlameMaterial.Get())
	{
		Flames->SetMaterial(0, Material);
	}
	Flames->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Flames->SetGenerateOverlapEvents(false);
	Flames->SetCanEverAffectNavigation(false);
	Flames->SetCastShadow(false);
	Flames->bReceivesDecals = false;
	Flames->bAffectDistanceFieldLighting = false;
	Flames->SetUsingAbsoluteLocation(true);
	Flames->SetUsingAbsoluteRotation(true);
	Flames->SetUsingAbsoluteScale(true);
	Flames->SetupAttachment(this);
	Flames->RegisterComponent();
	Flames->SetNumCustomDataFloats(CustomDataCount);

	TArray<FTransform> Hidden;
	Hidden.Init(FTransform(FRotator::ZeroRotator, GetComponentLocation(), FVector::ZeroVector), InstanceCount);
	Flames->AddInstances(Hidden, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
	SentCustomData.Init(-1.f, InstanceCount * CustomDataCount);
}

void UCurffeHearthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AGenCharacterBase* Character = OwnerCharacter.Get())
	{
		Character->OnFedResourceChanged.Remove(FedChangedHandle);
		Character->OnFedThresholdReached.Remove(ThresholdHandle);
	}
	FedChangedHandle.Reset();
	ThresholdHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

int32 UCurffeHearthComponent::GetFlyingFlameCount() const
{
	int32 Count = 0;
	for (const FFlight& Flight : Flights)
	{
		Count += Flight.bActive ? 1 : 0;
	}
	return Count;
}

int32 UCurffeHearthComponent::GetInstanceCount() const
{
	return Flames ? Flames->GetInstanceCount() : 0;
}

int32 UCurffeHearthComponent::GetFlamesNow() const
{
	const AGenCharacterBase* Character = OwnerCharacter.Get();
	return Character ? FMath::Clamp(FMath::FloorToInt32(Character->GetResource()), 0, SocketCount) : 0;
}

void UCurffeHearthComponent::OnFedResourceChanged(AGenCharacterBase* Character, int32 Old, int32 New)
{
	if (New > Old)
	{
		// Flammes au moment du nourrissage : les emplacements s'éteignent depuis le haut de ce compte
		FlamesWhileFeeding = GetFlamesNow();
		return;
	}
	if (New >= Old)
	{
		return;
	}

	// Flammes illimitées (Combustion) : les emplacements se sont rallumés au départ de chaque flamme, rien ne revient
	if (IsUnlimited())
	{
		FlownCount = FMath::Min(FlownCount, New);
		return;
	}

	// Seules les unités qui ont quitté le Foyer en volant y reviennent en volant (les autres se rallument simplement)
	const int32 ReturnCount = FMath::Max(FMath::Min(Old, FlownCount) - New, 0);
	FlownCount = FMath::Min(FlownCount, New);

	// Lancer : les flammes sont dans le sort, rien ne vole. Revue V6-V8, I-3 : le personnage le sait (lancer marqué par le
	// sort, répliqué avec le compte nourri aux autres joueurs), sans deviner par la ressource
	if ((Character && Character->WasLastFedDropSpent()) || ReturnCount == 0)
	{
		return;
	}

	// Annulation, interruption : les flammes reviennent au Foyer
	StartReturnFlights(New, ReturnCount, FlamesWhileFeeding);
}

void UCurffeHearthComponent::OnFedThresholdReached(AGenCharacterBase* Character, int32 NewCount)
{
	// Un seuil franchi (compte ABSOLU : une réplication regroupée 0 -> 2 fait voler les deux flammes)
	const FName SpellSocket = Character ? Character->GetCastInfo().FXSocket : NAME_None;
	LastSpellSocket = SpellSocket;
	for (int32 FedIndex = FlownCount; FedIndex < NewCount; ++FedIndex)
	{
		const int32 Socket = CurffeHearthRules::GetFedSocketIndex(FlamesWhileFeeding, FedIndex, SocketCount);
		if (Socket != INDEX_NONE)
		{
			StartFlight(Socket, /*bReturning*/ false, SpellSocket);
		}
	}
	FlownCount = FMath::Max(FlownCount, NewCount);
}

void UCurffeHearthComponent::StartReturnFlights(int32 FirstFedIndex, int32 Count, int32 FeedingFlames)
{
	for (int32 FedIndex = FirstFedIndex; FedIndex < FirstFedIndex + Count; ++FedIndex)
	{
		const int32 Socket = CurffeHearthRules::GetFedSocketIndex(FeedingFlames, FedIndex, SocketCount);
		if (Socket != INDEX_NONE)
		{
			StartFlight(Socket, /*bReturning*/ true, LastSpellSocket);
		}
	}
}

void UCurffeHearthComponent::StartFlight(int32 Socket, bool bReturning, FName SpellSocket)
{
	// Une place libre, sinon le vol le plus ancien (au plus 3 unités nourries par sort)
	FFlight* Slot = nullptr;
	for (FFlight& Flight : Flights)
	{
		if (!Flight.bActive)
		{
			Slot = &Flight;
			break;
		}
		if (!Slot || Flight.StartTime < Slot->StartTime)
		{
			Slot = &Flight;
		}
	}

	Slot->bActive = true;
	Slot->bReturning = bReturning;
	Slot->Socket = Socket;
	Slot->StartTime = GetNow();
	Slot->SpellSocket = SpellSocket;
	++(bReturning ? StartedReturnFlights : StartedFlights);

	Slot->bNiagaraVisual = false;

	// Vol en Niagara (contrat de NS_Curffe_FeedFly / FeedReturn) : attaché au socket du sort, axe local −X de la main
	// vers l'emplacement du Foyer, échelle = distance / 100. Il remplace l'instance de vol
	UNiagaraSystem* System = bReturning ? FeedReturnSystem.Get() : FeedFlySystem.Get();
	const AGenCharacterBase* Character = OwnerCharacter.Get();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (System && Mesh)
	{
		const FVector Hand = GetSpellLocation(SpellSocket);
		const FVector HandFromSocket = Hand - GetFlameSocketLocation(Socket);
		const float Distance = HandFromSocket.Size();
		if (Distance > KINDA_SMALL_NUMBER)
		{
			const bool bHasSocket = !SpellSocket.IsNone() && Mesh->DoesSocketExist(SpellSocket);
			const FRotator Rotation = FRotationMatrix::MakeFromXZ(HandFromSocket, FVector::UpVector).Rotator();
			Slot->bNiagaraVisual = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Mesh, bHasSocket ? SpellSocket : NAME_None, Hand, Rotation,
				FVector(Distance / FlightPathLength), EAttachLocation::KeepWorldPosition, /*bAutoDestroy*/ true, ENCPoolMethod::AutoRelease) != nullptr;
		}
	}
}

bool UCurffeHearthComponent::IsUnlimited() const
{
	const AGenCharacterBase* Character = OwnerCharacter.Get();
	const UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
	return ASC && ASC->HasMatchingGameplayTag(GenGameplayTags::State_FreeResource);
}

FVector UCurffeHearthComponent::GetFlameSocketLocation(int32 Socket) const
{
	const float Angle = FMath::DegreesToRadians(OrbitAngle + 360.f * Socket / SocketCount);
	return GetComponentLocation() + FVector(FMath::Cos(Angle) * CurrentOrbitRadius, FMath::Sin(Angle) * CurrentOrbitRadius, OrbitHeight);
}

FVector UCurffeHearthComponent::GetSpellLocation(FName SpellSocket) const
{
	const AGenCharacterBase* Character = OwnerCharacter.Get();
	if (!Character)
	{
		return GetComponentLocation();
	}
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (Mesh && !SpellSocket.IsNone() && Mesh->DoesSocketExist(SpellSocket))
	{
		return Mesh->GetSocketLocation(SpellSocket);
	}
	return Character->GetActorLocation();
}

void UCurffeHearthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Flames)
	{
		return;
	}

	TArray<FTransform, TInlineAllocator<InstanceCount>> Transforms;
	Transforms.Init(FTransform(FRotator::ZeroRotator, GetComponentLocation(), FVector::ZeroVector), InstanceCount);

	const AGenCharacterBase* Character = OwnerCharacter.Get();
	if (!Character || Character->IsDead())
	{
		// Mort : plus rien (ni flamme, ni braise, ni vol). Une seule mise à jour : ensuite tout est déjà caché
		if (!bHiddenForDeath)
		{
			VisibleFlames = 0;
			for (FFlight& Flight : Flights)
			{
				Flight.bActive = false;
			}
			ShownLitSockets = 0;
			Flames->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
			bHiddenForDeath = true;
		}
		return;
	}
	bHiddenForDeath = false;

	const double Now = GetNow();
	const int32 FlamesNow = GetFlamesNow();
	const int32 Fed = Character->GetFedResource();
	// Un compte reçu sans seuil (personnage devenu pertinent en plein nourrissage) : ses flammes ne volent pas
	FlownCount = Fed;
	// Flammes illimitées (Combustion, Curffe-Visuals.md §5) : une flamme nourrie part et son emplacement se rallume aussitôt
	const int32 ShownFed = IsUnlimited() ? 0 : Fed;

	// Vols : fin, et emplacements tenus éteints jusqu'à l'arrivée d'une flamme qui revient
	uint8 HeldDim = 0;
	ArrivedSockets = 0;
	for (FFlight& Flight : Flights)
	{
		if (Flight.bActive && Now - Flight.StartTime >= FlightDuration)
		{
			Flight.bActive = false;
			ArrivedSockets |= Flight.bReturning ? SocketBit(Flight.Socket) : 0;
		}
		if (Flight.bActive && Flight.bReturning)
		{
			HeldDim |= SocketBit(Flight.Socket);
		}
	}

	// Emplacements : règle du Foyer (floor(Resource) − nourries), puis vols de retour, puis recharge échelonnée
	TArray<CurffeHearthRules::ESocket, TInlineAllocator<8>> States;
	CurffeHearthRules::GetSocketStates(FlamesNow, ShownFed, SocketCount, States);
	VisibleFlames = 0;
	uint8 LitSockets = 0;
	for (int32 Socket = 0; Socket < SocketCount; ++Socket)
	{
		if (States[Socket] == CurffeHearthRules::ESocket::Lit)
		{
			++VisibleFlames;
			LitSockets |= (HeldDim & SocketBit(Socket)) ? 0 : SocketBit(Socket);
		}
	}

	const int32 LitCount = FMath::CountBits(LitSockets);
	const int32 ShownCount = FMath::CountBits(ShownLitSockets);
	if (RefillCap == INDEX_NONE && LitCount - ShownCount >= 3)
	{
		RefillCap = ShownCount;
		NextRefillTime = Now;
	}
	const bool bRefilling = RefillCap != INDEX_NONE;
	if (RefillCap != INDEX_NONE)
	{
		if (Now >= NextRefillTime)
		{
			++RefillCap;
			NextRefillTime = Now + RefillStagger;
		}
		if (RefillCap >= LitCount)
		{
			RefillCap = INDEX_NONE;
		}
		else
		{
			// Les emplacements allumés sont toujours les plus bas : on n'en montre que RefillCap
			LitSockets &= static_cast<uint8>((1u << RefillCap) - 1u);
		}
	}

	// Un emplacement qui s'allume : un gain discret d'une flamme (régénération) monte en RegenFadeDuration ; un retour,
	// une recharge ou un gain de plusieurs flammes (blocage du contre) éclate 2 images. Le client ne sait pas distinguer
	// la régénération d'un coup réussi (+1 aussi) : les deux montent sans éclat (point ouvert du rapport)
	const uint8 NewlyLit = LitSockets & ~ShownLitSockets;
	const bool bQuietGain = FMath::CountBits(NewlyLit) == 1 && !(NewlyLit & ArrivedSockets) && !bRefilling;
	for (int32 Socket = 0; Socket < SocketCount; ++Socket)
	{
		if (NewlyLit & SocketBit(Socket))
		{
			if (bQuietGain)
			{
				FadeStart[Socket] = Now;
			}
			else
			{
				PopFramesLeft[Socket] = 2;
			}
		}
	}
	ShownLitSockets = LitSockets;

	// Embrasé : flammes plus grandes, orbite plus rapide
	const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	const bool bAblaze = AblazeTag.IsValid() && ASC && ASC->HasMatchingGameplayTag(AblazeTag);

	// Forme de Living Flame (§3.6) : les flammes convergent vers le corps et tournent vite. Incantation de Combustion
	// (§3.7) : l'orbite se resserre et accélère. Lu sur les tags et l'incantation répliqués : vu par tous
	const bool bLivingFlameForm = LivingFlameTag.IsValid() && ASC && ASC->HasMatchingGameplayTag(LivingFlameTag);
	const FGenCastInfo& CastInfo = Character->GetCastInfo();
	const bool bBuildUp = !bLivingFlameForm && CastInfo.IsCasting() && !CastInfo.bChannel && CastInfo.Ability
		&& CastInfo.Ability->IsChildOf(UCurffeGA_Combustion::StaticClass());
	bConverging = bLivingFlameForm || bBuildUp;
	const float TargetRadius = bLivingFlameForm ? ConvergeRadius : (bBuildUp ? BuildUpRadius : OrbitRadius);
	const float BlendSpeed = ConvergeBlendTime > 0.f ? FMath::Abs(OrbitRadius - ConvergeRadius) / ConvergeBlendTime : 0.f;
	CurrentOrbitRadius = BlendSpeed > 0.f ? FMath::FInterpConstantTo(CurrentOrbitRadius, TargetRadius, DeltaTime, BlendSpeed) : TargetRadius;
	const float SpeedScale = bLivingFlameForm ? ConvergeOrbitSpeedScale : (bBuildUp ? BuildUpOrbitSpeedScale : (bAblaze ? AblazeOrbitSpeedScale : 1.f));
	OrbitAngle = FMath::Fmod(OrbitAngle + OrbitSpeedDegrees * SpeedScale * DeltaTime, 360.f);

	// Cartes (face à +X) tournées vers la caméra locale, haut vers +Z (pas de billboard par WPO) ; sphères du repli : sans importance
	const bool bFallback = UsesFallback();
	FRotator FaceCamera = FRotator::ZeroRotator;
	if (!bFallback)
	{
		const APlayerController* Viewer = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
		if (Viewer && Viewer->PlayerCameraManager)
		{
			FaceCamera = FRotationMatrix::MakeFromXZ(-Viewer->PlayerCameraManager->GetCameraRotation().Vector(), FVector::UpVector).Rotator();
		}
	}

	// Carte : même échelle allumée ou braise (la braise est dessinée dans la carte). Repli : pas de braise (rendu du Plan 1)
	const float LitScale = (bFallback ? FlameScale : FlameCardScale) * (bAblaze ? AblazeScale : 1.f);
	LitFlameScale = LitScale;
	const float EmberScale = bFallback ? 0.f : LitScale;

	float CustomData[InstanceCount * CustomDataCount];
	for (int32 Socket = 0; Socket < SocketCount; ++Socket)
	{
		const bool bLit = (LitSockets & SocketBit(Socket)) != 0;
		Transforms[Socket] = FTransform(FaceCamera, GetFlameSocketLocation(Socket), FVector(bLit ? LitScale : EmberScale));

		float Lit = bLit ? 1.f : 0.f;
		if (bLit && FadeStart[Socket] >= 0.0)
		{
			const float Fade = RegenFadeDuration > 0.f ? static_cast<float>(Now - FadeStart[Socket]) / RegenFadeDuration : 1.f;
			Lit = FMath::Clamp(Fade, 0.f, 1.f);
			FadeStart[Socket] = Fade >= 1.f ? -1.0 : FadeStart[Socket];
		}
		else if (!bLit)
		{
			FadeStart[Socket] = -1.0;
		}
		CustomData[Socket * CustomDataCount + 0] = Lit;
		CustomData[Socket * CustomDataCount + 1] = PopFramesLeft[Socket] == 2 ? 1.f : (PopFramesLeft[Socket] == 1 ? 0.5f : 0.f);
		PopFramesLeft[Socket] = PopFramesLeft[Socket] > 0 ? PopFramesLeft[Socket] - 1 : 0;
	}

	for (int32 FlightIndex = 0; FlightIndex < FlightCount; ++FlightIndex)
	{
		const FFlight& Flight = Flights[FlightIndex];
		const int32 Instance = SocketCount + FlightIndex;
		const bool bShowInstance = Flight.bActive && !Flight.bNiagaraVisual;
		if (bShowInstance)
		{
			// Court arc vers le haut entre l'emplacement (qui tourne) et le socket du sort (qui suit l'animation)
			const float Alpha = FlightDuration > 0.f ? FMath::Clamp(static_cast<float>(Now - Flight.StartTime) / FlightDuration, 0.f, 1.f) : 1.f;
			const FVector From = Flight.bReturning ? GetSpellLocation(Flight.SpellSocket) : GetFlameSocketLocation(Flight.Socket);
			const FVector To = Flight.bReturning ? GetFlameSocketLocation(Flight.Socket) : GetSpellLocation(Flight.SpellSocket);
			const FVector Location = FMath::Lerp(From, To, Alpha) + FVector(0.f, 0.f, FMath::Sin(Alpha * PI) * FlightArcHeight);
			Transforms[Instance] = FTransform(FaceCamera, Location, FVector(LitScale));
		}
		CustomData[Instance * CustomDataCount + 0] = 1.f;
		CustomData[Instance * CustomDataCount + 1] = 0.f;
	}

	// Données par instance : seulement celles qui ont changé (le repli n'en lit aucune)
	if (!bFallback)
	{
		for (int32 Instance = 0; Instance < InstanceCount; ++Instance)
		{
			const int32 Offset = Instance * CustomDataCount;
			bool bChanged = false;
			for (int32 Index = 0; Index < CustomDataCount; ++Index)
			{
				bChanged |= SentCustomData[Offset + Index] != CustomData[Offset + Index];
			}
			if (bChanged)
			{
				FMemory::Memcpy(&SentCustomData[Offset], &CustomData[Offset], CustomDataCount * sizeof(float));
				Flames->SetCustomData(Instance, TArrayView<const float>(&CustomData[Offset], CustomDataCount), /*bMarkRenderStateDirty*/ false);
			}
		}
	}

	// Une mise à jour groupée par image. Revue V6-V8, I-4 : sans MarkRenderStateDirty (qui recrée le proxy à chaque image) :
	// transformées et données par instance passent par le chemin delta des instances (MarkRenderInstancesDirty)
	Flames->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
}
