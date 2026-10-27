// Copyright Epic Games, Inc. All Rights Reserved.


#include "TP_WeaponComponent.h"
#include "ActionDemoCharacter.h"
#include "ActionDemoProjectile.h"
#include "FlattenComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/DecalActor.h"
#include "Components/DecalComponent.h"
#include "Engine/TextureRenderTarget2D.h"

#include "DrawDebugHelpers.h"
#include "Portal.h"
#include "PortalTraceLibrary.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

#define POINT(x, c) DrawDebugPoint(GetWorld(), x, 10, c, !5.0f, 5.0f);

// Sets default values for this component's properties
UTP_WeaponComponent::UTP_WeaponComponent()
{
	// Default offset from the character location for projectiles to spawn
	MuzzleOffset = FVector(100.0f, 0.0f, 10.0f);
	PrimaryComponentTick.bCanEverTick = true;
	Ammo = MaxAmmo;
}

void UTP_WeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	m_Character = Cast<AActionDemoCharacter>(GetOwner());
	if (!m_Character && GetOwner())
	{
		m_Character = Cast<AActionDemoCharacter>(GetOwner()->GetOwner());
	}
	if (m_Character)
	{
		PlayerController = Cast<APlayerController>(m_Character->GetController());
		Ammo = MaxAmmo;
		m_Character->OnAmmoChanged.Broadcast(Ammo, MaxAmmo);
	}
}

void UTP_WeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (m_FlattenFireTimer > 0.0f)
	{
		m_FlattenFireTimer -= DeltaTime;
	}
	else
	{
		m_FlattenFireTimer = 0.0f;
	}
}

void UTP_WeaponComponent::BindInput(UEnhancedInputComponent* EnhancedInputComponent)
{
	if (!EnhancedInputComponent)
	{
		return;
	}

	if (!m_Character)
	{
		m_Character = Cast<AActionDemoCharacter>(GetOwner());
		if (!m_Character && GetOwner())
		{
			m_Character = Cast<AActionDemoCharacter>(GetOwner()->GetOwner());
		}
	}
	if (!m_Character)
	{
		return;
	}

	PlayerController = Cast<APlayerController>(m_Character->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(FireMappingContext, 1);
		Subsystem->AddMappingContext(PortalMappingContext, 0);
	}

	EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::Fire);
	UInputTriggerPulse* FirePulse = NewObject<UInputTriggerPulse>(this, TEXT("TriggerImpulse"));
	FirePulse->bTriggerOnStart = true;
	FirePulse->Interval = 0.1f;
	FireAction->Triggers.Add(FirePulse);

	EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::Aim);
	EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::Reload);
	EnhancedInputComponent->BindAction(BluePortalAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::PortalFireBlue);
	EnhancedInputComponent->BindAction(OrangePortalAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::PortalFireOrange);
	EnhancedInputComponent->BindAction(ToggleARAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::ToggleAR);
	EnhancedInputComponent->BindAction(TogglePortalAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::TogglePortal);
	EnhancedInputComponent->BindAction(EmptyAction, ETriggerEvent::Triggered, this, &UTP_WeaponComponent::Empty);
}

void UTP_WeaponComponent::Fire()
{
	if (m_Character == nullptr || PlayerController == nullptr || IsReloading() || m_Character->IsHolstered() || m_Character->IsCloseToWall() || m_Character->IsSprinting() || Ammo <= 0)
	{
		return;
	}

	--Ammo;
	m_Character->OnAmmoChanged.Broadcast(Ammo, MaxAmmo);

	PlayFireMontages();
	SpawnCasing();

	FVector Start = PlayerController->PlayerCameraManager->GetCameraLocation();
	FRotator Fwd = PlayerController->PlayerCameraManager->GetCameraRotation();

	if (m_Character->IsAiming())
	{
		m_Character->AddControllerYawInput(FMath::RandRange(-BulletSideKick, BulletSideKick));
		if (Fwd.Pitch < 45.0f)
		{
			m_Character->AddControllerPitchInput(-BulletVertKick);
		}
	}
	else
	{
		Fwd = (Fwd + FRotator(FMath::FRandRange(-HipFireSpread, HipFireSpread), FMath::FRandRange(-HipFireSpread, HipFireSpread), 0.0f));
	}

	const FVector End = Start + Fwd.Vector() * FireRange;
	if (m_Character->HasAuthority())
	{
		m_Character->ProcessWeaponFire(Start, End, BulletBP, BulletForce);
	}
	else
	{
		m_Character->ServerProcessWeaponFire(Start, End, BulletBP, BulletForce);
	}
}

