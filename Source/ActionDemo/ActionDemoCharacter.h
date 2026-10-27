// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "Public/Portal.h"
#include "Kismet/KismetMathLibrary.h"
#include "ActionDemoCharacter.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHolsterChanged, bool, bIsHolstered);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChanged, int32, Ammo, int32, MaxAmmo);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHPChanged, float, HP, float, MaxHP);

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
class UTP_WeaponComponent;
class UUserWidget;

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
	float CrouchEyeOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	float CrouchSpeed = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Health, meta = (AllowPrivateAccess = "true"))
	float MaxHP = 100.0f;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HP, Category = Health, meta = (AllowPrivateAccess = "true"))
	float HP = 100.0f;
	
public:
	AActionDemoCharacter(const FObjectInitializer& ObjectInitializer);

	void Tick(float DeltaTime) override;

	FCollisionQueryParams GetIgnoreCharacterParams() const;

	FCollisionQueryParams GetIgnorePortalParams(bool isBlue) const;

	inline float GetCurrentRoll() const { return CurrentRoll; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
		
	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LookAction;

	/** Rifle class spawned on the server (defaults to BP_PickUp_Rifle). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	TSubclassOf<AActor> WeaponClass;

	/** Socket/bone on Mesh1P for the local gun (FP Manny: weapon_r). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	FName FirstPersonWeaponSocket = FName(TEXT("ik_hand_gun"));

	/** Socket on the 3P body mesh for other players' view of the gun / grapple cable. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	FName ThirdPersonWeaponSocket = FName(TEXT("hand_r"));

	/** Server-spawned weapon mesh; replicated reference (component must be net-addressable / on a replicated actor). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Weapon, Category = Weapon)
	TObjectPtr<UTP_WeaponComponent> Weapon;

	/** Replicated: hide gun for everyone; owning client also swaps fire mapping. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Holstered, Category = Weapon)
	bool bHolstered = false;

	UPROPERTY(BlueprintAssignable, Category = Weapon)
	FOnHolsterChanged OnHolsterChanged;

	UPROPERTY(BlueprintAssignable, Category = Gameplay)
	FOnAmmoChanged OnAmmoChanged;

	UPROPERTY(BlueprintAssignable, Category = Health)
	FOnHPChanged OnHPChanged;

	UFUNCTION(BlueprintPure, Category = Health)
	float GetHP() const { return HP; }

	UFUNCTION(BlueprintPure, Category = Health)
	float GetMaxHP() const { return MaxHP; }

	/** Authority: subtract damage. OnHPChanged fires for the local owner (ApplyDamage or OnRep). */
	UFUNCTION(BlueprintCallable, Category = Health)
	void ApplyDamage(float DamageAmount);

	void Die();

	void RestoreAfterRespawn();

	UFUNCTION(Client, Reliable)
	void ClientApplyRespawnOrientation(FVector GravityDirection, FRotator ControlRotation);

	UPROPERTY(BlueprintReadOnly, Replicated, Category = Weapon)
	bool bSuppressWeapon = false;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = Weapon)
	bool bPrevHolstered = false;

	UFUNCTION(BlueprintCallable, Category = Weapon)
	bool IsHolstered() const;

	UFUNCTION(BlueprintCallable, Category = Weapon)
	void SetHolstered(bool bInHolstered);

	/** Climb / active grapple: gun forced holstered; restore after if it was drawn. */
	void ForceHolsterForParkour();
	void TryRestoreWeaponAfterParkour();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<UAnimMontage> HolsterMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<UAnimMontage> EquipMontage;

	/** FP arms fire montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<UAnimMontage> FireMontage;

	/** FP arms reload montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Weapon)
	TObjectPtr<UAnimMontage> ReloadMontage;

	void PlayFireMontage();
	void PlayReloadMontage();
	void StopReloadMontage(float BlendOutTime = 0.0f);

	UFUNCTION(BlueprintPure, Category = Animation)
	FVector2D GetMouseInputForAnim() const { return LastMouseInput; }

	UFUNCTION(BlueprintPure, Category = Animation)
	bool IsCloseToWall() const { return bCloseToWall; }

	/** Gravity-relative control pitch/yaw for FP sway (maps Infima Rotation_Pitch / Rotation_Yaw). */
	UFUNCTION(BlueprintPure, Category = Animation)
	FRotator GetAnimLookRotation() const;

	UFUNCTION(BlueprintPure, Category = Weapon)
	bool IsAiming() const { return bAiming; }

	void SetAiming(bool bInAiming);

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

	void ApplyHolsterState();

	bool IsPlayingHolsterMontage() const;

	UFUNCTION(BlueprintPure, Category = Weapon)
	bool IsPlayingEquipMontage() const;

	void UpdateCloseToWall();

	/** Authority: spawn WeaponClass once. */
	void SpawnWeapon();

	/** Locally controlled: Mesh1P + FirstPersonWeaponSocket. Otherwise: mesh + ThirdPersonWeaponSocket. */
	void AttachWeapon();

	/** Same attach mesh/socket selection as AttachWeapon. */
	void AttachGrappler();

	void BindWeaponInput();

	/** Local player only: create HudWidgetClass and AddToViewport after possess. */
	void ShowHudWidget();

	void RemoveHudWidget();

	virtual void NotifyControllerChanged() override;

	UFUNCTION()
	void OnRep_Weapon();

	UFUNCTION()
	void OnRep_Holstered();

	UFUNCTION()
	void OnRep_HP();

	UFUNCTION(Server, Reliable)
	void ServerSetHolstered(bool bInHolstered);

	virtual bool CanJumpInternal_Implementation() const override;

	void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

	/** BaseEyeHeight + CrouchEyeOffset on the camera component so Mesh1P tracks the eye. */
	void ApplyCrouchCameraOffset();

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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	APortal* GetPortal(bool isBlue);

	inline bool IsInPortal() const { return CurrentPortal != nullptr; }

	inline APortal* GetCurrentPortal() const { return CurrentPortal; }

	inline void SetCurrentPortal(APortal* Portal) { CurrentPortal = Portal; }

	/**
	 * Owning client: predict teleport locally, then ask the server to commit.
	 * Listen-server / standalone: apply once on authority.
	 */
	void RequestPortalTeleport(APortal* EntryPortal);

	bool IsSprinting() const;

	/** Client traces locally, then asks the server to spawn/move the portal. */
	UFUNCTION(Server, Reliable)
	void ServerPlacePortal(bool bIsBlue, FVector_NetQuantize Centre, FRotator Rotation, TSubclassOf<APortal> PortalClass);

	void PlacePortal(bool bIsBlue, const FVector& Centre, const FRotator& Rotation, TSubclassOf<APortal> PortalClass);

	/** Server commits a portal transit detected on the owning client. */
	UFUNCTION(Server, Reliable)
	void ServerTeleportThroughPortal(APortal* EntryPortal);

	/** Owning client / listen host: send a fire trace for the server to resolve. */
	UFUNCTION(Server, Reliable)
	void ServerProcessWeaponFire(FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd, TSubclassOf<AActor> HoleClass, float ImpulseStrength);

	/** Authority: resolve hit, spawn hole, impulse, FX. */
	void ProcessWeaponFire(FVector TraceStart, FVector TraceEnd, TSubclassOf<AActor> HoleClass, float ImpulseStrength);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWeaponFireFX(FVector_NetQuantize SoundLocation);

	/** CMC pushes cable tip; authority replicates for simulated proxies. */
	void PushGrappleCableVisual(bool bActive, const FVector& EndWorldLocation);

