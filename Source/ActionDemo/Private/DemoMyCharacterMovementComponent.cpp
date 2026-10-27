// Fill out your copyright notice in the Description page of Project Settings.


#include "DemoMyCharacterMovementComponent.h"
#include "PortalTraceLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Engine/NetSerialization.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Character.h"
#include "CableComponent.h"
#include "Net/UnrealNetwork.h"

float MacroDuration = 5.f;
#define POINT(x, c) DrawDebugPoint(GetWorld(), x, 10, c, !MacroDuration, MacroDuration);
#define CAPSULE(x, c) DrawDebugCapsule(GetWorld(), x, CapHH(), CapR(), FQuat::Identity, c, !MacroDuration, MacroDuration);
#define CROUCHCAPSULE(x, c) DrawDebugCapsule(GetWorld(), x, GetCrouchedHalfHeight(), CapR(), FQuat::Identity, c, !MacroDuration, MacroDuration);

FNetworkPredictionData_Client* UDemoMyCharacterMovementComponent::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr);

	if (ClientPredictionData == nullptr)
	{
		UDemoMyCharacterMovementComponent* MutableThis = const_cast<UDemoMyCharacterMovementComponent*>(this);

		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Demo(*this);
		MutableThis->ClientPredictionData->MaxSmoothNetUpdateDist = 92.0f;
		MutableThis->ClientPredictionData->NoSmoothNetUpdateDist = 140.0f;
	}

	return ClientPredictionData;
}

void UDemoMyCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	Safe_bWantsToSprint = (Flags & FSavedMove_Demo::FLAG_Sprint) != 0;

	Safe_bPressedFlash = (Flags & FSavedMove_Demo::FLAG_Flash) != 0;

	Safe_bWantsToGrapple = (Flags & FSavedMove_Demo::FLAG_Grapple) != 0;
}

void UDemoMyCharacterMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	if (const FCharacterNetworkMoveData_Demo* MoveData = static_cast<const FCharacterNetworkMoveData_Demo*>(GetCurrentNetworkMoveData()))
	{
		SetGravityDirection(MoveData->GravityDirection);
	}

	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UDemoMyCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	// Base CMC serializes GravityDirection on corrections but does not apply it.
	if (!MoveResponse.IsGoodMove())
	{
		SetGravityDirection(MoveResponse.ClientAdjustment.GravityDirection);
	}

	Super::ClientHandleMoveResponse(MoveResponse);
}

void UDemoMyCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	
	if (MovementMode == MOVE_Walking)
	{
		// Acceleration is the networked move intent (LastInputVector is empty on server replay).
		const bool bCharacterAiming = DemoCharacterOwner && DemoCharacterOwner->IsAiming();
		if (!bCharacterAiming && Safe_bWantsToSprint && (Acceleration.GetSafeNormal() | UpdatedComponent->GetForwardVector()) > 0.5f)
		{
			MaxWalkSpeed = Sprint_MaxWalkSpeed;
			bIsSprinting = true;
		}
		else
		{
			MaxWalkSpeed = Walk_MaxWalkSpeed;
			bIsSprinting = false;
		}
	}

	Safe_bWantsToCrouch = bWantsToCrouch;
}

void UDemoMyCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	if (PreviousMovementMode == MOVE_Walking)
	{
		bIsSprinting = false;
	}

	if (PreviousMovementMode == MOVE_Falling)
	{
		CharacterOwner->JumpCurrentCount = 0;
		CharacterOwner->JumpCurrentCountPreJump = 0;
	}

	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Slide) ExitSlide();

	if (IsCustomMovementMode(CMOVE_Slide)) EnterSlide();

	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_WallRun)
	{
		DemoCharacterOwner->TargetRoll = 0.0f;
	}

	if (IsCustomMovementMode(CMOVE_WallRun)) 
	{
		DemoCharacterOwner->TargetRoll = Safe_bWallRunIsRight ? -10.0f : 10.0f;
	}

	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == CMOVE_Climb)
	{
		Safe_bFellOff = true;
		if (DemoCharacterOwner)
		{
			DemoCharacterOwner->TryRestoreWeaponAfterParkour();
		}
	}

	if (IsCustomMovementMode(CMOVE_Climb))
	{
		ClimbTime = 0.0f;
		if (DemoCharacterOwner)
		{
			DemoCharacterOwner->ForceHolsterForParkour();
		}
	}

	if (IsWallRunning() && GetOwnerRole() == ROLE_SimulatedProxy)
	{
		FVector Start = UpdatedComponent->GetComponentLocation();
		FVector End = Start + UpdatedComponent->GetRightVector() * CapR() * 2;
		FHitResult WallHit;
		Safe_bWallRunIsRight = UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
	}
}

bool UDemoMyCharacterMovementComponent::IsMovingOnGround() const
{
	return Super::IsMovingOnGround() || IsCustomMovementMode(CMOVE_Slide);
}

bool UDemoMyCharacterMovementComponent::CanCrouchInCurrentState() const
{
	return Super::CanCrouchInCurrentState() && (IsMovingOnGround() || IsGrappling());
}

void UDemoMyCharacterMovementComponent::Crouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}

	if (!bClientSimulation && !CanCrouchInCurrentState())
	{
		return;
	}

	// See if collision is already at desired size.
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == GetCrouchedHalfHeight())
	{
		if (!bClientSimulation)
		{
			CharacterOwner->bIsCrouched = true;
		}
		CharacterOwner->OnStartCrouch(0.f, 0.f);
		return;
	}

	if (bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// restore collision size before crouching
		ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
		CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
		bShrinkProxyCapsule = true;
	}

	// Change collision size to crouching dimensions
	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	// Height is not allowed to be smaller than radius.
	const float ClampedCrouchedHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, GetCrouchedHalfHeight());
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, ClampedCrouchedHalfHeight);
	float HalfHeightAdjust = (OldUnscaledHalfHeight - ClampedCrouchedHalfHeight);
	float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	if (!bClientSimulation)
	{
		const FVector GravityDir = GetNormalisedGravity();
		const FQuat CapsuleRotation = UpdatedComponent->GetComponentQuat();

		// Crouching to a larger height? (this is rare)
		if (ClampedCrouchedHalfHeight > OldUnscaledHalfHeight)
		{
			FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
			FCollisionResponseParams ResponseParam;
			InitCollisionParams(CapsuleParams, ResponseParam);
			const bool bEncroached = GetWorld()->OverlapBlockingTestByChannel(UpdatedComponent->GetComponentLocation() + GravityDir * ScaledHalfHeightAdjust, CapsuleRotation,
				UpdatedComponent->GetCollisionObjectType(), GetPawnCapsuleCollisionShape(SHRINK_None), CapsuleParams, ResponseParam);

			// If encroached, cancel
			if (bEncroached)
			{
				CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, OldUnscaledHalfHeight);
				return;
			}
		}

		if (bCrouchMaintainsBaseLocation)
		{
			// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
			UpdatedComponent->MoveComponent(GravityDir * ScaledHalfHeightAdjust, CapsuleRotation, true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
		}

		CharacterOwner->bIsCrouched = true;
	}

	bForceNextFloorCheck = true;

	// OnStartCrouch takes the change from the Default size, not the current one (though they are usually the same).
	const float MeshAdjust = ScaledHalfHeightAdjust;
	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	HalfHeightAdjust = (DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - ClampedCrouchedHalfHeight);
	ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	AdjustProxyCapsuleSize();
	CharacterOwner->OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			ClientData->MeshTranslationOffset -= FVector(0.f, 0.f, MeshAdjust);
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

void UDemoMyCharacterMovementComponent::UnCrouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();

	// See if collision is already at desired size.
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight())
	{
		if (!bClientSimulation)
		{
			CharacterOwner->bIsCrouched = false;
		}
		CharacterOwner->OnEndCrouch(0.f, 0.f);
		return;
	}

	const float CurrentCrouchedHalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float HalfHeightAdjust = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - OldUnscaledHalfHeight;
	const float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
	const FVector GravityDir = GetNormalisedGravity();
	const FQuat CapsuleRotation = UpdatedComponent->GetComponentQuat();

	// Grow to uncrouched size.
	check(CharacterOwner->GetCapsuleComponent());

	if (!bClientSimulation)
	{
		// Try to stay in place and see if the larger capsule fits. We use a slightly taller capsule to avoid penetration.
		const UWorld* MyWorld = GetWorld();
		const float SweepInflation = UE_KINDA_SMALL_NUMBER * 10.f;
		FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CrouchTrace), false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(CapsuleParams, ResponseParam);

		// Compensate for the difference between current capsule size and standing size
		const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - ScaledHalfHeightAdjust); // Shrink by negative amount, so actually grow it.
		const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
		bool bEncroached = true;

		if (!bCrouchMaintainsBaseLocation)
		{
			// Expand in place
			bEncroached = MyWorld->OverlapBlockingTestByChannel(PawnLocation, CapsuleRotation, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				// Try adjusting capsule position to see if we can avoid encroachment.
				if (ScaledHalfHeightAdjust > 0.f)
				{
					// Shrink to a short capsule, sweep down to base to find where that would hit something, and then try to stand up from there.
					float PawnRadius, PawnHalfHeight;
					CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);
					const float ShrinkHalfHeight = PawnHalfHeight - PawnRadius;
					const float TraceDist = PawnHalfHeight - ShrinkHalfHeight;
					const FVector Down = GravityDir * TraceDist;

					FHitResult Hit(1.f);
					const FCollisionShape ShortCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, ShrinkHalfHeight);
					const bool bBlockingHit = MyWorld->SweepSingleByChannel(Hit, PawnLocation, PawnLocation + Down, CapsuleRotation, CollisionChannel, ShortCapsuleShape, CapsuleParams);
					if (Hit.bStartPenetrating)
					{
						bEncroached = true;
					}
					else
					{
						// Compute where the base of the sweep ended up, and see if we can stand there
						const float DistanceToBase = (Hit.Time * TraceDist) + ShortCapsuleShape.Capsule.HalfHeight;
						const FVector NewLoc = PawnLocation + GravityDir * (DistanceToBase - StandingCapsuleShape.Capsule.HalfHeight - SweepInflation - MIN_FLOOR_DIST / 2.f);
						bEncroached = MyWorld->OverlapBlockingTestByChannel(NewLoc, CapsuleRotation, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
						if (!bEncroached)
						{
							// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
							UpdatedComponent->MoveComponent(NewLoc - PawnLocation, CapsuleRotation, false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						}
					}
				}
			}
		}
		else
		{
			// Expand while keeping base location the same.
			FVector StandingLocation = PawnLocation - GravityDir * (StandingCapsuleShape.GetCapsuleHalfHeight() - CurrentCrouchedHalfHeight);
			bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, CapsuleRotation, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				if (IsMovingOnGround())
				{
					// Something might be just barely overhead, try moving down closer to the floor to avoid it.
					const float MinFloorDist = UE_KINDA_SMALL_NUMBER * 10.f;
					if (CurrentFloor.bBlockingHit && CurrentFloor.FloorDist > MinFloorDist)
					{
						StandingLocation += GravityDir * (CurrentFloor.FloorDist - MinFloorDist);
						bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, CapsuleRotation, CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
					}
				}
			}

			if (!bEncroached)
			{
				// Commit the change in location.
				UpdatedComponent->MoveComponent(StandingLocation - PawnLocation, CapsuleRotation, false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
				bForceNextFloorCheck = true;
			}
		}

		// If still encroached then abort.
		if (bEncroached)
		{
			return;
		}

		CharacterOwner->bIsCrouched = false;
	}
	else
	{
		bShrinkProxyCapsule = true;
	}

	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow the capsule
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), true);

	const float MeshAdjust = ScaledHalfHeightAdjust;
	AdjustProxyCapsuleSize();
	CharacterOwner->OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			ClientData->MeshTranslationOffset += FVector(0.f, 0.f, MeshAdjust);
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}
}

void UDemoMyCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	if (IsCustomMovementMode(CMOVE_Grapple) && (bWantsToCrouch || !Safe_bWantsToGrapple))
	{
		ExitGrapple();
	}

	if (MovementMode == MOVE_Walking && bWantsToCrouch)
	{
		if (CanSlide())
		{
			EnterSlide();
		}
	}

	if (IsCustomMovementMode(CMOVE_Slide) && !bWantsToCrouch)
	{
		ExitSlide();
	}

	if (IsFalling()) 
	{
		TryWallRun();
		TryClimb();
	}

	const bool IsAuthProxy = CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled();

	if (Safe_bPressedFlash)
	{
		Safe_bPressedFlash = false;

		if ((!IsAuthProxy || (GetWorld()->GetTimeSeconds() > FlashTime + AuthFlashCooldown)) && (GetWorld()->GetTimeSeconds() > FlashTime + FlashCooldown) && CanFlash())
		{
			DoFlash();
		}
	}

	if (Safe_bWantsToGrapple)
	{
		if ((!IsAuthProxy || (GetWorld()->GetTimeSeconds() > GrappleTime + AuthGrappleCooldown)) && (GetWorld()->GetTimeSeconds() > GrappleTime + GrappleCooldown) && CanGrapple())
		{
			DoGrapple();
		}
	}

	if (GrappleStage != EGrapplingStage::GRAPPLE_NONE)
	{
		TickGrapple(DeltaSeconds);
	}

	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

void UDemoMyCharacterMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateAfterMovement(DeltaSeconds);
	if (!HasAnimRootMotion() && Safe_bHadAnimRootMotion && IsMovementMode(MOVE_Flying))
	{
		SetMovementMode(MOVE_Walking);
	}

	Safe_bHadAnimRootMotion = HasAnimRootMotion();
}


void UDemoMyCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	Super::PhysCustom(DeltaTime, Iterations);

	switch (CustomMovementMode)
	{
	case CMOVE_Slide:
		PhysSlide(DeltaTime, Iterations);
		break;
	case CMOVE_WallRun:
		PhysWallRun(DeltaTime, Iterations);
		break;
	case CMOVE_Climb:
		PhysClimb(DeltaTime, Iterations);
		break;
	case CMOVE_Grapple:
		PhysGrapple(DeltaTime, Iterations);
		break;
	default:
		UE_LOG(LogTemp, Fatal, TEXT("INVALID MOVEMENT MODE"));
	}
}


void UDemoMyCharacterMovementComponent::ToggleSprint(bool SprintValue)
{
	Safe_bWantsToSprint = SprintValue;
}

void UDemoMyCharacterMovementComponent::ToggleCrouch(bool CrouchValue)
{
	bWantsToCrouch = CrouchValue;
}

void UDemoMyCharacterMovementComponent::Flash()
{
	Safe_bPressedFlash = true;
}

bool UDemoMyCharacterMovementComponent::IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == InCustomMovementMode;
}

ECustomMovementMode UDemoMyCharacterMovementComponent::GetCustomMovementMode() const
{
	return MovementMode == MOVE_Custom ? static_cast<ECustomMovementMode>(CustomMovementMode) : CMOVE_None;
}

FVector2D UDemoMyCharacterMovementComponent::GetAnimMoveVelocity() const
{
	if (!UpdatedComponent)
	{
		return FVector2D::ZeroVector;
	}

	const FVector Local = UpdatedComponent->GetComponentQuat().UnrotateVector(Velocity);
	// Capsule local: X = forward, Y = right, Z = up
	const float Right = Local.Y;
	const float Along = IsClimbing() ? Local.Z : Local.X;
	return FVector2D(Right, Along);
}

float UDemoMyCharacterMovementComponent::GetJumpVelocityForAnim() const
{
	return FVector::DotProduct(Velocity, -GetNormalisedGravity());
}

UDemoMyCharacterMovementComponent::UDemoMyCharacterMovementComponent() : Super()
{
	SetNetworkMoveDataContainer(DemoNetworkMoveDataContainer);
	SetIsReplicatedByDefault(true);
}

void UDemoMyCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UDemoMyCharacterMovementComponent, bIsHitSlow, COND_OwnerOnly);
}

void UDemoMyCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();

	DemoCharacterOwner = Cast<AActionDemoCharacter>(GetOwner());
}

UDemoMyCharacterMovementComponent::FSavedMove_Demo::FSavedMove_Demo()
	: Super()
	, Saved_GravityDirection(UCharacterMovementComponent::DefaultGravityDirection)
{
}

#pragma region internet stuff

bool UDemoMyCharacterMovementComponent::FSavedMove_Demo::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	FSavedMove_Demo* NewDemoMove = static_cast<FSavedMove_Demo*>(NewMove.Get());

	if (Saved_bWantsToSprint != NewDemoMove->Saved_bWantsToSprint) {
		return false;
	}

	if (Saved_bWallRunIsRight != NewDemoMove->Saved_bWallRunIsRight) {
		return false;
	}

	if (Saved_bFellOff != NewDemoMove->Saved_bFellOff) {
		return false;
	}

	if (Saved_bPressedFlash != NewDemoMove->Saved_bPressedFlash) {
		return false;
	}

	if (Saved_bWantsToGrapple != NewDemoMove->Saved_bWantsToGrapple)
	{
		return false;
	}

	if (Saved_GrappleStage != NewDemoMove->Saved_GrappleStage)
	{
		return false;
	}

	if (!Saved_GravityDirection.Equals(NewDemoMove->Saved_GravityDirection))
	{
		return false;
	}

	return FSavedMove_Character::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void UDemoMyCharacterMovementComponent::FSavedMove_Demo::Clear()
{
	FSavedMove_Character::Clear();

	Saved_bWantsToSprint = 0;
	Saved_bWallRunIsRight = 0;
	Saved_bFellOff = 0;
	Saved_bPressedFlash = 0;
	Saved_bWantsToGrapple = 0;
	Saved_bGrappleTargetMovable = 0;
	Saved_GrappleStage = GRAPPLE_NONE;
	Saved_GrapplePivot = FVector::ZeroVector;
	Saved_GrapplePivotNormal = FVector::ZeroVector;
	Saved_GrappleTarget = nullptr;
	Saved_GravityDirection = UCharacterMovementComponent::DefaultGravityDirection;
}

uint8 UDemoMyCharacterMovementComponent::FSavedMove_Demo::GetCompressedFlags() const
{
	uint8 Result = FSavedMove_Character::GetCompressedFlags();

	if (Saved_bWantsToSprint) Result |= FLAG_Sprint;
	if (Saved_bPressedFlash) Result |= FLAG_Flash;
	if (Saved_bWantsToGrapple) Result |= FLAG_Grapple;

	return Result;
}

void UDemoMyCharacterMovementComponent::FSavedMove_Demo::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	UDemoMyCharacterMovementComponent* CharacterMovement = Cast<UDemoMyCharacterMovementComponent>(C->GetCharacterMovement());

	Saved_bWantsToSprint = CharacterMovement->Safe_bWantsToSprint;

	Saved_bWallRunIsRight = CharacterMovement->Safe_bWallRunIsRight;

	Saved_bFellOff = CharacterMovement->Safe_bFellOff;

	Saved_bPressedFlash = CharacterMovement->Safe_bPressedFlash;

	Saved_bWantsToGrapple = CharacterMovement->Safe_bWantsToGrapple;
	Saved_GrappleStage = CharacterMovement->GrappleStage;
	Saved_GrapplePivot = CharacterMovement->Grapple_Pivot;
	Saved_GrapplePivotNormal = CharacterMovement->Grapple_PivotNormal;
	Saved_GrappleTarget = CharacterMovement->Grapple_Target;
	Saved_bGrappleTargetMovable = CharacterMovement->Grapple_TargetMovable;

	Saved_bHadAnimRootMotion = CharacterMovement->Safe_bHadAnimRootMotion;

	Saved_GravityDirection = CharacterMovement->GetGravityDirection();
}

void UDemoMyCharacterMovementComponent::FSavedMove_Demo::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	UDemoMyCharacterMovementComponent* CharacterMovement = Cast<UDemoMyCharacterMovementComponent>(C->GetCharacterMovement());

	CharacterMovement->Safe_bWantsToSprint = Saved_bWantsToSprint;

	CharacterMovement->Safe_bWallRunIsRight = Saved_bWallRunIsRight;

	CharacterMovement->Safe_bFellOff = Saved_bFellOff;

	CharacterMovement->Safe_bPressedFlash = Saved_bPressedFlash;

	CharacterMovement->Safe_bWantsToGrapple = Saved_bWantsToGrapple;
	CharacterMovement->GrappleStage = static_cast<EGrapplingStage>(Saved_GrappleStage);
	CharacterMovement->Grapple_Pivot = Saved_GrapplePivot;
	CharacterMovement->Grapple_PivotNormal = Saved_GrapplePivotNormal;
	CharacterMovement->Grapple_Target = Saved_GrappleTarget.Get();
	CharacterMovement->Grapple_TargetMovable = Saved_bGrappleTargetMovable;

	CharacterMovement->Safe_bHadAnimRootMotion = Saved_bHadAnimRootMotion;

	CharacterMovement->SetGravityDirection(Saved_GravityDirection);
}

void UDemoMyCharacterMovementComponent::FCharacterNetworkMoveData_Demo::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);

	const FSavedMove_Demo& DemoMove = static_cast<const FSavedMove_Demo&>(ClientMove);
	GravityDirection = DemoMove.Saved_GravityDirection;
}

bool UDemoMyCharacterMovementComponent::FCharacterNetworkMoveData_Demo::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	if (!Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType))
	{
		return false;
	}

	NetSerializeOptionalValue(Ar.IsSaving(), Ar, GravityDirection, UCharacterMovementComponent::DefaultGravityDirection, PackageMap);
	return !Ar.IsError();
}

