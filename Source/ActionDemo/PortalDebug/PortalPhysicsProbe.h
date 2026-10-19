// Probe HUD / linker for L_PortalPhysicsProbe.
// T runs full matrix battery. -PortalProbeTest runs battery then quits.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PortalPhysicsProbe.generated.h"

class APortal;
class APortalPhysicsProbePlayerController;

/** One configurable probe case: start pose, launch, optional obstacles, expectations. */
USTRUCT()
struct FPortalPhysicsProbeCase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Probe")
	FName Name = NAME_None;

	/**
	 * Start relative to entry plane origin in portal local space
	 * (X = forward into room, Y = right, Z = up). Z snapped to entry floor unless bUseExactLocalZ.
	 */
	UPROPERTY(EditAnywhere, Category = "Probe")
	FVector StartLocalOffset = FVector(100.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bUseExactLocalZ = false;

	/** Launch velocity in entry portal local space (cm/s). Negative X = into the portal. */
	UPROPERTY(EditAnywhere, Category = "Probe")
	FVector LaunchVelocityLocal = FVector::ZeroVector;

	/** Angular velocity in entry portal local space (deg/s). */
	UPROPERTY(EditAnywhere, Category = "Probe")
	FVector LaunchAngularVelocityLocal = FVector::ZeroVector;

	/** Actor scale for this case. (0,0,0) keeps the level cube scale. */
	UPROPERTY(EditAnywhere, Category = "Probe")
	FVector CubeScale = FVector::ZeroVector;

	/** Extra yaw (deg) about portal up, applied on top of the cube's base rotation. */
	UPROPERTY(EditAnywhere, Category = "Probe")
	float StartYawDegrees = 0.f;

	/**
	 * When non-zero, split into two floors: entry portal on the low floor, exit portal on a
	 * floor raised by this many cm. Both portals stand on their own floor slab.
	 */
	UPROPERTY(EditAnywhere, Category = "Probe")
	float ExitFloorDeltaZ = 0.f;

	UPROPERTY(EditAnywhere, Category = "Probe")
	int32 ArmFrames = 5;

	UPROPERTY(EditAnywhere, Category = "Probe")
	int32 MaxFrames = 300;

	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bExpectCapture = true;

	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bExpectThrough = false;

	/** Pass only if the body never reaches the exit (e.g. wide object vs rim). */
	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bExpectNoThrough = false;

	/**
	 * Fail if the cube reaches the wall/cavity side of the entry plane without ever being
	 * captured (ghosted through the portal face instead of entering transit).
	 */
	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bExpectNoEntryGhost = true;

	/**
	 * For through cases: while captured on entry, clone must appear on the exit side;
	 * after handoff swap, clone must appear on the entry room side (original flies out exit).
	 */
	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bExpectCloneSwap = false;

	/**
	 * Expect exit-side obstacle contact: cube must not sink, and must not pass through
	 * the obstacle (stays on entry or stops in front of the exit blocker).
	 */
	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bExpectExitBlock = false;

	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bSpawnExitObstacle = false;

	/**
	 * Obstacle center in exit portal local space (X = exit forward into exit room).
	 * Default sits just outside the exit opening.
	 */
	UPROPERTY(EditAnywhere, Category = "Probe")
	FVector ExitObstacleLocalOffset = FVector(60.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Probe")
	FVector ExitObstacleScale = FVector(0.5f, 1.5f, 1.5f);

	/**
	 * Behind-entry obstacle must not physically block transit — fail if a hit impulse is recorded.
	 */
	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bExpectNoBehindEntryHit = false;

	/**
	 * After being behind the entry plane, returning to the entry room outside the aperture
	 * laterally means the body pierced the rim frame (e.g. exit-block slap-back).
	 */
	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bExpectNoRimBreachOnRebound = false;

	UPROPERTY(EditAnywhere, Category = "Probe")
	bool bSpawnBehindEntryObstacle = false;

	/** Obstacle center in entry local space; X should be negative (behind the plane). */
	UPROPERTY(EditAnywhere, Category = "Probe")
	FVector BehindEntryObstacleLocalOffset = FVector(-50.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Probe")
	FVector BehindEntryObstacleScale = FVector(0.5f, 1.5f, 2.f);

	UPROPERTY(EditAnywhere, Category = "Probe")
	int32 ThroughConfirmFrames = 20;

	UPROPERTY(EditAnywhere, Category = "Probe")
	int32 RestObserveFrames = 90;
};

UCLASS(NotPlaceable, Transient)
class APortalPhysicsProbe : public AActor
{
	GENERATED_BODY()

public:
	APortalPhysicsProbe();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Configure(int32 InDumpFrames, float InSinkThresholdCm);
	void SetOwningController(APortalPhysicsProbePlayerController* InController);

	void RestartSimulation();
	void RunPreset(int32 PresetIndex1Based);
	void RunAllPresets();

	int32 GetActivePresetIndex() const { return ActivePresetIndex; }
	int32 GetPresetCount() const { return Presets.Num(); }
	FName GetActivePresetName() const;

	static void RunFromConsole(const TArray<FString>& Args, UWorld* World);
	static void QuitFromConsole(const TArray<FString>& Args, UWorld* World);

private:
	enum class ECasePhase : uint8
	{
		Idle,
		Armed,
		Running,
		BetweenCases,
		Finished
	};

	bool BindPlacedActors();
	void BuildDefaultPresets();
	void DumpFrame(bool bSimAdvanced);
	void ZeroCubeVelocity() const;
	void PrepareCubePhysics();
	void ClearPortalTracking();
	void EnsureExitObstacle(const FPortalPhysicsProbeCase& Case);
	void DestroyExitObstacle();
	void EnsureBehindEntryObstacle(const FPortalPhysicsProbeCase& Case);
	void DestroyBehindEntryObstacle();
	void EnsureSplitFloors(const FPortalPhysicsProbeCase& Case);
	void DestroySplitFloors();
	void RestoreExitPortalTransform();
	AActor* SpawnFloorSlab(const FVector& Center, const FRotator& Rotation, float TopZ, const TCHAR* Label);
	FVector EntryLocalToWorld(const FVector& LocalOffset, bool bSnapToFloor) const;
	FVector ExitLocalToWorld(const FVector& LocalOffset, bool bSnapToFloor) const;
	FVector EntryLocalVectorToWorld(const FVector& LocalVector) const;
	void PlaceCubeForCase(const FPortalPhysicsProbeCase& Case);
	bool IsInPortalTransitZone(const FVector& Loc) const;
	bool IsOverlappingPortalReverse(const APortal* Portal, const AActor* Actor) const;
	float GetCubeExitPlaneSide(const FVector& Loc) const;
	float GetExitObstacleFrontSide() const;
	float GetCubeHalfExtentAlongEntryForward(const FPortalPhysicsProbeCase& Case) const;
	/** True if the exit-mapped cube OBB intersects the exit obstacle (setup contamination). */
	bool CubeExitPoseOverlapsExitObstacle(const FVector& EntryLoc, const FPortalPhysicsProbeCase& Case) const;
	float GetExpectedFloorTopZ(const FVector& Loc) const;
	float GetSinkCm(const FVector& Loc) const;

	UFUNCTION()
	void OnBehindEntryObstacleHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);
	void StartCase(int32 PresetIndex);
	void TickActiveCase();
	void FinishActiveCase(const TCHAR* Reason);
	void BeginNextBatteryCaseOrFinish();
	void RequestQuitAfterDelay(float Seconds);

	UPROPERTY()
	TObjectPtr<APortal> EntryPortal = nullptr;

	UPROPERTY()
	TObjectPtr<APortal> ExitPortal = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> ProbeCube = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> ExitObstacle = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> BehindEntryObstacle = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> EntryFloorSlab = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> ExitFloorSlab = nullptr;

	UPROPERTY()
	TObjectPtr<APortalPhysicsProbePlayerController> OwningController = nullptr;

	FTransform ExitPortalBaseTransform = FTransform::Identity;
	bool bExitPortalOffsetApplied = false;

	UPROPERTY()
	TArray<FPortalPhysicsProbeCase> Presets;

	FTransform CubeInitialTransform = FTransform::Identity;
	FVector CubeBaseScale = FVector(0.5f, 0.5f, 0.5f);
	float EntryFloorTopZ = 0.f;
	float ExitFloorTopZ = 0.f;
	float CubeHalfHeight = 25.f;
	float SinkThresholdCm = 7.f;
	int32 FrameIndex = 0;
	int32 ZeroVelocityFramesLeft = 0;
	bool bSceneReady = false;

	ECasePhase CasePhase = ECasePhase::Idle;
	int32 ActivePresetIndex = 0;
	int32 BatteryIndex = 0;
	bool bBatteryMode = false;
	bool bAutoQuitBattery = false;

	int32 CaseArmFramesLeft = 0;
	int32 CaseFramesLeft = 0;
	int32 CaseThroughFrames = 0;
	float CaseMaxSinkCm = 0.f;
	float CaseMaxExitPenetrationCm = 0.f;
	float CaseMaxBehindEntryHitImpulse = 0.f;
	bool bCaseSank = false;
	bool bCaseCaptured = false;
	bool bCaseWentThrough = false;
	bool bCaseBlockedByExit = false;
	bool bCaseHitBehindEntry = false;
	bool bCaseGhostedEntry = false;
	bool bCaseSawCloneAtExit = false;
	bool bCaseSawCloneAtEntryAfterSwap = false;
	bool bCaseWasBehindEntryPlane = false;
	bool bCaseRimBreachOnRebound = false;
};
