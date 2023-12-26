// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActionDemo/ActionDemoCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "DemoMyCharacterMovementComponent.generated.h"

UENUM(BlueprintType)
enum ECustomMovementMode
{
	CMOVE_None	UMETA(Hidden),
	CMOVE_Slide	UMETA(Display = "Slide"),
	CMOVE_WallRun UMETA(Display = "WallRun"),
	CMOVE_Climb UMETA(Display = "Climb"),
	CMOVE_Grapple UMETA(Display = "Grapple"),
	CMOVE_MAX	UMETA(Hidden),
};

UCLASS()
class ACTIONDEMO_API UDemoMyCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	class FSavedMove_Demo : public FSavedMove_Character
	{
		typedef FSavedMove_Character Super;

		uint8 Saved_bWantsToSprint : 1;

		uint8 Saved_bPrevWantsToCrouch : 1;
		uint8 Saved_bWallRunIsRight : 1;
		uint8 Saved_bPressedDemoJump : 1;

		uint8 Saved_bHadAnimRootMotion : 1;
		uint8 Saved_bTransitionFinished : 1;

	public:
		FSavedMove_Demo();

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
		virtual void Clear() override;
		virtual uint8 GetCompressedFlags() const override;
		virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
		virtual void PrepMoveFor(ACharacter* C) override;
	};

	class FNetworkPredictionData_Client_Demo : public FNetworkPredictionData_Client_Character
	{
	public:
		FNetworkPredictionData_Client_Demo(const UCharacterMovementComponent& ClientMovement);

		typedef FNetworkPredictionData_Client_Character Super;

		virtual FSavedMovePtr AllocateNewMove() override;
	};

	//Parameter
	//Sprint
	UPROPERTY(EditDefaultsOnly) float Sprint_MaxWalkSpeed = 800.0f;
	UPROPERTY(EditDefaultsOnly) float Walk_MaxWalkSpeed = 400.0f;
	//UPROPERTY(EditDefaultsOnly) float Walk_MaxBackWalkSpeed = 300.0f;

	//Sliding
	UPROPERTY(EditDefaultsOnly) float Slide_MinSpeed = 500.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_MaxSpeed = 1000.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_EnterImpulse = 500.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_GravityForce = 5000.0f;
	UPROPERTY(EditDefaultsOnly) float Slide_Friction = 0.1f; 
	UPROPERTY(EditDefaultsOnly) float Slide_SteerFactor = 0.1f;
	UPROPERTY(EditDefaultsOnly) float Slide_MaxBrakingDeceleration = 400.0f;

	//WallRun
	UPROPERTY(EditDefaultsOnly) float WallRun_MinSpeed = 500.0f;
	UPROPERTY(EditDefaultsOnly) float WallRun_MaxSpeed = 1000.0f;
	UPROPERTY(EditDefaultsOnly) float WallRun_MaxVerticalSpeed = 200.0f;
	UPROPERTY(EditDefaultsOnly) float WallRun_PullAwayAngle = 75.0f;
	UPROPERTY(EditDefaultsOnly) float WallRun_AttractionForce = 200.0f;
	UPROPERTY(EditDefaultsOnly) float WallRun_MinHeight = 50.0f;
	UPROPERTY(EditDefaultsOnly) UCurveFloat* WallRun_GravityScaleCurve;
	UPROPERTY(EditDefaultsOnly) float WallRun_JumpOffForce = 500.0f;

	//Mantle
	UPROPERTY(EditDefaultsOnly) float Mantle_MaxDistance = 200.0f;
	UPROPERTY(EditDefaultsOnly) float Mantle_ReachHeight = 70.0f;
	UPROPERTY(EditDefaultsOnly) float Mantle_MinDepth = 30.0f;
	UPROPERTY(EditDefaultsOnly) float Mantle_MinWallSteepnessAngle = 75.0f;
	UPROPERTY(EditDefaultsOnly) float Mantle_MaxSurfaceAngle = 40.0f;
	UPROPERTY(EditDefaultsOnly) float Mantle_MaxAlignmentAngle = 45.0f;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_TallMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_TransitionTallMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_ShortMontage;
	UPROPERTY(EditDefaultsOnly) UAnimMontage* Mantle_TransitionShortMontage;

	//Climb
	UPROPERTY(EditDefaultsOnly) float Climb_MaxSpeed = 300.0f;
	UPROPERTY(EditDefaultsOnly) float Climb_MinSpeed = 200.0f;
	UPROPERTY(EditDefaultsOnly) UCurveFloat* Climb_GravityScaleCurve;
	UPROPERTY(EditDefaultsOnly) float Climb_AttractionForce = 200.0f;
	UPROPERTY(EditDefaultsOnly) float Climb_EnterImpulse = 300.0f;
	UPROPERTY(EditDefaultsOnly) float Climb_MinWallSteepnessAngle = 75.0f;
	UPROPERTY(EditDefaultsOnly) float Climb_MaxAlignmentAngle = 30.0f;
	UPROPERTY(EditDefaultsOnly) float Climb_JumpOffForce = 300.0f;
	UPROPERTY(EditDefaultsOnly) float Climb_FallOffForce = 200.0f;
	UPROPERTY(EditDefaultsOnly) float Climb_GravityForce = 1000.0f;

	//Grapple
	UPROPERTY(EditDefaultsOnly) float Grapple_MaxSpeed = 1000.0f;
	UPROPERTY(EditDefaultsOnly) float Grapple_EnterImpulse = 200.0f;
	UPROPERTY(EditDefaultsOnly) float Grapple_EnterPullImpulse = 000.0f;
	UPROPERTY(EditDefaultsOnly) float Grapple_FinishDist = 160.0f;
	UPROPERTY(EditDefaultsOnly) float Grapple_PullForce = 1000.0f;
	UPROPERTY(EditDefaultsOnly) float Grapple_SteerFactor = 0.1f;
	UPROPERTY(EditDefaultsOnly) float Grapple_MaxTension = 700.0f;
	UPROPERTY(EditDefaultsOnly) float Grapple_CamOffset = 60.0f;
	UPROPERTY(EditDefaultsOnly) float Grapple_PullAwayAngle = 270.0f;
	FVector Grapple_Pivot;
	FVector Grapple_PivotNormal;
	UPrimitiveComponent* Grapple_Target;
	bool Grapple_TargetMovable;

	//Portal
	float Portal_MaxTravelSpeed = 1000.0f;

