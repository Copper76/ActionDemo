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
#include "Engine/Engine.h"

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
	DOREPLIFETIME_CONDITION(AActionDemoCharacter, HP, COND_OwnerOnly);
	DOREPLIFETIME(AActionDemoCharacter, bAiming);
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
	// FP mannequin faces +Y; camera faces +X. Keep -90 yaw vs camera or arms sit 90° off.
	Mesh1P->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
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

	// Already holstered by default — play Equip into drawn.
	if (IsLocallyControlled())
	{
		if (Mesh1P)
		{
			if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
			{
				if (EquipMontage)
				{
					AnimInstance->Montage_Play(EquipMontage);
				}
			}
		}
		bHolstered = false;
		ApplyHolsterState();
		if (!HasAuthority())
		{
			ServerSetHolstered(false);
		}
	}
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

bool AActionDemoCharacter::IsPlayingEquipMontage() const
{
	if (!EquipMontage || !Mesh1P)
	{
		return false;
	}
	const UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
	return AnimInstance && AnimInstance->Montage_IsPlaying(EquipMontage);
}

void AActionDemoCharacter::TryInterruptEquipMontage(const float minimumPlayed, const float BlendOutTime)
{
	if (!EquipMontage || !Mesh1P)
	{
		return;
	}

	UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
	if (!AnimInstance || !AnimInstance->Montage_IsPlaying(EquipMontage))
	{
		return;
	}

	const float Length = EquipMontage->GetPlayLength();
	if (Length <= 0.0f || AnimInstance->Montage_GetPosition(EquipMontage) < Length * minimumPlayed)
	{
		return;
	}

	AnimInstance->Montage_Stop(BlendOutTime, EquipMontage);
}

