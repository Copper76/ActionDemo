// Fill out your copyright notice in the Description page of Project Settings.


#include "Portal.h"
#include "../ActionDemoCharacter.h"
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

	BoxCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("Teleporter"));
	AddOwnedComponent(BoxCollider);
	BoxCollider->SetupAttachment(SceneRoot);
	//BoxCollider->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
	BoxCollider->SetRelativeLocation(FVector(10.0f, 0.f, 0.0f));
	BoxCollider->SetRelativeScale3D(FVector(0.2f, 1.5f, 3.25f));
	BoxCollider->SetGenerateOverlapEvents(true);

	ReverseCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("ReverseCollider"));
	AddOwnedComponent(ReverseCollider);
	ReverseCollider->SetupAttachment(SceneRoot);
	//BoxCollider->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
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
	//These are set in setup later
	Character = Cast<AActionDemoCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));
	PlayerController = GetWorld()->GetFirstPlayerController();
	BoxCollider->OnComponentEndOverlap.AddDynamic(this, &APortal::OnTeleportEnd);
	ReverseCollider->OnComponentBeginOverlap.AddDynamic(this, &APortal::OnOverlapBegin);
	ReverseCollider->OnComponentEndOverlap.AddDynamic(this, &APortal::OnOverlapEnd);
}

//Setup when there is no other portal
void APortal::SetUp(AActionDemoCharacter* Player, AActor* TargetSurface)
{
	Character = Player;
	SetUpCollision(TargetSurface);
	PlayerController = Cast<APlayerController>(Character->GetController());
	Plane->SetMaterial(0,EmptyMaterial);
}

//Setup when there is an existing portal
void APortal::SetUp(AActionDemoCharacter* Player, AActor* TargetSurface, APortal* Portal)
{
	SetUp(Player, TargetSurface);

	//link to the other portal and have the other portal link to this
	Link(Portal);
	OtherPortal->Link(this);
}

void APortal::SetUpCollision(AActor* TargetSurface)
{
	const FVector RefUp = Character ? Character->GetActorUpVector() : FVector::UpVector;
	VertComponent = GetActorForwardVector() | RefUp.GetSafeNormal();
	VertComponent = FMath::RoundHalfToZero(1000.0f * VertComponent) / 1000.0f;
	if (!TargetComponents.IsEmpty())
	{
		ResetChannel();
	}
	TargetComponents.Empty();
	TargetSurface->GetComponents<UStaticMeshComponent>(TargetComponents);
	TargetComponentsChannels.Empty();
	for (int32 i = 0; i < TargetComponents.Num(); i++)
	{
		TargetComponentsChannels.Add(TargetComponents[i]->GetCollisionObjectType());
	}
	SetToNoCollide();
}

void APortal::Link(APortal* Portal)
{
	OtherPortal = Portal;
	Plane->SetMaterial(0, LinkedMaterial);
	if (ReverseCollider->IsOverlappingActor(Character) && OtherPortal != nullptr)
	{
		bPlayerCanTeleport = true;
	}
}

// Called every frame
void APortal::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!(OtherPortal == nullptr || PlayerController == nullptr || Character == nullptr))
	{
		CaptureComponent->HiddenActors.Empty();

		const FQuat EntryQ = GetActorQuat();
		const FQuat ExitQ = OtherPortal->GetActorQuat();
		const FQuat CamQ = PlayerController->PlayerCameraManager->GetCameraRotation().Quaternion();
		const FQuat FlipQ = FQuat(FVector::ZAxisVector, PI);

		FVector PlayerOffset = Plane->GetComponentLocation() - PlayerController->PlayerCameraManager->GetCameraLocation();
		PlayerOffset = EntryQ.UnrotateVector(PlayerOffset);
		if (FMath::Abs(VertComponent) < 1.0f)
		{
			PlayerOffset.Z = -PlayerOffset.Z;
		}

		const FQuat CaptureWorldQ = ExitQ * FlipQ * EntryQ.Inverse() * CamQ;
		OtherPortal->CaptureComponent->SetWorldRotation(CaptureWorldQ);
		OtherPortal->CaptureComponent->SetRelativeLocation(CameraOffset + PlayerOffset);
		
		TArray<FHitResult> ObstacleHits;
		//GetWorld()->LineTraceMultiByProfile(ObstacleHits, Plane->GetComponentLocation(), CaptureComponent->GetComponentLocation(), "OverlapAll", Character->GetIgnoreCharacterParams());

		//need a better solution eventually
		FCollisionShape CapShape = FCollisionShape::MakeSphere(48.0f);//TODO:Find a better solution
		GetWorld()->SweepMultiByProfile(ObstacleHits, Plane->GetComponentLocation(), CaptureComponent->GetComponentLocation(), FQuat::Identity, "OverlapAll", CapShape, Character->GetIgnoreCharacterParams());
		if (!ObstacleHits.IsEmpty())
		{
			for(FHitResult hit : ObstacleHits)
			{
				if (!TeleportingObjects.Contains(hit.GetActor()))
				{
					CaptureComponent->HiddenActors.Add(hit.GetActor());
				}
			}
		}

		if (bPlayerCanTeleport && !bJustTeleported && OtherPortal->CaptureComponent->GetRelativeLocation().X > 0.0f)
		{
			Teleport();
		}
	}
}

void APortal::Teleport()
{
	OtherPortal->bJustTeleported = true;

	const FQuat ExitQ = OtherPortal->GetActorQuat();
	const FQuat TravelQ = ExitQ * FQuat(FVector::ZAxisVector, PI) * GetActorQuat().Inverse();

	const FQuat CaptureQ = OtherPortal->CaptureComponent->GetComponentQuat();
	PlayerController->SetControlRotation(CaptureQ.Rotator());

	Character->SetActorLocation(OtherPortal->CaptureComponent->GetComponentLocation() + ExitQ.RotateVector(-Character->CameraOffset), false, nullptr, ETeleportType::TeleportPhysics);

	Character->AlignMovement(TravelQ, ExitQ);
}