public:
	UPROPERTY(EditDefaultsOnly) float Grapple_Range = 1250.0f;

	//Transient
	UPROPERTY(Transient) AActionDemoCharacter* DemoCharacterOwner;

	bool Safe_bWantsToSprint;
	bool Safe_bWantsToCrouch;

	bool Safe_bWallRunIsRight;

	bool Safe_bHadAnimRootMotion;

	bool Safe_bTransitionFinished;
	TSharedPtr<FRootMotionSource_MoveToForce> TransitionRMS;
	UPROPERTY(Transient) UAnimMontage* TransitionQueueMontage;
	float TransitionQueueMontageSpeed;
	int TransitionRMS_ID;

	bool Safe_bWantsToGrapple;

public:
	bool bLeftSlide;
	bool bFellOff;
	float bClimbTime;
	bool bWantsToGrapple;

public:
	UDemoMyCharacterMovementComponent();

protected:
	virtual void InitializeComponent() override;

public:
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	virtual bool IsMovingOnGround() const override;

	virtual bool CanCrouchInCurrentState() const override;

	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;

	virtual float GetMaxSpeed() const override;

	virtual float GetMaxBrakingDeceleration() const override;

	virtual void PhysCustom(float DeltaTime, int32 Iterations);

	virtual bool CanAttemptJump() const override;
	virtual bool DoJump(bool bReplayingMoves) override;

//Slide
private:
	void EnterSlide();
	void ExitSlide();
	bool CanSlide() const;
	void PhysSlide(float DeltaTime, int32 Iterations);

//Wall Run
private:
	bool TryWallRun();
	void PhysWallRun(float DeltaTime, int32 Iterations);

//Mantle
private:
	bool TryMantle();
	FVector GetMantleStartLocation(FHitResult FrontHit, FHitResult SurfaceHit) const;

//Climb
private:
	bool TryClimb();
	void PhysClimb(float DeltaTime, int32 Iterations);

//Grapple
private:
	void EnterGrapple();
	void ExitGrapple();
	void PhysGrapple(float DeltaTime, int32 Iterations);

public:
	UFUNCTION(BlueprintCallable) void ToggleSprint(bool SprintValue);

	UFUNCTION(BlueprintCallable) void ToggleCrouch(bool CrouchValue);

	UFUNCTION(BlueprintCallable) void Grapple();

	UFUNCTION(BlueprintCallable) float TryGrapple();

	UFUNCTION(BlueprintCallable) FVector GetGrapplingPivot();

	UFUNCTION(BlueprintCallable) bool IsCustomMovementMode(ECustomMovementMode InCustomMovementMode) const;

	void AlignMovement(FRotator NewRotation);

//private:
	//UFUNCTION() void OnRep_ShortMantle();
	//UFUNCTION() void OnRep_TallMantle();

//Helper
private:
	bool IsWallRunning() const { return IsCustomMovementMode(CMOVE_WallRun); }
	bool IsSliding() const { return IsCustomMovementMode(CMOVE_Slide); }
	bool IsClimbing() const { return IsCustomMovementMode(CMOVE_Climb); }
	bool IsGrappling() const { return IsCustomMovementMode(CMOVE_Grapple); }
	bool WallRunningIsRight() const { return Safe_bWallRunIsRight;  }
	bool IsMovementMode(EMovementMode InMovementMode) const;
	float CapR() const;
	float CapHH() const;
	bool IsServer() const;
};