void AActionDemoCharacter::UpdateEquipMontageInterrupts()
{
	if (!IsPlayingEquipMontage())
	{
		return;
	}

	if (IsSprinting() || bIsCrouched || bAiming)
	{
		TryInterruptEquipMontage(0.5f, 0.1f);
		return;
	}
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

void AActionDemoCharacter::StopFireMontage(float BlendOutTime)
{
	if (!FireMontage || !Mesh1P)
	{
		return;
	}
	if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
	{
		if (AnimInstance->Montage_IsPlaying(FireMontage))
		{
			AnimInstance->Montage_Stop(BlendOutTime, FireMontage);
		}
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
	return bHolstered || IsPlayingEquipMontage();
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
	// While parkour suppress is active, only Force/Restore (and their server RPCs) own bHolstered.
	if (bSuppressWeapon)
	{
		return;
	}

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
			if (bHolstered)
			{
				// Stow / suppress cancels reload / fire mid-play (arms + weapon).
				if (Weapon)
				{
					Weapon->StopReload(0.0f);
					Weapon->StopFireMontages(0.0f);
				}
				else
				{
					StopFireMontage(0.0f);
				}

				if (HolsterMontage)
				{
					AnimInstance->Montage_Play(HolsterMontage);
				}
			}
			else if (EquipMontage)
			{
				AnimInstance->Montage_Play(EquipMontage);
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
	if (bSuppressWeapon)
	{
		return;
	}
	if (bHolstered == bInHolstered)
	{
		return;
	}
	bHolstered = bInHolstered;
	ApplyHolsterState();
}

void AActionDemoCharacter::ForceHolsterForParkour()
{
	if (!bSuppressWeapon)
	{
		bPrevHolstered = bHolstered;
		bSuppressWeapon = true;
	}

	// Apply stow without going through SetHolstered -> ServerSetHolstered (that race
	// was flipping server bHolstered true before authority latched prev=drawn).
	if (!bHolstered)
	{
		bHolstered = true;
		ApplyHolsterState();

		if (IsLocallyControlled() && Mesh1P)
		{
			if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
			{
				if (Weapon)
				{
					Weapon->StopReload(0.0f);
					Weapon->StopFireMontages(0.0f);
				}
				else
				{
					StopFireMontage(0.0f);
				}
				if (HolsterMontage)
				{
					AnimInstance->Montage_Play(HolsterMontage);
				}
			}
		}
	}

	if (IsLocallyControlled() && !HasAuthority())
	{
		ServerForceHolsterForParkour(bPrevHolstered);
	}
}

void AActionDemoCharacter::ServerForceHolsterForParkour_Implementation(bool bClientPrevHolstered)
{
	if (!bSuppressWeapon)
	{
		bPrevHolstered = bClientPrevHolstered;
		bSuppressWeapon = true;
	}

	if (!bHolstered)
	{
		bHolstered = true;
		ApplyHolsterState();
	}
}

void AActionDemoCharacter::TryRestoreWeaponAfterParkour()
{
	if (!bSuppressWeapon)
	{
		return;
	}

	if (HasAuthority() || IsLocallyControlled())
	{
		bSuppressWeapon = false;
		if (bHolstered != bPrevHolstered)
		{
			bHolstered = bPrevHolstered;
			ApplyHolsterState();
			if (IsLocallyControlled() && Mesh1P)
			{
				if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
				{
					if (bHolstered)
					{
						if (HolsterMontage)
						{
							AnimInstance->Montage_Play(HolsterMontage);
						}
					}
					else if (EquipMontage)
					{
						AnimInstance->Montage_Play(EquipMontage);
					}
				}
			}
		}
	}

	if (!HasAuthority() && IsLocallyControlled())
	{
		ServerRestoreWeaponAfterParkour();
	}
}

void AActionDemoCharacter::ServerRestoreWeaponAfterParkour_Implementation()
{
	if (!bSuppressWeapon)
	{
		return;
	}

	const bool bRestoreHolstered = bPrevHolstered;
	bSuppressWeapon = false;
	if (bHolstered != bRestoreHolstered)
	{
		bHolstered = bRestoreHolstered;
		ApplyHolsterState();
	}
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

	// Infima attaches the weapon actor (SK_AssaultRifle + mag) to ik_hand_gun.
	// Attaching only TP_WeaponComponent leaves the animated mesh behind on the actor root.
	AActor* WeaponActor = Weapon->GetOwner();
	const FAttachmentTransformRules Rules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
	if (WeaponActor && WeaponActor != this)
	{
		WeaponActor->AttachToComponent(AttachMesh, Rules, Socket);
	}
	else
	{
		Weapon->AttachToComponent(AttachMesh, Rules, Socket);
	}

	const bool bOwnerOnly = (AttachMesh == Mesh1P);
	Weapon->SetOnlyOwnerSee(bOwnerOnly);
	if (USkeletalMeshComponent* AnimatedMesh = Weapon->GetAnimatedWeaponMesh())
	{
		AnimatedMesh->SetOnlyOwnerSee(bOwnerOnly);
		AnimatedMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
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

void AActionDemoCharacter::OnRep_HP()
{
	// Owner-only rep; HUD binds on the locally controlled pawn.
	OnHPChanged.Broadcast(HP, MaxHP);
}

void AActionDemoCharacter::ApplyDamage(float DamageAmount)
{
	if (!HasAuthority() || DamageAmount <= 0.0f || HP <= 0.0f)
	{
		return;
	}

	HP = FMath::Clamp(HP - DamageAmount, 0.0f, MaxHP);

	if (IsLocallyControlled())
	{
		OnHPChanged.Broadcast(HP, MaxHP);
	}

	if (HP <= 0.0f)
	{
		Die();
	}
}

void AActionDemoCharacter::Die()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	if (UCheckpointManager* Manager = GetWorld()->GetSubsystem<UCheckpointManager>())
	{
		Manager->RespawnCharacter(this);
	}
}

void AActionDemoCharacter::RestoreAfterRespawn()
{
	HP = MaxHP;
	if (IsLocallyControlled())
	{
		OnHPChanged.Broadcast(HP, MaxHP);
	}

	if (Weapon)
	{
		Weapon->StopReload();
		Weapon->Ammo = Weapon->MaxAmmo;
		OnAmmoChanged.Broadcast(Weapon->Ammo, Weapon->MaxAmmo);
	}

	// Respawn: clear parkour suppress, start holstered. Equip plays in ClientApplyRespawnOrientation.
	bSuppressWeapon = false;
	bPrevHolstered = false;
	bHolstered = true;
	if (Mesh1P)
	{
		if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
		{
			if (HolsterMontage)
			{
				AnimInstance->Montage_Stop(0.0f, HolsterMontage);
			}
			if (EquipMontage)
			{
				AnimInstance->Montage_Stop(0.0f, EquipMontage);
			}
		}
	}
	ApplyHolsterState();

	bCheckpointRespawnPending = false;
}

void AActionDemoCharacter::ClientApplyRespawnOrientation_Implementation(FVector GravityDirection, FRotator ControlRotation)
{
	if (UDemoMyCharacterMovementComponent* Move = DemoMyCharacterMovementComponent
		? DemoMyCharacterMovementComponent
		: Cast<UDemoMyCharacterMovementComponent>(GetCharacterMovement()))
	{
		const FVector Gravity = GravityDirection.GetSafeNormal();
		Move->SetGravityDirection(Gravity.IsNearlyZero() ? FVector::DownVector : Gravity);
		Move->StopMovementImmediately();
		Move->bJustTeleported = true;
	}

	if (Controller)
	{
		Controller->SetControlRotation(ControlRotation);
	}

	// Already holstered from RestoreAfterRespawn — play Equip into drawn.
	if (Mesh1P)
	{
		if (UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance())
		{
			if (HolsterMontage)
			{
				AnimInstance->Montage_Stop(0.0f, HolsterMontage);
			}
			if (EquipMontage)
			{
				AnimInstance->Montage_Play(EquipMontage);
			}
		}
	}
	bHolstered = false;
	ApplyHolsterState();
	if (!HasAuthority())
	{
		ServerSetHolstered(false);
	}
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
	if (bAiming == bInAiming)
	{
		return;
	}

	bAiming = bInAiming;
	OnAimChanged.Broadcast(bAiming);

	if (IsLocallyControlled() && !HasAuthority())
	{
		ServerSetAiming(bInAiming);
	}
}

void AActionDemoCharacter::ServerSetAiming_Implementation(bool bInAiming)
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
	const bool bSprint = Value.Get<bool>();
	if (bSprint)
	{
		if (Weapon)
		{
			Weapon->StopFireMontages(0.1f);
		}
		else
		{
			StopFireMontage(0.1f);
		}
	}
	DemoMyCharacterMovementComponent->ToggleSprint(bSprint);
}

void AActionDemoCharacter::ToggleCrouch(const FInputActionValue& Value)
{
	DemoMyCharacterMovementComponent->ToggleCrouch(Value.Get<bool>());
}

bool AActionDemoCharacter::CanJumpInternal_Implementation() const
{
	return JumpIsAllowedInternal();
}

void AActionDemoCharacter::ApplyCrouchCameraOffset()
{
	const FVector WithEye(CameraOffset.X, CameraOffset.Y, BaseEyeHeight + CrouchEyeOffset);
	if (PlayerCamera)
	{
		PlayerCamera->SetRelativeLocation(WithEye, false);
	}
	if (m_FlattenCamera)
	{
		m_FlattenCamera->SetRelativeLocation(WithEye, false);
	}
}

void AActionDemoCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (HalfHeightAdjust == 0) return;
	const float StartBaseEyeHeight = BaseEyeHeight;
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CrouchEyeOffset += StartBaseEyeHeight - BaseEyeHeight + HalfHeightAdjust;
}

void AActionDemoCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	if (HalfHeightAdjust == 0) return;
	const float StartBaseEyeHeight = BaseEyeHeight;
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CrouchEyeOffset += StartBaseEyeHeight - BaseEyeHeight - HalfHeightAdjust;
}

void AActionDemoCharacter::CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult)
{
	if (PlayerCamera)
	{
		PlayerCamera->GetCameraView(DeltaTime, OutResult);
		// The portal surface sits on the plane the eye is crossing. The default near plane deletes it.
		if (CurrentPortal)
		{
			OutResult.PerspectiveNearClipPlane = 1.f;
		}
	}
}

