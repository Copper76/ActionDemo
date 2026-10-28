// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ActionDemoController.generated.h"

class UCharacterMovementComponent;
class AActionDemoCharacter;

/**
 *
 */
UCLASS()
class AActionDemoController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void UpdateRotation(float DeltaTime) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void AcknowledgePossession(APawn* P) override;

	/** Cache CMC and align gravity / control rotation once the pawn is actually possessed. */
	void InitPossessedPawn(APawn* InPawn);

private:
	static FQuat GetGravityRelativeRotation(FQuat Rotation, FVector GravityDirection);

	static FQuat GetGravityWorldRotation(FQuat Rotation, FVector GravityDirection);

	static FRotator GetGravityRelativeRotation(FRotator Rotation, FVector GravityDirection);

	static FRotator GetGravityWorldRotation(FRotator Rotation, FVector GravityDirection);

private:
	TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;

	AActionDemoCharacter* DemoCharacter;
};