UDemoMyCharacterMovementComponent::FNetworkPredictionData_Client_Demo::FNetworkPredictionData_Client_Demo(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement)
{
	//Called super
}

FSavedMovePtr UDemoMyCharacterMovementComponent::FNetworkPredictionData_Client_Demo::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Demo());
}
#pragma endregion

float UDemoMyCharacterMovementComponent::GetMaxSpeed() const
{
	if (MovementMode == MOVE_Custom)
	{
		switch (CustomMovementMode)
		{
		case CMOVE_Slide:
			return Slide_MaxSpeed;
		case CMOVE_WallRun:
			return WallRun_MaxSpeed;
		case CMOVE_Climb:
			return Climb_MaxSpeed;
		case CMOVE_Grapple:
			return Grapple_MaxSpeed;
		default:
			UE_LOG(LogTemp, Fatal, TEXT("INVALID MOVEMENT MODE"));
			return -1.0f;
		}
	}

	const float MaxSpeed = Super::GetMaxSpeed();

	if (MovementMode != MOVE_Walking)
	{
		return MaxSpeed;
	}

	float SpeedScale = 1.0f;
	if (bIsHitSlow)
	{
		SpeedScale = FMath::Min(SpeedScale, HitMaxSpeedModifier);
	}
	if (DemoCharacterOwner && DemoCharacterOwner->IsAiming())
	{
		SpeedScale = FMath::Min(SpeedScale, AimMaxSpeedModifier);
	}
	return SpeedScale * MaxSpeed;
}

void UDemoMyCharacterMovementComponent::ApplyHitSpeedPenalty()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	bIsHitSlow = true;

	if (HitSpeedPenaltyDuration <= 0.0f)
	{
		ClearHitSpeedPenalty();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		TimerManager.ClearTimer(HitSpeedPenaltyTimer);
		TimerManager.SetTimer(
			HitSpeedPenaltyTimer,
			this,
			&UDemoMyCharacterMovementComponent::ClearHitSpeedPenalty,
			HitSpeedPenaltyDuration,
			false);
	}
}

void UDemoMyCharacterMovementComponent::ClearHitSpeedPenalty()
{
	bIsHitSlow = false;
}

float UDemoMyCharacterMovementComponent::GetMaxBrakingDeceleration() const
{
	if (MovementMode != MOVE_Custom) return Super::GetMaxBrakingDeceleration();

	switch (CustomMovementMode)
	{
	case CMOVE_Slide:
		return Slide_MaxBrakingDeceleration;
	case CMOVE_WallRun:
		return 0.0f;
	case CMOVE_Climb:
		return 0.0f;
	case CMOVE_Grapple:
		return 0.0f;
	default:
		UE_LOG(LogTemp, Fatal, TEXT("INVALID MOVEMENT MODE"));
		return -1.0f;
	}
}

bool UDemoMyCharacterMovementComponent::CanAttemptJump() const
{
	return IsJumpAllowed() && (IsMovingOnGround() || IsFalling() || IsWallRunning() || IsCrouching() || IsSliding() || IsClimbing());
}

bool UDemoMyCharacterMovementComponent::DoJump(bool bReplayingMoves)
{
	bool bWasWallRunning = IsWallRunning();
	bool bWasClimbing = IsClimbing();
	if (Super::DoJump(bReplayingMoves))
	{
		if (bWasWallRunning)
		{
			FVector Start = UpdatedComponent->GetComponentLocation();
			FVector CastDelta = UpdatedComponent->GetRightVector() * CapR() * 2;
			FVector End = Safe_bWallRunIsRight ? Start + CastDelta : Start - CastDelta;
			FHitResult WallHit;
			UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
			Velocity += WallHit.Normal * WallRun_JumpOffForce;
		}
		else if (bWasClimbing)
		{
			FVector Start = UpdatedComponent->GetComponentLocation() + FVector::DownVector * CapHH();
			FVector End = Start + UpdatedComponent->GetForwardVector() * CapR() * 2;
			FHitResult WallHit;
			UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
			Velocity += WallHit.Normal * Climb_JumpOffForce;
		}
		return true;
	}
	return false;
}

void UDemoMyCharacterMovementComponent::AlignMovement(const FQuat& RelativeRotation, const FQuat& ViewRotation)
{
	UE_LOG(LogTemp, Warning, TEXT("PRE-VELOCITY: %s"), *Velocity.ToString());
	Velocity = RelativeRotation.RotateVector(Velocity).GetClampedToMaxSize(Portal_MaxTravelSpeed);
	Acceleration = RelativeRotation.RotateVector(Acceleration);
	UE_LOG(LogTemp, Warning, TEXT("After-VELOCITY: %s"), *Velocity.ToString());

	// View up is "up" as seen through the portal. Gravity is the opposite, snapped to the strongest world axis.
	const FVector GravityHint = -ViewRotation.GetUpVector();
	const FVector AbsGravity = GravityHint.GetAbs();

	FVector SnappedGravity = FVector::ZeroVector;
	if (AbsGravity.X >= AbsGravity.Y && AbsGravity.X >= AbsGravity.Z)
	{
		SnappedGravity.X = FMath::Sign(GravityHint.X);
	}
	else if (AbsGravity.Y >= AbsGravity.Z)
	{
		SnappedGravity.Y = FMath::Sign(GravityHint.Y);
	}
	else
	{
		SnappedGravity.Z = FMath::Sign(GravityHint.Z);
	}

	SetGravityDirection(SnappedGravity);

	bJustTeleported = true;
}

#pragma region Slide
void UDemoMyCharacterMovementComponent::EnterSlide()
{
	bWantsToCrouch = true;
	const FVector PlanarDir = FVector::VectorPlaneProject(Velocity, GetNormalisedGravity()).GetSafeNormal();
	Velocity += PlanarDir * Slide_EnterImpulse;
	SetMovementMode(MOVE_Custom, CMOVE_Slide);
}

void UDemoMyCharacterMovementComponent::ExitSlide()
{
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(UpdatedComponent->GetForwardVector().GetSafeNormal2D(), -GetNormalisedGravity()).ToQuat();
	FHitResult Hit;
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, true, Hit);
	if (CharacterOwner->bPressedJump)
	{
		SetMovementMode(MOVE_Falling);
	}
	else 
	{
		SetMovementMode(MOVE_Walking);
	}
}

bool UDemoMyCharacterMovementComponent::CanSlide() const
{
	return FVector::VectorPlaneProject(Velocity, GetNormalisedGravity()).SizeSquared() > FMath::Square(Slide_MinSpeed);
}


void UDemoMyCharacterMovementComponent::PhysSlide(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME) 
	{
		return;
	}

	if (!CanSlide())
	{
		SetMovementMode(MOVE_Walking);
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	bJustTeleported = false;
	bool bCheckedFall = false;
	bool bTriedLedgeMove = false;
	float remainingTime = DeltaTime;

	const FVector NormalisedGravity = GetNormalisedGravity();

	Velocity += Slide_GravityForce * NormalisedGravity * DeltaTime;

	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)))
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		// Save current values
		UPrimitiveComponent* const OldBase = GetMovementBase();
		const FVector PreviousBaseLocation = (OldBase != NULL) ? OldBase->GetComponentLocation() : FVector::ZeroVector;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FFindFloorResult OldFloor = CurrentFloor;

		// Ensure velocity is horizontal.
		MaintainHorizontalGroundVelocity();
		const FVector OldVelocity = Velocity;

		FVector SlopeForce = CurrentFloor.HitResult.Normal;
		SlopeForce -= FVector::DotProduct(SlopeForce, NormalisedGravity) * NormalisedGravity;
		Velocity += SlopeForce * Slide_GravityForce * DeltaTime;

		Acceleration = Acceleration.ProjectOnTo(UpdatedComponent->GetRightVector().GetSafeNormal2D()) * Slide_SteerFactor;

		// Apply acceleration
		CalcVelocity(timeTick, GroundFriction * Slide_Friction, false, GetMaxBrakingDeceleration());

		// Compute move parameters
		const FVector MoveVelocity = Velocity;
		const FVector Delta = timeTick * MoveVelocity;
		const bool bZeroDelta = Delta.IsNearlyZero();
		FStepDownResult StepDownResult;
		bool bFloorWalkable = CurrentFloor.IsWalkableFloor();

		if (bZeroDelta)
		{
			remainingTime = 0.f;
		}
		else
		{
			// try to move forward
			MoveAlongFloor(MoveVelocity, timeTick, &StepDownResult);

			if (IsFalling())
			{
				// pawn decided to jump up
				const float DesiredDist = Delta.Size();
				if (DesiredDist > KINDA_SMALL_NUMBER)
				{
					const float ActualDist = (UpdatedComponent->GetComponentLocation() - OldLocation).Size2D();
					remainingTime += timeTick * (1.f - FMath::Min(1.f, ActualDist / DesiredDist));
				}
				StartNewPhysics(remainingTime, Iterations);
				return;
			}
			else if (IsSwimming()) //just entered water
			{
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}
		}

		// Update floor.
		// StepUp might have already done it for us.
		if (StepDownResult.bComputedFloor)
		{
			CurrentFloor = StepDownResult.FloorResult;
		}
		else
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, NULL);
		}


		// check for ledges here
		const bool bCheckLedges = !CanWalkOffLedges();
		if (bCheckLedges && !CurrentFloor.IsWalkableFloor())
		{
			// calculate possible alternate movement
			const FVector GravDir = FVector(0.f, 0.f, -1.f);
			const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldLocation, Delta, GravDir);
			if (!NewDelta.IsZero())
			{
				// first revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, false);

				// avoid repeated ledge moves if the first one fails
				bTriedLedgeMove = true;

				// Try new movement direction
				Velocity = NewDelta / timeTick;
				remainingTime += timeTick;
				continue;
			}
			else
			{
				// see if it is OK to jump
				// @todo collision : only thing that can be problem is that oldbase has world collision on
				bool bMustJump = bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;

				// revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, true);
				remainingTime = 0.f;
				break;
			}
		}
		else
		{
			// Validate the floor check
			if (CurrentFloor.IsWalkableFloor())
			{
				if (ShouldCatchAir(OldFloor, CurrentFloor))
				{
					HandleWalkingOffLedge(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, OldLocation, timeTick);
					if (IsMovingOnGround())
					{
						// If still walking, then fall. If not, assume the user set a different mode they want to keep.
						StartFalling(Iterations, remainingTime, timeTick, Delta, OldLocation);
					}
					return;
				}

				AdjustFloorHeight();
				SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
			}
			else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
				FHitResult Hit(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit.TraceStart + FVector(0.f, 0.f, MAX_FLOOR_DIST);
				const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
				ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
				bForceNextFloorCheck = true;
			}

			// check if just entered water
			if (IsSwimming())
			{
				StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we need to start falling.
			if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = bJustTeleported || bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;
			}
		}

		// Allow overlap events and such to change physics state and velocity
		if (IsMovingOnGround() && bFloorWalkable)
		{
			// Make velocity reflect actual move
			if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && timeTick >= MIN_TICK_TIME)
			{
				// TODO-RootMotionSource: Allow this to happen during partial override Velocity, but only set allowed axes?
				Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick;
				MaintainHorizontalGroundVelocity();
			}
		}

		// If we didn't move at all this iteration then abort (since future iterations will also be stuck).
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.f;
			break;
		}
	}


	FHitResult Hit;
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(Velocity.GetSafeNormal2D(), -GetGravityDirection()).ToQuat();
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, Hit);
}
#pragma endregion

