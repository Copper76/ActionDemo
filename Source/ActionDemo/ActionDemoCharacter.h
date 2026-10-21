// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "Public/Portal.h"
#include "Kismet/KismetMathLibrary.h"
#include "ActionDemoCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class USceneComponent;
class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UAnimMontage;
class USoundBase;
class UImage;
class ADecalActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UCableComponent;

UCLASS(config=Game)
class AActionDemoCharacter : public ACharacter
{
	GENERATED_BODY()

	protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Movement)
	class UDemoMyCharacterMovementComponent* DemoMyCharacterMovementComponent;

private:
	/** Pawn mesh: 1st person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* Mesh1P;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* PlayerCamera;

	/** Flatten Capture camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USceneCaptureComponent2D* m_FlattenCamera;

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

	inline float GetCurrentRoll() const { return CurrentRoll; }

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

	inline UCableComponent* GetGrappler() { return Grappler; }

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
	virtual void CheckJumpInput(float DeltaTime) override;

	void AlignMovement(const FQuat& RelativeRotation, const FQuat& ViewRotation);

protected:
	// APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	// End of APawn interface

public:
	/** Returns Mesh1P subobject **/
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns PlayerCamera subobject **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return PlayerCamera; }

	APortal* GetPortal(bool isBlue);

	inline bool IsInPortal() const { return CurrentPortal != nullptr; }

	inline APortal* GetCurrentPortal() const { return CurrentPortal; }

	inline void SetCurrentPortal(APortal* Portal) { CurrentPortal = Portal; }

	bool IsSprinting() const;

	/** Client traces locally, then asks the server to spawn/move the portal. */
	UFUNCTION(Server, Reliable)
	void ServerPlacePortal(bool bIsBlue, FVector_NetQuantize Centre, FRotator Rotation, TSubclassOf<APortal> PortalClass);

	void PlacePortal(bool bIsBlue, const FVector& Centre, const FRotator& Rotation, TSubclassOf<APortal> PortalClass);

protected:
	//Tilt
	UPROPERTY(BlueprintReadOnly) float TiltSpeed = 50.0f;

	//MuzzlePointSpeed
	UPROPERTY(BlueprintReadOnly) float MuzzlePointSpeed = 50.0f;
	UPROPERTY(BlueprintReadOnly) FRotator MuzzlePointDir;
	UPROPERTY(BlueprintReadOnly) float MuzzleMaxTilt = 15.0f;

public:
	bool bOwnsRifle;
	UPROPERTY(BlueprintReadOnly) AActor* bWeapon;
	UPROPERTY(BlueprintReadOnly) float TargetRoll;

	APortal* BluePortal;
	APortal* OrangePortal;

	FVector CameraOffset;
	APortal* CurrentPortal = nullptr;

	//AimingAPlayerController
	UPROPERTY(BlueprintReadOnly) FVector AimingPoseLoc;
	UPROPERTY(BlueprintReadOnly) FRotator AimingPoseRotation;
	UPROPERTY(BlueprintReadOnly) float ADSSpeed = 100.0f;
	bool isAiming = false;

private:
	void ReturnToLastCheckpoint();

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Defaults", meta = (AllowPrivateAccess))
	float KillZ = -1000.0f;

	float CurrentRoll = 0.0f;
};