void AActionDemoCharacter::Landed(const FHitResult& Hit)
{
	DemoMyCharacterMovementComponent->Safe_FellOffTimer = 0.f;
}

void AActionDemoCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	CurrentRoll = FMath::FInterpTo(CurrentRoll, -TargetRoll, DeltaTime, TiltSpeed);

	if (IsLocallyControlled())
	{
		UpdateCloseToWall();
		UpdateEquipMontageInterrupts();

		if (GetActorLocation().Z < KillZ)
		{
			RequestReturnToLastCheckpoint();
		}
		else
		{
			bCheckpointRespawnPending = false;
		}
	}

	{
		const bool bCrouchEyeBlending = !FMath::IsNearlyZero(CrouchEyeOffset, 0.01f);
		CrouchEyeOffset = FMath::FInterpTo(CrouchEyeOffset, 0.f, DeltaTime, CrouchSpeed);
		if (bCrouchEyeBlending || !FMath::IsNearlyZero(CrouchEyeOffset, 0.01f))
		{
			ApplyCrouchCameraOffset();
		}
	}

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

	if (AActionDemoCharacter* HitCharacter = Cast<AActionDemoCharacter>(BulletHit.GetActor()))
	{
		const float WeaponDamage = Weapon ? Weapon->GetDamage() : 0.0f;
		if (WeaponDamage > 0.0f)
		{
			HitCharacter->ApplyDamage(WeaponDamage);
		}
		if (UDemoMyCharacterMovementComponent* HitMove = Cast<UDemoMyCharacterMovementComponent>(HitCharacter->GetCharacterMovement()))
		{
			HitMove->ApplyHitSpeedPenalty();
		}
		return;
	}

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