// Copyright Epic Games, Inc. All Rights Reserved.


#include "TP_WeaponComponent.h"
#include "ActionDemoCharacter.h"
#include "ActionDemoProjectile.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

#define POINT(x, c) DrawDebugPoint(GetWorld(), x, 10, c, !5.0f, 5.0f);

// Sets default values for this component's properties
UTP_WeaponComponent::UTP_WeaponComponent()
{
	// Default offset from the character location for projectiles to spawn
	MuzzleOffset = FVector(100.0f, 0.0f, 10.0f);
}


void UTP_WeaponComponent::Fire()
{
	if (Character == nullptr || Character->GetController() == nullptr)
	{
		return;
	}

	// Try and fire a projectile
	if (ProjectileClass != nullptr)
	{
		UWorld* const World = GetWorld();
		if (World != nullptr)
		{
			APlayerController* PlayerController = Cast<APlayerController>(Character->GetController());
			const FRotator SpawnRotation = PlayerController->PlayerCameraManager->GetCameraRotation();
			// MuzzleOffset is in camera space, so transform it to world space before offsetting from the character location to find the final muzzle position
			const FVector SpawnLocation = GetOwner()->GetActorLocation() + SpawnRotation.RotateVector(MuzzleOffset);
	
			//Set Spawn Collision Handling Override
			FActorSpawnParameters ActorSpawnParams;
			ActorSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	
			// Spawn the projectile at the muzzle
			World->SpawnActor<AActionDemoProjectile>(ProjectileClass, SpawnLocation, SpawnRotation, ActorSpawnParams);
		}
	}
}

void UTP_WeaponComponent::AttachWeapon(AActionDemoCharacter* TargetCharacter)
{
	Character = TargetCharacter;
	if (Character == nullptr)
	{
		return;
	}

	// Attach the weapon to the First Person Character
	FAttachmentTransformRules AttachmentRules(EAttachmentRule::SnapToTarget, true);
	AttachToComponent(Character->GetMesh1P(), AttachmentRules, FName(TEXT("GripPoint")));
	
	// switch bHasRifle so the animation blueprint can switch to another animation set
	Character->SetHasRifle(true, GetOwner());

	// Set up action bindings
	if (APlayerController* PlayerController = Cast<APlayerController>(Character->GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			// Set the priority of the mapping to 1, so that it overrides the Jump action with the Fire action when using touch input
			Subsystem->AddMappingContext(FireMappingContext, 1);
		}

		if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerController->InputComponent))
		{
			// Fire
			//EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::Fire);

			// Fire Blue Portal
			EnhancedInputComponent->BindAction(BluePortalAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::BluePortalFire);

			// Fire Orange Portal
			EnhancedInputComponent->BindAction(OrangePortalAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::OrangePortalFire);
		}
	}
}

void UTP_WeaponComponent::BluePortalFire()
{
	if (Character == nullptr || Character->GetController() == nullptr)
	{
		return;
	}

	// Try and play the sound if specified
	if (FireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Character->GetActorLocation());
	}

	// Try and play a firing animation if specified
	if (FireAnimation != nullptr)
	{
		// Get the animation object for the arms mesh
		UAnimInstance* AnimInstance = Character->GetMesh1P()->GetAnimInstance();
		if (AnimInstance != nullptr)
		{
			AnimInstance->Montage_Play(FireAnimation, 1.f);
		}
	}

	FHitResult PortalHit;
	FRotator PortalRotation;
	if (!CheckValidLoc(PortalHit, PortalRotation))
	{
		return;
	}
	if (Character->BluePortal == nullptr)
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FRotator NewPortalRotation = PortalHit.ImpactNormal.Rotation();
		Character->BluePortal = GetWorld()->SpawnActor<APortal>(BluePortalBP, PortalHit.ImpactPoint, PortalRotation, SpawnInfo);
		if (Character->OrangePortal == nullptr)
		{
			Character->BluePortal->SetUp(Character);
		}
		else
		{
			Character->BluePortal->SetUp(Character, Character->OrangePortal);
		}
	}
	else
	{
		FRotator NewPortalRotation = PortalHit.ImpactNormal.Rotation();
		Character->BluePortal->GetRootComponent()->SetWorldLocation(PortalHit.ImpactPoint);
		Character->BluePortal->GetRootComponent()->SetWorldRotation(PortalRotation);
	}
}

