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
#include "Net/UnrealNetwork.h"
#include "Engine/DecalActor.h"
#include "GameFramework/PlayerController.h"
#include "TP_WeaponComponent.h"
#include "TP_PickUpComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "PortalTraceLibrary.h"
#include "Portal.h"
#include "Kismet/GameplayStatics.h"
#include "CableComponent.h"

//////////////////////////////////////////////////////////////////////////
// AActionDemoCharacter

void AActionDemoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AActionDemoCharacter, BluePortal, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AActionDemoCharacter, OrangePortal, COND_OwnerOnly);
	DOREPLIFETIME(AActionDemoCharacter, CurrentPortal);
	DOREPLIFETIME(AActionDemoCharacter, Weapon);
	DOREPLIFETIME(AActionDemoCharacter, bHolstered);
	DOREPLIFETIME(AActionDemoCharacter, bGrappleCableActive);
	DOREPLIFETIME(AActionDemoCharacter, GrappleCableEndWS);
}

AActionDemoCharacter::AActionDemoCharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer.SetDefaultSubobjectClass<UDemoMyCharacterMovementComponent>(CharacterMovementComponentName))
{

	DemoMyCharacterMovementComponent = Cast<UDemoMyCharacterMovementComponent>(GetCharacterMovement());

	bHolstered = true;

	static ConstructorHelpers::FClassFinder<AActor> WeaponClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_PickUp_Rifle"));
	if (WeaponClassFinder.Succeeded())
	{
		WeaponClass = WeaponClassFinder.Class;
	}

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// Dual mesh: local player sees Mesh1P only; remotes see the full body.
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		BodyMesh->SetOwnerNoSee(true);
	}

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
	Super::BeginPlay();

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

	if (HasAuthority())
	{
		if (UCheckpointManager* Manager = GetWorld()->GetSubsystem<UCheckpointManager>())
		{
			Manager->RegisterCharacter(this);
		}
	}

	CameraOffset = PlayerCamera->GetRelativeLocation();

	SpawnWeapon();
	AttachWeapon();
	AttachGrappler();
	BindWeaponInput();
	ApplyHolsterState();
}

void AActionDemoCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	AttachWeapon();
	AttachGrappler();
}

void AActionDemoCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && GetWorld())
	{
		if (UCheckpointManager* Manager = GetWorld()->GetSubsystem<UCheckpointManager>())
		{
			Manager->UnregisterCharacter(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AActionDemoCharacter::ServerPlacePortal_Implementation(bool bIsBlue, FVector_NetQuantize Centre, FRotator Rotation, TSubclassOf<APortal> PortalClass)
{
	PlacePortal(bIsBlue, Centre, Rotation, PortalClass);
}

void AActionDemoCharacter::PlacePortal(bool bIsBlue, const FVector& Centre, const FRotator& Rotation, TSubclassOf<APortal> PortalClass)
{
	if (!HasAuthority() || !PortalClass || !GetWorld())
	{
		return;
	}

	APortal* Slot = bIsBlue ? BluePortal : OrangePortal;
	APortal* Other = bIsBlue ? OrangePortal : BluePortal;

	if (!Slot)
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.Owner = this;
		SpawnInfo.Instigator = this;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Slot = GetWorld()->SpawnActor<APortal>(PortalClass, Centre, Rotation, SpawnInfo);
		if (bIsBlue)
		{
			BluePortal = Slot;
		}
		else
		{
			OrangePortal = Slot;
		}
		if (Slot)
		{
			Slot->Setup(nullptr, bIsBlue, Other);
		}
	}
	else
	{
		Slot->SetActorLocationAndRotation(Centre, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		Slot->ForceNetUpdate();
	}
}

void AActionDemoCharacter::RequestPortalTeleport(APortal* EntryPortal)
{
	if (!IsLocallyControlled() || !EntryPortal || !EntryPortal->OtherPortal)
	{
		return;
	}

	if (GetCurrentPortal() != EntryPortal || !EntryPortal->IsPointBehindPlane(GetActorLocation(), -2.0f))
	{
		return;
	}

	EntryPortal->Teleport(this);

	// Autonomous proxies need the server to commit; listen-server / standalone already applied on authority.
	if (GetLocalRole() == ROLE_AutonomousProxy)
	{
		ServerTeleportThroughPortal(EntryPortal);
	}
}

void AActionDemoCharacter::ServerTeleportThroughPortal_Implementation(APortal* EntryPortal)
{
	if (!EntryPortal || !EntryPortal->OtherPortal)
	{
		return;
	}

	const bool bTagged = GetCurrentPortal() == EntryPortal;
	const bool bBehind = EntryPortal->IsPointBehindPlane(GetActorLocation(), 8.0f);
	if (!bTagged && !bBehind)
	{
		return;
	}

	EntryPortal->Teleport(this);
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
	SetHolstered(!bHolstered);
}

void AActionDemoCharacter::SetHolstered(bool bInHolstered)
{
	if (bHolstered == bInHolstered)
	{
		return;
	}

	bHolstered = bInHolstered;
	ApplyHolsterState();

	if (!HasAuthority() && IsLocallyControlled())
	{
		ServerSetHolstered(bInHolstered);
	}
}

void AActionDemoCharacter::ServerSetHolstered_Implementation(bool bInHolstered)
{
	if (bHolstered == bInHolstered)
	{
		return;
	}
	bHolstered = bInHolstered;
	ApplyHolsterState();
}

void AActionDemoCharacter::SpawnWeapon()
{
	if (!HasAuthority() || Weapon || !WeaponClass || !GetWorld())
	{
		return;
	}

	USkeletalMeshComponent* SpawnMesh = Mesh1P ? Mesh1P : GetMesh();
	const FName SpawnSocket = (SpawnMesh == Mesh1P) ? FName(TEXT("GripPoint")) : ThirdPersonWeaponSocket;
	const FTransform SpawnTM = SpawnMesh
		? SpawnMesh->GetSocketTransform(SpawnSocket)
		: GetActorTransform();

	AActor* NewWeapon = GetWorld()->SpawnActorDeferred<AActor>(
		WeaponClass,
		SpawnTM,
		this,
		this,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!NewWeapon)
	{
		return;
	}

	NewWeapon->SetReplicates(true);
	NewWeapon->SetReplicateMovement(false);
	NewWeapon->FinishSpawning(SpawnTM);
	NewWeapon->SetOwner(this);

	Weapon = NewWeapon;
	AttachWeapon();
}

void AActionDemoCharacter::AttachWeapon()
{
	if (!Weapon)
	{
		return;
	}

	USkeletalMeshComponent* AttachMesh = nullptr;
	FName Socket = NAME_None;

	if (IsLocallyControlled() && Mesh1P)
	{
		AttachMesh = Mesh1P;
		Socket = FName(TEXT("GripPoint"));
	}
	else if (GetMesh())
	{
		AttachMesh = GetMesh();
		Socket = ThirdPersonWeaponSocket;
	}
	else if (Mesh1P)
	{
		AttachMesh = Mesh1P;
		Socket = FName(TEXT("GripPoint"));
	}

	if (AttachMesh)
	{
		Weapon->AttachToComponent(AttachMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
	}
}

void AActionDemoCharacter::AttachGrappler()
{
	if (!Grappler)
	{
		return;
	}

	USkeletalMeshComponent* AttachMesh = nullptr;
	FName Socket = NAME_None;

	if (IsLocallyControlled() && Mesh1P)
	{
		AttachMesh = Mesh1P;
		Socket = FName(TEXT("GripPoint"));
	}
	else if (GetMesh())
	{
		AttachMesh = GetMesh();
		Socket = ThirdPersonWeaponSocket;
	}
	else if (Mesh1P)
	{
		AttachMesh = Mesh1P;
		Socket = FName(TEXT("GripPoint"));
	}

	if (AttachMesh)
	{
		Grappler->AttachToComponent(AttachMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
	}
}

void AActionDemoCharacter::BindWeaponInput()
{
	if (!IsLocallyControlled() || !Weapon)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC)
	{
		return;
	}

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PC->InputComponent);
	UTP_WeaponComponent* WeaponComp = Weapon->FindComponentByClass<UTP_WeaponComponent>();
	if (EnhancedInput && WeaponComp)
	{
		WeaponComp->BindInput(EnhancedInput);
	}
}

void AActionDemoCharacter::OnRep_Weapon()
{
	AttachWeapon();
	BindWeaponInput();
	ApplyHolsterState();
}

void AActionDemoCharacter::OnRep_Holstered()
{
	ApplyHolsterState();
}

void AActionDemoCharacter::ApplyHolsterState()
{
	if (IsLocallyControlled())
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
			{
				Subsystem->AddMappingContext(DefaultMappingContext, bHolstered ? 2 : 0);
			}
		}

		if (bHolstered)
		{
			AimingPoseLoc = FVector(-30.0f, 0.0f, -150.0f);
			AimingPoseRotation = FRotator(0.0f, 0.0f, 0.0f);
		}
	}

	if (Weapon)
	{
		Weapon->SetActorHiddenInGame(bHolstered);
		Weapon->SetActorTickEnabled(!bHolstered);
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

	if (IsLocallyControlled())
	{
		if (GetActorLocation().Z < KillZ)
		{
			RequestReturnToLastCheckpoint();
		}
		else
		{
			bCheckpointRespawnPending = false;
		}
	}

	CurrentRoll = FMath::FInterpTo(CurrentRoll, -TargetRoll, DeltaTime, TiltSpeed);

	float CrouchInterpTime = FMath::Min(1.0f, CrouchSpeed * DeltaTime);
	CrouchEyeOffset = (1.0f - CrouchInterpTime) * CrouchEyeOffset;

	// Simulated proxies: cable tip is replicated from the server each frame while active.
	if (!IsLocallyControlled() && bGrappleCableActive)
	{
		ApplyGrappleCableVisual(true, GrappleCableEndWS);
	}
}

void AActionDemoCharacter::ServerProcessWeaponFire_Implementation(FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd, TSubclassOf<AActor> HoleClass, float ImpulseStrength)
{
	ProcessWeaponFire(TraceStart, TraceEnd, HoleClass, ImpulseStrength);
}

void AActionDemoCharacter::ProcessWeaponFire(FVector TraceStart, FVector TraceEnd, TSubclassOf<AActor> HoleClass, float ImpulseStrength)
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	MulticastWeaponFireFX(GetActorLocation());

	FHitResult BulletHit;
	UPortalTraceLibrary::LineTraceSingleByChannel(
		GetWorld(),
		BulletHit,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		GetIgnoreCharacterParams());

	if (!BulletHit.IsValidBlockingHit() || !BulletHit.GetActor() || BulletHit.GetActor()->IsA<APortal>())
	{
		return;
	}

	const FVector ShotDir = (TraceEnd - TraceStart).GetSafeNormal();

	if (HoleClass)
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.Owner = this;
		SpawnInfo.Instigator = this;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* BulletHole = GetWorld()->SpawnActor<AActor>(HoleClass, BulletHit.ImpactPoint, BulletHit.ImpactNormal.Rotation(), SpawnInfo);
		if (BulletHole)
		{
			BulletHole->SetReplicates(true);
			BulletHole->AttachToActor(BulletHit.GetActor(), FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	if (UPrimitiveComponent* HitComp = BulletHit.GetComponent())
	{
		if (AActor* HitOwner = HitComp->GetOwner())
		{
			if (HitOwner->IsRootComponentMovable())
			{
				HitComp->AddImpulse(ShotDir * ImpulseStrength);
			}
		}
	}
}

void AActionDemoCharacter::MulticastWeaponFireFX_Implementation(FVector_NetQuantize SoundLocation)
{
	if (UTP_WeaponComponent* WeaponComp = Weapon ? Weapon->FindComponentByClass<UTP_WeaponComponent>() : nullptr)
	{
		if (WeaponComp->FireSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, WeaponComp->FireSound, SoundLocation);
		}
	}
}

void AActionDemoCharacter::PushGrappleCableVisual(bool bActive, const FVector& EndWorldLocation)
{
	ApplyGrappleCableVisual(bActive, EndWorldLocation);

	if (HasAuthority())
	{
		bGrappleCableActive = bActive;
		GrappleCableEndWS = EndWorldLocation;
	}
}

void AActionDemoCharacter::ApplyGrappleCableVisual(bool bActive, const FVector& EndWorldLocation)
{
	if (!Grappler)
	{
		return;
	}

	Grappler->SetHiddenInGame(!bActive);
	if (bActive)
	{
		AttachGrappler();
		Grappler->EndLocation = Grappler->GetComponentTransform().InverseTransformPosition(EndWorldLocation);
	}
	else
	{
		Grappler->EndLocation = FVector::ZeroVector;
	}
}

void AActionDemoCharacter::OnRep_GrappleCable()
{
	ApplyGrappleCableVisual(bGrappleCableActive, GrappleCableEndWS);
}

APortal* AActionDemoCharacter::GetPortal(bool isBlue)
{
	return isBlue ? BluePortal : OrangePortal;
}

bool AActionDemoCharacter::IsSprinting() const
{
	return DemoMyCharacterMovementComponent ? DemoMyCharacterMovementComponent->bIsSprinting : false;
}

void AActionDemoCharacter::RequestReturnToLastCheckpoint()
{
	if (!IsLocallyControlled() || bCheckpointRespawnPending)
	{
		return;
	}

	bCheckpointRespawnPending = true;
	ServerRequestReturnToCheckpoint();
}

void AActionDemoCharacter::ServerRequestReturnToCheckpoint_Implementation()
{
	PushGrappleCableVisual(false, GetActorLocation());
	if (DemoMyCharacterMovementComponent)
	{
		DemoMyCharacterMovementComponent->SetMovementMode(EMovementMode::MOVE_Falling);
	}

	if (UCheckpointManager* Manager = GetWorld()->GetSubsystem<UCheckpointManager>())
	{
		Manager->RespawnCharacter(this);
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