// Runtime scene: linked floor portal under the player; logs whether CMC lets them fall through.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PortalFloorFallTest.generated.h"

class APortal;
class AActionDemoCharacter;
class APlayerController;

UCLASS(NotPlaceable, Transient)
class APortalFloorFallTest : public AActor
{
	GENERATED_BODY()

public:
	APortalFloorFallTest();

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
		Passed,
		Failed,
		Finished
	};

	bool BuildScene();
	void DestroyScene();
	AActor* SpawnFloorSlab(const FVector& Center, float TopZ, const FVector& ScaleXYH, const TCHAR* Label);
	AActor* SpawnWall(const FVector& Center, const FRotator& Rotation, const FVector& Scale, const TCHAR* Label);
	TSubclassOf<APortal> LoadPortalClass() const;
	TSubclassOf<AActionDemoCharacter> LoadCharacterClass() const;
	void PlaceCharacterAboveFloorPortal();
	void BindPortalLocals(AActionDemoCharacter* Character, APlayerController* PC);
	void DumpFrame(float ElapsedSec) const;
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
	float TimeLimitSec = 6.f;
	float DropHeightCm = 0.f;
	bool bAutoQuit = false;
	bool bEverHadCurrentPortal = false;
	bool bEverBehindPlane = false;
	float MinPlaneSide = TNumericLimits<float>::Max();
	FString LastFloorCompName;
};