protected:
	//Tilt
	UPROPERTY(BlueprintReadOnly) float TiltSpeed = 50.0f;

	//MuzzlePointSpeed
	UPROPERTY(BlueprintReadOnly) float MuzzlePointSpeed = 50.0f;
	UPROPERTY(BlueprintReadOnly) FRotator MuzzlePointDir;
	UPROPERTY(BlueprintReadOnly) float MuzzleMaxTilt = 15.0f;

public:
	UPROPERTY(BlueprintReadOnly) float TargetRoll;

	/** Server-owned slots; replicated so owning client placement traces can ignore / colour-check. */
	UPROPERTY(Replicated)
	TObjectPtr<APortal> BluePortal;

	UPROPERTY(Replicated)
	TObjectPtr<APortal> OrangePortal;

	FVector CameraOffset;

	/** Which portal volume this character is in; replicated so CMC / server validation stay aligned. Local maps like portal TeleportingObjects stay per-machine. */
	UPROPERTY(Replicated)
	TObjectPtr<APortal> CurrentPortal = nullptr;

	/** Grapple cable visible to remotes. */
	UPROPERTY(ReplicatedUsing = OnRep_GrappleCable)
	bool bGrappleCableActive = false;

	UPROPERTY(Replicated)
	FVector_NetQuantize GrappleCableEndWS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = Weapon)
	bool bAiming = false;

private:
	FVector2D LastMouseInput = FVector2D::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = Animation, meta = (AllowPrivateAccess = "true"))
	float CloseToWallTraceDistance = 120.0f;

	bool bCloseToWall = false;

	/** Local: detect kill-Z / death and ask the server to respawn. */
	void RequestReturnToLastCheckpoint();

	UFUNCTION(Server, Reliable)
	void ServerRequestReturnToCheckpoint();

	void ApplyGrappleCableVisual(bool bActive, const FVector& EndWorldLocation);

	UFUNCTION()
	void OnRep_GrappleCable();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Defaults", meta = (AllowPrivateAccess))
	float KillZ = -1000.0f;

	/** UMG HUD (defaults to /Game/UI/HUD). Shown for locally controlled players after possession. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = UI, meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UUserWidget> HudWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HudWidget;

	/** Prevents spamming ServerRequestReturnToCheckpoint while still below KillZ. */
	bool bCheckpointRespawnPending = false;

	float CurrentRoll = 0.0f;
};
