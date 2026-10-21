// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Portal.generated.h"

class AActionDemoCharacter;
class FPortalEntryContactCallback;
class UPrimitiveComponent;

namespace Chaos
{
	struct FPhysicsObject;
}

UCLASS(ClassGroup = (Custom), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class ACTIONDEMO_API APortal : public AActor
{
	GENERATED_BODY()
	
public:	
	APortal();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnOverlapEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void HandOffTeleportingObject(AActor* Original);

	AActor* CreatePortalClone(AActor* Original, const FTransform& SpawnTransform);

	void UpdateCloneTransform(AActor* Original, AActor* Clone);

	void RegisterPhysicsCallbacks();

	void UnregisterPhysicsCallbacks();

	void SetSimulatingThroughPortal(AActor* Actor, bool bEnable);

	bool IsSimulatingThroughPortal(const AActor* Actor) const;

	void TransferSimulatingThroughPortal(AActor* Actor, APortal* Destination);

	void PushPhysicsCallbackInputs(float DeltaTime);

	void CachePortalPhysicsObjects();

	void NotifyCurrentOverlaps();

	/**
	 * Half-extents of the passable opening from rim box colliders:
	 * width = Top/Bottom extent along portal Right; height = Left/Right along Up.
	 */
	void GetApertureHalfExtents(float& OutHalfWidth, float& OutHalfHeight) const;

	/** OBB half-size of Actor along Dir (for plane-side transit tests). */
	float GetActorHalfExtentAlong(const AActor* Actor, const FVector& Dir) const;

	/**
	 * After ReverseCollider EndOverlap, keep capture only while the body still straddles
	 * the plane (center Side ≤ OBB half-extent along normal). Otherwise drop the clone.
	 */
	bool ShouldKeepTransitAfterOverlapEnd(const AActor* Actor) const;

	void SetActorClip(AActor* Actor, const FLinearColor Origin, const FLinearColor Normal, const bool bEnable);

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Default, meta = (AllowPrivateAccess = "true"))
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USceneCaptureComponent2D* CaptureComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Collider, meta = (AllowPrivateAccess = "true"))
	UBoxComponent* ReverseCollider;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Collider, meta = (AllowPrivateAccess = "true"))
	USceneComponent* EdgeColliders;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Collider, meta = (AllowPrivateAccess = "true"))
	UBoxComponent* Top;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Collider, meta = (AllowPrivateAccess = "true"))
	UBoxComponent* Bottom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Collider, meta = (AllowPrivateAccess = "true"))
	UBoxComponent* Left;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Collider, meta = (AllowPrivateAccess = "true"))
	UBoxComponent* Right;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Plane, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* Plane;

	/** Cached opening half-size (local Y / Z). Filled in BeginPlay from rim colliders. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Portal)
	float ApertureHalfWidth = 64.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Portal)
	float ApertureHalfHeight = 112.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Material, meta = (AllowPrivateAccess = "true"))
	UMaterialInstance* OrangeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Material, meta = (AllowPrivateAccess = "true"))
	UTextureRenderTarget2D* OrangeTextureTarget;

private:
	FPortalEntryContactCallback* EntryContactCallback = nullptr;

	TArray<TWeakObjectPtr<UPrimitiveComponent>> SimulatingComponents;

	TArray<Chaos::FPhysicsObject*> CachedPortalPhysicsObjects;

	TArray<Chaos::FPhysicsObject*> CachedRimPhysicsObjects;

	bool bPortalPhysicsObjectsCached = false;

public:	
	virtual void Tick(float DeltaTime) override;

	void Setup(AActor* TargetSurface, const bool bInIsBlue = true, APortal* InOtherPortal = nullptr);

	void Link(APortal* Portal);

	/** Drop all captured props (destroys clones, clears contact-mod). Probe/reset use this. */
	void AbortAllTransit();

	/**
	 * Live handoff rotation from the two portal actor quats
	 * (exit * 180° yaw * entry⁻¹). Recomputed from current poses each call.
	 */
	bool GetTravelQuat(FQuat& OutTravelQ) const;

	// Maps a world point through this portal onto the linked portal.
	bool TranslatePoint(const FVector& WorldPoint, FVector& OutPoint) const;

	// Maps a direction through this portal onto the linked portal.
	bool TranslateDirection(const FVector& WorldDirection, FVector& OutDirection) const;

	// True on the wall side of the portal, including contacts sitting on the surface.
	bool IsPointBehindPlane(const FVector& Point, float Cutoff = 2.0f) const;

	// True when the actor's oriented bounds fit through this portal's aperture.
	bool ActorFitsAperture(const AActor* Actor) const;

	/** Apply character transit through this portal onto OtherPortal (prediction or authority). */
	void Teleport(AActionDemoCharacter* Character);

	FVector GetPlaneOrigin() const
	{
		return Plane ? Plane->GetComponentLocation() : GetActorLocation();
	}

private:
	/** Colour/RT setup; multicast so clients apply without storing bIsBlue. */
	UFUNCTION(NetMulticast, Reliable)
	void SetupVisual(bool bInIsBlue);

	UFUNCTION()
	void OnRep_OtherPortal();

public:
	UPROPERTY(ReplicatedUsing = OnRep_OtherPortal)
	TObjectPtr<APortal> OtherPortal; //set it on spawn if there is another

	/** Local-only original→clone map. Do not replicate — clone pointers are per-machine and would dangle on peers. */
	TMap<AActor*, AActor*> TeleportingObjects = TMap<AActor*, AActor*>();
};