#pragma region Mantle
bool UDemoMyCharacterMovementComponent::TryMantle()
{
	return false;
	if (!IsMovementMode(MOVE_Walking) && !IsMovementMode(MOVE_Falling)) return false;

	const FVector NormalisedGravity = GetNormalisedGravity();

	FVector BaseLoc = UpdatedComponent->GetComponentLocation() + NormalisedGravity * CapHH();
	FVector Fwd = FVector::VectorPlaneProject(UpdatedComponent->GetForwardVector(), -NormalisedGravity).GetSafeNormal();
	float MaxHeight = CapHH() + Mantle_ReachHeight;
	float CosMMWSA = FMath::Cos(FMath::DegreesToRadians(Mantle_MinWallSteepnessAngle));
	float CosMMSA = FMath::Cos(FMath::DegreesToRadians(Mantle_MaxSurfaceAngle));
	float CosMMAA = FMath::Cos(FMath::DegreesToRadians(Mantle_MaxAlignmentAngle));

	FHitResult FrontHit;
	float CheckDistance = FMath::Clamp(Velocity | Fwd, CapR() + 30.0f, Mantle_MaxDistance);
	FVector FrontStart = BaseLoc - NormalisedGravity * (MaxStepHeight - 1);
	for (int i = 0; i < 6; i++)
	{
		if (UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), FrontHit, FrontStart, FrontStart + Fwd * CheckDistance, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams())) break;
		FrontStart -= NormalisedGravity * (2.0f * CapHH() - (MaxStepHeight - 1)) / 5;
	}
	if (!FrontHit.IsValidBlockingHit()) return false;
	float CosWallSteepnessAngle = FrontHit.Normal | -NormalisedGravity;
	if (FMath::Abs(CosWallSteepnessAngle) > CosMMWSA || (Fwd | -FrontHit.Normal) < CosMMAA) return false;

	TArray<FHitResult> HeightHits;
	FHitResult SurfaceHit;
	FVector WallUp = FVector::VectorPlaneProject(-NormalisedGravity, FrontHit.Normal).GetSafeNormal();
	float WallCos = -NormalisedGravity | FrontHit.Normal;
	float WallSin = FMath::Sqrt(1 - WallCos * WallCos);
	FVector TraceStart = FrontHit.Location + Fwd + WallUp * (MaxHeight - (MaxStepHeight - 1)) / WallSin;

	if (!UPortalTraceLibrary::LineTraceMultiByProfile(GetWorld(), HeightHits, TraceStart, FrontHit.Location + Fwd, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams())) return false;

	for (const FHitResult& Hit : HeightHits)
	{
		if (Hit.IsValidBlockingHit())
		{
			SurfaceHit = Hit;
			break;
		}
	}
	if (!SurfaceHit.IsValidBlockingHit() || (SurfaceHit.Normal | -NormalisedGravity) < CosMMSA) return false;
	float Height = (SurfaceHit.Location - BaseLoc) | -NormalisedGravity;
	if (Height > MaxHeight) return false;

	float SurfaceCos = -NormalisedGravity | SurfaceHit.Normal;
	float SurfaceSin = FMath::Sqrt(1 - SurfaceCos * SurfaceCos);
	FVector ClearCapLoc = SurfaceHit.Location + Fwd * CapR() - NormalisedGravity * (CapHH() + 1 + CapR() * 2 * SurfaceSin);
	FCollisionShape CapShape = FCollisionShape::MakeCapsule(CapR(), CapHH());
	FCollisionShape CrouchedCapShape = FCollisionShape::MakeCapsule(CapR(), GetCrouchedHalfHeight());
	if (GetWorld()->OverlapAnyTestByProfile(ClearCapLoc, FQuat::Identity, "BlockAll", CapShape, DemoCharacterOwner->GetIgnoreCharacterParams()))
	{
		if (!GetWorld()->OverlapAnyTestByProfile(ClearCapLoc, FQuat::Identity, "BlockAll", CrouchedCapShape, DemoCharacterOwner->GetIgnoreCharacterParams()))
		{
			bWantsToCrouch = true;
		}
		else
		{
			return false;
		}
	}

	const float UpSpeed = Velocity | -NormalisedGravity;
	const float MontageSpeed = FMath::GetMappedRangeValueClamped(FVector2D(-500, 750), FVector2D(0.9f, 1.2f), UpSpeed);
	const FVector MantleStart = GetMantleStartLocation(FrontHit, SurfaceHit);

	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Flying);
	UpdatedComponent->SetWorldLocation(MantleStart, false, nullptr, ETeleportType::TeleportPhysics);
	bJustTeleported = true;

	if (IsValid(Mantle_Montage))
	{
		CharacterOwner->PlayAnimMontage(Mantle_Montage, MontageSpeed);
	}

	return true;
}

FVector UDemoMyCharacterMovementComponent::GetMantleStartLocation(FHitResult FrontHit, FHitResult SurfaceHit) const
{
	const FVector NormalisedGravity = GetNormalisedGravity();

	const FVector WallHoriz = FVector::VectorPlaneProject(FrontHit.Normal, -NormalisedGravity).GetSafeNormal();
	const FVector FwdHoriz = FVector::VectorPlaneProject(UpdatedComponent->GetForwardVector(), -NormalisedGravity).GetSafeNormal();

	float CosWallSteepnessAngle = FrontHit.Normal | -NormalisedGravity;
	float DownDistance = MaxStepHeight - 1;
	FVector EdgeTangent = FVector::CrossProduct(SurfaceHit.Normal, FrontHit.Normal).GetSafeNormal();
	FVector MantleStart = SurfaceHit.Location;
	MantleStart += WallHoriz * (2.0f + CapR());
	MantleStart += FwdHoriz.ProjectOnTo(EdgeTangent) * CapR() * 0.3f;
	MantleStart -= NormalisedGravity * CapHH();
	MantleStart += NormalisedGravity * DownDistance;
	MantleStart += WallHoriz * CosWallSteepnessAngle * DownDistance;
	return MantleStart;
}

#pragma endregion
#pragma region WallRun
bool UDemoMyCharacterMovementComponent::TryWallRun()
{
	const FVector NormalisedGravity = GetNormalisedGravity();

	double UpVelocity = FVector::DotProduct(Velocity, NormalisedGravity);

	if (!IsFalling()) return false;
	if (FVector::VectorPlaneProject(Velocity, NormalisedGravity).SizeSquared() < pow(WallRun_MinSpeed, 2)) return false;
	if (UpVelocity < -WallRun_MaxVerticalSpeed) return false;
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector LeftEnd = Start - UpdatedComponent->GetRightVector() * CapR() * 2;
	FVector RightEnd = Start + UpdatedComponent->GetRightVector() * CapR() * 2;
	FHitResult FloorHit, WallHit;
	if (UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), FloorHit, Start, Start + NormalisedGravity * (CapHH() + WallRun_MinHeight), "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams())) return false;

	UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), WallHit, Start, LeftEnd, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
	if (WallHit.IsValidBlockingHit() && (Velocity | WallHit.Normal) < 0)
	{
		Safe_bWallRunIsRight = false;
	}
	else
	{
		UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), WallHit, Start, RightEnd, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
		if (WallHit.IsValidBlockingHit() && (Velocity | WallHit.Normal) < 0)
		{
			Safe_bWallRunIsRight = true;
		}
		else
		{
			return false;
		}
	}

	FVector ProjectedVelocity = FVector::VectorPlaneProject(Velocity, WallHit.Normal);
	if (FVector::VectorPlaneProject(ProjectedVelocity, NormalisedGravity).SizeSquared() < pow(WallRun_MinSpeed, 2)) return false;

	Velocity = ProjectedVelocity;
	Velocity -= (FMath::Clamp(UpVelocity, 0.0f, WallRun_MaxVerticalSpeed) - UpVelocity) * NormalisedGravity;
	SetMovementMode(MOVE_Custom, CMOVE_WallRun);
	return true;
}

void UDemoMyCharacterMovementComponent::PhysWallRun(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	const FVector NormalisedGravity = GetNormalisedGravity();

	bJustTeleported = false;
	float remainingTime = DeltaTime;
	// Perform the move
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)))
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();

		FVector Start = UpdatedComponent->GetComponentLocation();
		FVector CastDelta = UpdatedComponent->GetRightVector() * CapR() * 2;
		FVector End = Safe_bWallRunIsRight ? Start + CastDelta : Start - CastDelta;
		float SinPullAwayAngle = FMath::Sin(FMath::DegreesToRadians(WallRun_PullAwayAngle));
		FHitResult WallHit;
		UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
		bool bWantsToPullAway = WallHit.IsValidBlockingHit() && !Acceleration.IsNearlyZero() && (Acceleration.GetSafeNormal() | WallHit.Normal) > SinPullAwayAngle;
		if (!WallHit.IsValidBlockingHit() || bWantsToPullAway)
		{
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}
		// Clamp Acceleration
		Acceleration = FVector::VectorPlaneProject(Acceleration, WallHit.Normal);
		Acceleration -= FVector::DotProduct(Acceleration, NormalisedGravity) * NormalisedGravity;
		// Apply acceleration
		CalcVelocity(timeTick, 0.f, false, GetMaxBrakingDeceleration());
		Velocity = FVector::VectorPlaneProject(Velocity, WallHit.Normal);
		FVector TangentVelocity = FVector::VectorPlaneProject(Velocity, NormalisedGravity);
		float TangentAccel = Acceleration.GetSafeNormal() | TangentVelocity.GetSafeNormal();
		bool bVelUp = FVector::DotProduct(Velocity, -NormalisedGravity) > 0.0f;
		Velocity -= GetGravityZ() * NormalisedGravity * WallRun_GravityScaleCurve->GetFloatValue(bVelUp ? 0.f : TangentAccel) * timeTick;
		if (TangentVelocity.SizeSquared() < pow(WallRun_MinSpeed, 2) || FVector::DotProduct(Acceleration, NormalisedGravity) > WallRun_MaxVerticalSpeed)
		{
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}

		// Compute move parameters
		const FVector Delta = timeTick * Velocity; // dx = v * dt
		const bool bZeroDelta = Delta.IsNearlyZero();
		if (bZeroDelta)
		{
			remainingTime = 0.0f;
		}
		else
		{
			FHitResult Hit;
			SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
			FVector WallAttractionDelta = -WallHit.Normal * WallRun_AttractionForce * timeTick;
			SafeMoveUpdatedComponent(WallAttractionDelta, UpdatedComponent->GetComponentQuat(), true, Hit);
		}
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.f;
			break;
		}
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick; // v = dx / dt
	}


	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector CastDelta = UpdatedComponent->GetRightVector() * CapR() * 2;
	FVector End = Safe_bWallRunIsRight ? Start + CastDelta : Start - CastDelta;
	FHitResult FloorHit, WallHit;
	UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
	UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), FloorHit, Start, Start + NormalisedGravity * (CapHH() + WallRun_MinHeight * .5f), "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
	if (FloorHit.IsValidBlockingHit() || !WallHit.IsValidBlockingHit() || FVector::VectorPlaneProject(Velocity, NormalisedGravity).SizeSquared() < pow(WallRun_MinSpeed, 2))
	{
		SetMovementMode(MOVE_Falling);
	}
}

