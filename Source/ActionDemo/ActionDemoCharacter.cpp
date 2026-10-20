// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActionDemoCharacter.h"
#include "ActionDemoProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureDefines.h" 
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "DemoMyCharacterMovementComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "CheckpointManager.h"
#include "CableComponent.h"

#include "Engine/DecalActor.h"


//////////////////////////////////////////////////////////////////////////
// AActionDemoCharacter

AActionDemoCharacter::AActionDemoCharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer.SetDefaultSubobjectClass<UDemoMyCharacterMovementComponent>(CharacterMovementComponentName))
{

	DemoMyCharacterMovementComponent = Cast<UDemoMyCharacterMovementComponent>(GetCharacterMovement());

	// Character doesnt have a rifle at start
	bHasRifle = false;
	
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	CameraOffset = FVector(-10.f, 0.f, 60.f);
		
	// Create a CameraComponent	
	PlayerCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	PlayerCamera->SetupAttachment(GetCapsuleComponent());
	PlayerCamera->SetRelativeLocation(CameraOffset); // Position the camera
	PlayerCamera->bUsePawnControlRotation = true;

	//// Create a CaptureComponent	
	m_FlattenCamera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Flatten Camera"));
	m_FlattenCamera->SetupAttachment(GetCapsuleComponent());
	m_FlattenCamera->SetRelativeLocation(CameraOffset); // Position the camera

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(PlayerCamera);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	//Mesh1P->SetRelativeRotation(FRotator(0.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-30.f, 0.f, -150.f));
	AimingPoseLoc = Mesh1P->GetRelativeLocation();
	AimingPoseRotation = Mesh1P->GetRelativeRotation();

	//FlashCoolDown = FlashMaxCoolDown;

	Grappler = CreateDefaultSubobject<UCableComponent>(TEXT("Grappling Hook"));
	//Grappler->AttachToComponent(this->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	Grappler->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	Grappler->SetHiddenInGame(true);
	//Grappler->SetAttachEndTo(this, RootComponent->GetDefaultSceneRootVariableName());
}

void AActionDemoCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}

	FVector2D ViewportSize;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	// if (m_FlattenCamera)
	// {
	// 	m_FlattenCamera->TextureTarget->InitAutoFormat(ViewportSize.X, ViewportSize.Y);
	// }

	// Temporarily disabled: CheckpointManager is world-global / not multiplayer-safe.
	// if (UCheckpointManager* CheckpointManager = GetWorld()->GetSubsystem<UCheckpointManager>())
	// {
	// 	CheckpointManager->AddCheckpoint(0, GetActorTransform());
	// 	if (CheckpointManager->GetCheckpointIndex() != 0)
	// 	{
	// 		//To be confirmed
	// 	}
	// }

	CameraOffset = PlayerCamera->GetRelativeLocation();
}

//////////////////////////////////////////////////////////////////////////// Input

void AActionDemoCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent))
	{
		//Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		//Crouching
		EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Triggered, this, &AActionDemoCharacter::ToggleCrouch);

		//Sprinting
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Triggered, this, &AActionDemoCharacter::ToggleSprint);

		//Sprinting
		EnhancedInputComponent->BindAction(FlashAction, ETriggerEvent::Triggered, this, &AActionDemoCharacter::Flash);

		//Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AActionDemoCharacter::Move);

		//Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AActionDemoCharacter::Look);

		//Swap Weapon
		EnhancedInputComponent->BindAction(SwapAction, ETriggerEvent::Triggered, this, &AActionDemoCharacter::SwapControl);

		//Grapple
		EnhancedInputComponent->BindAction(GrappleAction, ETriggerEvent::Triggered, this, &AActionDemoCharacter::Grapple);
	}
}


void AActionDemoCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		AddMovementInput(GetActorForwardVector(), MovementVector.Y);
		AddMovementInput(GetActorRightVector(), DemoMyCharacterMovementComponent->IsCustomMovementMode(CMOVE_Climb) ? MovementVector.X * 0.5f : MovementVector.X);
	}
}

