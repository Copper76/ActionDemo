// Fill out your copyright notice in the Description page of Project Settings.


#include "Portal.h"
#include "../ActionDemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ActionDemo/Public/Teleportor.h"
#include "Components/StaticMeshComponent.h"

#define POINT(x, c) DrawDebugPoint(GetWorld(), x, 10, c, !5.0f, 5.0f);
#define LINE(x1, x2, c) DrawDebugLine(GetWorld(), x1, x2, c, !5.0f, 5.0f);
#define PortalChannel ECC_GameTraceChannel2

// Sets default values
APortal::APortal()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	AddOwnedComponent(SceneRoot);
	SetRootComponent(SceneRoot);

	CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("OtherPortalView"));
	AddOwnedComponent(CaptureComponent);
	CaptureComponent->SetupAttachment(SceneRoot);
	//CaptureComponent->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
	CameraOffset = FVector(10.f, 0.f, 0.0f);
	CaptureComponent->SetRelativeLocation(CameraOffset); // Position the camera
	CameraRotation = FRotator(0.0f, 0.0f, 90.0f).Quaternion();
	CaptureComponent->SetRelativeRotation(CameraRotation);

	ReverseCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("ReverseCollider"));
	AddOwnedComponent(ReverseCollider);
	ReverseCollider->SetupAttachment(SceneRoot);
	ReverseCollider->SetRelativeLocation(FVector(0.0f, 0.f, 0.0f));
	ReverseCollider->SetRelativeScale3D(FVector(0.1f, 1.5f, 3.25f));
	ReverseCollider->SetGenerateOverlapEvents(true);

	Plane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plane"));
	AddOwnedComponent(Plane);
	Plane->SetupAttachment(SceneRoot);
	//Plane->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
	Plane->SetRelativeScale3D(FVector(1.25f, 2.25f, 1.0f));
	Plane->SetRelativeRotation(FRotator(0.0f, -90.0f, 90.0f));

}

// Called when the game starts or when spawned
void APortal::BeginPlay()
{
	Super::BeginPlay();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PlayerController = PC;
		LocalPlayer = Cast<AActionDemoCharacter>(PC->GetCharacter());
	}
	//These are set in setup later
	ReverseCollider->OnComponentBeginOverlap.AddDynamic(this, &APortal::OnOverlapBegin);
	ReverseCollider->OnComponentEndOverlap.AddDynamic(this, &APortal::OnOverlapEnd);

	CaptureComponent->bEnableClipPlane = true;
	CaptureComponent->ClipPlaneBase = GetPlaneOrigin();
	CaptureComponent->ClipPlaneNormal = GetActorForwardVector();
}

//Setup when there is an existing portal
void APortal::SetUp(AActor* TargetSurface, const bool bIsBlue, APortal* Portal)
{
	if (!bIsBlue)
	{
		Plane->SetMaterial(0, OrangeMaterial);
		CaptureComponent->TextureTarget = OrangeTextureTarget;
	}

	if (Portal)
	{
		Link(Portal);
		OtherPortal->Link(this);
	}
}

void APortal::Link(APortal* Portal)
{
	OtherPortal = Portal;
	UMaterialInstanceDynamic* PortalMID = Plane->CreateDynamicMaterialInstance(0);
	PortalMID->SetScalarParameterValue(TEXT("Linked"), 1.f);
}

bool APortal::GetTravelQuat(FQuat& OutTravelQ) const
{
	if (!OtherPortal)
	{
		return false;
	}
	OutTravelQ = OtherPortal->GetActorQuat() * FQuat(FVector::ZAxisVector, PI) * GetActorQuat().Inverse();
	return true;
}

bool APortal::TranslatePoint(const FVector& WorldPoint, FVector& OutPoint) const
{
	FQuat TravelQ = FQuat::Identity;
	if (!GetTravelQuat(TravelQ) || !Plane || !OtherPortal->Plane)
	{
		return false;
	}

	OutPoint = OtherPortal->GetPlaneOrigin() + TravelQ.RotateVector(WorldPoint - GetPlaneOrigin());
	return true;
}

