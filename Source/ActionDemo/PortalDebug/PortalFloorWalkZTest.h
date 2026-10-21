// Walk toward a floor portal; log capsule Z / FloorDist to catch rim step-up.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PortalFloorWalkZTest.generated.h"

class APortal;
class AActionDemoCharacter;

UCLASS(NotPlaceable, Transient)
class APortalFloorWalkZTest : public AActor
{
	GENERATED_BODY()

public:
	APortalFloorWalkZTest();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	static void RunFromConsole(const TArray<FString>& Args, UWorld* World);
	static void QuitFromConsole(const TArray<FString>& Args, UWorld* World);

private:
	enum class EPhase : uint8
	{
		Building,
		Running,
		Finished
	};

	bool BuildScene();
	void DestroyScene();
	AActor* SpawnFloorSlab(const FVector& Center, float TopZ, const FVector& ScaleXYH, const TCHAR* Label);
	AActor* SpawnWall(const FVector& Center, const FRotator& Rotation, const FVector& Scale, const TCHAR* Label);
	TSubclassOf<APortal> LoadPortalClass() const;
	TSubclassOf<AActionDemoCharacter> LoadCharacterClass() const;
	void PlaceAndAimCharacter();
	void DriveWalk(float DeltaTime);
	void SampleZ();
	void Finish(bool bPass, const TCHAR* Reason);

	UPROPERTY()
	TObjectPtr<APortal> FloorPortal = nullptr;

	UPROPERTY()
	TObjectPtr<APortal> ExitPortal = nullptr;

	UPROPERTY()
	TObjectPtr<AActionDemoCharacter> TestCharacter = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> EntryFloor = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> ExitFloor = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> ExitWall = nullptr;

	EPhase Phase = EPhase::Building;
	float ElapsedSec = 0.f;
	float TimeLimitSec = 8.f;
	float MaxStepCm = 1.5f;
	bool bAutoQuit = false;

	float BaselineZ = 0.f;
	float MinZ = TNumericLimits<float>::Max();
	float MaxZ = -TNumericLimits<float>::Max();
	float MaxRiseCm = 0.f;
	FString FloorAtMaxRise;
	bool bBaselineSet = false;
	bool bReachedPortalXY = false;
};
