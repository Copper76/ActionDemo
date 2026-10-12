// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ActionDemoController.generated.h"

class UCharacterMovementComponent;

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
	virtual void BeginPlay() override;

private:
	static FQuat GetGravityRelativeRotation(FQuat Rotation, FVector GravityDirection);

	static FQuat GetGravityWorldRotation(FQuat Rotation, FVector GravityDirection);

	static FRotator GetGravityRelativeRotation(FRotator Rotation, FVector GravityDirection);

	static FRotator GetGravityWorldRotation(FRotator Rotation, FVector GravityDirection);

private:
	TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;
};
