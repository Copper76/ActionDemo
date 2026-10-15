// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Portal.generated.h"

class AActionDemoCharacter;

UCLASS(ClassGroup = (Custom), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class ACTIONDEMO_API APortal : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APortal();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnOverlapEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void VDestroy(AActor* ToDestroy);

	void HandOffTeleportingObject(AActor* Original);

	void UpdateCloneConstraint(AActor* Original, AActor* Clone);

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Default, meta = (AllowPrivateAccess = "true"))
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USceneCaptureComponent2D* CaptureComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Collider, meta = (AllowPrivateAccess = "true"))
	UBoxComponent* ReverseCollider;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Plane, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* Plane;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Material, meta = (AllowPrivateAccess = "true"))
	UMaterialInstance* OrangeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Material, meta = (AllowPrivateAccess = "true"))
	UTextureRenderTarget2D* OrangeTextureTarget;

	void Teleport(AActionDemoCharacter* Character);

	void SetActorClip(AActor* Actor, const FLinearColor Origin, const FLinearColor Normal, const bool bEnable);

private:
	FVector CameraOffset;
	FQuat CameraRotation;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	void SetUp(AActor* TargetSurface, const bool bIsBlue = true,  APortal* OtherPortal = nullptr);

	void Link(APortal* Portal);

	// Rotation that takes this portal's space onto the linked portal. False when there is no linked portal.
	bool GetTravelQuat(FQuat& OutTravelQ) const;

	// Maps a world point through this portal onto the linked portal.
	bool TranslatePoint(const FVector& WorldPoint, FVector& OutPoint) const;

	// Maps a direction through this portal onto the linked portal.
	bool TranslateDirection(const FVector& WorldDirection, FVector& OutDirection) const;

	// True on the wall side of the portal, including contacts sitting on the surface.
	bool IsPointBehindPlane(const FVector& Point) const;

	FVector GetPlaneOrigin() const
	{
		return Plane ? Plane->GetComponentLocation() : GetActorLocation();
	}

public:
	AActionDemoCharacter* LocalPlayer;

	APlayerController* PlayerController;//set it on spawn

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Default, meta = (AllowPrivateAccess = "true"))
	APortal* OtherPortal; //set it on spawn if there is another

	TMap<AActor*, AActor*> TeleportingObjects = TMap<AActor*, AActor*>();

	TMap<AActionDemoCharacter*, AActor*> TeleportingCharacters = TMap<AActionDemoCharacter*, AActor*>();
};