#pragma endregion

#pragma region Climb
bool UDemoMyCharacterMovementComponent::TryClimb()
{
	if (!IsFalling()) return false;
	const FVector NormalisedGravity = GetNormalisedGravity();
	if (FVector::DotProduct(Acceleration, NormalisedGravity) > Climb_MinSpeed) return false;
	float CosMMWSA = FMath::Cos(FMath::DegreesToRadians(Climb_MinWallSteepnessAngle));
	float CosMMAA = FMath::Cos(FMath::DegreesToRadians(Climb_MaxAlignmentAngle));

	FVector Start = UpdatedComponent->GetComponentLocation() + NormalisedGravity * CapHH();
	FVector Fwd = FVector::VectorPlaneProject(UpdatedComponent->GetForwardVector(), -NormalisedGravity).GetSafeNormal();
	FHitResult WallHit;

	for (int i = 0; i < 5; i++)
	{
		//if (UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), WallHit, Start, Start + Fwd * CapR() * 2, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams())) break;
		if (UPortalTraceLibrary::LineTraceSingleByChannel(GetWorld(), WallHit, Start, Start + Fwd * CapR() * 2, ECollisionChannel::ECC_WorldStatic, DemoCharacterOwner->GetIgnoreCharacterParams())) break;
		Start -= NormalisedGravity * (1.5f * CapHH() - (MaxStepHeight - 1)) / 4;
	}
	if (!WallHit.IsValidBlockingHit() || WallHit.GetActor()->IsA<APortal>() || WallHit.GetComponent()->GetCollisionObjectType() != ECollisionChannel::ECC_WorldStatic)
	{
		Safe_bFellOff = false;
		return false;
	}

	if (Safe_bFellOff) return false;
	if ((Acceleration.GetSafeNormal() | UpdatedComponent->GetForwardVector().GetSafeNormal()) <= 0.5f) return false;//Don't enter if the player is not actively moving forward
	float CosWallSteepnessAngle = WallHit.Normal | -NormalisedGravity;
	if (FMath::Abs(CosWallSteepnessAngle) > CosMMWSA || (Fwd | -WallHit.Normal) < CosMMAA) return false;

	FQuat NewRotation = FRotationMatrix::MakeFromXZ(-WallHit.Normal, -NormalisedGravity).ToQuat();
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, WallHit);

	Velocity -= NormalisedGravity * Climb_EnterImpulse;
	SetMovementMode(MOVE_Custom, CMOVE_Climb);

	return true;
}

void UDemoMyCharacterMovementComponent::PhysClimb(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	const FVector NormalisedGravity = GetNormalisedGravity();

	bJustTeleported = false;
	float remainingTime = DeltaTime;
	// Perform the move
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)))
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		ClimbTime += timeTick;
		remainingTime -= timeTick;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();

		FVector Start = UpdatedComponent->GetComponentLocation() + NormalisedGravity * CapHH();
		FVector Fwd = FVector::VectorPlaneProject(UpdatedComponent->GetForwardVector(), -NormalisedGravity).GetSafeNormal();
		FHitResult WallHit;

		for (int i = 0; i < 5; i++)
		{
			if (UPortalTraceLibrary::LineTraceSingleByProfile(GetWorld(), WallHit, Start, Start + Fwd * CapR() * 2, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams())) break;
			Start -= NormalisedGravity * (1.5f * CapHH() - (MaxStepHeight - 1)) / 4;
		}
		if (!WallHit.IsValidBlockingHit())
		{
			Velocity -= FVector::DotProduct(Velocity, NormalisedGravity) * NormalisedGravity;
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}

		if (WallHit.GetActor()->IsA<APortal>())
		{
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}

		// Clamp Acceleration
		Acceleration = Acceleration.RotateAngleAxis(90.0f, -UpdatedComponent->GetRightVector());
		//Acceleration.X = 0.0f;
		//Acceleration.Y = 0.0f;
		AddImpulse(NormalisedGravity * Climb_GravityForce * Climb_GravityScaleCurve->GetFloatValue(ClimbTime));
		// Apply acceleration
		CalcVelocity(timeTick, 1.0f, false, GetMaxBrakingDeceleration());
		Velocity = FVector::VectorPlaneProject(Velocity, WallHit.Normal);
		if (FVector::DotProduct(Velocity, NormalisedGravity) > Climb_MinSpeed)
		{
			Velocity += WallHit.Normal * Climb_FallOffForce;
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}

		// Comute move parameters
		const FVector Delta = timeTick * Velocity; // dx = v * dt
		const bool bZeroDelta = Delta.IsNearlyZero();
		if (bZeroDelta)
		{
			remainingTime = 0.f;
		}
		else
		{
			FHitResult Hit;
			SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
			FVector WallAttractionDelta = -WallHit.Normal * Climb_AttractionForce * timeTick;
			SafeMoveUpdatedComponent(WallAttractionDelta, UpdatedComponent->GetComponentQuat(), true, Hit);
		}
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.f;
			break;
		}
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick; // v = dx / dt
	}
}
#pragma endregion

#pragma region Grapple
FVector UDemoMyCharacterMovementComponent::GetGrapplingPivot()
{
	if (Grapple_Target && Grapple_Target->GetOwner())
	{
		return UKismetMathLibrary::TransformLocation(Grapple_Target->GetOwner()->GetTransform(), Grapple_Pivot);
	}
	return Grapple_Pivot;
}

void UDemoMyCharacterMovementComponent::Grapple()
{
	if (CanGrapple())
	{
		Safe_bWantsToGrapple = true;
	}
}

bool UDemoMyCharacterMovementComponent::CanGrapple() const
{
	return GrappleStage == GRAPPLE_NONE;
}

void UDemoMyCharacterMovementComponent::DoGrapple()
{
	const FVector Start = UpdatedComponent->GetComponentLocation() - GetNormalisedGravity() * Grapple_CamOffset;
	const FVector Fwd = (CharacterOwner && CharacterOwner->Controller)
		? CharacterOwner->GetControlRotation().Vector()
		: UpdatedComponent->GetForwardVector();
	FHitResult PivotHit;

	GetWorld()->LineTraceSingleByChannel(PivotHit, Start, Start + Fwd * Grapple_Range, ECC_Visibility, DemoCharacterOwner->GetIgnoreCharacterParams());

	if (!PivotHit.IsValidBlockingHit() || PivotHit.GetActor()->IsA<APortal>())
	{
		Grapple_Target = nullptr;
		Grapple_TargetMovable = false;
		Grapple_Pivot = Start + Fwd * Grapple_Range;
		Grapple_PivotNormal = -Fwd;
		Safe_bWantsToGrapple = false;
	}
	else
	{
		if (PivotHit.GetActor()->IsA<APortal>())
		{
			//Portal interaction stuff
		}

		Grapple_Target = PivotHit.GetComponent();
		Grapple_PivotNormal = PivotHit.ImpactNormal;
		Grapple_Pivot = UKismetMathLibrary::InverseTransformLocation(Grapple_Target->GetOwner()->GetTransform(), PivotHit.ImpactPoint);
		Grapple_TargetMovable = Grapple_Target->GetOwner()->IsRootComponentMovable();
		GrappleTime = GetWorld()->GetTimeSeconds();
	}

	UCableComponent* Grappler = DemoCharacterOwner->GetGrappler();

	GrappleEndLoc = FVector::ZeroVector;
	Grappler->EndLocation = GrappleEndLoc;
	Grappler->SetHiddenInGame(false);
	GrappleStage = GRAPPLE_SHOOT;
	DemoCharacterOwner->ForceHolsterForParkour();
	DemoCharacterOwner->PushGrappleCableVisual(true, Grappler->GetComponentLocation());
}

void UDemoMyCharacterMovementComponent::EnterGrapple()
{
	Grapple_TargetMovable = Grapple_Target->GetOwner()->IsRootComponentMovable();
	Velocity += Velocity.GetSafeNormal() * Grapple_EnterImpulse;
	const FVector Start = UpdatedComponent->GetComponentLocation() - GetNormalisedGravity() * Grapple_CamOffset;
	const FVector PivotDir = (GetGrapplingPivot() - Start).GetSafeNormal();
	Velocity += PivotDir* Grapple_EnterPullImpulse;
	SetMovementMode(MOVE_Custom, CMOVE_Grapple);
}

void UDemoMyCharacterMovementComponent::ExitGrapple()
{
	if (Grapple_TargetMovable)
	{
		Grapple_Target->ComponentVelocity = FVector::ZeroVector;
	}
	GrappleStage = EGrapplingStage::GRAPPLE_RETRACT;
	Safe_bWantsToGrapple = false;
	SetMovementMode(MOVE_Falling);
}

