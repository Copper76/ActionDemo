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
#include "Animation/AnimMontage.h"
#include "Engine/World.h"

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
	DOREPLIFETIME(AActionDemoCharacter, bSuppressWeapon);
	DOREPLIFETIME(AActionDemoCharacter, bPrevHolstered);
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

	PlayerCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	PlayerCamera->SetupAttachment(GetCapsuleComponent());
	PlayerCamera->SetRelativeLocation(CameraOffset);
	PlayerCamera->bUsePawnControlRotation = true;

	m_FlattenCamera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Flatten Camera"));
	m_FlattenCamera->SetupAttachment(GetCapsuleComponent());
	m_FlattenCamera->SetRelativeLocation(CameraOffset);

	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(PlayerCamera);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeLocation(FVector(-30.f, 0.f, -150.f));

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

	if (IsLocallyControlled())
	{
		ShowHudWidget();
	}
	else
	{
		RemoveHudWidget();
	}
}

void AActionDemoCharacter::ShowHudWidget()
{
	if (HudWidget || !HudWidgetClass || !IsLocallyControlled())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController)
	{
		return;
	}

	HudWidget = CreateWidget<UUserWidget>(PlayerController, HudWidgetClass);
	if (HudWidget)
	{
		HudWidget->AddToViewport();
	}
}

void AActionDemoCharacter::RemoveHudWidget()
{
	if (HudWidget)
	{
		HudWidget->RemoveFromParent();
		HudWidget = nullptr;
	}
}

void AActionDemoCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveHudWidget();

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
		}
	}
}

void AActionDemoCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	LastMouseInput = LookAxisVector;

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
		//MuzzlePointDir = Cast<APlayerController>(GetController())->RotationInput.GetNormalized() * MuzzleMaxTilt;
		//UE_LOG(LogTemp, Warning, TEXT("%s"), *MuzzlePointDir.ToString())
	}
}

bool AActionDemoCharacter::IsPlayingHolsterMontage() const
{
	if (!HolsterMontage || !Mesh1P)
	{
		return false;
	}
	const UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
	return AnimInstance && AnimInstance->Montage_IsPlaying(HolsterMontage);
}

bool AActionDemoCharacter::IsPlayingEquipMontage() const
{
	if (!EquipMontage || !Mesh1P)
	{
		return false;
	}
	const UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
	return AnimInstance && AnimInstance->Montage_IsPlaying(EquipMontage);
}

void AActionDemoCharacter::PlayFireMontage()
{
	if (!FireMontage || !Mesh1P)
	{
		return;
	}
	if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
	{
		AnimInstance->Montage_Play(FireMontage);
	}
}

void AActionDemoCharacter::PlayReloadMontage()
{
	if (!ReloadMontage || !Mesh1P)
	{
		return;
	}
	if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
	{
		AnimInstance->Montage_Play(ReloadMontage);
	}
}

void AActionDemoCharacter::StopReloadMontage(float BlendOutTime)
{
	if (!ReloadMontage || !Mesh1P)
	{
		return;
	}
	if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
	{
		if (AnimInstance->Montage_IsPlaying(ReloadMontage))
		{
			AnimInstance->Montage_Stop(BlendOutTime, ReloadMontage);
		}
	}
}

bool AActionDemoCharacter::IsHolstered() const
{
	return bHolstered || IsPlayingHolsterMontage() || IsPlayingEquipMontage();
}

