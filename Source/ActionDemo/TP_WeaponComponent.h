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

	/** AnimMontage to play each time we fire */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Gameplay)
	UAnimMontage* FireAnimation;

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

	/** Make the weapon Fire an orange Portal */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void OrangePortalFire();

	/** Make the weapon Fire a blue Portal */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void BluePortalFire();

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

	//Assault Rifle
	bool isReloading = false;
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