void AActionDemoCharacter::CheckJumpInput(float DeltaTime)
{
	JumpCurrentCountPreJump = JumpCurrentCount;

	if (DemoMyCharacterMovementComponent)
	{
		if (bPressedJump)
		{
			if (DemoMyCharacterMovementComponent->TryMantle())
			{
				StopJumping();
				return;
			}

			// If this is the first jump and we're already falling,
			// then increment the JumpCount to compensate.
			const bool bFirstJump = JumpCurrentCount == 0;
			if (bFirstJump && DemoMyCharacterMovementComponent->IsFalling())
			{
				JumpCurrentCount++;
			}

			const bool bDidJump = CanJump() && DemoMyCharacterMovementComponent->DoJump(bClientUpdating);
			if (bDidJump)
			{
				// Transition from not (actively) jumping to jumping.
				if (!bWasJumping)
				{
					JumpCurrentCount++;
					JumpForceTimeRemaining = GetJumpMaxHoldTime();
					OnJumped();
				}
			}

			bWasJumping = bDidJump;

			DemoMyCharacterMovementComponent->TryFinishMantleTransition();
		}
	}
}

void AActionDemoCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
		//MuzzlePointDir = Cast<APlayerController>(GetController())->RotationInput.GetNormalized() * MuzzleMaxTilt;
		//UE_LOG(LogTemp, Warning, TEXT("%s"), *MuzzlePointDir.ToString())
	}
}

void AActionDemoCharacter::SwapControl()
{
	if (bOwnsRifle)
	{
		if (bHasRifle)
		{
			if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
			{
				if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
				{
					Subsystem->AddMappingContext(DefaultMappingContext, 2);
				}
				// Hides visible components
				bWeapon->SetActorHiddenInGame(true);

				// Disables collision components
				bWeapon->SetActorEnableCollision(false);

				// Stops the Actor from ticking
				bWeapon->SetActorTickEnabled(false);

				AimingPoseLoc = FVector(-30.0f, 0.0f, -150.0f);

				AimingPoseRotation = FRotator(0.0f, 0.0f, 0.0f);
			}
		}
		else
		{
			if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
			{
				if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
				{
					Subsystem->AddMappingContext(DefaultMappingContext, 0);
				}
				// Hides visible components
				bWeapon->SetActorHiddenInGame(false);

				// Disables collision components
				bWeapon->SetActorEnableCollision(true);

				// Stops the Actor from ticking
				bWeapon->SetActorTickEnabled(true);
			}
		}
		bHasRifle = !bHasRifle;
	}
}

void AActionDemoCharacter::Flash()
{
	DemoMyCharacterMovementComponent->Flash();
}

void AActionDemoCharacter::AlignMovement(const FQuat& RelativeRotation, const FQuat& ViewRotation)
{
	DemoMyCharacterMovementComponent->AlignMovement(RelativeRotation, ViewRotation);
}

void AActionDemoCharacter::Grapple()
{
	DemoMyCharacterMovementComponent->Grapple();
}

void AActionDemoCharacter::ToggleSprint(const FInputActionValue& Value)
{
	DemoMyCharacterMovementComponent->ToggleSprint(Value.Get<bool>());
}

void AActionDemoCharacter::ToggleCrouch(const FInputActionValue& Value)
{
	DemoMyCharacterMovementComponent->ToggleCrouch(Value.Get<bool>());
}

bool AActionDemoCharacter::CanJumpInternal_Implementation() const
{
	return JumpIsAllowedInternal();
}

void AActionDemoCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (HalfHeightAdjust == 0) return;
	float StartBaseEyeHeight = BaseEyeHeight;
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CrouchEyeOffset.Z += StartBaseEyeHeight - BaseEyeHeight + HalfHeightAdjust;
	PlayerCamera->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeight), false);
}

void AActionDemoCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (HalfHeightAdjust == 0) return;
	float StartBaseEyeHeight = BaseEyeHeight;
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CrouchEyeOffset.Z += StartBaseEyeHeight - BaseEyeHeight - HalfHeightAdjust;
	PlayerCamera->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeight), false);
}

void AActionDemoCharacter::CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult)
{
	if (PlayerCamera)
	{
		PlayerCamera->GetCameraView(DeltaTime, OutResult);
		const FQuat GravityAlign = FQuat::FindBetweenNormals(FVector::UpVector, -DemoMyCharacterMovementComponent->GetGravityDirection());
		OutResult.Location += GravityAlign.RotateVector(CrouchEyeOffset);
		// The portal surface sits on the plane the eye is crossing. The default near plane deletes it.
		if (CurrentPortal)
		{
			OutResult.PerspectiveNearClipPlane = 1.f;
		}
	}
}

void AActionDemoCharacter::Landed(const FHitResult& Hit)
{
	DemoMyCharacterMovementComponent->Safe_bFellOff = false;
}

void AActionDemoCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (GetActorLocation().Z < KillZ)
	{
		ReturnToLastCheckpoint();
	}

	CurrentRoll = FMath::FInterpTo(CurrentRoll, -TargetRoll, DeltaTime, TiltSpeed);

	float CrouchInterpTime = FMath::Min(1.0f, CrouchSpeed * DeltaTime);
	CrouchEyeOffset = (1.0f - CrouchInterpTime) * CrouchEyeOffset;
}

void AActionDemoCharacter::SetHasRifle(bool bNewHasRifle, AActor* Weapon)
{
	bHasRifle = bNewHasRifle;
	bOwnsRifle = true;
	bWeapon = Weapon;
}

bool AActionDemoCharacter::GetHasRifle()
{
	return bHasRifle;
}

APortal* AActionDemoCharacter::GetPortal(bool isBlue)
{
	return isBlue ? BluePortal : OrangePortal;
}

bool AActionDemoCharacter::IsSprinting() const
{
	return DemoMyCharacterMovementComponent ? DemoMyCharacterMovementComponent->bIsSprinting : false;
}

void AActionDemoCharacter::ReturnToLastCheckpoint()
{
	Grappler->EndLocation = FVector::ZeroVector;
	DemoMyCharacterMovementComponent->SetMovementMode(EMovementMode::MOVE_Falling);
	if (UCheckpointManager* CheckpointManager = GetWorld()->GetSubsystem<UCheckpointManager>())
	{
		FTransform LastCheckpoint = *CheckpointManager->GetLastCheckpoint();
		Controller->SetControlRotation(LastCheckpoint.GetRotation().Rotator());
		SetActorLocation(LastCheckpoint.GetLocation(), false, nullptr, ETeleportType::ResetPhysics);
	}
}

FCollisionQueryParams AActionDemoCharacter::GetIgnoreCharacterParams() const
{
	FCollisionQueryParams Params;

	TArray<AActor*> CharacterChildren;
	GetAllChildActors(CharacterChildren);
	Params.AddIgnoredActors(CharacterChildren);
	Params.AddIgnoredActor(this);

	return Params;
}

FCollisionQueryParams AActionDemoCharacter::GetIgnorePortalParams(bool isBlue) const
{
	FCollisionQueryParams Params;

	TArray<AActor*> CharacterChildren;
	GetAllChildActors(CharacterChildren);
	Params.AddIgnoredActors(CharacterChildren);
	Params.AddIgnoredActor(this);
	if (isBlue)
	{
		if (BluePortal != nullptr)
		{
			Params.AddIgnoredActor(BluePortal);
		}
	}
	else
	{
		if (OrangePortal != nullptr)
		{
			Params.AddIgnoredActor(OrangePortal);
		}
	}
	return Params;
}