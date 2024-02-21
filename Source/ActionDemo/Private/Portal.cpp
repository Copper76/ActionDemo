// Fill out your copyright notice in the Description page of Project Settings.


#include "Portal.h"
#include "../ActionDemoCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "ActionDemo/Public/Teleportor.h"

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
	CameraRotation = FRotator(0.0f, 0.0f, 90.0f);
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
	VertComponent = GetActorRotation().Pitch / 90.0f;
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
		FVector PlayerOffset = Plane->GetComponentLocation() - PlayerController->PlayerCameraManager->GetCameraLocation();
		PlayerOffset = GetActorRotation().UnrotateVector(PlayerOffset);

		FRotator CamRotation = PlayerController->PlayerCameraManager->GetCameraRotation();
		//OtherPortal->CaptureComponent->SetRelativeRotation(FRotator(FRotator(0.0f, 180.0f, 0.0f) - GetActorRotation(), CamRotation.Yaw * VertComponent));
		if (VertComponent == 1.0f || VertComponent == -1.0f)
		{
			float RollRotation = (180.0f -GetActorRotation().Yaw + CamRotation.Yaw)* VertComponent + GetActorRotation().Roll;
			float PitchFactor = FMath::Cos(RollRotation * PI / 180.0f);
			float YawFactor = FMath::Sin(RollRotation * PI / 180.0f);
			float PitchRotation = (GetActorRotation().Pitch + CamRotation.Pitch) * PitchFactor;
			float YawRotation = (GetActorRotation().Pitch + CamRotation.Pitch) * YawFactor;
			OtherPortal->CaptureComponent->SetRelativeRotation(FRotator(FMath::Clamp(PitchRotation, -90.0f, 90.0f), YawRotation, RollRotation));
		}
		else
		{
			PlayerOffset.Z = -PlayerOffset.Z;
			//OtherPortal->CaptureComponent->SetRelativeRotation(FRotator(GetActorRotation().Pitch + CamRotation.Pitch + FMath::Abs(CamRotation.Yaw) * VertComponent, 180.0f - GetActorRotation().Yaw + CamRotation.Yaw * (1 - VertComponent), VertComponent == 0 ? 0.0f : 180.0f + GetActorRotation().Yaw - CamRotation.Yaw));//not the best on slope
			float YawRotation = 180.0f-GetActorRotation().Yaw + CamRotation.Yaw;
			float YawAdjustment = YawRotation;
			if (YawAdjustment > 180.0f)
			{
				YawAdjustment -= 360.0f;
			}
			float PitchRotation = GetActorRotation().Pitch + CamRotation.Pitch - FMath::Abs(YawAdjustment) * VertComponent;
			OtherPortal->CaptureComponent->SetRelativeRotation(FRotator(FMath::Clamp(PitchRotation, -90.0f, 90.0f), YawRotation, YawAdjustment * VertComponent));
		}
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
				CaptureComponent->HiddenActors.Add(hit.GetActor());
			}
		}

		if (bPlayerCanTeleport && !bJustTeleported && OtherPortal->CaptureComponent->GetRelativeLocation().X > 0.0f)
		{
			Teleport();
		}
		/**
		for (int32 i = TeleportingObjects.Num() - 1; i >= 0; i--)
		{
			FVector Location = GetActorRotation().UnrotateVector(Plane->GetComponentLocation() - TeleportingObjects[i]->GetActorLocation());
			UTeleportor* ActorTeleportor = Cast<UTeleportor>(TeleportingObjects[i]->GetComponentByClass(UTeleportor::StaticClass()));
			if (Location.X > 0.0f)
			{
				FVector NewLoc = ActorTeleportor->Clone->GetActorLocation();
				FRotator NewRot = ActorTeleportor->Clone->GetActorRotation();
				VDestroy(ActorTeleportor->Clone);
				ActorTeleportor->Clone = NULL;
				TeleportingObjects.RemoveAt(i);
				TeleportingObjects[i]->SetActorLocationAndRotation(NewLoc, NewRot);
			}
			else
			{
				FRotator Rotation = TeleportingObjects[i]->GetActorRotation() - GetActorRotation() + OtherPortal->GetActorRotation();
				if (VertComponent == 1.0f || VertComponent == -1.0f)
				{
				}
				else
				{
					Location.Z = -Location.Z;
				}
				Location += OtherPortal->GetActorLocation();
				Location = OtherPortal->GetActorRotation().RotateVector(Location);
				ActorTeleportor->Clone->SetActorLocationAndRotation(Location, Rotation);
			}
		}
		**/
	}
}