void UTP_WeaponComponent::OrangePortalFire()
{
	if (Character == nullptr || Character->GetController() == nullptr)
	{
		return;
	}

	// Try and play the sound if specified
	if (FireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Character->GetActorLocation());
	}

	// Try and play a firing animation if specified
	if (FireAnimation != nullptr)
	{
		// Get the animation object for the arms mesh
		UAnimInstance* AnimInstance = Character->GetMesh1P()->GetAnimInstance();
		if (AnimInstance != nullptr)
		{
			AnimInstance->Montage_Play(FireAnimation, 1.f);
		}
	}

	FHitResult PortalHit;
	FRotator PortalRotation;
	if (!CheckValidLoc(PortalHit, PortalRotation))
	{
		return;
	}
	if (Character->OrangePortal == nullptr)
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character->OrangePortal = GetWorld()->SpawnActor<APortal>(OrangePortalBP, PortalHit.ImpactPoint, PortalRotation, SpawnInfo);
		if (Character->BluePortal == nullptr)
		{
			Character->OrangePortal->SetUp(Character);
		}
		else
		{
			Character->OrangePortal->SetUp(Character, Character->BluePortal);
		}
	}
	else
	{
		Character->OrangePortal->GetRootComponent()->SetWorldLocation(PortalHit.ImpactPoint);
		Character->OrangePortal->GetRootComponent()->SetWorldRotation(PortalRotation);
	}
}

bool UTP_WeaponComponent::CheckValidLoc(FHitResult& PortalHit, FRotator& PortalRotation)
{
	APlayerController* PlayerController = Cast<APlayerController>(Character->GetController());
	FVector Start = PlayerController->PlayerCameraManager->GetCameraLocation();
	FVector Fwd = PlayerController->PlayerCameraManager->GetCameraRotation().Vector();
	float HorizontalRotation = PlayerController->PlayerCameraManager->GetCameraRotation().Yaw;

	GetWorld()->LineTraceSingleByProfile(PortalHit, Start, Start + Fwd * Portal_Range, "BlockAll", Character->GetIgnoreCharacterParams());

	//There is nothing in range
	if (!PortalHit.IsValidBlockingHit())
	{
		return false;
	}

	float VertComponent = PortalHit.ImpactNormal.Rotation().Pitch / 90.0f;
	VertComponent = FMath::Abs(FMath::RoundHalfToZero(1000.0f * VertComponent) / 1000.0f);

	if (VertComponent == 1.0f)
	{
		PortalRotation = FRotator(PortalHit.ImpactNormal.Rotation().Pitch, HorizontalRotation, 0);
	}
	else
	{
		PortalRotation = FRotator(PortalHit.ImpactNormal.Rotation().Pitch, PortalHit.ImpactNormal.Rotation().Yaw, 0.0f);
	}
	

	//The target is not a valid surface
	//if (!PortalHit.GetActor()->ActorHasTag("CanPortal"))
	//{
	//	return false;
	//}

	FVector PortalCentre = PortalHit.ImpactPoint;
	FVector PortalSurface = PortalHit.ImpactNormal;

	FHitResult EdgeHit;

	FVector RightEdge = PortalCentre + FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::RightVector), PortalSurface).GetSafeNormal() * Portal_Width / 2;
	GetWorld()->LineTraceSingleByProfile(EdgeHit, RightEdge + PortalSurface * Portal_EdgeCheckDelta, RightEdge - PortalSurface * Portal_EdgeCheckDelta, "BlockAll", Character->GetIgnoreCharacterParams());
	if (!EdgeHit.IsValidBlockingHit())
	{
		return false;
	}

	FVector LeftEdge = PortalCentre + FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::LeftVector), PortalSurface).GetSafeNormal() * Portal_Width / 2;
	GetWorld()->LineTraceSingleByProfile(EdgeHit, LeftEdge + PortalSurface * Portal_EdgeCheckDelta, LeftEdge - PortalSurface * Portal_EdgeCheckDelta, "BlockAll", Character->GetIgnoreCharacterParams());
	if (!EdgeHit.IsValidBlockingHit())
	{
		return false;
	}

	FVector UpEdge = PortalCentre + FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::UpVector), PortalSurface).GetSafeNormal() * Portal_Height / 2;
	GetWorld()->LineTraceSingleByProfile(EdgeHit, UpEdge + PortalSurface * Portal_EdgeCheckDelta, UpEdge - PortalSurface * Portal_EdgeCheckDelta, "BlockAll", Character->GetIgnoreCharacterParams());
	if (!EdgeHit.IsValidBlockingHit())
	{
		return false;
	}

	FVector DownEdge = PortalCentre + FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::DownVector), PortalSurface).GetSafeNormal() * Portal_Height / 2;
	GetWorld()->LineTraceSingleByProfile(EdgeHit, DownEdge + PortalSurface * Portal_EdgeCheckDelta, DownEdge - PortalSurface * Portal_EdgeCheckDelta, "BlockAll", Character->GetIgnoreCharacterParams());
	if (!EdgeHit.IsValidBlockingHit())
	{
		return false;
	}
	return true;
}

void UTP_WeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Character == nullptr)
	{
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(Character->GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->RemoveMappingContext(FireMappingContext);
		}
	}
}