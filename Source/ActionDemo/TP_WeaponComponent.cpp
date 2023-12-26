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

	FVector PortalCentre;
	FRotator PortalRotation;
	UPrimitiveComponent* TargetSurface = nullptr;
	if (!CheckValidLoc(PortalCentre, PortalRotation, TargetSurface, true))
	{
		return;
	}
	if (Character->BluePortal == NULL)
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character->BluePortal = GetWorld()->SpawnActor<APortal>(BluePortalBP, PortalCentre, PortalRotation, SpawnInfo);
		if (Character->OrangePortal == nullptr)
		{
			Character->BluePortal->SetUp(Character, TargetSurface);
		}
		else
		{
			Character->BluePortal->SetUp(Character, TargetSurface, Character->OrangePortal);
		}
	}
	else
	{
		Character->BluePortal->GetRootComponent()->SetWorldLocation(PortalCentre);
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

	FRotator PortalRotation;
	FVector PortalCentre;
	UPrimitiveComponent* TargetSurface = NULL;
	if (!CheckValidLoc(PortalCentre, PortalRotation, TargetSurface, false))
	{
		return;
	}
	if (TargetSurface == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("DIDN'T REGISTER"));
	}
	if (Character->OrangePortal == nullptr)
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character->OrangePortal = GetWorld()->SpawnActor<APortal>(OrangePortalBP, PortalCentre, PortalRotation, SpawnInfo);
		if (Character->BluePortal == nullptr)
		{
			Character->OrangePortal->SetUp(Character, TargetSurface);
		}
		else
		{
			Character->OrangePortal->SetUp(Character, TargetSurface, Character->BluePortal);
		}
	}
	else
	{
		Character->OrangePortal->GetRootComponent()->SetWorldLocation(PortalCentre);
		Character->OrangePortal->GetRootComponent()->SetWorldRotation(PortalRotation);
	}
}

