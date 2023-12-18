// Fill out your copyright notice in the Description page of Project Settings.


#include "DemoMyCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

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

	Safe_bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

void UDemoMyCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	
	if (MovementMode == MOVE_Walking)
	{
		if (Safe_bWantsToSprint)
		{
			MaxWalkSpeed = Sprint_MaxWalkSpeed;
		}
		else
		{
			MaxWalkSpeed = Walk_MaxWalkSpeed;
		}
	}

	Safe_bWantsToCrouch = bWantsToCrouch;
}

void UDemoMyCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	UE_LOG(LogTemp, Warning, TEXT("%d"), MovementMode.GetIntValue());
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

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
		bFellOff = true;
	}

	if (IsCustomMovementMode(CMOVE_Climb))
	{
		bClimbTime = 0.0f;
	}

	if (IsWallRunning() && GetOwnerRole() == ROLE_SimulatedProxy)
	{
		FVector Start = UpdatedComponent->GetComponentLocation();
		FVector End = Start + UpdatedComponent->GetRightVector() * CapR() * 2;
		FHitResult WallHit;
		Safe_bWallRunIsRight = GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
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

void UDemoMyCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	if (!IsCustomMovementMode(CMOVE_Grapple) && bWantsToGrapple)
	{
		EnterGrapple();
	}
	
	if (IsCustomMovementMode(CMOVE_Grapple) && (bWantsToCrouch || !bWantsToGrapple))
	{
		ExitGrapple();
	}

	if (MovementMode == MOVE_Walking && bWantsToCrouch)
	{
		if (Velocity.SizeSquared() > pow(Slide_MinSpeed, 2) && !bLeftSlide)
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

	//Try Mantle or Jump
	if (DemoCharacterOwner->bPressedDemoJump)
	{
		if (TryMantle())
		{
			DemoCharacterOwner->StopJumping();
		}
		else
		{
			DemoCharacterOwner->bPressedDemoJump = false;
			CharacterOwner->bPressedJump = true;
			CharacterOwner->CheckJumpInput(DeltaSeconds);
		}

		if (Safe_bTransitionFinished)
		{
			if (IsValid(TransitionQueueMontage))
			{
				SetMovementMode(MOVE_Flying);
				CharacterOwner->PlayAnimMontage(TransitionQueueMontage, TransitionQueueMontageSpeed);
				TransitionQueueMontageSpeed = 0.0f;
				TransitionQueueMontage = nullptr;
			}
			else
			{
				SetMovementMode(MOVE_Walking);
			}
			Safe_bTransitionFinished = false;
		}
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
	if (GetRootMotionSourceByID(TransitionRMS_ID) && GetRootMotionSourceByID(TransitionRMS_ID)->Status.HasFlag(ERootMotionSourceStatusFlags::Finished))
	{
		RemoveRootMotionSourceByID(TransitionRMS_ID);
		Safe_bTransitionFinished = true;
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

bool UDemoMyCharacterMovementComponent::IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == InCustomMovementMode;
}

UDemoMyCharacterMovementComponent::UDemoMyCharacterMovementComponent() : Super()
{

}

void UDemoMyCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();

	DemoCharacterOwner = Cast<AActionDemoCharacter>(GetOwner());
}

UDemoMyCharacterMovementComponent::FSavedMove_Demo::FSavedMove_Demo() : Super()
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

	return FSavedMove_Character::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void UDemoMyCharacterMovementComponent::FSavedMove_Demo::Clear()
{
	FSavedMove_Character::Clear();

	Saved_bWantsToSprint = 0;
	Saved_bWallRunIsRight = 0;
	Saved_bPressedDemoJump = 0;
}

uint8 UDemoMyCharacterMovementComponent::FSavedMove_Demo::GetCompressedFlags() const
{
	uint8 Result = FSavedMove_Character::GetCompressedFlags();

	if (Saved_bWantsToSprint) Result |= FLAG_Custom_0;
	if (Saved_bPressedDemoJump) Result |= FLAG_JumpPressed;

	return Result;
}

void UDemoMyCharacterMovementComponent::FSavedMove_Demo::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	FSavedMove_Character::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	UDemoMyCharacterMovementComponent* CharacterMovement = Cast<UDemoMyCharacterMovementComponent>(C->GetCharacterMovement());

	Saved_bWantsToSprint = CharacterMovement->Safe_bWantsToSprint;

	Saved_bPrevWantsToCrouch = CharacterMovement->Safe_bWantsToCrouch;

	Saved_bWallRunIsRight = CharacterMovement->Safe_bWallRunIsRight;

	Saved_bPressedDemoJump = CharacterMovement->DemoCharacterOwner->bPressedDemoJump;

	Saved_bHadAnimRootMotion = CharacterMovement->Safe_bHadAnimRootMotion;

	Saved_bTransitionFinished = CharacterMovement->Safe_bTransitionFinished;
}

void UDemoMyCharacterMovementComponent::FSavedMove_Demo::PrepMoveFor(ACharacter* C)
{
	FSavedMove_Character::PrepMoveFor(C);

	UDemoMyCharacterMovementComponent* CharacterMovement = Cast<UDemoMyCharacterMovementComponent>(C->GetCharacterMovement());

	CharacterMovement->Safe_bWantsToSprint = Saved_bWantsToSprint;

	CharacterMovement->Safe_bWantsToCrouch = Saved_bPrevWantsToCrouch;

	CharacterMovement->Safe_bWallRunIsRight = Saved_bWallRunIsRight;

	CharacterMovement->DemoCharacterOwner->bPressedDemoJump = Saved_bPressedDemoJump;

	CharacterMovement->Safe_bHadAnimRootMotion = Saved_bHadAnimRootMotion;

	CharacterMovement->Safe_bTransitionFinished = Saved_bTransitionFinished;
}

UDemoMyCharacterMovementComponent::FNetworkPredictionData_Client_Demo::FNetworkPredictionData_Client_Demo(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement)
{

}

FSavedMovePtr UDemoMyCharacterMovementComponent::FNetworkPredictionData_Client_Demo::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Demo());
}
#pragma endregion

float UDemoMyCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsMovementMode(MOVE_Walking) && Safe_bWantsToSprint && !IsCrouching()) return Sprint_MaxWalkSpeed;

	if (MovementMode != MOVE_Custom) return Super::GetMaxSpeed();

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
			GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
			Velocity += WallHit.Normal * WallRun_JumpOffForce;
		}
		else if (bWasClimbing)
		{
			FVector Start = UpdatedComponent->GetComponentLocation() + FVector::DownVector * CapHH();
			FVector End = Start + UpdatedComponent->GetForwardVector() * CapR() * 2;
			FHitResult WallHit;
			GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
			Velocity += WallHit.Normal * Climb_JumpOffForce;
		}
		return true;
	}
	return false;
}

#pragma region Slide
void UDemoMyCharacterMovementComponent::EnterSlide()
{
	bWantsToCrouch = true;
	Velocity += Velocity.GetSafeNormal2D() * Slide_EnterImpulse;
	SetMovementMode(MOVE_Custom, CMOVE_Slide);
}
void UDemoMyCharacterMovementComponent::ExitSlide()
{
	bLeftSlide = true;
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(UpdatedComponent->GetForwardVector().GetSafeNormal2D(), FVector::UpVector).ToQuat();
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
	return Velocity.SizeSquared() > pow(Slide_MinSpeed, 2);
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


	Velocity += Slide_GravityForce * FVector::DownVector * DeltaTime;

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
		SlopeForce.Z = 0.f;
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
	FQuat NewRotation = FRotationMatrix::MakeFromXZ(Velocity.GetSafeNormal2D(), FVector::UpVector).ToQuat();
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, Hit);
}
#pragma endregion

