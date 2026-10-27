// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "TP_WeaponComponent.generated.h"

class AActionDemoCharacter;
class APortal;
class UEnhancedInputComponent;

UENUM(BlueprintType)
enum EPortalType
{
	PORTAL_ENTER,
	PORTAL_EXIT,
	PORTAL_NONE
};

UCLASS(Blueprintable, BlueprintType, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class ACTIONDEMO_API UTP_WeaponComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:

	/** Projectile class to spawn */
	UPROPERTY(EditDefaultsOnly, Category = Projectile)
	TSubclassOf<class AActionDemoProjectile> ProjectileClass;

	/** Sound to play each time we fire */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay)
	USoundBase* FireSound;

	/** Weapon mesh fire montage (authority); character FireMontage tags along. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay)
	TObjectPtr<UAnimMontage> WeaponFireMontage;

	/** Weapon mesh reload montage (authority); character ReloadMontage tags along. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay)
	TObjectPtr<UAnimMontage> WeaponReloadMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay)
	int32 MaxAmmo = 30;

	UPROPERTY(BlueprintReadOnly, Category = Gameplay)
	int32 Ammo = 30;

	/** Damage dealt per hit on ProcessWeaponFire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay)
	float Damage = 20.0f;

	/** Gun muzzle's offset from the characters location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay)
	FVector MuzzleOffset;

	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputMappingContext* FireMappingContext;

	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputMappingContext* PortalMappingContext;

	/** Fire Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* FireAction;

	/** Fire Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* AimAction;

	/** Reload (Digital Bool) — mapped in IMC_AR / FireMappingContext. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* ReloadAction;

	/** Fire Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* OrangePortalAction;

	/** Fire Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* BluePortalAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* ToggleARAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* TogglePortalAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* EmptyAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Default, meta = (AllowPrivateAccess = "true"))
	TSubclassOf<APortal> PortalBP;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Default, meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AActor> BulletBP;

	/** Shell casing spawned at EjectionSocketName on rifle Fire (not portal). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Default, meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AActor> CasingBP;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Default, meta = (AllowPrivateAccess = "true"))
	FName EjectionSocketName = FName(TEXT("SOCKET_Ejection"));

	/** Sets default values for this component's properties */
	UTP_WeaponComponent();

	/** Bind fire / portal input on the owning local player (after weapon spawn). */
	void BindInput(UEnhancedInputComponent* EnhancedInputComponent);

	/** Make the weapon Fire a Projectile */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void Fire();

	/** Make the weapon aim down sight */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void Aim(const FInputActionValue& Value);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void Reload(const FInputActionValue& Value);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StopReload(float BlendOutTime = 0.0f);

	/** Stop FP arms + weapon fire montages (sprint / crouch / holster interrupt). */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StopFireMontages(float BlendOutTime = 0.0f);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsReloading() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetAmmo() const { return Ammo; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetMaxAmmo() const { return MaxAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetDamage() const { return Damage; }

	/** Fill magazine and notify HUD (respawn / restock). */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void RestockAmmo();

	/** Place a portal; bIsBlue true = blue, false = orange. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void PortalFire(bool bIsBlue);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void PortalFireBlue();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void PortalFireOrange();

	/** Make the weapon Fire a Projectile */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void ToggleAR();

	/** Make the weapon Fire a Projectile */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void TogglePortal();

	/** Empty action */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void Empty() {};

	bool CheckValidLoc(FVector& PortalCentre, FQuat& OutRotation, AActor*& TargetSurface, bool isBlue);

	/** True if Actor is a portal of the opposite colour (same colour is relocating, so not a block). */
	bool IsOtherColourPortal(const AActor* Actor, bool bIsBlue) const;

	/**
	 * Infima plays WEP montages / ejection on SK_AssaultRifle (SkeletalMesh + ABP),
	 * not on the logic component. Prefer that mesh when present on the weapon actor.
	 */
	USkeletalMeshComponent* GetAnimatedWeaponMesh() const;

protected:
	virtual void BeginPlay() override;

	/** Ends gameplay for this component. */
	UFUNCTION()
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Owning character (weapon actor owned by the pawn). */
	AActionDemoCharacter* m_Character = nullptr;
	APlayerController* PlayerController = nullptr;

	//Portal
	float Portal_Range = 5000.0f;
	float Portal_CamOffset = 60.0f;
	float Portal_Width = 125.0f;
	float Portal_Height = 225.0f;
	float Portal_EdgeCheckDelta = 20.0f;
	/** Pull the portal off the wall along the surface normal to avoid Z-fighting. */
	float Portal_SurfaceOffset = 1.0f;

	UFUNCTION()
	void OnWeaponMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void PlayFireMontages();
	void PlayReloadMontages();
	void SpawnCasing();

	/** Blocking face probe from portal centre outward along LateralOffset. */
	void TracePortalFaceHit(FHitResult& OutHit, const FVector& PortalCentre, const FVector& PortalSurface,
		const FVector& LateralOffset, const FCollisionQueryParams& QueryParams) const;

	/** Nudge PortalCentre if the edge has no wall; fail if another colour portal is there. */
	bool TryAdjustPortalEdge(FVector& PortalCentre, const FVector& PortalSurface, const FVector& EdgeOffset,
		const FVector& NudgeCamLocalDir, const FHitResult& FaceHit, float HalfExtent, bool bIsBlue,
		const FCollisionQueryParams& QueryParams) const;

	/** Fail if a corner sits on the opposite-colour portal. */
	bool IsPortalCornerBlocked(const FVector& PortalCentre, const FVector& PortalSurface,
		const FVector& CornerOffset, bool bIsBlue, const FCollisionQueryParams& QueryParams) const;

	//Assault Rifle
	float HipFireSpread = 3.0f;
	float FireRange = 100000.0f;
	float BulletVertKick = 0.5f;
	float BulletSideKick = 0.3f;
	float BulletForce = 1000.0f;

	//Flatten
	float m_FlattenRange = 10000.0f;
	float m_FlattenFireRate = 1.0f;
	float m_FlattenFireTimer = 0.0f;
};