void UTP_WeaponComponent::Aim(const FInputActionValue& Value)
{
	if (!m_Character)
	{
		return;
	}

	if (m_Character->IsHolstered() && m_Character->IsAiming())
	{
		m_Character->SetAiming(false);
		return;
	}

	m_Character->SetAiming(Value.Get<bool>());
}

void UTP_WeaponComponent::Reload(const FInputActionValue& Value)
{
	if (m_Character == nullptr || IsReloading() || m_Character->bHolstered || !WeaponReloadMontage || Ammo >= MaxAmmo)
	{
		return;
	}

	m_Character->TryInterruptEquipMontage();
	PlayReloadMontages();
}

USkeletalMeshComponent* UTP_WeaponComponent::GetAnimatedWeaponMesh() const
{
	// Infima: PlayMontage(InSkeletalMeshComponent = SK_AssaultRifle). That mesh owns AnimClass.
	if (const AActor* WeaponActor = GetOwner())
	{
		TArray<USkeletalMeshComponent*> Meshes;
		WeaponActor->GetComponents<USkeletalMeshComponent>(Meshes);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh != this && Mesh->GetSkeletalMeshAsset() && Mesh->GetAnimClass())
			{
				return Mesh;
			}
		}
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh != this && Mesh->GetSkeletalMeshAsset())
			{
				return Mesh;
			}
		}
	}

	return const_cast<UTP_WeaponComponent*>(this);
}

bool UTP_WeaponComponent::IsReloading() const
{
	if (!WeaponReloadMontage)
	{
		return false;
	}
	const USkeletalMeshComponent* Mesh = GetAnimatedWeaponMesh();
	const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	return AnimInstance && AnimInstance->Montage_IsPlaying(WeaponReloadMontage);
}

void UTP_WeaponComponent::RestockAmmo()
{
	StopReload();
	Ammo = MaxAmmo;
	if (m_Character)
	{
		m_Character->OnAmmoChanged.Broadcast(Ammo, MaxAmmo);
	}
}

void UTP_WeaponComponent::OnWeaponMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!WeaponReloadMontage || Montage != WeaponReloadMontage)
	{
		return;
	}

	if (m_Character)
	{
		m_Character->StopReloadMontage(0.0f);
	}

	if (bInterrupted)
	{
		return;
	}

	Ammo = MaxAmmo;
	m_Character->OnAmmoChanged.Broadcast(Ammo, MaxAmmo);
}

void UTP_WeaponComponent::PlayFireMontages()
{
	if (WeaponFireMontage)
	{
		if (USkeletalMeshComponent* Mesh = GetAnimatedWeaponMesh())
		{
			if (UAnimInstance* WeaponAnim = Mesh->GetAnimInstance())
			{
				WeaponAnim->Montage_Play(WeaponFireMontage);
			}
		}
	}
	if (m_Character)
	{
		m_Character->PlayFireMontage();
	}
}

void UTP_WeaponComponent::SpawnCasing()
{
	USkeletalMeshComponent* Mesh = GetAnimatedWeaponMesh();
	if (!CasingBP || !Mesh || !Mesh->DoesSocketExist(EjectionSocketName) || !GetWorld())
	{
		return;
	}

	const FTransform SpawnTM = Mesh->GetSocketTransform(EjectionSocketName, RTS_World);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = m_Character;
	SpawnParams.Instigator = m_Character;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Casing = GetWorld()->SpawnActor<AActor>(CasingBP, SpawnTM, SpawnParams);
	if (!Casing || !m_Character)
	{
		return;
	}

	// Casings colliding with the shooter look wrong (same as Infima BP).
	TArray<UPrimitiveComponent*> Primitives;
	Casing->GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Prim : Primitives)
	{
		Prim->IgnoreActorWhenMoving(m_Character, true);
	}
}