#pragma region Mantle
bool UDemoMyCharacterMovementComponent::TryMantle()
{
	return false;
	//Check mode
	if (!IsMovementMode(MOVE_Walking) && !IsMovementMode(MOVE_Falling)) return false;

	//Check facing a wall
	FVector BaseLoc = UpdatedComponent->GetComponentLocation() + FVector::DownVector * CapHH();
	FVector Fwd = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	float MaxHeight = CapHH() + Mantle_ReachHeight;
	float CosMMWSA = FMath::Cos(FMath::DegreesToRadians(Mantle_MinWallSteepnessAngle));
	float CosMMSA = FMath::Cos(FMath::DegreesToRadians(Mantle_MaxSurfaceAngle));
	float CosMMAA = FMath::Cos(FMath::DegreesToRadians(Mantle_MaxAlignmentAngle));

	FHitResult FrontHit;
	float CheckDistance = FMath::Clamp(Velocity | Fwd, CapR() + 30.0f, Mantle_MaxDistance);
	FVector FrontStart = BaseLoc + FVector::UpVector * (MaxStepHeight - 1);
	for (int i = 0; i < 6; i++)
	{
		if (GetWorld() -> LineTraceSingleByProfile(FrontHit, FrontStart, FrontStart + Fwd*CheckDistance, "BlockAll",DemoCharacterOwner->GetIgnoreCharacterParams())) break;
		FrontStart += FVector::UpVector * (2.0f * CapHH() - (MaxStepHeight - 1)) / 5;
	}
	if (!FrontHit.IsValidBlockingHit()) return false;
	float CosWallSteepnessAngle = FrontHit.Normal | FVector::UpVector;
	if (FMath::Abs(CosWallSteepnessAngle) > CosMMWSA || (Fwd | -FrontHit.Normal) < CosMMAA) return false;
	
	//Check top surface
	TArray<FHitResult> HeightHits;
	FHitResult SurfaceHit;
	FVector WallUp = FVector::VectorPlaneProject(FVector::UpVector, FrontHit.Normal).GetSafeNormal();
	float WallCos = FVector::UpVector | FrontHit.Normal;
	float WallSin = FMath::Sqrt(1 - WallCos * WallCos);
	FVector TraceStart = FrontHit.Location + Fwd + WallUp * (MaxHeight - (MaxStepHeight - 1)) / WallSin;//Fwd is used to avoid edge cases

	if (!GetWorld()->LineTraceMultiByProfile(HeightHits, TraceStart, FrontHit.Location + Fwd, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams())) return false;

	for (const FHitResult& Hit : HeightHits)
	{
		if (Hit.IsValidBlockingHit())
		{
			SurfaceHit = Hit;
			break;
		}
	}
	if (!SurfaceHit.IsValidBlockingHit() || (SurfaceHit.Normal | FVector::UpVector) < CosMMSA) return false;
	float Height = (SurfaceHit.Location - BaseLoc) | FVector::UpVector;

	if (Height > MaxHeight) return false;

	//Check Clearance on top
	float SurfaceCos = FVector::UpVector | SurfaceHit.Normal;
	float SurfaceSin = FMath::Sqrt(1 - SurfaceCos * SurfaceCos);
	FVector ClearCapLoc = SurfaceHit.Location + Fwd * CapR() + FVector::UpVector * (CapHH() + 1 + CapR() * 2 * SurfaceSin);
	FCollisionShape CapShape = FCollisionShape::MakeCapsule(CapR(), CapHH());
	FCollisionShape CrouchedCapShape = FCollisionShape::MakeCapsule(CapR(), GetCrouchedHalfHeight());
	CAPSULE(ClearCapLoc, FColor::Red);
	CROUCHCAPSULE(ClearCapLoc, FColor::Red);
	if (GetWorld()->OverlapAnyTestByProfile(ClearCapLoc, FQuat::Identity, "BlockAll", CapShape, DemoCharacterOwner->GetIgnoreCharacterParams()))
	{
		if (!GetWorld()->OverlapAnyTestByProfile(ClearCapLoc, FQuat::Identity, "BlockAll", CrouchedCapShape, DemoCharacterOwner->GetIgnoreCharacterParams()))
		{
			bWantsToCrouch = true;//explore how it should be done
		}
		else 
		{
			return false;
		}
	}

	//Mantle Selection
	FVector TransitionTarget = GetMantleStartLocation(FrontHit, SurfaceHit);

	float UpSpeed = Velocity | FVector::UpVector;
	float TransDistance = FVector::Dist(TransitionTarget, UpdatedComponent->GetComponentLocation());
	TransitionQueueMontageSpeed = FMath::GetMappedRangeValueClamped(FVector2D(-500, 750), FVector2D(0.9f, 1.2f), UpSpeed);
	TransitionRMS.Reset();
	TransitionRMS = MakeShared<FRootMotionSource_MoveToForce>();
	TransitionRMS->AccumulateMode = ERootMotionAccumulateMode::Override;

	TransitionRMS->Duration = FMath::Clamp(TransDistance / 500.0f, 0.1f, 0.25f);
	TransitionRMS->StartLocation = UpdatedComponent->GetComponentLocation();
	TransitionRMS->TargetLocation = TransitionTarget;

	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Flying);
	TransitionRMS_ID = ApplyRootMotionSource(TransitionRMS);

	TransitionQueueMontage = Mantle_ShortMontage;
	CharacterOwner->PlayAnimMontage(Mantle_TransitionShortMontage, 1 / TransitionRMS->Duration);
	//if (IsServer()) Proxy_bShortMantle = !Proxy_bShortMantle;

	return true;
}