bool APortal::TranslateDirection(const FVector& WorldDirection, FVector& OutDirection) const
{
	FQuat TravelQ = FQuat::Identity;
	if (!GetTravelQuat(TravelQ))
	{
		return false;
	}

	OutDirection = TravelQ.RotateVector(WorldDirection);
	return true;
}

bool APortal::IsPointBehindPlane(const FVector& Point) const
{
	const FVector Origin = Plane ? Plane->GetComponentLocation() : GetActorLocation();
	return FVector::DotProduct(Point - Origin, GetActorForwardVector()) <= 2.f;
}

// Called every frame
void APortal::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (OtherPortal == nullptr)
	{
		return;
	}

	if (!(PlayerController == nullptr || LocalPlayer == nullptr))
	{
		const FQuat EntryQ = GetActorQuat();
		const FQuat ExitQ = OtherPortal->GetActorQuat();
		const FQuat CamQ = PlayerController->PlayerCameraManager->GetCameraRotation().Quaternion();
		const FQuat FlipQ = FQuat(FVector::ZAxisVector, PI);

		FVector PlayerOffset = Plane->GetComponentLocation() - PlayerController->PlayerCameraManager->GetCameraLocation();
		PlayerOffset = EntryQ.UnrotateVector(PlayerOffset);

		const FVector RefUp = LocalPlayer ? LocalPlayer->GetActorUpVector() : FVector::UpVector;
		if (FMath::Abs(GetActorForwardVector() | RefUp.GetSafeNormal()) < 1.0f)
		{
			PlayerOffset.Z = -PlayerOffset.Z;
		}

		const FQuat CaptureWorldQ = ExitQ * FlipQ * EntryQ.Inverse() * CamQ;
		OtherPortal->CaptureComponent->SetWorldRotation(CaptureWorldQ);
		OtherPortal->CaptureComponent->SetRelativeLocation(CameraOffset + PlayerOffset);
	}

	if (LocalPlayer && LocalPlayer->GetCurrentPortal() == this && IsPointBehindPlane(LocalPlayer->GetActorLocation()))
	{
		Teleport(LocalPlayer);
	}

	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		UpdateCloneTransform(Pair.Key, Pair.Value);
	}

	TArray<AActor*> ToHandOff;
	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		if (Pair.Key && Pair.Value && IsPointBehindPlane(Pair.Key->GetActorLocation()))
		{
			ToHandOff.Add(Pair.Key);
		}
	}
	for (AActor* Original : ToHandOff)
	{
		HandOffTeleportingObject(Original);
	}

	const FLinearColor Origin = FLinearColor(Plane->GetComponentLocation());
	const FLinearColor Normal = FLinearColor(GetActorForwardVector());

	FLinearColor OtherOrigin;
	FLinearColor OtherNormal;

	if (OtherPortal)
	{
		OtherOrigin = FLinearColor(OtherPortal->Plane->GetComponentLocation());
		OtherNormal = FLinearColor(OtherPortal->GetActorForwardVector());
	}

	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		SetActorClip(Pair.Key, Origin, Normal, true);
		if (OtherPortal && Pair.Value)
		{
			SetActorClip(Pair.Value, OtherOrigin, OtherNormal, true);
		}
	}
}