void APortal::Teleport()
{
	OtherPortal->bJustTeleported = true;
	float OtherVertComponent = FMath::Abs(OtherPortal->VertComponent);

	PlayerController->SetControlRotation(FRotator(OtherPortal->CaptureComponent->GetComponentRotation().Pitch, (OtherPortal->CaptureComponent->GetComponentRotation().Yaw + OtherPortal->CaptureComponent->GetComponentRotation().Roll), 0.0f));

	Character->SetActorLocation(OtherPortal->CaptureComponent->GetComponentLocation() + OtherPortal->GetActorRotation().RotateVector(-Character->CameraOffset), false, nullptr, ETeleportType::TeleportPhysics);

	if (VertComponent == 1.0f || VertComponent == -1.0f)
	{
		Character->AlignMovement(FRotator(OtherVertComponent == 1.0f ? OtherPortal->GetActorRotation().Pitch : OtherPortal->GetActorRotation().Pitch, OtherVertComponent == 1.0f ? 0.0f : OtherPortal->GetActorRotation().Yaw, 0.0f) + FRotator(GetActorRotation().Pitch, 0.0f, 0.0f));
	}
	else
	{
		float YawAdjustment = 180.0f - GetActorRotation().Yaw;
		if (YawAdjustment > 180.0f)
		{
			YawAdjustment -= 360.0f;
		}
		Character->AlignMovement(FRotator(OtherVertComponent == 1.0f ? -OtherPortal->GetActorRotation().Pitch : -OtherPortal->GetActorRotation().Pitch, OtherVertComponent == 1.0f ? 0.0f : OtherPortal->GetActorRotation().Yaw, 0.0f) + FRotator(-GetActorRotation().Pitch, YawAdjustment, 0.0f));
		//Character->AlignMovement(FRotator(-GetActorRotation().Pitch, YawAdjustment, 0.0f));
	}
	if (OtherVertComponent == 1.0f)
	{
		//TP to centre to avoid premature loop break
		//Character->SetActorLocation(OtherPortal->GetActorLocation() + OtherPortal->GetActorRotation().RotateVector(-Character->CameraOffset), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void APortal::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherPortal != nullptr)
	{
		OtherComp->SetCollisionResponseToChannel(PortalChannel, ECollisionResponse::ECR_Ignore);
		AActionDemoCharacter* OverlappedActor = Cast<AActionDemoCharacter>(OtherActor);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *OtherActor->GetActorLabel())
		if (OverlappedActor == nullptr)
		{
			UTeleportor* ActorTeleportor = Cast<UTeleportor>(OtherActor->GetComponentByClass(UTeleportor::StaticClass()));
			if (ActorTeleportor == nullptr)
			{
				ActorTeleportor = Cast<UTeleportor>(OtherActor->AddComponentByClass(UTeleportor::StaticClass(), false, FTransform::Identity, false));
				ActorTeleportor->Cloned = false;
				ActorTeleportor->RegisterComponent();
			}
			UE_LOG(LogTemp, Warning, TEXT("Cloned? %s"), ActorTeleportor->Cloned ? TEXT("TRUE") : TEXT("FALSE"))

			if (ActorTeleportor->Cloned == false)
			{
				FVector Location = GetActorRotation().UnrotateVector(Plane->GetComponentLocation() - OtherActor->GetActorLocation());
				FRotator Rotation = OtherActor->GetActorRotation() - GetActorRotation() + OtherPortal->GetActorRotation();
				if (VertComponent == 1.0f || VertComponent == -1.0f)
				{
				}
				else
				{
					Location.Z = -Location.Z;
				}
				Location = OtherPortal->GetActorRotation().RotateVector(Location);
				Location += OtherPortal->GetActorLocation();

				//Creating the cloned actor
				Location = OtherActor->GetActorLocation() + FVector(0.0f, 0.0f, 1000.0f);
				FTransform NewTrans = FTransform(Rotation, Location);
				//AActor* ClonedActor = Cast<AActor>(StaticDuplicateObject(OtherActor, OtherActor->GetOuter()));
				FActorSpawnParameters params;
				params.Template = OtherActor;
				AActor* ClonedActor = GetWorld()->SpawnActor<AActor>(OtherActor->GetClass(),params);
				if (ClonedActor)
				{
					ClonedActor->SetActorTransform(NewTrans);
					ClonedActor->AttachToActor(OtherActor->GetAttachParentActor(), FAttachmentTransformRules::KeepWorldTransform);
					ClonedActor->FinishSpawning(NewTrans);
					UE_LOG(LogTemp, Warning, TEXT("%s"), *ClonedActor->GetWorld()->GetName())
					//UE_LOG(LogTemp, Warning, TEXT("PORTAL LOC? %s"), *OtherPortal->GetActorLocation().ToString())
				}
				//UTeleportor* ClonedTeleportor = Cast<UTeleportor>(ClonedActor->AddComponentByClass(UTeleportor::StaticClass(), false, FTransform::Identity, false));
				//ClonedTeleportor->RegisterComponent();

				//ActorTeleportor->Clone = ClonedActor;
				//ClonedActor->SetActorLocationAndRotation(Location, Rotation);
				//UE_LOG(LogTemp, Warning, TEXT("%s"), *ClonedActor->GetActorLabel())
				//TeleportingObjects.Add(OtherActor);
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
	if (OtherPortal != nullptr)
	{
		OtherComp->SetCollisionResponseToChannel(PortalChannel, ECollisionResponse::ECR_Block);
		AActionDemoCharacter* OverlappedActor = Cast<AActionDemoCharacter>(OtherActor);
		if (OverlappedActor == nullptr)
		{
			return;
			UTeleportor* ActorTeleportor = Cast<UTeleportor>(OtherActor->GetComponentByClass(UTeleportor::StaticClass()));
			if (ActorTeleportor == nullptr)
			{
				return;
			}
			if (!ActorTeleportor->Cloned)
			{
				VDestroy(ActorTeleportor->Clone);
				ActorTeleportor->Clone = NULL;
				TeleportingObjects.Remove(OtherActor);
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

	//GEngine->ForceGarbageCollection(true);
}
