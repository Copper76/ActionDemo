// Copyright Epic Games, Inc. All Rights Reserved.


#include "ActionDemoController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "ActionDemoCharacter.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"

void AActionDemoController::UpdateRotation(float DeltaTime)
{
	const FVector GravityDirection = MovementComponent->GetGravityDirection();

	FRotator ViewRotation = GetControlRotation();

	ViewRotation = GetGravityRelativeRotation(ViewRotation, GravityDirection);

	if (PlayerCameraManager)
	{
		PlayerCameraManager->ProcessViewRotation(DeltaTime, ViewRotation, RotationInput);
		ViewRotation.Roll = 0.f;
	}

	ViewRotation = GetGravityWorldRotation(ViewRotation, GravityDirection);

	if (AActionDemoCharacter* const PC = Cast<AActionDemoCharacter>(GetPawnOrSpectator()))
	{
		const FQuat LeanQ(FVector::XAxisVector, FMath::DegreesToRadians(PC->GetCurrentRoll()));

		ViewRotation += LeanQ.Rotator();
	}

	SetControlRotation(ViewRotation);

	if (AActionDemoCharacter* const PC = Cast<AActionDemoCharacter>(GetPawnOrSpectator()))
	{
		const FVector Up = -GravityDirection;
		const FVector Fwd = FVector::VectorPlaneProject(ViewRotation.Vector(), Up).GetSafeNormal();

		if (Fwd.IsNearlyZero()) return;
		
		PC->SetActorRotation(FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat());
	}
}

void AActionDemoController::BeginPlay()
{
	Super::BeginPlay();

	if (AActionDemoCharacter* MyCharacter = Cast<AActionDemoCharacter>(GetPawn()))
	{
		MovementComponent = Cast<UCharacterMovementComponent>(MyCharacter->GetMovementComponent());
		MovementComponent->SetGravityDirection(-MyCharacter->GetActorUpVector());
		SetControlRotation(MyCharacter->GetActorRotation());
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