void UDemoMyCharacterMovementComponent::TickGrapple(const float DeltaTime)
{
	UCableComponent* Grappler = DemoCharacterOwner->GetGrappler();
	if (!Grappler || !DemoCharacterOwner)
	{
		return;
	}

	auto PushVisual = [this, Grappler]()
	{
		const FVector EndWS = Grappler->GetComponentTransform().TransformPosition(GrappleEndLoc);
		DemoCharacterOwner->PushGrappleCableVisual(true, EndWS);
	};

	switch (GrappleStage)
	{
	case GRAPPLE_SHOOT:
	{
		const FVector Target = Grappler->GetComponentTransform().InverseTransformPosition(GetGrapplingPivot());
		GrappleEndLoc = FMath::VInterpConstantTo(GrappleEndLoc, Target, DeltaTime, GrappleSpeed);
		Grappler->EndLocation = GrappleEndLoc;
		PushVisual();
		if (FVector::DistSquared(GrappleEndLoc, Target) <= 1.0f)
		{
			GrappleEndLoc = Target;
			Grappler->EndLocation = GrappleEndLoc;
			PushVisual();
			if (Safe_bWantsToGrapple)
			{
				GrappleStage = GRAPPLE_INPROGRESS;
				EnterGrapple();
			}
			else
			{
				GrappleStage = EGrapplingStage::GRAPPLE_RETRACT;
			}
		}
		break;
	}
	case GRAPPLE_INPROGRESS:
		GrappleEndLoc = Grappler->GetComponentTransform().InverseTransformPosition(GetGrapplingPivot());
		Grappler->EndLocation = GrappleEndLoc;
		PushVisual();
		break;
	case GRAPPLE_RETRACT:
	{
		GrappleEndLoc = FMath::VInterpConstantTo(GrappleEndLoc, FVector::ZeroVector, DeltaTime, GrappleSpeed);
		Grappler->EndLocation = GrappleEndLoc;
		if (GrappleEndLoc.SizeSquared() <= 1.0f)
		{
			GrappleEndLoc = FVector::ZeroVector;
			Grappler->EndLocation = GrappleEndLoc;
			Grappler->SetHiddenInGame(true);
			GrappleStage = GRAPPLE_NONE;
			DemoCharacterOwner->PushGrappleCableVisual(false, Grappler->GetComponentLocation());
			DemoCharacterOwner->TryRestoreWeaponAfterParkour();
		}
		else
		{
			PushVisual();
		}
		break;
	}
	default:
		break;
	}
}

void UDemoMyCharacterMovementComponent::PhysGrapple(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	bJustTeleported = false;

	const FVector NormalisedGravity = GetNormalisedGravity();

	// Perform the move
	Iterations++;
	bJustTeleported = false;
	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	FVector Start = UpdatedComponent->GetComponentLocation() - NormalisedGravity * Grapple_CamOffset;
	FVector bGrapplePivot = GetGrapplingPivot();
	FVector PivotDir = (bGrapplePivot - Start).GetSafeNormal();

	RestorePreAdditiveRootMotionVelocity();

	//Check if the player has reached destination
	if (FVector::Dist(Start, bGrapplePivot)<Grapple_FinishDist)
	{
		Safe_bWantsToGrapple = false;
		return;
	}

	//Check if there is obstacle in between, break grapple if true
	if ((PivotDir | Grapple_PivotNormal)>0.0f && !Grapple_TargetMovable)
	{
		Safe_bWantsToGrapple = false;
		return;
	 }

	// Calculate pull and steer and break the grapple if the tension is too great
	if (Grapple_TargetMovable)
	{
		Grapple_Target->AddImpulse(-PivotDir * Grapple_PullForce);
	}
	AddImpulse(PivotDir * Grapple_PullForce);
	Acceleration = Acceleration * Grapple_SteerFactor;
	const float SinPullAwayAngle = FMath::Cos(FMath::DegreesToRadians(Grapple_PullAwayAngle));
	const FVector AimDir = (CharacterOwner && CharacterOwner->Controller)
		? CharacterOwner->GetControlRotation().Vector()
		: UpdatedComponent->GetForwardVector();
	const bool bWantsToPullAway = (AimDir | PivotDir) < SinPullAwayAngle;
	//UE_LOG(LogTemp, Warning, TEXT("Acceleration: %s"), *Acceleration.ToString())
	// Apply acceleration
	CalcVelocity(DeltaTime, 0.0f, false, GetMaxBrakingDeceleration());
	//UE_LOG(LogTemp, Warning, TEXT("Velocity: %s"), *Velocity.ToString());
	if (bWantsToPullAway || (((-PivotDir) | Velocity) > Grapple_MaxTension && Grapple_MaxTension > 0.0f))
	{
		Safe_bWantsToGrapple = false;
		return;
	}

	// Comute move parameters
	const FVector Adjusted = DeltaTime * Velocity; // dx = v * dt

	FHitResult Hit;
	SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.Time < 1.f)
	{
		//adjust and try again
		HandleImpact(Hit, DeltaTime, Adjusted);
		SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}

	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;// v = dx / dt
	}
}

#pragma endregion

#pragma region Flash
bool UDemoMyCharacterMovementComponent::CanFlash() const
{
	return IsFalling() || (IsWalking() && !IsCrouching());
}
void UDemoMyCharacterMovementComponent::DoFlash()
{
	FHitResult Hit;
	const FVector Delta = CharacterOwner->GetActorForwardVector() * FlashReach;
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

	bJustTeleported = true;
	FlashTime = GetWorld()->GetTimeSeconds();
}
#pragma endregion

#pragma region Portal
bool UDemoMyCharacterMovementComponent::ShouldUsePortalCollision() const
{
	return CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy;
}

namespace
{
	FVector PortalContactPoint(const FHitResult& Hit)
	{
		if (Hit.bStartPenetrating && Hit.ImpactPoint.IsNearlyZero())
		{
			return Hit.Location;
		}
		return Hit.ImpactPoint;
	}

	void MapExitHitToEntry(const FHitResult& ExitHit, const FQuat& InvTravelQ, const FVector& EntryOrigin, const FVector& ExitOrigin, const FVector& EntryStart, const FVector& EntryEnd, FHitResult& OutHit)
	{
		OutHit = ExitHit;
		OutHit.TraceStart = EntryStart;
		OutHit.TraceEnd = EntryEnd;
		OutHit.bBlockingHit = true;
		auto ToEntry = [&](const FVector& ExitPoint)
		{
			return EntryOrigin + InvTravelQ.RotateVector(ExitPoint - ExitOrigin);
		};
		OutHit.Location = ToEntry(ExitHit.Location);
		OutHit.ImpactPoint = ToEntry(ExitHit.ImpactPoint);
		OutHit.Normal = InvTravelQ.RotateVector(ExitHit.Normal).GetSafeNormal();
		OutHit.ImpactNormal = InvTravelQ.RotateVector(ExitHit.ImpactNormal).GetSafeNormal();
		OutHit.Time = ExitHit.Time;
		const float FullDistance = FVector::Distance(EntryStart, EntryEnd);
		OutHit.Distance = ExitHit.bStartPenetrating ? ExitHit.Distance : ExitHit.Time * FullDistance;
	}

	bool IsPortalOwnedComponent(const UPrimitiveComponent* Component)
	{
		return Component && Component->GetOwner() && Component->GetOwner()->IsA(APortal::StaticClass());
	}

	// The wall the portal is mounted on reaches behind the plane. A start-overlap has no contact on that wall.
	bool ComponentReachesBehindPlane(const APortal* Portal, const UPrimitiveComponent* Component)
	{
		if (!Portal || !Component)
		{
			return false;
		}

		const FVector Forward = Portal->GetActorForwardVector();
		const FBoxSphereBounds& Bounds = Component->Bounds;
		const float CenterDist = FVector::DotProduct(Bounds.Origin - Portal->GetPlaneOrigin(), Forward);
		const float ExtentAlongForward =
			Bounds.BoxExtent.X * FMath::Abs(Forward.X)
			+ Bounds.BoxExtent.Y * FMath::Abs(Forward.Y)
			+ Bounds.BoxExtent.Z * FMath::Abs(Forward.Z);
		return CenterDist - ExtentAlongForward <= 2.f;
	}

	bool HitIsPortalWall(const APortal* Portal, const FHitResult& Hit)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (!Portal || IsPortalOwnedComponent(Component))
		{
			return false;
		}
		if (Hit.bStartPenetrating)
		{
			return ComponentReachesBehindPlane(Portal, Component);
		}
		return Portal->IsPointBehindPlane(PortalContactPoint(Hit));
	}

	bool PortalCast(UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, const FQuat& Rot, ECollisionChannel Channel, const FCollisionShape& Shape, FCollisionQueryParams Params, const FCollisionResponseParams& ResponseParam, const APortal* TravelPortal)
	{
		auto CastSegment = [&](const FVector& SegStart, const FVector& SegEnd, const FQuat& SegRot, const APortal* PlanePortal, FCollisionQueryParams SegParams, FHitResult& Hit) -> bool
		{
			for (int32 Attempt = 0; Attempt < 8; ++Attempt)
			{
				const bool bHit = World->SweepSingleByChannel(Hit, SegStart, SegEnd, SegRot, Channel, Shape, SegParams, ResponseParam);
				if (!bHit || !Hit.bBlockingHit)
				{
					return false;
				}
				if (!HitIsPortalWall(PlanePortal, Hit))
				{
					return true;
				}
				if (UPrimitiveComponent* Component = Hit.GetComponent())
				{
					SegParams.AddIgnoredComponent(Component);
					continue;
				}
				return true;
			}
			return false;
		};

		FHitResult EntryHit;
		const bool bEntry = CastSegment(Start, End, Rot, TravelPortal, Params, EntryHit);

		bool bExit = false;
		FHitResult ExitHit;
		FQuat TravelQ = FQuat::Identity;
		FVector ExitStart = FVector::ZeroVector;
		FVector ExitEnd = FVector::ZeroVector;
		if (TravelPortal->OtherPortal && TravelPortal->GetTravelQuat(TravelQ) && TravelPortal->TranslatePoint(Start, ExitStart) && TravelPortal->TranslatePoint(End, ExitEnd))
		{
			bExit = CastSegment(ExitStart, ExitEnd, TravelQ * Rot, TravelPortal->OtherPortal, Params, ExitHit);
			if (bExit)
			{
				FHitResult MappedHit;
				MapExitHitToEntry(ExitHit, TravelQ.Inverse(), TravelPortal->GetPlaneOrigin(), TravelPortal->OtherPortal->GetPlaneOrigin(), Start, End, MappedHit);
				ExitHit = MappedHit;
			}
		}

		if (bEntry && (!bExit || EntryHit.Time <= ExitHit.Time))
		{
			OutHit = EntryHit;
			return true;
		}
		if (bExit)
		{
			OutHit = ExitHit;
			return true;
		}
		return false;
	}

}