void UTP_WeaponComponent::PlayReloadMontages()
{
	if (WeaponReloadMontage)
	{
		if (USkeletalMeshComponent* Mesh = GetAnimatedWeaponMesh())
		{
			if (UAnimInstance* WeaponAnim = Mesh->GetAnimInstance())
			{
				const float Duration = WeaponAnim->Montage_Play(WeaponReloadMontage);
				if (Duration > 0.f)
				{
					FOnMontageEnded EndDelegate;
					EndDelegate.BindUObject(this, &UTP_WeaponComponent::OnWeaponMontageEnded);
					WeaponAnim->Montage_SetEndDelegate(EndDelegate, WeaponReloadMontage);
				}
			}
		}
	}
	if (m_Character)
	{
		m_Character->PlayReloadMontage();
	}
}

void UTP_WeaponComponent::StopReload(float BlendOutTime)
{
	if (WeaponReloadMontage)
	{
		if (USkeletalMeshComponent* Mesh = GetAnimatedWeaponMesh())
		{
			if (UAnimInstance* WeaponAnim = Mesh->GetAnimInstance())
			{
				if (WeaponAnim->Montage_IsPlaying(WeaponReloadMontage))
				{
					WeaponAnim->Montage_Stop(BlendOutTime, WeaponReloadMontage);
				}
			}
		}
	}
	if (m_Character)
	{
		m_Character->StopReloadMontage(BlendOutTime);
	}
}

void UTP_WeaponComponent::StopFireMontages(float BlendOutTime)
{
	if (WeaponFireMontage)
	{
		if (USkeletalMeshComponent* Mesh = GetAnimatedWeaponMesh())
		{
			if (UAnimInstance* WeaponAnim = Mesh->GetAnimInstance())
			{
				if (WeaponAnim->Montage_IsPlaying(WeaponFireMontage))
				{
					WeaponAnim->Montage_Stop(BlendOutTime, WeaponFireMontage);
				}
			}
		}
	}
	if (m_Character)
	{
		m_Character->StopFireMontage(BlendOutTime);
	}
}

void UTP_WeaponComponent::PortalFireBlue()
{
	PortalFire(true);
}

void UTP_WeaponComponent::PortalFireOrange()
{
	PortalFire(false);
}

void UTP_WeaponComponent::PortalFire(bool bIsBlue)
{
	if (m_Character == nullptr || PlayerController == nullptr || m_Character->IsHolstered() || m_Character->IsCloseToWall() || m_Character->IsSprinting())
	{
		return;
	}

	if (FireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, m_Character->GetActorLocation());
	}

	PlayFireMontages();

	FVector PortalCentre;
	FQuat PortalRotation;
	AActor* TargetSurface = nullptr;
	if (!CheckValidLoc(PortalCentre, PortalRotation, TargetSurface, bIsBlue) || !PortalBP)
	{
		return;
	}

	const FRotator PortalRotator = PortalRotation.Rotator();
	if (m_Character->HasAuthority())
	{
		m_Character->PlacePortal(bIsBlue, PortalCentre, PortalRotator, PortalBP);
	}
	else
	{
		m_Character->ServerPlacePortal(bIsBlue, PortalCentre, PortalRotator, PortalBP);
	}
}


void UTP_WeaponComponent::ToggleAR()
{
	if (PlayerController)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			// Set the priority of the mapping to 1, so that it overrides the Jump action with the Fire action when using touch input
			UE_LOG(LogTemp, Warning, TEXT("TOGGLING TO 1"));
			Subsystem->AddMappingContext(FireMappingContext, 1);
			Subsystem->AddMappingContext(PortalMappingContext, 0);
		}
	}
}

void UTP_WeaponComponent::TogglePortal()
{
	if (PlayerController)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			// Set the priority of the mapping to 1, so that it overrides the Jump action with the Fire action when using touch input
			UE_LOG(LogTemp, Warning, TEXT("TOGGLING TO 2"));
			Subsystem->AddMappingContext(FireMappingContext, 0);
			Subsystem->AddMappingContext(PortalMappingContext, 1);
		}
	}
}

bool UTP_WeaponComponent::IsOtherColourPortal(const AActor* Actor, bool bIsBlue) const
{
	const APortal* HitPortal = Cast<APortal>(Actor);
	if (!HitPortal || !m_Character)
	{
		return false;
	}
	const APortal* SameColour = bIsBlue ? m_Character->BluePortal : m_Character->OrangePortal;
	return HitPortal != SameColour;
}

