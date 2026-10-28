// Copyright Epic Games, Inc. All Rights Reserved.


#include "ActionDemoController.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "ActionDemoCharacter.h"

void AActionDemoController::UpdateRotation(float DeltaTime)
{
	if (!MovementComponent.IsValid())
	{
		Super::UpdateRotation(DeltaTime);
		return;
	}

	const FVector GravityDirection = MovementComponent->GetGravityDirection();

	FRotator ViewRotation = GetControlRotation();

	ViewRotation = GetGravityRelativeRotation(ViewRotation, GravityDirection);

	if (PlayerCameraManager)
	{
		PlayerCameraManager->ProcessViewRotation(DeltaTime, ViewRotation, RotationInput);
		ViewRotation.Roll = 0.f;
	}

	ViewRotation = GetGravityWorldRotation(ViewRotation, GravityDirection);

	if (DemoCharacter)
	{
		const FQuat LeanQ(FVector::XAxisVector, FMath::DegreesToRadians(DemoCharacter->GetCurrentRoll()));

		ViewRotation += LeanQ.Rotator();

		const FVector2D RecoilDelta = DemoCharacter->GetRecoilOffsetDelta();

		ViewRotation += FRotator(RecoilDelta.Y, RecoilDelta.X, 0.0f);
	}

	SetControlRotation(ViewRotation);

	if (DemoCharacter)
	{
		const FVector Up = -GravityDirection;
		const FVector Fwd = FVector::VectorPlaneProject(ViewRotation.Vector(), Up).GetSafeNormal();

		if (Fwd.IsNearlyZero()) return;
		
		DemoCharacter->SetActorRotation(FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat());
	}
}

void AActionDemoController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	InitPossessedPawn(InPawn);
}

void AActionDemoController::OnUnPossess()
{
	MovementComponent.Reset();
	Super::OnUnPossess();
}

void AActionDemoController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);
	InitPossessedPawn(P);
}

void AActionDemoController::InitPossessedPawn(APawn* InPawn)
{
	DemoCharacter = Cast<AActionDemoCharacter>(InPawn);
	if (!DemoCharacter)
	{
		MovementComponent.Reset();
		return;
	}

	MovementComponent = Cast<UCharacterMovementComponent>(DemoCharacter->GetMovementComponent());
	if (!MovementComponent.IsValid())
	{
		return;
	}

	// Owning client and server both need this; SetGravityDirection is not replicated.
	MovementComponent->SetGravityDirection(-DemoCharacter->GetActorUpVector());
	SetControlRotation(DemoCharacter->GetActorRotation());

	if (IsLocalController())
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
		bShowMouseCursor = false;
	}
}

FQuat AActionDemoController::GetGravityRelativeRotation(FQuat Rotation, FVector GravityDirection)
{
	if (!GravityDirection.Equals(FVector::DownVector))
	{
		FQuat GravityRotation = FQuat::FindBetweenNormals(GravityDirection, FVector::DownVector);
		return GravityRotation * Rotation;
	}

	return Rotation;
}

FQuat AActionDemoController::GetGravityWorldRotation(FQuat Rotation, FVector GravityDirection)
{
	if (!GravityDirection.Equals(FVector::DownVector))
	{
		FQuat GravityRotation = FQuat::FindBetweenNormals(FVector::DownVector, GravityDirection);
		return GravityRotation * Rotation;
	}

	return Rotation;
}

FRotator AActionDemoController::GetGravityRelativeRotation(FRotator Rotation, FVector GravityDirection)
{
	return GetGravityRelativeRotation(Rotation.Quaternion(), GravityDirection).Rotator();
}

FRotator AActionDemoController::GetGravityWorldRotation(FRotator Rotation, FVector GravityDirection)
{
	return GetGravityWorldRotation(Rotation.Quaternion(), GravityDirection).Rotator();
}