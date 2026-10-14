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

	UFUNCTION()
	void OnTeleportEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void VDestroy(AActor* ToDestroy);

	bool HasCrossedPortal(AActor* Actor) const;

	void HandOffTeleportingObject(AActor* Original);

	void UpdateCloneConstraint(AActor* Original, AActor* Clone);

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Default, meta = (AllowPrivateAccess = "true"))
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USceneCaptureComponent2D* CaptureComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Collider, meta = (AllowPrivateAccess = "true"))
	UBoxComponent* BoxCollider;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Collider, meta = (AllowPrivateAccess = "true"))
	UBoxComponent* ReverseCollider;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Plane, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* Plane;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Material, meta = (AllowPrivateAccess = "true"))
	UMaterial* EmptyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Material, meta = (AllowPrivateAccess = "true"))
	UMaterial* LinkedMaterial;

	TArray<UStaticMeshComponent*> TargetComponents;

	TArray<ECollisionChannel> TargetComponentsChannels;

	void Teleport();

	void SetActorClip(AActor* Actor, const FLinearColor Origin, const FLinearColor Normal, const bool bEnable);

private:
	FVector CameraOffset;
	FQuat CameraRotation;
	bool bPlayerCanTeleport;
	bool bJustTeleported;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	void SetUp(AActionDemoCharacter* Player, AActor* TargetSurface);

	void SetUp(AActionDemoCharacter* Player, AActor* TargetSurface, APortal* OtherPortal);

	void SetUpCollision(AActor* TargetSurface);

	void Link(APortal* Portal);

	// Maps a world point and direction through this portal onto the linked portal.
	bool PassageTransform(const FVector& WorldPoint, const FVector& WorldDirection, FVector& OutPoint, FVector& OutDirection) const;

	void SetToNoCollide();

	void ResetChannel();

public:
	AActionDemoCharacter* Character;

	APlayerController* PlayerController;//set it on spawn

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Default, meta = (AllowPrivateAccess = "true"))
	APortal* OtherPortal; //set it on spawn if there is another

	float VertComponent;

	TMap<AActor*, AActor*> TeleportingObjects = TMap<AActor*, AActor*>();
};