bool UTP_WeaponComponent::CheckValidLoc(FVector& PortalCentre, FQuat& PortalRotation, AActor*& TargetSurface, bool isBlue)
{
	FCollisionQueryParams QueryParams = m_Character->GetIgnorePortalParams(isBlue);
	FVector Start = PlayerController->PlayerCameraManager->GetCameraLocation();
	FVector Fwd = PlayerController->PlayerCameraManager->GetCameraRotation().Vector();

	FHitResult PortalHit;
	GetWorld()->LineTraceSingleByChannel(PortalHit, Start, Start + Fwd * Portal_Range, ECC_Visibility, QueryParams);

	//There is nothing in range
	if (!PortalHit.IsValidBlockingHit() || PortalHit.GetActor()->IsA<APortal>())
	{
		return false;
	}
	TargetSurface = PortalHit.GetActor();

	//The target is not a valid surface
	//if (PortalHit.GetActor()->ActorHasTag("CanNotPortal"))
	//{
	//	return false;
	//}

	PortalCentre = PortalHit.ImpactPoint;
	FVector PortalSurface = PortalHit.ImpactNormal;

	// Player-space basis (actor up = gravity up)
	const FVector PlayerUp = m_Character->GetActorUpVector().GetSafeNormal();
	FVector PlayerFwd = FVector::VectorPlaneProject(Fwd, PlayerUp).GetSafeNormal();
	if (PlayerFwd.IsNearlyZero())
	{
		PlayerFwd = FVector::VectorPlaneProject(m_Character->GetActorForwardVector(), PlayerUp).GetSafeNormal();
	}

	// Wall (no player-up component) is the special case: stand straight, yaw from surface.
	// Anything with a vertical component: face ±player up, yaw/twist from look (+180).
	const FVector ImpactNormal = PortalHit.ImpactNormal.GetSafeNormal();
	const float UpDot = ImpactNormal | PlayerUp;
	const bool bWall = FMath::IsNearlyZero(UpDot, 1.e-3f);

	FVector PortalNormal;
	FVector PortalUp;
	if (bWall)
	{
		PortalNormal = FVector::VectorPlaneProject(ImpactNormal, PlayerUp).GetSafeNormal();
		if (PortalNormal.IsNearlyZero())
		{
			PortalNormal = ImpactNormal;
		}
		PortalUp = PlayerUp;
	}
	else
	{
		PortalNormal = (UpDot >= 0.f ? 1.f : -1.f) * PlayerUp;
		PortalUp = PlayerFwd;
	}

	PortalRotation = FRotationMatrix::MakeFromXZ(PortalNormal, PortalUp).ToQuat();

	FVector SurfaceRight = FVector::VectorPlaneProject(PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(FVector::RightVector), PortalSurface).GetSafeNormal();

	FVector SurfaceUp = PortalRotation.RotateVector(FVector::UpVector).GetSafeNormal();

	const float HalfWidth = Portal_Width * 0.5f;
	const float HalfHeight = Portal_Height * 0.5f;
	const float EdgePad = 10.0f;

	FHitResult LeftFaceHit;
	FHitResult RightFaceHit;
	TracePortalFaceHit(LeftFaceHit, PortalCentre, PortalSurface, -SurfaceRight * Portal_Width, QueryParams);
	TracePortalFaceHit(RightFaceHit, PortalCentre, PortalSurface, SurfaceRight * Portal_Width, QueryParams);
	if (LeftFaceHit.bBlockingHit && RightFaceHit.bBlockingHit
		&& FVector::Dist(LeftFaceHit.ImpactPoint, RightFaceHit.ImpactPoint) < Portal_Width)
	{
		return false;
	}

	FHitResult TopFaceHit;
	FHitResult BottomFaceHit;
	TracePortalFaceHit(TopFaceHit, PortalCentre, PortalSurface, SurfaceUp * Portal_Height, QueryParams);
	TracePortalFaceHit(BottomFaceHit, PortalCentre, PortalSurface, -SurfaceUp * Portal_Height, QueryParams);
	if (TopFaceHit.bBlockingHit && BottomFaceHit.bBlockingHit
		&& FVector::Dist(TopFaceHit.ImpactPoint, BottomFaceHit.ImpactPoint) < Portal_Height)
	{
		return false;
	}

	if (!TryAdjustPortalEdge(PortalCentre, PortalSurface, SurfaceRight * (HalfWidth + EdgePad),
			FVector::LeftVector, RightFaceHit, HalfWidth, isBlue, QueryParams)
		|| !TryAdjustPortalEdge(PortalCentre, PortalSurface, -SurfaceRight * (HalfWidth + EdgePad),
			FVector::RightVector, LeftFaceHit, HalfWidth, isBlue, QueryParams)
		|| !TryAdjustPortalEdge(PortalCentre, PortalSurface, SurfaceUp * (HalfHeight + EdgePad),
			FVector::DownVector, TopFaceHit, HalfHeight, isBlue, QueryParams)
		|| !TryAdjustPortalEdge(PortalCentre, PortalSurface, -SurfaceUp * (HalfHeight + EdgePad),
			FVector::UpVector, BottomFaceHit, HalfHeight, isBlue, QueryParams))
	{
		return false;
	}

	const FVector CornerR = SurfaceRight * (HalfWidth + EdgePad);
	const FVector CornerU = SurfaceUp * (HalfHeight + EdgePad);
	if (IsPortalCornerBlocked(PortalCentre, PortalSurface, CornerU + CornerR, isBlue, QueryParams)
		|| IsPortalCornerBlocked(PortalCentre, PortalSurface, -CornerU + CornerR, isBlue, QueryParams)
		|| IsPortalCornerBlocked(PortalCentre, PortalSurface, CornerU - CornerR, isBlue, QueryParams)
		|| IsPortalCornerBlocked(PortalCentre, PortalSurface, -CornerU - CornerR, isBlue, QueryParams))
	{
		return false;
	}

	// Impact point is coplanar with the wall; nudge into free space so the portal mesh doesn't Z-fight.
	PortalCentre += PortalNormal * Portal_SurfaceOffset;

	return true;
}