AActor* APortal::CreatePortalClone(AActor* Original, const FTransform& SpawnTransform)
{
	if (!Original || !GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	AActor* Clone = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform, Params);
	if (!Clone)
	{
		return nullptr;
	}

	USceneComponent* Root = NewObject<USceneComponent>(Clone, TEXT("CloneRoot"));
	Clone->SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
	Clone->FinishSpawning(SpawnTransform);
	Root->RegisterComponent();
	Clone->SetActorTickEnabled(false);

	TArray<UStaticMeshComponent*> Meshes;
	Original->GetComponents<UStaticMeshComponent>(Meshes);
	for (UStaticMeshComponent* Source : Meshes)
	{
		if (!Source || !Source->GetStaticMesh())
		{
			continue;
		}

		UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(Clone);
		Copy->SetStaticMesh(Source->GetStaticMesh());
		Copy->SetMobility(EComponentMobility::Movable);
		const FTransform Relative = Source->GetComponentTransform().GetRelativeTransform(Original->GetActorTransform());
		Copy->SetupAttachment(Root);
		Copy->SetRelativeTransform(Relative);
		const int32 NumMaterials = Source->GetNumMaterials();
		for (int32 Index = 0; Index < NumMaterials; ++Index)
		{
			Copy->SetMaterial(Index, Source->GetMaterial(Index));
		}

		Copy->SetCollisionProfileName(Source->GetCollisionProfileName());
		Copy->SetCollisionEnabled(Source->GetCollisionEnabled());
		Copy->SetCollisionObjectType(Source->GetCollisionObjectType());
		Copy->SetCollisionResponseToChannels(Source->GetCollisionResponseToChannels());
		Copy->CanCharacterStepUpOn = Source->CanCharacterStepUpOn;
		Copy->SetSimulatePhysics(false);
		Copy->SetEnableGravity(false);
		Copy->SetGenerateOverlapEvents(false);
		Copy->SetNotifyRigidBodyCollision(false);
		Copy->RegisterComponent();
		Clone->AddInstanceComponent(Copy);
	}

	return Clone;
}

void APortal::UpdateCloneTransform(AActor* Original, AActor* Clone)
{
	if (!Original || !Clone)
	{
		return;
	}

	FQuat TravelQ = FQuat::Identity;
	FVector ExitLocation = FVector::ZeroVector;
	if (!GetTravelQuat(TravelQ) || !TranslatePoint(Original->GetActorLocation(), ExitLocation))
	{
		return;
	}

	const FQuat ExitRotation = TravelQ * Original->GetActorQuat();
	Clone->SetActorTransform(FTransform(ExitRotation, ExitLocation, Original->GetActorScale3D()), false, nullptr, ETeleportType::TeleportPhysics);
}

void APortal::HandOffTeleportingObject(AActor* Original)
{
	if (!OtherPortal || !Original || !TeleportingObjects.Contains(Original))
	{
		return;
	}

	AActor* Clone = TeleportingObjects[Original];
	if (!Clone)
	{
		TeleportingObjects.Remove(Original);
		return;
	}

	UpdateCloneTransform(Original, Clone);

	UPrimitiveComponent* OriginalComp = Original->FindComponentByClass<UPrimitiveComponent>();
	const FTransform OriginalTransform = Original->GetActorTransform();
	FVector LinVel = OriginalComp ? OriginalComp->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	FVector AngVel = OriginalComp ? OriginalComp->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
	FQuat TravelQ = FQuat::Identity;
	if (GetTravelQuat(TravelQ))
	{
		LinVel = TravelQ.RotateVector(LinVel);
		AngVel = TravelQ.RotateVector(AngVel);
	}

	TeleportingObjects.Remove(Original);
	OtherPortal->TeleportingObjects.Add(Original, Clone);

	Original->SetActorTransform(Clone->GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
	Clone->SetActorTransform(OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
	if (OriginalComp && OriginalComp->IsSimulatingPhysics())
	{
		OriginalComp->SetPhysicsLinearVelocity(LinVel);
		OriginalComp->SetPhysicsAngularVelocityInRadians(AngVel);
	}
}

void APortal::Teleport(AActionDemoCharacter* Character)
{
	const FQuat ExitQ = OtherPortal->GetActorQuat();
	FQuat TravelQ = FQuat::Identity;
	if (!GetTravelQuat(TravelQ))
	{
		return;
	}

	const FQuat CaptureQ = OtherPortal->CaptureComponent->GetComponentQuat();
	PlayerController->SetControlRotation(CaptureQ.Rotator());

	// Enter the exit portal before the move, so the placement resolves against that plane.
	OtherPortal->OnOverlapBegin(OtherPortal->ReverseCollider, Character, Character->GetCapsuleComponent(), INDEX_NONE, false, FHitResult());

	Character->SetActorLocation(OtherPortal->CaptureComponent->GetComponentLocation() + ExitQ.RotateVector(-Character->CameraOffset), false, nullptr, ETeleportType::TeleportPhysics);

	Character->AlignMovement(TravelQ, ExitQ);
}

void APortal::SetActorClip(AActor* Actor, const FLinearColor Origin, const FLinearColor Normal, const bool bEnable)
{
	if (!Actor) return;
	TArray<UMeshComponent*> Meshes;
	Actor->GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		const int32 Num = Mesh->GetNumMaterials();
		for (int32 i = 0; i < Num; ++i)
		{
			UMaterialInterface* Mat = Mesh->GetMaterial(i);
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat);
			if (!MID)
			{
				MID = Mesh->CreateDynamicMaterialInstance(i, Mat);
			}

			if (!MID)
			{
				continue;
			}

			MID->SetScalarParameterValue(TEXT("ClipEnabled"), bEnable ? 1.f : 0.f);
			MID->SetVectorParameterValue(TEXT("ClipPlaneOrigin"), Origin);
			MID->SetVectorParameterValue(TEXT("ClipPlaneNormal"), Normal);
		}
	}
}

