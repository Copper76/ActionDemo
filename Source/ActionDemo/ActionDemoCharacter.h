// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "CableComponent.h"
#include "Public/Portal.h"
#include "Kismet/KismetMathLibrary.h"
#include "ActionDemoCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class USceneComponent;
class UCameraComponent;
class UAnimMontage;
class USoundBase;

UENUM(BlueprintType)
enum EGrapplingStage
{
	GRAPPLE_SHOOT,
	GRAPPLE_INPROGRESS,
	GRAPPLE_RETRACT,
	GRAPPLE_NONE
};

UCLASS(config=Game)
class AActionDemoCharacter : public ACharacter
{
	GENERATED_BODY()

	protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Movement) class UDemoMyCharacterMovementComponent* DemoMyCharacterMovementComponent;

private:
	/** Pawn mesh: 1st person view (arms; seen only by self) */
	UPROPERTY(VisibleDefaultsOnly, Category=Mesh)
	USkeletalMeshComponent* Mesh1P;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* PlayerCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = cable, meta = (AllowPrivateAccess = "true"))
	class UCableComponent* Grappler;

	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	class UInputMappingContext* DefaultMappingContext;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	class UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	class UInputAction* MoveAction;

	/** Sprint Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* SprintAction;

	/** Sprint Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* FlashAction;

	/** Swap Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* SwapAction;

	/** Grapple Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* GrappleAction;

	/** Crouch Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* CrouchAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	FVector CrouchEyeOffset = FVector(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	float CrouchSpeed = 12.0f;

	
public:
	AActionDemoCharacter(const FObjectInitializer& ObjectInitializer);

	void Tick(float DeltaTime) override;

	FCollisionQueryParams GetIgnoreCharacterParams() const;

	FCollisionQueryParams GetIgnorePortalParams(bool isBlue) const;

protected:
	virtual void BeginPlay();

public:
		
	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LookAction;

	/** Bool for AnimBP to switch to another animation set */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Weapon)
	bool bHasRifle;

	/** Setter to set the bool */
	UFUNCTION(BlueprintCallable, Category = Weapon)
	void SetHasRifle(bool bNewHasRifle, AActor* weapon);

	/** Getter for the bool */
	UFUNCTION(BlueprintCallable, Category = Weapon)
	bool GetHasRifle();

protected:
	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	void ToggleSprint(const FInputActionValue& Value);

	void ToggleCrouch(const FInputActionValue& Value);

	void Grapple();

	void Flash();

	void SwapControl();

	virtual bool CanJumpInternal_Implementation() const override;

	void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

	virtual void Landed(const FHitResult& Hit) override;

public:
	virtual void Jump() override;

	virtual void StopJumping() override;

	void UnGrapple();

	void AlignMovement(FRotator NewRotation);

protected:
	// APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	// End of APawn interface

public:
	/** Returns Mesh1P subobject **/
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns PlayerCamera subobject **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return PlayerCamera; }

protected:
	//Flash
	float FlashReach = 1000.0f;
	FVector FlashOffset;
	float FlashCoolDown;
	float FlashMaxCoolDown = 1.0f;

	//Grapple
	float GrappleCoolDown = 0.0f;
	float GrappleMaxCoolDown = 3.0f;
	float GrappleTravelTime;
	bool GrappleIsSuccessful;
	UPROPERTY(BlueprintReadOnly) FVector GrappleEndLoc;
	UPROPERTY(BlueprintReadOnly) float GrappleSpeed = 4000.0f;

	//Tilt
	UPROPERTY(BlueprintReadOnly) float TiltSpeed = 50.0f;

	//MuzzlePointSpeed
	UPROPERTY(BlueprintReadOnly) float MuzzlePointSpeed = 50.0f;
	UPROPERTY(BlueprintReadOnly) FRotator MuzzlePointDir;
	UPROPERTY(BlueprintReadOnly) float MuzzleMaxTilt = 15.0f;

public:
	bool bPressedDemoJump;
	bool bOwnsRifle;
	UPROPERTY(BlueprintReadOnly) AActor* bWeapon;
	UPROPERTY(BlueprintReadOnly) float TargetRoll;
	UPROPERTY(BlueprintReadOnly) TEnumAsByte<EGrapplingStage> GrappleStage;

	//Portal
	APortal* BluePortal;
	APortal* OrangePortal;
};