void APortal::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherPortal != nullptr)
	{
		OtherComp->SetCollisionResponseToChannel(PortalChannel, ECollisionResponse::ECR_Ignore);
		TeleportingObjects.Add(OtherActor);
		AActionDemoCharacter* OverlappedActor = Cast<AActionDemoCharacter>(OtherActor);
		if (OverlappedActor == nullptr)
		{
			UTeleportor* ActorTeleportor = Cast<UTeleportor>(OtherActor->GetComponentByClass(UTeleportor::StaticClass()));
			if (ActorTeleportor == nullptr)
			{
				ActorTeleportor = Cast<UTeleportor>(OtherActor->AddComponentByClass(UTeleportor::StaticClass(), false, FTransform::Identity, false));
				ActorTeleportor->RegisterComponent();
			}

			if (ActorTeleportor->Cloned == false && ActorTeleportor->Inportal == false)
			{
				ActorTeleportor->Inportal = true;
				FVector Location = GetActorLocation() - OtherActor->GetActorLocation();
				Location.Z = -Location.Z;

				const FQuat Rotation = OtherPortal->GetActorQuat() * FQuat(FVector::ZAxisVector, PI) * GetActorQuat().Inverse() * OtherActor->GetActorQuat();
				Location = Rotation.RotateVector(Location);
				Location += OtherPortal->GetActorLocation();

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

					UTeleportor* ClonedTeleportor = Cast<UTeleportor>(ClonedActor->GetComponentByClass(UTeleportor::StaticClass()));
					ClonedTeleportor->Cloned = true;
					ActorTeleportor->Clone = ClonedActor;
					ClonedActor->SetActorLocationAndRotation(Location, Rotation);
					UE_LOG(LogTemp, Warning, TEXT("Cloned Actor Loc: %s"), *ClonedActor->GetActorLocation().ToString())
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

						Constraint->AttachToComponent(OtherActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
						Constraint->RegisterComponent();
					}
				}
			}
		}
		else
		{
			bPlayerCanTeleport = true;
		}
	}
}

void APortal::OnOverlapEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("EXIT: %s"), *OtherActor->GetActorLabel())
	OtherComp->SetCollisionResponseToChannel(PortalChannel, ECollisionResponse::ECR_Block);
	TeleportingObjects.Remove(OtherActor);
	if (OtherPortal != nullptr)
	{
		AActionDemoCharacter* OverlappedActor = Cast<AActionDemoCharacter>(OtherActor);
		if (OverlappedActor == nullptr)
		{
			UTeleportor* ActorTeleportor = Cast<UTeleportor>(OtherActor->GetComponentByClass(UTeleportor::StaticClass()));
			if (ActorTeleportor == nullptr)
			{
				return;
			}
			if (!ActorTeleportor->Cloned && ActorTeleportor->Inportal)
			{
				UPhysicsConstraintComponent* Constraint = Cast<UPhysicsConstraintComponent>(OtherActor->GetComponentByClass(UPhysicsConstraintComponent::StaticClass()));
				if (Constraint != nullptr)
				{
					Constraint->BreakConstraint();
					Constraint->DestroyComponent();
					GEngine->ForceGarbageCollection(true);
					Constraint = nullptr;
				}
				FVector Location = GetActorQuat().UnrotateVector(Plane->GetComponentLocation() - OtherActor->GetActorLocation());
				if (Location.X > 0.0f && ActorTeleportor->Clone != nullptr)
				{
					UE_LOG(LogTemp, Warning, TEXT("WILL TP"))
					FVector NewLoc = ActorTeleportor->Clone->GetActorLocation();
					FQuat NewRot = ActorTeleportor->Clone->GetActorQuat();
					const FQuat Rotation = OtherPortal->GetActorQuat() * FQuat(FVector::ZAxisVector, PI) * GetActorQuat().Inverse();
					UE_LOG(LogTemp, Warning, TEXT("Old Vel: %s"), *OtherComp->GetComponentVelocity().ToString())
					FVector NewVel = Rotation.RotateVector(OtherComp->GetComponentVelocity());
					OtherActor->SetActorLocationAndRotation(NewLoc, NewRot, false, nullptr, ETeleportType::TeleportPhysics);
					OtherComp->SetPhysicsLinearVelocity(NewVel);
					UE_LOG(LogTemp, Warning, TEXT("New Vel: %s"), *OtherComp->GetComponentVelocity().ToString())
				}
				ActorTeleportor->Inportal = false;
				VDestroy(ActorTeleportor->Clone);
				ActorTeleportor->Clone = nullptr;
			}
		}
		else
		{
			bPlayerCanTeleport = false;
		}
	}
}

void APortal::OnTeleportEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AActionDemoCharacter* OverlappedActor = Cast<AActionDemoCharacter>(OtherActor);
	if (OtherPortal != nullptr)
	{
		if (OverlappedActor != nullptr)
		{
			bJustTeleported = false;
		}
	}
}

void APortal::SetToNoCollide()
{
	for (int32 i = 0; i < TargetComponents.Num(); i++)
	{
		TargetComponents[i]->SetCollisionObjectType(PortalChannel);
	}
}
void APortal::ResetChannel()
{
	for (int32 i = 0; i < TargetComponents.Num(); i++)
	{
		TargetComponents[i]->SetCollisionObjectType(TargetComponentsChannels[i]);
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