void UTP_WeaponComponent::TracePortalFaceHit(FHitResult& OutHit, const FVector& PortalCentre,
	const FVector& PortalSurface, const FVector& LateralOffset, const FCollisionQueryParams& QueryParams) const
{
	OutHit = FHitResult();
	const FVector TraceStart = PortalCentre - PortalSurface * Portal_EdgeCheckDelta;
	const FVector TraceEnd = TraceStart + LateralOffset;
	GetWorld()->LineTraceSingleByChannel(OutHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
}

bool UTP_WeaponComponent::TryAdjustPortalEdge(FVector& PortalCentre, const FVector& PortalSurface,
	const FVector& EdgeOffset, const FVector& NudgeCamLocalDir, const FHitResult& FaceHit, float HalfExtent,
	bool bIsBlue, const FCollisionQueryParams& QueryParams) const
{
	const FVector EdgePos = PortalCentre + EdgeOffset;
	FHitResult EdgeHit;
	GetWorld()->LineTraceSingleByChannel(EdgeHit,
		EdgePos + PortalSurface * Portal_EdgeCheckDelta,
		EdgePos - PortalSurface * Portal_EdgeCheckDelta,
		ECC_Visibility, QueryParams);

	if (!EdgeHit.IsValidBlockingHit())
	{
		const FVector NudgeDir = FVector::VectorPlaneProject(
			PlayerController->PlayerCameraManager->GetCameraRotation().RotateVector(NudgeCamLocalDir),
			PortalSurface).GetSafeNormal();
		PortalCentre += NudgeDir * (FaceHit.Distance - HalfExtent);
		return true;
	}

	return !IsOtherColourPortal(EdgeHit.GetActor(), bIsBlue);
}

bool UTP_WeaponComponent::IsPortalCornerBlocked(const FVector& PortalCentre, const FVector& PortalSurface,
	const FVector& CornerOffset, bool bIsBlue, const FCollisionQueryParams& QueryParams) const
{
	const FVector CornerPos = PortalCentre + CornerOffset;
	FHitResult EdgeHit;
	GetWorld()->LineTraceSingleByChannel(EdgeHit,
		CornerPos + PortalSurface * Portal_EdgeCheckDelta,
		CornerPos - PortalSurface * Portal_EdgeCheckDelta,
		ECC_Visibility, QueryParams);
	return EdgeHit.IsValidBlockingHit() && IsOtherColourPortal(EdgeHit.GetActor(), bIsBlue);
}

void UTP_WeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PlayerController)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->RemoveMappingContext(FireMappingContext);
			Subsystem->RemoveMappingContext(PortalMappingContext);
		}
	}
}