FVector UDemoMyCharacterMovementComponent::GetMantleStartLocation(FHitResult FrontHit, FHitResult SurfaceHit) const
{
	float CosWallSteepnessAngle = FrontHit.Normal | FVector::UpVector;
	float DownDistance = MaxStepHeight - 1;
	FVector EdgeTangent = FVector::CrossProduct(SurfaceHit.Normal, FrontHit.Normal).GetSafeNormal();
	FVector MantleStart = SurfaceHit.Location;
	MantleStart += FrontHit.Normal.GetSafeNormal2D()*(2.0f + CapR());
	MantleStart += UpdatedComponent->GetForwardVector().GetSafeNormal2D().ProjectOnTo(EdgeTangent) * CapR() * 0.3f;
	MantleStart += FVector::UpVector * CapHH();
	MantleStart += FVector::DownVector * DownDistance;
	MantleStart += FrontHit.Normal.GetSafeNormal2D() * CosWallSteepnessAngle * DownDistance;
	return MantleStart;
}

#pragma endregion

#pragma region WallRun
bool UDemoMyCharacterMovementComponent::TryWallRun()
{
	if (!IsFalling()) return false;
	if (Velocity.SizeSquared2D() < pow(WallRun_MinSpeed, 2)) return false;
	if (Velocity.Z < -WallRun_MaxVerticalSpeed) return false;
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector LeftEnd = Start - UpdatedComponent->GetRightVector() * CapR() * 2;
	FVector RightEnd = Start + UpdatedComponent->GetRightVector() * CapR() * 2;
	FHitResult FloorHit, WallHit;
	if (GetWorld()->LineTraceSingleByProfile(FloorHit, Start, Start + FVector::DownVector * (CapHH() + WallRun_MinHeight), "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams())) return false;

	GetWorld()->LineTraceSingleByProfile(WallHit, Start, LeftEnd, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
	if (WallHit.IsValidBlockingHit() && (Velocity | WallHit.Normal) < 0)
	{
		Safe_bWallRunIsRight = false;
	}
	else
	{
		GetWorld()->LineTraceSingleByProfile(WallHit, Start, RightEnd, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
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
	if (ProjectedVelocity.SizeSquared2D() < pow(WallRun_MinSpeed, 2)) return false;

	Velocity = ProjectedVelocity;
	Velocity.Z = FMath::Clamp(Velocity.Z, 0.0f, WallRun_MaxVerticalSpeed);
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
		GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
		bool bWantsToPullAway = WallHit.IsValidBlockingHit() && !Acceleration.IsNearlyZero() && (Acceleration.GetSafeNormal() | WallHit.Normal) > SinPullAwayAngle;
		if (!WallHit.IsValidBlockingHit() || bWantsToPullAway)
		{
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}
		// Clamp Acceleration
		Acceleration = FVector::VectorPlaneProject(Acceleration, WallHit.Normal);
		Acceleration.Z = 0.0f;
		// Apply acceleration
		CalcVelocity(timeTick, 0.f, false, GetMaxBrakingDeceleration());
		Velocity = FVector::VectorPlaneProject(Velocity, WallHit.Normal);
		float TangentAccel = Acceleration.GetSafeNormal() | Velocity.GetSafeNormal2D();
		bool bVelUp = Velocity.Z > 0.0f;
		Velocity.Z += GetGravityZ() * WallRun_GravityScaleCurve->GetFloatValue(bVelUp ? 0.f : TangentAccel) * timeTick;
		if (Velocity.SizeSquared2D() < pow(WallRun_MinSpeed, 2) || Velocity.Z < -WallRun_MaxVerticalSpeed)
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
	GetWorld()->LineTraceSingleByProfile(WallHit, Start, End, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
	GetWorld()->LineTraceSingleByProfile(FloorHit, Start, Start + FVector::DownVector * (CapHH() + WallRun_MinHeight * .5f), "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());
	if (FloorHit.IsValidBlockingHit() || !WallHit.IsValidBlockingHit() || Velocity.SizeSquared2D() < pow(WallRun_MinSpeed, 2))
	{
		SetMovementMode(MOVE_Falling);
	}
}

#pragma endregion

#pragma region Climb
bool UDemoMyCharacterMovementComponent::TryClimb()
{
	if (!IsFalling()) return false;
	if (Velocity.Z < -Climb_MinSpeed) return false;
	float CosMMWSA = FMath::Cos(FMath::DegreesToRadians(Climb_MinWallSteepnessAngle));
	float CosMMAA = FMath::Cos(FMath::DegreesToRadians(Climb_MaxAlignmentAngle));

	FVector Start = UpdatedComponent->GetComponentLocation() + FVector::DownVector * CapHH();
	FVector Fwd = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	FHitResult WallHit;

	for (int i = 0; i < 5; i++)
	{
		if (GetWorld()->LineTraceSingleByProfile(WallHit, Start, Start + Fwd * CapR() * 2, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams())) break;
		Start += FVector::UpVector * (1.5f * CapHH() - (MaxStepHeight - 1)) / 4;
	}
	if (!WallHit.IsValidBlockingHit())
	{
		bFellOff = false;
		return false;
	}

	if (bFellOff) return false;
	if ((Acceleration | UpdatedComponent->GetForwardVector().GetSafeNormal2D()) <= 0.0f) return false;//Don't enter if the player is not actively moving forward
	float CosWallSteepnessAngle = WallHit.Normal | FVector::UpVector;
	if (FMath::Abs(CosWallSteepnessAngle) > CosMMWSA || (Fwd | -WallHit.Normal) < CosMMAA) return false;

	FQuat NewRotation = FRotationMatrix::MakeFromXZ(-WallHit.Normal, FVector::UpVector).ToQuat();
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, WallHit);

	Velocity.Z += Climb_EnterImpulse;
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

	bJustTeleported = false;
	float remainingTime = DeltaTime;
	// Perform the move
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)))
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		bClimbTime += timeTick;
		remainingTime -= timeTick;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();

		FVector Start = UpdatedComponent->GetComponentLocation() + FVector::DownVector * CapHH();
		FVector Fwd = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
		FHitResult WallHit;

		for (int i = 0; i < 5; i++)
		{
			if (GetWorld()->LineTraceSingleByProfile(WallHit, Start, Start + Fwd * CapR() * 2, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams())) break;
			Start += FVector::UpVector * (1.5f * CapHH() - (MaxStepHeight - 1)) / 4;
		}
		if (!WallHit.IsValidBlockingHit())
		{
			Velocity.Z = 0.0f;
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(remainingTime, Iterations);
			return;
		}
		// Clamp Acceleration
		Acceleration = Acceleration.RotateAngleAxis(90.0f, -UpdatedComponent->GetRightVector());
		Acceleration.X = 0.0f;
		Acceleration.Y = 0.0f;
		//Acceleration.Z += GetGravityZ() * Climb_GravityScaleCurve->GetFloatValue(bClimbTime);
		AddImpulse(FVector::DownVector * Climb_GravityForce * Climb_GravityScaleCurve->GetFloatValue(bClimbTime));
		//UE_LOG(LogTemp, Warning, TEXT("Acceleration: %s"), *Acceleration.ToString())
		// Apply acceleration
		CalcVelocity(timeTick, 1.0f, false, GetMaxBrakingDeceleration());
		//UE_LOG(LogTemp, Warning, TEXT("Velocity: %s"), *Velocity.ToString())
		Velocity = FVector::VectorPlaneProject(Velocity, WallHit.Normal);
		if (Velocity.Z <= -Climb_MinSpeed)
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
float UDemoMyCharacterMovementComponent::TryGrapple()
{
	FVector Start = UpdatedComponent->GetComponentLocation() + FVector::UpVector * Grapple_CamOffset;
	FVector Fwd = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraRotation().Vector();
	FHitResult PivotHit;

	GetWorld()->LineTraceSingleByProfile(PivotHit, Start, Start + Fwd * Grapple_Range, "BlockAll", DemoCharacterOwner->GetIgnoreCharacterParams());

	if (!PivotHit.IsValidBlockingHit())
	{
		Grapple_Target = nullptr;
		Grapple_Pivot = Start + Fwd * Grapple_Range;
		return -1.0f;
	}

	//Grapple_Target = PivotHit.GetActor();
	Grapple_Target = PivotHit.GetComponent();
	Grapple_PivotNormal = PivotHit.ImpactNormal;
	//Grapple_Pivot = UKismetMathLibrary::InverseTransformLocation(Grapple_Target->GetTransform(), PivotHit.ImpactPoint);
	Grapple_Pivot = UKismetMathLibrary::InverseTransformLocation(Grapple_Target->GetOwner()->GetTransform(), PivotHit.ImpactPoint);

	return FVector::Dist(Start,PivotHit.ImpactPoint);
}

FVector UDemoMyCharacterMovementComponent::GetGrapplingPivot()
{
	if (Grapple_Target)
	{
		//return UKismetMathLibrary::TransformLocation(Grapple_Target->GetTransform(), Grapple_Pivot);
		return UKismetMathLibrary::TransformLocation(Grapple_Target->GetOwner()->GetTransform(), Grapple_Pivot);
	}
	return Grapple_Pivot;
}

void UDemoMyCharacterMovementComponent::Grapple()
{
	bWantsToGrapple = true;
}

void UDemoMyCharacterMovementComponent::EnterGrapple()
{
	Grapple_TargetMovable = Grapple_Target->GetOwner()->IsRootComponentMovable();
	Velocity += Velocity.GetSafeNormal() * Grapple_EnterImpulse;
	SetMovementMode(MOVE_Custom, CMOVE_Grapple);
}

void UDemoMyCharacterMovementComponent::ExitGrapple()
{
	if (Grapple_TargetMovable)
	{
		Grapple_Target->ComponentVelocity = FVector::ZeroVector;
	}
	DemoCharacterOwner->UnGrapple();
	bWantsToGrapple = false;
	SetMovementMode(MOVE_Falling);
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

	// Perform the move
	Iterations++;
	bJustTeleported = false;
	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	FVector Start = UpdatedComponent->GetComponentLocation() + FVector::UpVector * Grapple_CamOffset;
	FVector bGrapplePivot = GetGrapplingPivot();
	FVector PivotDir = (bGrapplePivot - Start).GetSafeNormal();

	RestorePreAdditiveRootMotionVelocity();

	//Check if the player has reached destination
	if (FVector::Dist(Start, bGrapplePivot)<Grapple_FinishDist)
	{
		bWantsToGrapple = false;
		return;
	}

	//Check if there is obstacle in between, break grapple if true
	if ((PivotDir | Grapple_PivotNormal)>0.0f && !Grapple_TargetMovable)
	{
		bWantsToGrapple = false;
		return;
	 }

	// Calculate pull and steer and break the grapple if the tension is too great
	if (Grapple_TargetMovable)
	{
		Grapple_Target->AddImpulse(-PivotDir * Grapple_PullForce);
	}
	AddImpulse(PivotDir * Grapple_PullForce);
	Acceleration = Acceleration * Grapple_SteerFactor;
	float SinPullAwayAngle = FMath::Sin(FMath::DegreesToRadians(Grapple_PullAwayAngle));
	float TowardsComponent = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraRotation().Vector().GetSafeNormal() | PivotDir;
	bool bWantsToPullAway =  TowardsComponent < SinPullAwayAngle;
	//UE_LOG(LogTemp, Warning, TEXT("Acceleration: %s"), *Acceleration.ToString())
	// Apply acceleration
	CalcVelocity(DeltaTime, 0.0f, false, GetMaxBrakingDeceleration());
	//UE_LOG(LogTemp, Warning, TEXT("Velocity: %s"), *Velocity.ToString());
	if (bWantsToPullAway)
	{
		bWantsToGrapple = false;
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