bool UDemoMyCharacterMovementComponent::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport)
{
	const APortal* TravelPortal = DemoCharacterOwner ? DemoCharacterOwner->GetCurrentPortal() : nullptr;
	if (!TravelPortal || !ShouldUsePortalCollision() || !bSweep || !UpdatedComponent || !UpdatedPrimitive || !GetWorld())
	{
		return Super::MoveUpdatedComponentImpl(Delta, NewRotation, bSweep, OutHit, Teleport);
	}

	const FVector ConstrainedDelta = ConstrainDirectionToPlane(Delta);
	if (ConstrainedDelta.IsNearlyZero())
	{
		return Super::MoveUpdatedComponentImpl(Delta, NewRotation, bSweep, OutHit, Teleport);
	}

	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = Start + ConstrainedDelta;
	const FQuat MoveRot = UpdatedComponent->GetComponentQuat();

	TArray<UPrimitiveComponent*> Ignored;
	FComponentQueryParams QueryParams(SCENE_QUERY_STAT(PortalMove), CharacterOwner);
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		TArray<FHitResult> Hits;
		if (!GetWorld()->ComponentSweepMulti(Hits, UpdatedPrimitive, Start, End, MoveRot, QueryParams))
		{
			break;
		}

		bool bIgnoredBehind = false;
		for (const FHitResult& Hit : Hits)
		{
			UPrimitiveComponent* Component = Hit.GetComponent();
			if (!Hit.bBlockingHit || !Component)
			{
				continue;
			}

			// Portal-owned colliders (rim strips, plane) stay solid.
			if (Component->GetOwner() && Component->GetOwner()->IsA(APortal::StaticClass()))
			{
				continue;
			}

			if (!TravelPortal->IsPointBehindPlane(PortalContactPoint(Hit)))
			{
				continue;
			}

			UpdatedPrimitive->IgnoreComponentWhenMoving(Component, true);
			QueryParams.AddIgnoredComponent(Component);
			Ignored.AddUnique(Component);
			bIgnoredBehind = true;
		}

		if (!bIgnoredBehind)
		{
			break;
		}
	}

	// Same sweep, played out of the linked portal. Geometry behind that plane is the exit wall.
	float ExitLimit = 1.f;
	bool bExitBlocked = false;
	bool bClippedExitOverlap = false;
	FVector ExitClipNormal = FVector::ZeroVector;
	FVector AppliedDelta = ConstrainedDelta;
	FHitResult ExitHit;
	FQuat InvTravelQ = FQuat::Identity;
	const FVector EntryOrigin = TravelPortal->GetPlaneOrigin();
	FVector ExitOrigin = FVector::ZeroVector;
	if (APortal* ExitPortal = TravelPortal->OtherPortal)
	{
		FVector ExitStart = FVector::ZeroVector;
		FVector ExitEnd = FVector::ZeroVector;
		if (TravelPortal->TranslatePoint(Start, ExitStart) && TravelPortal->TranslatePoint(End, ExitEnd))
		{
			FQuat TravelQ = FQuat::Identity;
			if (TravelPortal->GetTravelQuat(TravelQ))
			{
			InvTravelQ = TravelQ.Inverse();
			ExitOrigin = ExitPortal->GetPlaneOrigin();

			FCollisionQueryParams ExitParams(SCENE_QUERY_STAT(PortalExitMove), false, CharacterOwner);
			FCollisionResponseParams ExitResponse;
			InitCollisionParams(ExitParams, ExitResponse);
			const ECollisionChannel Channel = UpdatedPrimitive->GetCollisionObjectType();
			const FCollisionShape Shape = UpdatedPrimitive->GetCollisionShape();
			const FQuat ExitRot = TravelQ * MoveRot;

			for (int32 Attempt = 0; Attempt < 8; ++Attempt)
			{
				FHitResult Hit;
				if (!GetWorld()->SweepSingleByChannel(Hit, ExitStart, ExitEnd, ExitRot, Channel, Shape, ExitParams, ExitResponse) || !Hit.bBlockingHit)
				{
					break;
				}

				UPrimitiveComponent* Component = Hit.GetComponent();
				if (ExitPortal->IsPointBehindPlane(PortalContactPoint(Hit)))
				{
					if (Component)
					{
						ExitParams.AddIgnoredComponent(Component);
						continue;
					}
					break;
				}

				ExitHit = Hit;
				bExitBlocked = true;
				if (Hit.bStartPenetrating)
				{
					const FVector EntryNormal = InvTravelQ.RotateVector(Hit.Normal).GetSafeNormal();
					const float IntoSurface = FVector::DotProduct(ConstrainedDelta, EntryNormal);
					if (!EntryNormal.IsNearlyZero() && IntoSurface < -KINDA_SMALL_NUMBER)
					{
						// Block only the direction into the obstacle, same as a normal surface hit.
						AppliedDelta = ConstrainedDelta - EntryNormal * IntoSurface;
						bClippedExitOverlap = true;
						ExitClipNormal = EntryNormal;
					}
					else
					{
						bExitBlocked = false;
					}
				}
				else
				{
					ExitLimit = FMath::Clamp(Hit.Time, 0.f, 1.f);
					AppliedDelta = ConstrainedDelta * ExitLimit;
				}
				break;
			}
			}
		}
	}

	const bool bMoved = Super::MoveUpdatedComponentImpl(AppliedDelta, NewRotation, bSweep, OutHit, Teleport);

	for (UPrimitiveComponent* Component : Ignored)
	{
		if (Component)
		{
			UpdatedPrimitive->IgnoreComponentWhenMoving(Component, false);
		}
	}

	if (OutHit)
	{
		if (OutHit->bBlockingHit && !OutHit->bStartPenetrating)
		{
			OutHit->Time *= ExitLimit;
			OutHit->Distance = OutHit->Time * ConstrainedDelta.Size();
			OutHit->TraceStart = Start;
			OutHit->TraceEnd = End;
		}

		if (bClippedExitOverlap && (!OutHit->bBlockingHit || OutHit->Time >= 1.f - KINDA_SMALL_NUMBER))
		{
			MapExitHitToEntry(ExitHit, InvTravelQ, EntryOrigin, ExitOrigin, Start, End, *OutHit);
			OutHit->bStartPenetrating = false;
			OutHit->PenetrationDepth = 0.f;
			OutHit->Normal = ExitClipNormal;
			OutHit->ImpactNormal = ExitClipNormal;
			if (AppliedDelta.IsNearlyZero())
			{
				OutHit->Time = 0.f;
				OutHit->Distance = 0.f;
				OutHit->Location = Start;
			}
			else
			{
				// The along-surface part already moved. Leave no remainder for another slide to reapply.
				OutHit->Time = 1.f;
				OutHit->Distance = ConstrainedDelta.Size();
				OutHit->Location = UpdatedComponent->GetComponentLocation();
			}
		}
		else if (bExitBlocked && (!OutHit->bBlockingHit || ExitHit.Time <= OutHit->Time + KINDA_SMALL_NUMBER))
		{
			MapExitHitToEntry(ExitHit, InvTravelQ, EntryOrigin, ExitOrigin, Start, End, *OutHit);
		}
	}

	return bMoved;
}

bool UDemoMyCharacterMovementComponent::ResolvePenetrationImpl(const FVector& ProposedAdjustment, const FHitResult& Hit, const FQuat& NewRotation)
{
	const APortal* TravelPortal = DemoCharacterOwner ? DemoCharacterOwner->GetCurrentPortal() : nullptr;
	if (!TravelPortal || !ShouldUsePortalCollision() || !UpdatedComponent || !UpdatedPrimitive || !GetWorld())
	{
		return Super::ResolvePenetrationImpl(ProposedAdjustment, Hit, NewRotation);
	}

	const FVector Adjustment = ConstrainDirectionToPlane(ProposedAdjustment);
	if (Adjustment.IsNearlyZero())
	{
		return Super::ResolvePenetrationImpl(ProposedAdjustment, Hit, NewRotation);
	}

	// The wall behind the plane is not a penetration. Pushing along its normal shoves the capsule out of the portal.
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	const bool bPortalOwned = HitComponent && HitComponent->GetOwner() && HitComponent->GetOwner()->IsA(APortal::StaticClass());
	if (HitComponent && !bPortalOwned && TravelPortal->IsPointBehindPlane(PortalContactPoint(Hit)))
	{
		return false;
	}

	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = Start + Adjustment;
	const FQuat MoveRot = UpdatedComponent->GetComponentQuat();

	TArray<UPrimitiveComponent*> Ignored;
	FComponentQueryParams QueryParams(SCENE_QUERY_STAT(PortalResolve), CharacterOwner);
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		TArray<FHitResult> Hits;
		if (!GetWorld()->ComponentSweepMulti(Hits, UpdatedPrimitive, Start, End, MoveRot, QueryParams))
		{
			break;
		}

		bool bIgnoredBehind = false;
		for (const FHitResult& SweepHit : Hits)
		{
			UPrimitiveComponent* Component = SweepHit.GetComponent();
			if (!SweepHit.bBlockingHit || !Component)
			{
				continue;
			}

			if (Component->GetOwner() && Component->GetOwner()->IsA(APortal::StaticClass()))
			{
				continue;
			}

			if (!TravelPortal->IsPointBehindPlane(PortalContactPoint(SweepHit)))
			{
				continue;
			}

			UpdatedPrimitive->IgnoreComponentWhenMoving(Component, true);
			QueryParams.AddIgnoredComponent(Component);
			Ignored.AddUnique(Component);
			bIgnoredBehind = true;
		}

		if (!bIgnoredBehind)
		{
			break;
		}
	}

	FVector AppliedAdjustment = Adjustment;
	if (APortal* ExitPortal = TravelPortal->OtherPortal)
	{
		FVector ExitStart = FVector::ZeroVector;
		FVector ExitEnd = FVector::ZeroVector;
		FQuat TravelQ = FQuat::Identity;
		if (TravelPortal->TranslatePoint(Start, ExitStart) && TravelPortal->TranslatePoint(End, ExitEnd) && TravelPortal->GetTravelQuat(TravelQ))
		{
			const FQuat InvTravelQ = TravelQ.Inverse();
			FCollisionQueryParams ExitParams(SCENE_QUERY_STAT(PortalExitResolve), false, CharacterOwner);
			FCollisionResponseParams ExitResponse;
			InitCollisionParams(ExitParams, ExitResponse);
			const ECollisionChannel Channel = UpdatedPrimitive->GetCollisionObjectType();
			const FCollisionShape Shape = UpdatedPrimitive->GetCollisionShape();
			const FQuat ExitRot = TravelQ * MoveRot;

			for (int32 Attempt = 0; Attempt < 8; ++Attempt)
			{
				FHitResult ExitHit;
				if (!GetWorld()->SweepSingleByChannel(ExitHit, ExitStart, ExitEnd, ExitRot, Channel, Shape, ExitParams, ExitResponse) || !ExitHit.bBlockingHit)
				{
					break;
				}

				UPrimitiveComponent* Component = ExitHit.GetComponent();
				if (ExitPortal->IsPointBehindPlane(PortalContactPoint(ExitHit)))
				{
					if (Component)
					{
						ExitParams.AddIgnoredComponent(Component);
						continue;
					}
					break;
				}

				if (ExitHit.bStartPenetrating)
				{
					const FVector EntryNormal = InvTravelQ.RotateVector(ExitHit.Normal).GetSafeNormal();
					const float IntoSurface = FVector::DotProduct(Adjustment, EntryNormal);
					if (!EntryNormal.IsNearlyZero() && IntoSurface < -KINDA_SMALL_NUMBER)
					{
						AppliedAdjustment = Adjustment - EntryNormal * IntoSurface;
					}
				}
				else
				{
					AppliedAdjustment = Adjustment * FMath::Clamp(ExitHit.Time, 0.f, 1.f);
				}
				break;
			}
		}
	}

	const bool bResolved = AppliedAdjustment.IsNearlyZero()
		? false
		: Super::ResolvePenetrationImpl(AppliedAdjustment, Hit, NewRotation);

	for (UPrimitiveComponent* Component : Ignored)
	{
		if (Component)
		{
			UpdatedPrimitive->IgnoreComponentWhenMoving(Component, false);
		}
	}

	return bResolved;
}