void AActionDemoCharacter::SwapControl()
{
	if (bSuppressWeapon)
	{
		return;
	}

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

	if (IsLocallyControlled() && Mesh1P)
	{
		if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
		{
			// If canceling the other montage mid-way, start this one at the mirrored progress
			// (e.g. equip at 70% → stow starts at 30%).
			auto MirroredStart = [AnimInstance](UAnimMontage* Other, UAnimMontage* Next) -> float
			{
				if (!Other || !Next || !AnimInstance->Montage_IsPlaying(Other))
				{
					return 0.0f;
				}
				const float OtherLen = Other->GetPlayLength();
				if (OtherLen <= KINDA_SMALL_NUMBER)
				{
					return 0.0f;
				}
				const float Alpha = FMath::Clamp(AnimInstance->Montage_GetPosition(Other) / OtherLen, 0.0f, 1.0f);
				return (1.0f - Alpha) * Next->GetPlayLength();
			};

			if (bHolstered)
			{
				// Stow / suppress cancels reload mid-play (arms + weapon).
				if (Weapon)
				{
					Weapon->StopReload(0.0f);
				}

				if (HolsterMontage)
				{
					AnimInstance->Montage_Play(HolsterMontage, 1.0f, EMontagePlayReturnType::MontageLength,
						MirroredStart(EquipMontage, HolsterMontage));
				}
			}
			else if (EquipMontage)
			{
				AnimInstance->Montage_Play(EquipMontage, 1.0f, EMontagePlayReturnType::MontageLength,
					MirroredStart(HolsterMontage, EquipMontage));
			}
		}
	}

	if (!HasAuthority() && IsLocallyControlled())
	{
		ServerSetHolstered(bHolstered);
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

void AActionDemoCharacter::ForceHolsterForParkour()
{
	bSuppressWeapon = true;
	bPrevHolstered = bHolstered;
	SetHolstered(true);
}

void AActionDemoCharacter::TryRestoreWeaponAfterParkour()
{
	if (!bSuppressWeapon)
	{
		return;
	}

	bSuppressWeapon = false;
	SetHolstered(bPrevHolstered);
}

void AActionDemoCharacter::SpawnWeapon()
{
	if (!HasAuthority() || Weapon || !WeaponClass || !GetWorld())
	{
		return;
	}

	USkeletalMeshComponent* SpawnMesh = (IsLocallyControlled() && Mesh1P) ? Mesh1P : (GetMesh() ? GetMesh() : Mesh1P);
	const FName SpawnSocket = (SpawnMesh == Mesh1P) ? FirstPersonWeaponSocket : ThirdPersonWeaponSocket;
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

	Weapon = NewWeapon->FindComponentByClass<UTP_WeaponComponent>();
	if (!Weapon)
	{
		NewWeapon->Destroy();
		return;
	}
	Weapon->SetIsReplicated(true);

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
		Socket = FirstPersonWeaponSocket;
	}
	else if (GetMesh())
	{
		AttachMesh = GetMesh();
		Socket = ThirdPersonWeaponSocket;
	}

	if (!AttachMesh)
	{
		return;
	}

	Weapon->AttachToComponent(AttachMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
	Weapon->SetOnlyOwnerSee(AttachMesh == Mesh1P);
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
		Socket = FirstPersonWeaponSocket;
	}
	else if (GetMesh())
	{
		AttachMesh = GetMesh();
		Socket = ThirdPersonWeaponSocket;
	}

	if (!AttachMesh)
	{
		return;
	}

	Grappler->AttachToComponent(AttachMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
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

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PC->InputComponent))
	{
		Weapon->BindInput(EnhancedInput);
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
			SetAiming(false);
		}
	}

	OnHolsterChanged.Broadcast(bHolstered);
}

void AActionDemoCharacter::SetAiming(bool bInAiming)
{
	bAiming = bInAiming;
}

FRotator AActionDemoCharacter::GetAnimLookRotation() const
{
	FRotator ViewRotation = GetControlRotation();
	if (DemoMyCharacterMovementComponent)
	{
		const FVector GravityDirection = DemoMyCharacterMovementComponent->GetGravityDirection();
		if (!GravityDirection.Equals(FVector::DownVector))
		{
			const FQuat GravityRotation = FQuat::FindBetweenNormals(GravityDirection, FVector::DownVector);
			ViewRotation = (GravityRotation * ViewRotation.Quaternion()).Rotator();
		}
	}
	return ViewRotation;
}

void AActionDemoCharacter::UpdateCloseToWall()
{
	bCloseToWall = false;

	if (!IsLocallyControlled() || !PlayerCamera || !GetWorld())
	{
		return;
	}

	const FVector Start = PlayerCamera->GetComponentLocation();
	const FVector End = Start + PlayerCamera->GetForwardVector() * CloseToWallTraceDistance;

	FHitResult Hit;
	if (UPortalTraceLibrary::LineTraceSingleByChannel(GetWorld(), Hit, Start, End, ECC_Visibility, GetIgnoreCharacterParams()))
	{
		bCloseToWall = Hit.bBlockingHit;
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

	CurrentRoll = FMath::FInterpTo(CurrentRoll, -TargetRoll, DeltaTime, TiltSpeed);

	if (IsLocallyControlled())
	{
		UpdateCloseToWall();

		if (GetActorLocation().Z < KillZ)
		{
			RequestReturnToLastCheckpoint();
		}
		else
		{
			bCheckpointRespawnPending = false;
		}
	}

	float CrouchInterpTime = FMath::Min(1.0f, CrouchSpeed * DeltaTime);
	CrouchEyeOffset = (1.0f - CrouchInterpTime) * CrouchEyeOffset;

	// Simulated proxies: cable tip is replicated from the server each frame while active.
	if (!IsLocallyControlled() && bGrappleCableActive)
	{
		ApplyGrappleCableVisual(true, GrappleCableEndWS);
	}

	// Look runs before Tick; clear after so AnimBP sees this frame's mouse delta only.
	if (IsLocallyControlled())
	{
		LastMouseInput = FVector2D::ZeroVector;
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
	if (Weapon && Weapon->FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Weapon->FireSound, SoundLocation);
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