void APortal::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherPortal != nullptr)
	{
		if (AActionDemoCharacter* OverlappedCharacter = Cast<AActionDemoCharacter>(OtherActor))
		{
			if (OverlappedCharacter == LocalPlayer)
			{
				OverlappedCharacter->SetCurrentPortal(this);
			}
		}

		if (TeleportingObjects.Contains(OtherActor))
		{
			return;
		}

		for (const TPair<AActor*, AActor*>& Pair : OtherPortal->TeleportingObjects)
		{
			if (Pair.Value == OtherActor) //This is a clone of the original
			{
				return;
			}
		}

		FQuat TravelQ = FQuat::Identity;
		FVector Location = FVector::ZeroVector;
		if (!GetTravelQuat(TravelQ) || !TranslatePoint(OtherActor->GetActorLocation(), Location))
		{
			return;
		}

		const FTransform NewTrans(TravelQ * OtherActor->GetActorQuat(), Location, OtherActor->GetActorScale3D());
		if (AActor* ClonedActor = CreatePortalClone(OtherActor, NewTrans))
		{
			TeleportingObjects.Add(OtherActor, ClonedActor);
		}
	}
}

void APortal::OnOverlapEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("EXIT: %s"), *OtherActor->GetActorLabel())

	if (AActionDemoCharacter* OverlappedCharacter = Cast<AActionDemoCharacter>(OtherActor))
	{
		if (OverlappedCharacter == LocalPlayer && OverlappedCharacter->GetCurrentPortal() == this)
		{
			OverlappedCharacter->SetCurrentPortal(nullptr);
		}
	}

	if (TeleportingObjects.Contains(OtherActor))
	{
		AActor* Clone = TeleportingObjects[OtherActor];
		if (Clone)
		{
			VDestroy(Clone);
		}

		SetActorClip(OtherActor, FLinearColor(Plane->GetComponentLocation()), FLinearColor(GetActorForwardVector()), false);

		TeleportingObjects.Remove(OtherActor);
	}
}

void APortal::VDestroy(AActor* ToDestroy)
{
	if (!ToDestroy) return;
	if (!ToDestroy->IsValidLowLevel()) return;

	ToDestroy->K2_DestroyActor();
	ToDestroy = NULL;

	GEngine->ForceGarbageCollection(true);
}
