// Fill out your copyright notice in the Description page of Project Settings.


#include "Portal.h"
#include "../ActionDemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ActionDemo/Public/Teleportor.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

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

void APortal::UpdateCloneConstraint(AActor* Original, AActor* Clone)
{
	if (!Original || !Clone)
	{
		return;
	}

	UPhysicsConstraintComponent* Constraint = Cast<UPhysicsConstraintComponent>(
		Original->GetComponentByClass(UPhysicsConstraintComponent::StaticClass()));
	if (!Constraint)
	{
		Constraint = Cast<UPhysicsConstraintComponent>(
			Clone->GetComponentByClass(UPhysicsConstraintComponent::StaticClass()));
	}
	if (!Constraint)
	{
		return;
	}

	UPrimitiveComponent* OriginalComp = Cast<UPrimitiveComponent>(Original->GetRootComponent());
	UPrimitiveComponent* CloneComp = Cast<UPrimitiveComponent>(Clone->GetRootComponent());
	if (!OriginalComp || !CloneComp)
	{
		OriginalComp = Original->FindComponentByClass<UPrimitiveComponent>();
		CloneComp = Clone->FindComponentByClass<UPrimitiveComponent>();
	}
	if (!OriginalComp || !CloneComp)
	{
		return;
	}

	Constraint->ConstraintActor1 = Original;
	Constraint->ConstraintActor2 = Clone;
	Constraint->SetConstrainedComponents(OriginalComp, NAME_None, CloneComp, NAME_None);

	FConstraintInstance& ConstraintInstance = Constraint->ConstraintInstance;
	ConstraintInstance.SetRefFrame(EConstraintFrame::Frame1, FTransform(OriginalComp->GetComponentQuat(), OriginalComp->GetComponentLocation()));
	ConstraintInstance.SetRefFrame(EConstraintFrame::Frame2, FTransform(CloneComp->GetComponentQuat(), CloneComp->GetComponentLocation()));
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

	UPrimitiveComponent* OriginalComp = Original->FindComponentByClass<UPrimitiveComponent>();
	UPrimitiveComponent* CloneComp = Clone->FindComponentByClass<UPrimitiveComponent>();

	TeleportingObjects.Remove(Original);
	OtherPortal->TeleportingObjects.Add(Original, Clone);

	const FTransform OriginalTransform = Original->GetActorTransform();
	const FTransform CloneTransform = Clone->GetActorTransform();
	const FVector OriginalLinVel = OriginalComp ? OriginalComp->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	const FVector CloneLinVel = CloneComp ? CloneComp->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	const FVector OriginalAngVel = OriginalComp ? OriginalComp->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
	const FVector CloneAngVel = CloneComp ? CloneComp->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;

	Original->SetActorTransform(CloneTransform, false, nullptr, ETeleportType::TeleportPhysics);
	Clone->SetActorTransform(OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);

	if (OriginalComp)
	{
		OriginalComp->SetPhysicsLinearVelocity(CloneLinVel);
		OriginalComp->SetPhysicsAngularVelocityInRadians(CloneAngVel);
	}
	if (CloneComp)
	{
		CloneComp->SetPhysicsLinearVelocity(OriginalLinVel);
		CloneComp->SetPhysicsAngularVelocityInRadians(OriginalAngVel);
	}

	UpdateCloneConstraint(Original, Clone);
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
			return;
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
		if (!GetTravelQuat(TravelQ))
		{
			return;
		}
		const FQuat Rotation = TravelQ * OtherActor->GetActorQuat();
		const FVector Location = OtherPortal->GetActorLocation() + Rotation.RotateVector(GetActorLocation() - OtherActor->GetActorLocation());

		//Creating the cloned actor
		FTransform NewTrans = FTransform(Rotation, Location);
		FActorSpawnParameters params;
		params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		params.Template = OtherActor;
		params.bDeferConstruction = true;
		AActor* ClonedActor = GetWorld()->SpawnActor<AActor>(OtherActor->GetClass(), NewTrans, params);
		if (ClonedActor)
		{
			ClonedActor->AttachToActor(OtherActor->GetAttachParentActor(), FAttachmentTransformRules::KeepWorldTransform);
			ClonedActor->FinishSpawning(NewTrans);


			//Add constraint to the two objects	
			UPhysicsConstraintComponent* Constraint = Cast<UPhysicsConstraintComponent>(OtherActor->GetComponentByClass(UPhysicsConstraintComponent::StaticClass()));
			if (Constraint == nullptr) {
				Constraint = NewObject<UPhysicsConstraintComponent>(OtherActor);
				UPrimitiveComponent* ClonedPhysicalComponent = ClonedActor->GetComponentByClass<UPrimitiveComponent>();
				Constraint->ConstraintActor1 = OtherActor;
				Constraint->ConstraintActor2 = ClonedActor;
				Constraint->SetConstrainedComponents(OtherComp, NAME_None, ClonedPhysicalComponent, NAME_None);
				FConstraintInstance& ConstraintInstance = Constraint->ConstraintInstance;

				ConstraintInstance.SetRefFrame(EConstraintFrame::Frame1, FTransform(OtherComp->GetComponentQuat(), OtherComp->GetComponentLocation()));
				ConstraintInstance.SetRefFrame(EConstraintFrame::Frame2, FTransform(ClonedPhysicalComponent->GetComponentQuat(), ClonedPhysicalComponent->GetComponentLocation()));

				ConstraintInstance.SetLinearLimits(ELinearConstraintMotion::LCM_Locked, ELinearConstraintMotion::LCM_Locked, ELinearConstraintMotion::LCM_Locked, 0);

				ConstraintInstance.SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0);
				ConstraintInstance.SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0);
				ConstraintInstance.SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0);

				Constraint->AttachToComponent(ClonedActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				Constraint->RegisterComponent();
			}
		}
		TeleportingObjects.Add(OtherActor, ClonedActor);
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
		return;
	}

	if (TeleportingObjects.Contains(OtherActor))
	{
		AActor* Clone = TeleportingObjects[OtherActor];
		UPhysicsConstraintComponent* Constraint;
		if (Clone)
		{
			Constraint = Cast<UPhysicsConstraintComponent>(Clone->GetComponentByClass(UPhysicsConstraintComponent::StaticClass()));
			if (Constraint)
			{
				Constraint->BreakConstraint();
				Constraint->DestroyComponent();
			}

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
