// Fill out your copyright notice in the Description page of Project Settings.


#include "Portal.h"
#include "../ActionDemoCharacter.h"
#include "Kismet/GameplayStatics.h"

#define POINT(x, c) DrawDebugPoint(GetWorld(), x, 10, c, !5.0f, 5.0f);
#define LINE(x1, x2, c) DrawDebugLine(GetWorld(), x1, x2, c, !5.0f, 5.0f);

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
	ReverseCollider->SetRelativeScale3D(FVector(2.0f, 1.5f, 3.25f));
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
	BoxCollider->OnComponentBeginOverlap.AddDynamic(this, &APortal::OnTeleportBegin);
	BoxCollider->OnComponentEndOverlap.AddDynamic(this, &APortal::OnTeleportEnd);
	ReverseCollider->OnComponentBeginOverlap.AddDynamic(this, &APortal::OnOverlapBegin);
	ReverseCollider->OnComponentEndOverlap.AddDynamic(this, &APortal::OnOverlapEnd);
}

//Setup when there is no other portal
void APortal::SetUp(AActionDemoCharacter* Player, UPrimitiveComponent* TargetSurface)
{
	Character = Player;
	this->Target = TargetSurface;
	PlayerController = Cast<APlayerController>(Character->GetController());
	Plane->SetMaterial(0,EmptyMaterial);
}

//Setup when there is an existing portal
void APortal::SetUp(AActionDemoCharacter* Player, UPrimitiveComponent* TargetSurface, APortal* Portal)
{
	Character = Player;
	this->Target = TargetSurface;
	PlayerController = Cast<APlayerController>(Character->GetController());

	//link to the other portal and have the other portal link to this
	Link(Portal);
	OtherPortal->Link(this);
}

void APortal::Link(APortal* Portal)
{
	OtherPortal = Portal;
	Plane->SetMaterial(0, LinkedMaterial);
	CanTeleport = true;
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
		PlayerOffset.Y = FMath::Clamp(PlayerOffset.Y, PlayerOffset.Y, 0.0f);

		FRotator CamRotation = PlayerController->PlayerCameraManager->GetCameraRotation();
		float VertComponent = GetActorRotation().Pitch / 90.0f;
		VertComponent = FMath::RoundHalfToZero(1000.0f * VertComponent) / 1000.0f;
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
	}
}

void APortal::OnTeleportBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AActionDemoCharacter* OverlappedActor = Cast<AActionDemoCharacter>(OtherActor);
	UE_LOG(LogTemp, Warning, TEXT("Activated: %s"), CanTeleport ? TEXT("YES") : TEXT("NO"));
	if (OverlappedActor != nullptr && CanTeleport && OtherPortal != nullptr)
	{
		OtherPortal->CanTeleport = false;
		FRotator RelativeRotation = OtherPortal->GetActorRotation() + OtherPortal->CaptureComponent->GetRelativeRotation();
		RelativeRotation.Roll = 0.0f;
		PlayerController->SetControlRotation(RelativeRotation);

		float VertComponent = GetActorRotation().Pitch / 90.0f;
		VertComponent = FMath::Abs(FMath::RoundHalfToZero(1000.0f * VertComponent) / 1000.0f);
		float OtherVertComponent = OtherPortal->GetActorRotation().Pitch / 90.0f;
		OtherVertComponent = FMath::Abs(FMath::RoundHalfToZero(1000.0f * OtherVertComponent) / 1000.0f);
		if (VertComponent == 1.0f)
		{
			OverlappedActor->SetActorLocation(OtherPortal->GetActorLocation() + OtherPortal->GetActorRotation().RotateVector(FVector::ForwardVector) * 55.0f, false, nullptr, ETeleportType::TeleportPhysics);
			Character->AlignMovement(FRotator(OtherVertComponent == 1.0f ? -OtherPortal->GetActorRotation().Pitch : OtherPortal->GetActorRotation().Pitch, OtherVertComponent == 1.0f? 180.0f : OtherPortal->GetActorRotation().Yaw, 0.0f) - FRotator(GetActorRotation().Pitch, 180.0f, 0.0f));
		}
		else
		{
			OverlappedActor->SetActorLocation(OtherPortal->GetActorLocation() + OtherPortal->CaptureComponent->GetRelativeLocation() + OtherPortal->GetActorRotation().RotateVector(FVector::ForwardVector) * 55.0f, false, nullptr, ETeleportType::TeleportPhysics);
			Character->AlignMovement(FRotator(OtherVertComponent == 1.0f ? -OtherPortal->GetActorRotation().Pitch : OtherPortal->GetActorRotation().Pitch, OtherVertComponent == 1.0f ? 180.0f : OtherPortal->GetActorRotation().Yaw, 0.0f) + FRotator(GetActorRotation().Pitch, 180.0f - GetActorRotation().Yaw, 0.0f));
		}
	}
}

void APortal::OnTeleportEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AActionDemoCharacter* OverlappedActor = Cast<AActionDemoCharacter>(OtherActor);
	if (OverlappedActor != nullptr && OtherPortal != nullptr)
	{
		CanTeleport = true;
	}
}

void APortal::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	//UE_LOG(LogTemp, Warning, TEXT("ENTERED"));
	//OverlappedComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
}

void APortal::OnOverlapEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	//UE_LOG(LogTemp, Warning, TEXT("Ended"));
	//OverlappedComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
}