bool UDemoMyCharacterMovementComponent::OverlapTest(const FVector& Location, const FQuat& RotationQuat, const ECollisionChannel CollisionChannel, const FCollisionShape& CollisionShape, const AActor* IgnoreActor) const
{
	const APortal* TravelPortal = DemoCharacterOwner ? DemoCharacterOwner->GetCurrentPortal() : nullptr;
	if (!TravelPortal || !ShouldUsePortalCollision() || !GetWorld())
	{
		return Super::OverlapTest(Location, RotationQuat, CollisionChannel, CollisionShape, IgnoreActor);
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MovementOverlapTest), false, IgnoreActor);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(QueryParams, ResponseParam);

	auto BlocksAt = [&](const FVector& TestLocation, const FQuat& TestRot, const APortal* PlanePortal) -> bool
	{
		FCollisionQueryParams Params = QueryParams;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			TArray<FOverlapResult> Overlaps;
			if (!GetWorld()->OverlapMultiByChannel(Overlaps, TestLocation, TestRot, CollisionChannel, CollisionShape, Params, ResponseParam))
			{
				return false;
			}

			bool bIgnoredWall = false;
			for (const FOverlapResult& Overlap : Overlaps)
			{
				UPrimitiveComponent* Component = Overlap.GetComponent();
				if (!Overlap.bBlockingHit || !Component)
				{
					continue;
				}
				if (IsPortalOwnedComponent(Component))
				{
					return true;
				}
				if (ComponentReachesBehindPlane(PlanePortal, Component))
				{
					Params.AddIgnoredComponent(Component);
					bIgnoredWall = true;
					continue;
				}
				return true;
			}
			if (!bIgnoredWall)
			{
				return false;
			}
		}
		return false;
	};

	if (BlocksAt(Location, RotationQuat, TravelPortal))
	{
		return true;
	}

	FQuat TravelQ = FQuat::Identity;
	FVector ExitLocation = FVector::ZeroVector;
	if (TravelPortal->OtherPortal && TravelPortal->GetTravelQuat(TravelQ) && TravelPortal->TranslatePoint(Location, ExitLocation))
	{
		return BlocksAt(ExitLocation, TravelQ * RotationQuat, TravelPortal->OtherPortal);
	}
	return false;
}

bool UDemoMyCharacterMovementComponent::FloorSweepTest(FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const FCollisionShape& CollisionShape, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParam) const
{
	const APortal* TravelPortal = DemoCharacterOwner ? DemoCharacterOwner->GetCurrentPortal() : nullptr;
	if (!TravelPortal || !ShouldUsePortalCollision() || !GetWorld())
	{
		return Super::FloorSweepTest(OutHit, Start, End, TraceChannel, CollisionShape, Params, ResponseParam);
	}

	auto Sweep = [&](const FQuat& Rot, const FCollisionShape& Shape, FHitResult& Hit) -> bool
	{
		return PortalCast(GetWorld(), Hit, Start, End, Rot, TraceChannel, Shape, Params, ResponseParam, TravelPortal);
	};

	if (!bUseFlatBaseForFloorChecks)
	{
		return Sweep(GetWorldToGravityTransform(), CollisionShape, OutHit);
	}

	const float CapsuleRadius = CollisionShape.GetCapsuleRadius();
	const float CapsuleHeight = CollisionShape.GetCapsuleHalfHeight();
	const FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(CapsuleRadius * 0.707f, CapsuleRadius * 0.707f, CapsuleHeight));
	if (Sweep(FQuat(RotateGravityToWorld(FVector(0.f, 0.f, -1.f)), UE_PI * 0.25f), BoxShape, OutHit))
	{
		return true;
	}

	OutHit.Reset(1.f, false);
	return Sweep(GetWorldToGravityTransform(), BoxShape, OutHit);
}

void UDemoMyCharacterMovementComponent::ComputeFloorDist(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, const FHitResult* DownwardSweepResult) const
{
	const APortal* TravelPortal = DemoCharacterOwner ? DemoCharacterOwner->GetCurrentPortal() : nullptr;
	if (!TravelPortal || !ShouldUsePortalCollision() || !CharacterOwner || !UpdatedComponent || !GetWorld())
	{
		Super::ComputeFloorDist(CapsuleLocation, LineDistance, SweepDistance, OutFloorResult, SweepRadius, DownwardSweepResult);
		return;
	}

	OutFloorResult.Clear();

	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

	if (DownwardSweepResult && DownwardSweepResult->IsValidBlockingHit() && HitIsPortalWall(TravelPortal, *DownwardSweepResult))
	{
		DownwardSweepResult = nullptr;
	}

	bool bSkipSweep = false;
	if (DownwardSweepResult != nullptr && DownwardSweepResult->IsValidBlockingHit())
	{
		const bool bIsDownward = RotateWorldToGravity(DownwardSweepResult->TraceStart - DownwardSweepResult->TraceEnd).Z > 0;
		const bool bIsVertical = RotateWorldToGravity(DownwardSweepResult->TraceStart - DownwardSweepResult->TraceEnd).SizeSquared2D() <= UE_KINDA_SMALL_NUMBER;
		if (bIsDownward && bIsVertical)
		{
			if (IsWithinEdgeTolerance(DownwardSweepResult->Location, DownwardSweepResult->ImpactPoint, PawnRadius))
			{
				bSkipSweep = true;

				const bool bIsWalkable = IsWalkable(*DownwardSweepResult);
				const float FloorDist = RotateWorldToGravity(CapsuleLocation - DownwardSweepResult->Location).Z;
				OutFloorResult.SetFromSweep(*DownwardSweepResult, FloorDist, bIsWalkable);
				if (bIsWalkable)
				{
					return;
				}
			}
		}
	}

	if (SweepDistance < LineDistance)
	{
		ensure(SweepDistance >= LineDistance);
		return;
	}

	bool bBlockingHit = false;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ComputeFloorDist), false, CharacterOwner);
	QueryParams.bTraceIntoSubComponents = true;
	QueryParams.bReplaceHitWithSubComponents = false;
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(QueryParams, ResponseParam);
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();

	if (!bSkipSweep && SweepDistance > 0.f && SweepRadius > 0.f)
	{
		const float ShrinkScale = 0.9f;
		const float ShrinkScaleOverlap = 0.1f;
		float ShrinkHeight = (PawnHalfHeight - PawnRadius) * (1.f - ShrinkScale);
		float TraceDist = SweepDistance + ShrinkHeight;
		FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(SweepRadius, PawnHalfHeight - ShrinkHeight);

		FHitResult Hit(1.f);
		bBlockingHit = FloorSweepTest(Hit, CapsuleLocation, CapsuleLocation + RotateGravityToWorld(FVector(0.f, 0.f, -TraceDist)), CollisionChannel, CapsuleShape, QueryParams, ResponseParam);

		if (bBlockingHit)
		{
			if (Hit.bStartPenetrating || !IsWithinEdgeTolerance(CapsuleLocation, Hit.ImpactPoint, CapsuleShape.Capsule.Radius))
			{
				CapsuleShape.Capsule.Radius = FMath::Max(0.f, CapsuleShape.Capsule.Radius - SWEEP_EDGE_REJECT_DISTANCE - UE_KINDA_SMALL_NUMBER);
				if (!CapsuleShape.IsNearlyZero())
				{
					ShrinkHeight = (PawnHalfHeight - PawnRadius) * (1.f - ShrinkScaleOverlap);
					TraceDist = SweepDistance + ShrinkHeight;
					CapsuleShape.Capsule.HalfHeight = FMath::Max(PawnHalfHeight - ShrinkHeight, CapsuleShape.Capsule.Radius);
					Hit.Reset(1.f, false);

					bBlockingHit = FloorSweepTest(Hit, CapsuleLocation, CapsuleLocation + RotateGravityToWorld(FVector(0.f, 0.f, -TraceDist)), CollisionChannel, CapsuleShape, QueryParams, ResponseParam);
				}
			}

			const float MaxPenetrationAdjust = FMath::Max(MAX_FLOOR_DIST, PawnRadius);
			const float SweepResult = FMath::Max(-MaxPenetrationAdjust, Hit.Time * TraceDist - ShrinkHeight);

			OutFloorResult.SetFromSweep(Hit, SweepResult, false);
			if (Hit.IsValidBlockingHit() && IsWalkable(Hit))
			{
				if (SweepResult <= SweepDistance)
				{
					OutFloorResult.bWalkableFloor = true;
					return;
				}
			}
		}
	}

	if (!OutFloorResult.bBlockingHit && !OutFloorResult.HitResult.bStartPenetrating)
	{
		OutFloorResult.FloorDist = SweepDistance;
		return;
	}

	if (LineDistance > 0.f)
	{
		const float ShrinkHeight = PawnHalfHeight;
		const float TraceDist = LineDistance + ShrinkHeight;
		const FVector Down = RotateGravityToWorld(FVector(0.f, 0.f, -TraceDist));
		QueryParams.TraceTag = SCENE_QUERY_STAT_NAME_ONLY(FloorLineTrace);

		FHitResult Hit(1.f);
		bBlockingHit = PortalCast(GetWorld(), Hit, CapsuleLocation, CapsuleLocation + Down, FQuat::Identity, CollisionChannel, FCollisionShape(), QueryParams, ResponseParam, TravelPortal);

		if (bBlockingHit && Hit.Time > 0.f)
		{
			const float MaxPenetrationAdjust = FMath::Max(MAX_FLOOR_DIST, PawnRadius);
			const float LineResult = FMath::Max(-MaxPenetrationAdjust, Hit.Time * TraceDist - ShrinkHeight);
			OutFloorResult.bBlockingHit = true;
			if (LineResult <= LineDistance && IsWalkable(Hit))
			{
				OutFloorResult.SetFromLineTrace(Hit, OutFloorResult.FloorDist, LineResult, true);
				return;
			}
		}
	}

	OutFloorResult.bWalkableFloor = false;
}
#pragma endregion

#pragma region Helper Functions
bool UDemoMyCharacterMovementComponent::IsMovementMode(EMovementMode InMovementMode) const
{
	return InMovementMode == MovementMode;
}

bool UDemoMyCharacterMovementComponent::IsServer() const
{
	return CharacterOwner->HasAuthority();
}

float UDemoMyCharacterMovementComponent::CapR() const
{
	return CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
}

float UDemoMyCharacterMovementComponent::CapHH() const
{
	return CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}
#pragma endregion