bool UTP_WeaponComponent::CheckValidLoc(FVector& PortalCentre, FRotator& PortalRotation, UPrimitiveComponent*& TargetSurface, bool isBlue)
{
	FCollisionQueryParams QueryParams = Character->GetIgnorePortalParams(isBlue);
	APlayerController* PlayerController = Cast<APlayerController>(Character->GetController());
	FVector Start = PlayerController->PlayerCameraManager->GetCameraLocation();
	FVector Fwd = PlayerController->PlayerCameraManager->GetCameraRotation().Vector();
	float HorizontalRotation = PlayerController->PlayerCameraManager->GetCameraRotation().Yaw;

	FHitResult PortalHit;
	GetWorld()->LineTraceSingleByChannel(PortalHit, Start, Start + Fwd * Portal_Range, ECC_Visibility, QueryParams);

	//There is nothing in range
	if (!PortalHit.IsValidBlockingHit() || PortalHit.GetActor()->IsA<APortal>())
	{
		return false;
	}
	TargetSurface = PortalHit.GetComponent();
	UE_LOG(LogTemp, Warning, TEXT("TARGET ACTOR: %s"), *PortalHit.GetComponent()->GetName());

	//The target is not a valid surface
	//if (PortalHit.GetActor()->ActorHasTag("CanNotPortal"))
	//{
	//	return false;
	//}

	PortalCentre = PortalHit.ImpactPoint;
	FVector PortalSurface = PortalHit.ImpactNormal;

	//Calculate the correct rotation now that the checks are passed
	float VertComponent = FMath::Abs(FMath::RoundHalfToZero(1000.0f * PortalHit.ImpactNormal.Rotation().Pitch / 90.0f) / 1000.0f);
	PortalRotation = FRotator(PortalHit.ImpactNormal.Rotation().Pitch, VertComponent == 1.0f? HorizontalRotation + 180.0f : PortalHit.ImpactNormal.Rotation().Yaw, 0.0f);

	FVector SurfaceRight = FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::RightVector), PortalSurface).GetSafeNormal();

	FVector SurfaceUp = PortalRotation.RotateVector(FVector::UpVector).GetSafeNormal();

	AActor* TargetActor = PortalHit.GetActor();

	TArray<FHitResult> FaceHitResults;

	//check if enough horizontal space
	FHitResult LeftFaceHit;
	FVector LeftFace = PortalCentre - PortalSurface * Portal_EdgeCheckDelta - SurfaceRight * Portal_Width;
	GetWorld()->LineTraceMultiByProfile(FaceHitResults, LeftFace, PortalCentre - PortalSurface * Portal_EdgeCheckDelta, "OverlapAll", QueryParams);

	if (!FaceHitResults.IsEmpty())
	{
		LeftFaceHit = FaceHitResults.Last();
		LeftFaceHit.bBlockingHit = true;
	}
	FaceHitResults.Empty();

	FHitResult RightFaceHit;
	FVector RightFace = PortalCentre - PortalSurface * Portal_EdgeCheckDelta + SurfaceRight * Portal_Width;
	GetWorld()->LineTraceMultiByProfile(FaceHitResults, RightFace, PortalCentre - PortalSurface * Portal_EdgeCheckDelta, "OverlapAll", QueryParams);
	if (!FaceHitResults.IsEmpty())
	{
		RightFaceHit = FaceHitResults.Last();
		RightFaceHit.bBlockingHit = true;
	}
	FaceHitResults.Empty();

	if (LeftFaceHit.bBlockingHit && RightFaceHit.bBlockingHit && FVector::Dist(LeftFaceHit.ImpactPoint, RightFaceHit.ImpactPoint) < Portal_Width)
	{
		return false;
	}

	//check if enough vertical space
	FHitResult TopFaceHit;
	FVector TopFace = PortalCentre - PortalSurface * Portal_EdgeCheckDelta + SurfaceUp * Portal_Height;
	GetWorld()->LineTraceMultiByProfile(FaceHitResults, TopFace, PortalCentre - PortalSurface * Portal_EdgeCheckDelta, "OverlapAll", QueryParams);
	if (!FaceHitResults.IsEmpty())
	{
		TopFaceHit = FaceHitResults.Last();
		TopFaceHit.bBlockingHit = true;
	}
	FaceHitResults.Empty();

	FHitResult BottomFaceHit;
	FVector BottomFace = PortalCentre - PortalSurface * Portal_EdgeCheckDelta - SurfaceUp * Portal_Height;
	GetWorld()->LineTraceMultiByProfile(FaceHitResults, BottomFace, PortalCentre - PortalSurface * Portal_EdgeCheckDelta, "OverlapAll", QueryParams);
	if (!FaceHitResults.IsEmpty())
	{
		BottomFaceHit = FaceHitResults.Last();
		BottomFaceHit.bBlockingHit = true;
	}
	FaceHitResults.Empty();

	if (TopFaceHit.bBlockingHit && BottomFaceHit.bBlockingHit && FVector::Dist(TopFaceHit.ImpactPoint, BottomFaceHit.ImpactPoint) < Portal_Height)
	{
		return false;
	}

	FHitResult EdgeHit;

	FVector RightEdge = PortalCentre + SurfaceRight * (Portal_Width / 2 + 10.0f);
	GetWorld()->LineTraceSingleByChannel(EdgeHit, RightEdge + PortalSurface * Portal_EdgeCheckDelta, RightEdge - PortalSurface * Portal_EdgeCheckDelta, ECC_Visibility, QueryParams);
	if (!EdgeHit.IsValidBlockingHit())
	{
		PortalCentre += FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::LeftVector), PortalSurface).GetSafeNormal() * (RightFaceHit.Distance - Portal_Width / 2);
	}
	else
	{
		if (EdgeHit.GetActor()->IsA<APortal>())
		{
			return false;
		}
	}

	FVector LeftEdge = PortalCentre - SurfaceRight * (Portal_Width / 2 + 10.0f);
	GetWorld()->LineTraceSingleByChannel(EdgeHit, LeftEdge + PortalSurface * Portal_EdgeCheckDelta, LeftEdge - PortalSurface * Portal_EdgeCheckDelta, ECC_Visibility, QueryParams);
	if (!EdgeHit.IsValidBlockingHit())
	{
		PortalCentre += FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::RightVector), PortalSurface).GetSafeNormal() * (LeftFaceHit.Distance - Portal_Width / 2);
	}
	else
	{
		if (EdgeHit.GetActor()->IsA<APortal>())
		{
			return false;
		}
	}

	FVector UpEdge = PortalCentre + SurfaceUp * (Portal_Height / 2 + 10.0f);
	GetWorld()->LineTraceSingleByChannel(EdgeHit, UpEdge + PortalSurface * Portal_EdgeCheckDelta, UpEdge - PortalSurface * Portal_EdgeCheckDelta, ECC_Visibility, QueryParams);
	if (!EdgeHit.IsValidBlockingHit())
	{
		PortalCentre += FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::DownVector), PortalSurface).GetSafeNormal() * (TopFaceHit.Distance - Portal_Height / 2);
	}
	else
	{
		if (EdgeHit.GetActor()->IsA<APortal>())
		{
			return false;
		}
	}

	FVector DownEdge = PortalCentre - SurfaceUp * (Portal_Height / 2.0f + 10.0f);
	GetWorld()->LineTraceSingleByChannel(EdgeHit, DownEdge + PortalSurface * Portal_EdgeCheckDelta, DownEdge - PortalSurface * Portal_EdgeCheckDelta, ECC_Visibility, QueryParams);
	if (!EdgeHit.IsValidBlockingHit())
	{
		PortalCentre += FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::UpVector), PortalSurface).GetSafeNormal() * (BottomFaceHit.Distance - Portal_Height / 2);
	}
	else
	{
		if (EdgeHit.GetActor()->IsA<APortal>())
		{
			return false;
		}
	}

	FVector TopRightCorner = PortalCentre + SurfaceUp * (Portal_Height / 2 + 10.0f) + SurfaceRight * (Portal_Width/2 + 10.0f);
	GetWorld()->LineTraceSingleByChannel(EdgeHit, TopRightCorner + PortalSurface * Portal_EdgeCheckDelta, TopRightCorner - PortalSurface * Portal_EdgeCheckDelta, ECC_Visibility, QueryParams);
	if (EdgeHit.IsValidBlockingHit() && EdgeHit.GetActor()->IsA<APortal>())
	{
		return false;
	}

	FVector BottomRightCorner = PortalCentre - SurfaceUp * (Portal_Height / 2 + 10.0f) + SurfaceRight * (Portal_Width / 2 + 10.0f);
	GetWorld()->LineTraceSingleByChannel(EdgeHit, BottomRightCorner + PortalSurface * Portal_EdgeCheckDelta, BottomRightCorner - PortalSurface * Portal_EdgeCheckDelta, ECC_Visibility, QueryParams);
	if (EdgeHit.IsValidBlockingHit() && EdgeHit.GetActor()->IsA<APortal>())
	{
		return false;
	}

	FVector TopLeftCorner = PortalCentre + SurfaceUp * (Portal_Height / 2 + 10.0f) - SurfaceRight * (Portal_Width / 2 + 10.0f);
	GetWorld()->LineTraceSingleByChannel(EdgeHit, TopLeftCorner + PortalSurface * Portal_EdgeCheckDelta, TopLeftCorner - PortalSurface * Portal_EdgeCheckDelta, ECC_Visibility, QueryParams);
	if (EdgeHit.IsValidBlockingHit() && EdgeHit.GetActor()->IsA<APortal>())
	{
		return false;
	}

	FVector BottomLeftCorner = PortalCentre - SurfaceUp * (Portal_Height / 2 + 10.0f) - SurfaceRight * (Portal_Width / 2 + 10.0f);
	GetWorld()->LineTraceSingleByChannel(EdgeHit, BottomLeftCorner + PortalSurface * Portal_EdgeCheckDelta, BottomLeftCorner - PortalSurface * Portal_EdgeCheckDelta, ECC_Visibility, QueryParams);
	if (EdgeHit.IsValidBlockingHit() && EdgeHit.GetActor()->IsA<APortal>())
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