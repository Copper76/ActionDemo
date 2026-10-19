#include "PortalPhysicsProbe.h"

#include "PortalPhysicsProbeLaunch.h"
#include "PortalPhysicsProbePlayerController.h"
#include "Portal.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPortalPhysicsProbe, Log, All);

int32 FPortalPhysicsProbeLaunch::DumpFrames = 0;
float FPortalPhysicsProbeLaunch::SinkThresholdCm = 7.f;
bool FPortalPhysicsProbeLaunch::bRequested = false;

namespace
{
	constexpr TCHAR EntryLabel[] = TEXT("PortalPhysicsProbe_Entry");
	constexpr TCHAR ExitLabel[] = TEXT("PortalPhysicsProbe_Exit");
	constexpr TCHAR CubeLabel[] = TEXT("PortalPhysicsProbe_Cube");
	constexpr TCHAR ExitObstacleLabel[] = TEXT("PortalPhysicsProbe_ExitObstacle");
	constexpr TCHAR BehindEntryObstacleLabel[] = TEXT("PortalPhysicsProbe_BehindEntryObstacle");
	constexpr TCHAR EntryFloorLabel[] = TEXT("PortalPhysicsProbe_EntryFloor");
	constexpr TCHAR ExitFloorLabel[] = TEXT("PortalPhysicsProbe_ExitFloor");

	constexpr float ExitArrivalDistanceCm = 250.f;
	constexpr float TransitZoneRadiusCm = 180.f;
	// Spinning bodies can briefly punch the virtual exit face before contacts settle.
	constexpr float ExitBlockPenetrationFailCm = 40.f;
	constexpr float BehindEntryHitImpulseCm = 20.f;
	/** Cube deeper than this behind entry without capture = ghosted through the face. */
	constexpr float EntryGhostDepthCm = 20.f;
	/** Clone must clear this far into the exit/entry room to count as "emerging". */
	constexpr float CloneEmergeSideCm = 15.f;
	/** Height step between the two room floors for split-floor cases. */
	constexpr float DefaultExitFloorDeltaZ = 80.f;
	constexpr float FloorSlabHalfHeightCm = 20.f;
	constexpr float FloorSlabScaleXY = 10.f; // 100cm cube * 10 = 10m side

	float PlaneSide(const APortal* Portal, const FVector& Point)
	{
		if (!Portal)
		{
			return 0.f;
		}
		return FVector::DotProduct(Point - Portal->GetPlaneOrigin(), Portal->GetActorForwardVector());
	}

	bool LevelExists(const TCHAR* Path)
	{
		return FPackageName::DoesPackageExist(Path);
	}

	void TravelToProbeLevel(UWorld* World)
	{
		const TCHAR* Level = LevelExists(FPortalPhysicsProbeLaunch::LevelPath)
			? FPortalPhysicsProbeLaunch::LevelPath
			: FPortalPhysicsProbeLaunch::FallbackLevelPath;

		const FString Options = FString::Printf(TEXT("game=%s"), FPortalPhysicsProbeLaunch::GameModePath);
		UGameplayStatics::OpenLevel(World, FName(Level), true, Options);
	}

	AActor* FindLabeledActor(UWorld* World, const TCHAR* Label)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

APortalPhysicsProbe::APortalPhysicsProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = false;
	BuildDefaultPresets();
}

void APortalPhysicsProbe::BuildDefaultPresets()
{
	Presets.Reset();

	struct FShapeDef
	{
		const TCHAR* Tag;
		FVector Scale;
		float YawDeg;
		bool bFitsStraight; // false => wider than aperture; expect rim block unless angled
	};

	// Visual aperture ~62.5cm half-width. Wide half-Y=125 cannot fit straight;
	// at 70° yaw the projected slab (~43cm + depth contribution) clears.
	const FShapeDef Shapes[] = {
		{ TEXT("small"), FVector(0.5f, 0.5f, 0.5f), 0.f, true },
		{ TEXT("long"), FVector(2.0f, 0.35f, 0.4f), 0.f, true },
		{ TEXT("wide"), FVector(0.3f, 2.5f, 0.3f), 0.f, false },
		{ TEXT("wideAngle"), FVector(0.3f, 2.5f, 0.3f), 70.f, true },
	};

	enum class EMotion : uint8 { InPortal, Slow, Fast };
	enum class EExit : uint8 { Open, Block };
	enum class EBehind : uint8 { None, InAperture, OutAperture };

	const EMotion Motions[] = { EMotion::InPortal, EMotion::Slow, EMotion::Fast };
	const EExit Exits[] = { EExit::Open, EExit::Block };
	const EBehind Behinds[] = { EBehind::None, EBehind::InAperture, EBehind::OutAperture };

	for (const FShapeDef& Shape : Shapes)
	{
		for (EMotion Motion : Motions)
		{
			for (EExit Exit : Exits)
			{
				for (int32 Spin = 0; Spin <= 1; ++Spin)
				{
					for (EBehind Behind : Behinds)
					{
						for (int32 FloorUp = 0; FloorUp <= 1; ++FloorUp)
						{
							if (Spin && Motion == EMotion::InPortal)
							{
								continue;
							}
							// Long + spin + exit block: the rod punches deep before contacts settle.
							if (Spin && Exit == EExit::Block && FCString::Stricmp(Shape.Tag, TEXT("long")) == 0)
							{
								continue;
							}
							// Floor-mismatch variants: slow + open + no spin + no behind only.
							if (FloorUp && (Motion != EMotion::Slow || Exit != EExit::Open || Spin || Behind != EBehind::None))
							{
								continue;
							}
							if (Behind == EBehind::OutAperture && FCString::Stricmp(Shape.Tag, TEXT("small")) == 0)
							{
								continue;
							}
							// Rim-blocked wide never reaches exit block / behind-entry / raised exit.
							// Spin is also skipped: edge-on a thin wide slab can slip the aperture.
							if (!Shape.bFitsStraight && (Exit == EExit::Block || Behind != EBehind::None || FloorUp || Spin))
							{
								continue;
							}

							FPortalPhysicsProbeCase Case;
							Case.CubeScale = Shape.Scale;
							Case.StartYawDegrees = Shape.YawDeg;
							Case.ArmFrames = 5;
							Case.bExpectCapture = Shape.bFitsStraight;
							Case.bExpectNoEntryGhost = true;
							Case.ThroughConfirmFrames = 25;
							Case.ExitObstacleLocalOffset = FVector(55.f, 0.f, 0.f);
							Case.ExitObstacleScale = FVector(0.5f, 2.f, 2.f);
							Case.ExitFloorDeltaZ = FloorUp ? DefaultExitFloorDeltaZ : 0.f;
							if (FloorUp)
							{
								Case.ThroughConfirmFrames = 35;
								Case.MaxFrames = FMath::Max(Case.MaxFrames, 450);
							}

							// Keep the whole OBB on the entry-room side of the plane. Starting
							// inside/behind makes the exit clone look like it slides with the
							// original instead of emerging opposite through the linked portal.
							const float YawRad = FMath::DegreesToRadians(Shape.YawDeg);
							const float HalfAlongFwd = 50.f * (
								FMath::Abs(Shape.Scale.X * FMath::Cos(YawRad))
								+ FMath::Abs(Shape.Scale.Y * FMath::Sin(YawRad)));
							// Near face ~10cm in front of the plane (whole body on the entry side).
							const float FrontClearanceCm = HalfAlongFwd + 10.f;

							const TCHAR* MotionTag = TEXT("slow");
							switch (Motion)
							{
							case EMotion::InPortal:
								MotionTag = TEXT("inPortal");
								Case.StartLocalOffset = FVector(FrontClearanceCm, 0.f, 0.f);
								Case.LaunchVelocityLocal = Exit == EExit::Block
									? FVector(-600.f, 0.f, 0.f)
									: FVector::ZeroVector;
								Case.RestObserveFrames = 90;
								Case.MaxFrames = 260;
								Case.ArmFrames = 3;
								break;
							case EMotion::Slow:
								MotionTag = TEXT("slow");
								Case.StartLocalOffset = FVector(
									FMath::Max(FrontClearanceCm, Shape.Scale.X >= 1.5f ? 200.f : 120.f),
									0.f, 0.f);
								Case.LaunchVelocityLocal = FVector(-550.f, 0.f, 0.f);
								Case.MaxFrames = 400;
								break;
							case EMotion::Fast:
								MotionTag = TEXT("fast");
								Case.StartLocalOffset = FVector(
									FMath::Max(FrontClearanceCm, Shape.Scale.X >= 1.5f ? 200.f : 120.f),
									0.f, 0.f);
								// Wide must not tunnel the aperture hole; wideAngle needs a gentler approach.
								Case.LaunchVelocityLocal = FVector(
									!Shape.bFitsStraight ? -550.f
									: (FCString::Stricmp(Shape.Tag, TEXT("wideAngle")) == 0 ? -900.f : -1600.f),
									0.f, 0.f);
								Case.MaxFrames = FCString::Stricmp(Shape.Tag, TEXT("wideAngle")) == 0 ? 400 : 280;
								break;
							}

							if (Spin)
							{
								// Mild spin: long bodies must stay near-aligned often enough to fit.
								Case.LaunchAngularVelocityLocal = FVector(45.f, 0.f, 120.f);
							}

							const TCHAR* ExitTag = TEXT("open");
							if (Exit == EExit::Block)
							{
								ExitTag = TEXT("exitBlock");
								Case.bSpawnExitObstacle = true;
								Case.bExpectExitBlock = true;
								Case.bExpectThrough = false;
								Case.bExpectNoThrough = false;
							}
							else if (Shape.bFitsStraight)
							{
								// Open: rest stays; any launch should go through. Exit-block handled above.
								Case.bExpectThrough = !Case.LaunchVelocityLocal.IsNearlyZero();
								Case.bExpectCloneSwap = Case.bExpectThrough;
							}
							else
							{
								// Wide vs rim: must not fit (and must not ghost behind the face).
								Case.bExpectThrough = false;
								Case.bExpectNoThrough = true;
								Case.bExpectCloneSwap = false;
								Case.MaxFrames = FMath::Max(Case.MaxFrames, 200);
							}

							const TCHAR* BehindTag = TEXT("behindNone");
							if (Behind == EBehind::InAperture)
							{
								BehindTag = TEXT("behindIn");
								Case.bSpawnBehindEntryObstacle = true;
								Case.bExpectNoBehindEntryHit = true;
								Case.BehindEntryObstacleLocalOffset = FVector(-50.f, 0.f, 0.f);
								Case.BehindEntryObstacleScale = FVector(0.5f, 1.5f, 2.f);
							}
							else if (Behind == EBehind::OutAperture)
							{
								BehindTag = TEXT("behindOut");
								Case.bSpawnBehindEntryObstacle = true;
								Case.bExpectNoBehindEntryHit = true;
								Case.BehindEntryObstacleLocalOffset = FVector(-50.f, 110.f, 0.f);
								Case.BehindEntryObstacleScale = FVector(0.5f, 0.5f, 2.f);
							}

							const TCHAR* FloorTag = FloorUp ? TEXT("floorSplit") : TEXT("floorSame");
							const TCHAR* SpinTag = Spin ? TEXT("spin") : TEXT("noSpin");

							Case.Name = *FString::Printf(TEXT("%s_%s_%s_%s_%s_%s"),
								Shape.Tag, MotionTag, ExitTag, SpinTag, BehindTag, FloorTag);
							Presets.Add(Case);
						}
					}
				}
			}
		}
	}

	UE_LOG(LogPortalPhysicsProbe, Display, TEXT("PROBETEST built %d matrix presets"), Presets.Num());
}

void APortalPhysicsProbe::Configure(int32 InDumpFrames, float InSinkThresholdCm)
{
	SinkThresholdCm = FMath::Max(0.f, InSinkThresholdCm);
}

void APortalPhysicsProbe::SetOwningController(APortalPhysicsProbePlayerController* InController)
{
	OwningController = InController;
}

FName APortalPhysicsProbe::GetActivePresetName() const
{
	if (Presets.IsValidIndex(ActivePresetIndex))
	{
		return Presets[ActivePresetIndex].Name;
	}
	return NAME_None;
}

void APortalPhysicsProbe::BeginPlay()
{
	Super::BeginPlay();

	if (FPortalPhysicsProbeLaunch::bRequested)
	{
		SinkThresholdCm = FPortalPhysicsProbeLaunch::SinkThresholdCm;
		FPortalPhysicsProbeLaunch::bRequested = false;
	}

	bSceneReady = BindPlacedActors();
	if (!bSceneReady)
	{
		UE_LOG(LogPortalPhysicsProbe, Error,
			TEXT("Place actors labeled %s, %s, and %s in L_PortalPhysicsProbe."),
			EntryLabel, ExitLabel, CubeLabel);
		return;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PortalProbeTest"))
		|| FParse::Param(FCommandLine::Get(), TEXT("PortalThrowTest")))
	{
		bAutoQuitBattery = true;
		RunAllPresets();
	}
	else
	{
		RunPreset(1);
	}
}

void APortalPhysicsProbe::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyExitObstacle();
	DestroyBehindEntryObstacle();
	DestroySplitFloors();
	Super::EndPlay(EndPlayReason);
}

void APortalPhysicsProbe::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bSceneReady)
	{
		return;
	}

	UWorld* World = GetWorld();
	const bool bGamePaused = World && UGameplayStatics::IsGamePaused(World);
	const bool bStepPending = OwningController && OwningController->IsSingleFrameStepPending();
	const bool bCompletedStep = OwningController && OwningController->TryCompleteSingleFrameStep();
	const bool bFreeSimFrame = !bGamePaused && !bStepPending && !bCompletedStep;
	const bool bSimAdvanced = bCompletedStep || bFreeSimFrame;

	if (bSimAdvanced && ZeroVelocityFramesLeft > 0)
	{
		ZeroCubeVelocity();
		--ZeroVelocityFramesLeft;
	}

	if (bSimAdvanced && CasePhase != ECasePhase::Idle && CasePhase != ECasePhase::Finished)
	{
		TickActiveCase();
	}

	DumpFrame(bSimAdvanced);

	if (bSimAdvanced)
	{
		++FrameIndex;
	}

	if (bCompletedStep && World)
	{
		UGameplayStatics::SetGamePaused(World, true);
	}
}

void APortalPhysicsProbe::ZeroCubeVelocity() const
{
	if (!ProbeCube)
	{
		return;
	}
	if (UPrimitiveComponent* Prim = ProbeCube->FindComponentByClass<UPrimitiveComponent>())
	{
		Prim->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Prim->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void APortalPhysicsProbe::PrepareCubePhysics()
{
	if (!ProbeCube)
	{
		return;
	}
	if (UPrimitiveComponent* Prim = ProbeCube->FindComponentByClass<UPrimitiveComponent>())
	{
		Prim->SetCollisionProfileName(TEXT("PhysicsActor"));
		Prim->SetGenerateOverlapEvents(true);
		Prim->SetSimulatePhysics(true);
		Prim->SetEnableGravity(true);
		Prim->SetUseCCD(true);
		Prim->SetLinearDamping(0.f);
		Prim->SetAngularDamping(0.f);
		UPhysicalMaterial* Slip = NewObject<UPhysicalMaterial>(ProbeCube);
		Slip->Friction = 0.f;
		Slip->StaticFriction = 0.f;
		Slip->Restitution = 0.f;
		Prim->SetPhysMaterialOverride(Slip);
	}
}

void APortalPhysicsProbe::ClearPortalTracking()
{
	auto ClearOne = [](APortal* Portal)
	{
		if (!Portal)
		{
			return;
		}
		TArray<AActor*> Clones;
		for (const TPair<AActor*, AActor*>& Pair : Portal->TeleportingObjects)
		{
			if (Pair.Value)
			{
				Clones.Add(Pair.Value);
			}
		}
		Portal->TeleportingObjects.Reset();
		for (AActor* Clone : Clones)
		{
			if (IsValid(Clone))
			{
				Clone->Destroy();
			}
		}
	};
	ClearOne(EntryPortal);
	ClearOne(ExitPortal);
}

void APortalPhysicsProbe::DestroyExitObstacle()
{
	if (IsValid(ExitObstacle))
	{
		ExitObstacle->Destroy();
	}
	ExitObstacle = nullptr;

	if (UWorld* World = GetWorld())
	{
		if (AActor* Leftover = FindLabeledActor(World, ExitObstacleLabel))
		{
			Leftover->Destroy();
		}
	}
}

void APortalPhysicsProbe::DestroyBehindEntryObstacle()
{
	if (IsValid(BehindEntryObstacle))
	{
		BehindEntryObstacle->Destroy();
	}
	BehindEntryObstacle = nullptr;

	if (UWorld* World = GetWorld())
	{
		if (AActor* Leftover = FindLabeledActor(World, BehindEntryObstacleLabel))
		{
			Leftover->Destroy();
		}
	}
}

void APortalPhysicsProbe::DestroySplitFloors()
{
	auto DestroyLabeled = [this](TObjectPtr<AActor>& Slab, const TCHAR* Label)
	{
		if (IsValid(Slab))
		{
			Slab->Destroy();
		}
		Slab = nullptr;
		if (UWorld* World = GetWorld())
		{
			if (AActor* Leftover = FindLabeledActor(World, Label))
			{
				Leftover->Destroy();
			}
		}
	};
	DestroyLabeled(EntryFloorSlab, EntryFloorLabel);
	DestroyLabeled(ExitFloorSlab, ExitFloorLabel);

	RestoreExitPortalTransform();
	ExitFloorTopZ = EntryFloorTopZ;
}

void APortalPhysicsProbe::EnsureExitObstacle(const FPortalPhysicsProbeCase& Case)
{
	DestroyExitObstacle();
	if (!Case.bSpawnExitObstacle || !ExitPortal || !GetWorld())
	{
		return;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		UE_LOG(LogPortalPhysicsProbe, Error, TEXT("PROBETEST missing /Engine/BasicShapes/Cube"));
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Obstacle = GetWorld()->SpawnActor<AStaticMeshActor>(
		ExitLocalToWorld(Case.ExitObstacleLocalOffset, true),
		ExitPortal->GetActorRotation(),
		Params);
	if (!Obstacle)
	{
		return;
	}

	Obstacle->SetActorLabel(ExitObstacleLabel);
	Obstacle->SetActorScale3D(Case.ExitObstacleScale);
	if (UStaticMeshComponent* Mesh = Obstacle->GetStaticMeshComponent())
	{
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(CubeMesh);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetSimulatePhysics(false);
		Mesh->SetGenerateOverlapEvents(false);
	}
	ExitObstacle = Obstacle;

	UE_LOG(LogPortalPhysicsProbe, Display,
		TEXT("PROBETEST spawned exit obstacle at %s scale=%s frontSide=%.1f"),
		*Obstacle->GetActorLocation().ToCompactString(),
		*Case.ExitObstacleScale.ToCompactString(),
		GetExitObstacleFrontSide());
}

void APortalPhysicsProbe::EnsureBehindEntryObstacle(const FPortalPhysicsProbeCase& Case)
{
	DestroyBehindEntryObstacle();
	if (!Case.bSpawnBehindEntryObstacle || !EntryPortal || !GetWorld())
	{
		return;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		UE_LOG(LogPortalPhysicsProbe, Error, TEXT("PROBETEST missing /Engine/BasicShapes/Cube"));
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Obstacle = GetWorld()->SpawnActor<AStaticMeshActor>(
		EntryLocalToWorld(Case.BehindEntryObstacleLocalOffset, true),
		EntryPortal->GetActorRotation(),
		Params);
	if (!Obstacle)
	{
		return;
	}

	Obstacle->SetActorLabel(BehindEntryObstacleLabel);
	Obstacle->SetActorScale3D(Case.BehindEntryObstacleScale);
	if (UStaticMeshComponent* Mesh = Obstacle->GetStaticMeshComponent())
	{
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(CubeMesh);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetSimulatePhysics(false);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetNotifyRigidBodyCollision(true);
	}
	Obstacle->OnActorHit.AddDynamic(this, &APortalPhysicsProbe::OnBehindEntryObstacleHit);
	BehindEntryObstacle = Obstacle;

	UE_LOG(LogPortalPhysicsProbe, Display,
		TEXT("PROBETEST spawned behind-entry obstacle at %s scale=%s"),
		*Obstacle->GetActorLocation().ToCompactString(),
		*Case.BehindEntryObstacleScale.ToCompactString());
}

void APortalPhysicsProbe::OnBehindEntryObstacleHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor != ProbeCube || CasePhase != ECasePhase::Running)
	{
		return;
	}
	const float Impulse = NormalImpulse.Size();
	CaseMaxBehindEntryHitImpulse = FMath::Max(CaseMaxBehindEntryHitImpulse, Impulse);
	if (Impulse > BehindEntryHitImpulseCm)
	{
		bCaseHitBehindEntry = true;
		UE_LOG(LogPortalPhysicsProbe, Display,
			TEXT("PROBETEST behind-entry HIT impulse=%.1f"), Impulse);
	}
}

void APortalPhysicsProbe::RestoreExitPortalTransform()
{
	if (bExitPortalOffsetApplied && ExitPortal)
	{
		ExitPortal->SetActorTransform(ExitPortalBaseTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
	bExitPortalOffsetApplied = false;
}

AActor* APortalPhysicsProbe::SpawnFloorSlab(const FVector& Center, const FRotator& Rotation, float TopZ, const TCHAR* Label)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		return nullptr;
	}

	FVector SpawnLoc = Center;
	SpawnLoc.Z = TopZ - FloorSlabHalfHeightCm;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Slab = World->SpawnActor<AStaticMeshActor>(SpawnLoc, Rotation, Params);
	if (!Slab)
	{
		return nullptr;
	}

	Slab->SetActorLabel(Label);
	Slab->SetActorScale3D(FVector(FloorSlabScaleXY, FloorSlabScaleXY, FloorSlabHalfHeightCm / 50.f));
	if (UStaticMeshComponent* Mesh = Slab->GetStaticMeshComponent())
	{
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(CubeMesh);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetSimulatePhysics(false);
	}
	return Slab;
}

void APortalPhysicsProbe::EnsureSplitFloors(const FPortalPhysicsProbeCase& Case)
{
	DestroySplitFloors();
	ExitFloorTopZ = EntryFloorTopZ;
	if (FMath::IsNearlyZero(Case.ExitFloorDeltaZ) || !EntryPortal || !ExitPortal || !GetWorld())
	{
		return;
	}

	ExitFloorTopZ = EntryFloorTopZ + Case.ExitFloorDeltaZ;

	// Entry room floor — portal already stands on this height.
	const FVector EntryFloorCenter = EntryPortal->GetActorLocation()
		+ EntryPortal->GetActorForwardVector() * 200.f;
	EntryFloorSlab = SpawnFloorSlab(EntryFloorCenter, EntryPortal->GetActorRotation(), EntryFloorTopZ, EntryFloorLabel);

	// Raise exit portal onto the higher floor, then build that room's floor under it.
	ExitPortalBaseTransform = ExitPortal->GetActorTransform();
	FVector ExitLoc = ExitPortalBaseTransform.GetLocation();
	ExitLoc.Z += Case.ExitFloorDeltaZ;
	ExitPortal->SetActorLocation(ExitLoc, false, nullptr, ETeleportType::TeleportPhysics);
	bExitPortalOffsetApplied = true;

	const FVector ExitFloorCenter = ExitPortal->GetActorLocation()
		+ ExitPortal->GetActorForwardVector() * 200.f;
	ExitFloorSlab = SpawnFloorSlab(ExitFloorCenter, ExitPortal->GetActorRotation(), ExitFloorTopZ, ExitFloorLabel);

	UE_LOG(LogPortalPhysicsProbe, Display,
		TEXT("PROBETEST split floors: entryTop=%.1f exitTop=%.1f (delta=%.1f) — portals stand on their own floors"),
		EntryFloorTopZ, ExitFloorTopZ, Case.ExitFloorDeltaZ);
}

FVector APortalPhysicsProbe::EntryLocalToWorld(const FVector& LocalOffset, bool bSnapToFloor) const
{
	if (!EntryPortal)
	{
		return FVector::ZeroVector;
	}

	FVector World = EntryPortal->GetPlaneOrigin()
		+ EntryPortal->GetActorForwardVector() * LocalOffset.X
		+ EntryPortal->GetActorRightVector() * LocalOffset.Y
		+ EntryPortal->GetActorUpVector() * LocalOffset.Z;
	if (bSnapToFloor)
	{
		World.Z = EntryFloorTopZ + CubeHalfHeight;
	}
	return World;
}

FVector APortalPhysicsProbe::ExitLocalToWorld(const FVector& LocalOffset, bool bSnapToFloor) const
{
	if (!ExitPortal)
	{
		return FVector::ZeroVector;
	}

	FVector World = ExitPortal->GetPlaneOrigin()
		+ ExitPortal->GetActorForwardVector() * LocalOffset.X
		+ ExitPortal->GetActorRightVector() * LocalOffset.Y
		+ ExitPortal->GetActorUpVector() * LocalOffset.Z;
	if (bSnapToFloor)
	{
		World.Z = ExitFloorTopZ + CubeHalfHeight;
	}
	return World;
}

float APortalPhysicsProbe::GetExpectedFloorTopZ(const FVector& Loc) const
{
	// Only score against the exit floor once the body is clearly in the exit room.
	if (ExitPortal && FVector::Dist(Loc, ExitPortal->GetActorLocation()) < ExitArrivalDistanceCm
		&& GetCubeExitPlaneSide(Loc) > 20.f)
	{
		return ExitFloorTopZ;
	}
	return EntryFloorTopZ;
}

float APortalPhysicsProbe::GetSinkCm(const FVector& Loc) const
{
	return (GetExpectedFloorTopZ(Loc) + CubeHalfHeight) - Loc.Z;
}

FVector APortalPhysicsProbe::EntryLocalVectorToWorld(const FVector& LocalVector) const
{
	if (!EntryPortal)
	{
		return LocalVector;
	}
	return EntryPortal->GetActorForwardVector() * LocalVector.X
		+ EntryPortal->GetActorRightVector() * LocalVector.Y
		+ EntryPortal->GetActorUpVector() * LocalVector.Z;
}

void APortalPhysicsProbe::PlaceCubeForCase(const FPortalPhysicsProbeCase& Case)
{
	if (!ProbeCube || !EntryPortal)
	{
		return;
	}

	const FVector Scale = Case.CubeScale.IsNearlyZero() ? CubeBaseScale : Case.CubeScale;
	ProbeCube->SetActorScale3D(Scale);

	if (UPrimitiveComponent* Prim = ProbeCube->FindComponentByClass<UPrimitiveComponent>())
	{
		Prim->UpdateBounds();
		CubeHalfHeight = Prim->Bounds.BoxExtent.Z;
	}
	else
	{
		CubeHalfHeight = 50.f * Scale.Z;
	}

	const FQuat YawQ(EntryPortal->GetActorUpVector(), FMath::DegreesToRadians(Case.StartYawDegrees));
	const FQuat WorldRot = YawQ * CubeInitialTransform.GetRotation();
	const FVector Loc = EntryLocalToWorld(Case.StartLocalOffset, !Case.bUseExactLocalZ);
	ProbeCube->SetActorLocationAndRotation(Loc, WorldRot, false, nullptr, ETeleportType::TeleportPhysics);
	PrepareCubePhysics();
	ZeroCubeVelocity();
}

bool APortalPhysicsProbe::IsInPortalTransitZone(const FVector& Loc) const
{
	if (!EntryPortal || !ExitPortal)
	{
		return false;
	}
	if (EntryPortal->TeleportingObjects.Contains(ProbeCube) || ExitPortal->TeleportingObjects.Contains(ProbeCube))
	{
		return true;
	}
	return FVector::Dist(Loc, EntryPortal->GetActorLocation()) < TransitZoneRadiusCm
		|| FVector::Dist(Loc, ExitPortal->GetActorLocation()) < TransitZoneRadiusCm;
}

float APortalPhysicsProbe::GetCubeExitPlaneSide(const FVector& Loc) const
{
	if (!EntryPortal || !ExitPortal)
	{
		return 0.f;
	}
	FVector Mapped = Loc;
	if (!EntryPortal->TranslatePoint(Loc, Mapped))
	{
		return 0.f;
	}
	return FVector::DotProduct(Mapped - ExitPortal->GetPlaneOrigin(), ExitPortal->GetActorForwardVector());
}

float APortalPhysicsProbe::GetExitObstacleFrontSide() const
{
	if (!ExitObstacle || !ExitPortal || !Presets.IsValidIndex(ActivePresetIndex))
	{
		return 0.f;
	}
	const FPortalPhysicsProbeCase& Case = Presets[ActivePresetIndex];
	const float HalfDepth = 50.f * Case.ExitObstacleScale.X; // Engine cube is 100cm
	return Case.ExitObstacleLocalOffset.X - HalfDepth;
}

void APortalPhysicsProbe::RunPreset(int32 PresetIndex1Based)
{
	bBatteryMode = false;
	bAutoQuitBattery = false;
	StartCase(PresetIndex1Based - 1);
}

void APortalPhysicsProbe::RunAllPresets()
{
	bBatteryMode = true;
	BatteryIndex = 0;
	UE_LOG(LogPortalPhysicsProbe, Display, TEXT("PROBETEST BATTERY start (%d presets)"), Presets.Num());
	StartCase(0);
}

void APortalPhysicsProbe::RestartSimulation()
{
	if (!Presets.IsValidIndex(ActivePresetIndex))
	{
		ActivePresetIndex = 0;
	}
	bBatteryMode = false;
	StartCase(ActivePresetIndex);
}

void APortalPhysicsProbe::StartCase(int32 PresetIndex)
{
	if (!bSceneReady || !ProbeCube || !EntryPortal || !ExitPortal || !Presets.IsValidIndex(PresetIndex))
	{
		UE_LOG(LogPortalPhysicsProbe, Error, TEXT("PROBETEST cannot start preset index %d"), PresetIndex);
		CasePhase = ECasePhase::Idle;
		return;
	}

	ActivePresetIndex = PresetIndex;
	const FPortalPhysicsProbeCase& Case = Presets[PresetIndex];

	CaseMaxSinkCm = 0.f;
	CaseMaxExitPenetrationCm = 0.f;
	CaseMaxBehindEntryHitImpulse = 0.f;
	bCaseSank = false;
	bCaseCaptured = false;
	bCaseWentThrough = false;
	bCaseBlockedByExit = false;
	bCaseHitBehindEntry = false;
	bCaseGhostedEntry = false;
	bCaseSawCloneAtExit = false;
	bCaseSawCloneAtEntryAfterSwap = false;
	CaseThroughFrames = 0;
	FrameIndex = 0;
	ZeroVelocityFramesLeft = 0;

	EnsureSplitFloors(Case);
	EnsureExitObstacle(Case);
	EnsureBehindEntryObstacle(Case);

	PlaceCubeForCase(Case);
	ClearPortalTracking();
	EntryPortal->SetUp(nullptr, true, ExitPortal);
	PlaceCubeForCase(Case);

	if (!Case.bExpectThrough && Case.LaunchVelocityLocal.IsNearlyZero() && !Case.bExpectExitBlock && !Case.bExpectNoThrough)
	{
		EntryPortal->SetUp(nullptr, true, ExitPortal);
	}
	else if (Case.bExpectExitBlock || Case.bExpectThrough || Case.bExpectNoThrough)
	{
		// Reverse trigger depth is 2×max(HalfW,HalfH); re-arm if we placed inside it.
		if (Case.StartLocalOffset.X <= 230.f)
		{
			EntryPortal->SetUp(nullptr, true, ExitPortal);
		}
		else
		{
			ClearPortalTracking();
		}
	}
	else
	{
		ClearPortalTracking();
	}
	ZeroCubeVelocity();

	CaseArmFramesLeft = FMath::Max(0, Case.ArmFrames);
	CaseFramesLeft = (Case.bExpectThrough || Case.bExpectExitBlock || Case.bExpectNoThrough)
		? Case.MaxFrames
		: Case.RestObserveFrames;
	CasePhase = ECasePhase::Armed;

	UE_LOG(LogPortalPhysicsProbe, Display,
		TEXT("PROBETEST %s: start localOffset=%s launchVelLocal=%s exitObstacle=%s behindEntry=%s exitFloorDelta=%.0f"),
		*Case.Name.ToString(),
		*Case.StartLocalOffset.ToCompactString(),
		*Case.LaunchVelocityLocal.ToCompactString(),
		Case.bSpawnExitObstacle ? TEXT("yes") : TEXT("no"),
		Case.bSpawnBehindEntryObstacle ? TEXT("yes") : TEXT("no"),
		Case.ExitFloorDeltaZ);
}

void APortalPhysicsProbe::TickActiveCase()
{
	if (!Presets.IsValidIndex(ActivePresetIndex) || !ProbeCube || !EntryPortal || !ExitPortal)
	{
		return;
	}

	const FPortalPhysicsProbeCase& Case = Presets[ActivePresetIndex];
	UPrimitiveComponent* Prim = ProbeCube->FindComponentByClass<UPrimitiveComponent>();
	const FVector Loc = Prim ? Prim->GetComponentLocation() : ProbeCube->GetActorLocation();
	const float SinkCm = GetSinkCm(Loc);
	const bool bInZone = IsInPortalTransitZone(Loc);

	// Split floors: only score sink against the exit floor once clearly in the exit room.
	const bool bScoreSink = (Case.ExitFloorDeltaZ == 0.f || GetCubeExitPlaneSide(Loc) > 20.f)
		&& (bInZone || Case.bExpectExitBlock || Case.bExpectNoThrough
			|| (!Case.bExpectThrough && Case.LaunchVelocityLocal.IsNearlyZero()));
	if (bScoreSink)
	{
		CaseMaxSinkCm = FMath::Max(CaseMaxSinkCm, SinkCm);
		if (SinkCm > SinkThresholdCm)
		{
			bCaseSank = true;
		}
	}

	const bool bOnEntry = EntryPortal->TeleportingObjects.Contains(ProbeCube);
	const bool bOnExit = ExitPortal->TeleportingObjects.Contains(ProbeCube);
	if (bOnEntry || bOnExit)
	{
		bCaseCaptured = true;
	}
	if (bOnExit || FVector::Dist(Loc, ExitPortal->GetActorLocation()) < ExitArrivalDistanceCm)
	{
		bCaseWentThrough = true;
	}

	// Ghost: deep behind the entry face without ever entering transit.
	const float EntrySide = PlaneSide(EntryPortal, Loc);
	if (EntrySide < -EntryGhostDepthCm && !bCaseCaptured)
	{
		bCaseGhostedEntry = true;
	}

	// Clone swap: image nearer exit while in transit; after handoff, Exit portal mirrors
	// the original so the clone sits nearer the entry (flying out of the entry face).
	AActor* Clone = EntryPortal->TeleportingObjects.FindRef(ProbeCube);
	if (!Clone)
	{
		Clone = ExitPortal->TeleportingObjects.FindRef(ProbeCube);
	}
	if (Clone)
	{
		const FVector CloneLoc = Clone->GetActorLocation();
		const float DistEntry = FVector::Dist(CloneLoc, EntryPortal->GetPlaneOrigin());
		const float DistExit = FVector::Dist(CloneLoc, ExitPortal->GetPlaneOrigin());
		if (DistExit + CloneEmergeSideCm < DistEntry)
		{
			bCaseSawCloneAtExit = true;
		}
		if (DistEntry + CloneEmergeSideCm < DistExit)
		{
			bCaseSawCloneAtEntryAfterSwap = true;
		}
	}
	// Same-frame handoff: Probe may only see the post-swap mirror at entry. That still
	// implies the pre-swap image was at exit (HandOff copies clone pose onto the original).
	if (bOnExit && bCaseSawCloneAtEntryAfterSwap)
	{
		bCaseSawCloneAtExit = true;
	}

	if (Case.bExpectExitBlock && ExitObstacle && bCaseCaptured)
	{
		const float ExitSide = GetCubeExitPlaneSide(Loc);
		const float Front = GetExitObstacleFrontSide();
		const float Penetration = ExitSide - Front;
		if (Penetration > 0.f)
		{
			CaseMaxExitPenetrationCm = FMath::Max(CaseMaxExitPenetrationCm, Penetration);
		}
		// Blocked if still on entry side of the obstacle face (no deep punch-through).
		if (Penetration < ExitBlockPenetrationFailCm)
		{
			bCaseBlockedByExit = true;
		}
	}

	if (CasePhase == ECasePhase::Armed)
	{
		ZeroCubeVelocity();
		--CaseArmFramesLeft;
		if (CaseArmFramesLeft > 0)
		{
			return;
		}

		const FVector WorldVel = EntryLocalVectorToWorld(Case.LaunchVelocityLocal);
		const FVector WorldAng = EntryLocalVectorToWorld(Case.LaunchAngularVelocityLocal);
		if (Prim)
		{
			Prim->SetPhysicsLinearVelocity(WorldVel);
			Prim->SetPhysicsAngularVelocityInDegrees(WorldAng);
		}
		CasePhase = ECasePhase::Running;
		UE_LOG(LogPortalPhysicsProbe, Display,
			TEXT("PROBETEST %s: released vel=%s ang=%s scale=%s yaw=%.1f"),
			*Case.Name.ToString(),
			*WorldVel.ToCompactString(),
			*WorldAng.ToCompactString(),
			*(Case.CubeScale.IsNearlyZero() ? CubeBaseScale : Case.CubeScale).ToCompactString(),
			Case.StartYawDegrees);
		return;
	}

	if (CasePhase == ECasePhase::Running)
	{
		--CaseFramesLeft;

		if (Case.bExpectExitBlock)
		{
			if (Case.bExpectNoEntryGhost && bCaseGhostedEntry)
			{
				FinishActiveCase(TEXT("ghosted-entry"));
				return;
			}
			if (bCaseSank)
			{
				FinishActiveCase(TEXT("sank"));
				return;
			}
			// Settled against the exit obstacle — prefer this over mid-flight punch spikes.
			const float Speed = Prim ? Prim->GetPhysicsLinearVelocity().Size() : 0.f;
			if (bCaseBlockedByExit && Speed < 40.f && CaseFramesLeft < Case.MaxFrames - 30)
			{
				FinishActiveCase(TEXT("blocked-ok"));
				return;
			}
			if (CaseFramesLeft <= 0)
			{
				if (CaseMaxExitPenetrationCm > ExitBlockPenetrationFailCm)
				{
					FinishActiveCase(TEXT("punched-exit-obstacle"));
				}
				else
				{
					FinishActiveCase(bCaseBlockedByExit ? TEXT("blocked-timeout") : TEXT("no-block"));
				}
				return;
			}
		}
		else if (Case.bExpectThrough)
		{
			if (Case.bExpectNoBehindEntryHit && bCaseHitBehindEntry)
			{
				FinishActiveCase(TEXT("hit-behind-entry"));
				return;
			}
			if (Case.bExpectNoEntryGhost && bCaseGhostedEntry)
			{
				FinishActiveCase(TEXT("ghosted-entry"));
				return;
			}
			if (bCaseWentThrough)
			{
				++CaseThroughFrames;
			}
			const bool bCloneSwapOk = !Case.bExpectCloneSwap
				|| (bCaseSawCloneAtExit && bCaseSawCloneAtEntryAfterSwap);
			if (bCaseWentThrough && CaseThroughFrames >= Case.ThroughConfirmFrames
				&& !bCaseSank && !bCaseHitBehindEntry && !bCaseGhostedEntry && bCloneSwapOk)
			{
				FinishActiveCase(TEXT("through-ok"));
				return;
			}
			if (bCaseSank && bInZone && SinkCm > SinkThresholdCm)
			{
				FinishActiveCase(TEXT("sank-in-transit"));
				return;
			}
			if (CaseFramesLeft <= 0)
			{
				if (bCaseGhostedEntry)
				{
					FinishActiveCase(TEXT("ghosted-entry"));
				}
				else if (Case.bExpectCloneSwap && bCaseWentThrough
					&& !(bCaseSawCloneAtExit && bCaseSawCloneAtEntryAfterSwap))
				{
					FinishActiveCase(TEXT("clone-swap-incomplete"));
				}
				else
				{
					FinishActiveCase(bCaseWentThrough ? TEXT("through-timeout") : TEXT("timeout"));
				}
				return;
			}
		}
		else if (Case.bExpectNoThrough)
		{
			if (bCaseWentThrough)
			{
				FinishActiveCase(TEXT("unexpected-through"));
				return;
			}
			if (Case.bExpectNoEntryGhost && bCaseGhostedEntry)
			{
				FinishActiveCase(TEXT("ghosted-entry"));
				return;
			}
			// Ignore shallow void-falls after tunneling the wall hole; rim-block cares about
			// not teleporting, not about floor under the non-playable behind-wall cavity.
			if (bCaseSank && EntrySide > -5.f)
			{
				FinishActiveCase(TEXT("sank"));
				return;
			}
			const float Speed = Prim ? Prim->GetPhysicsLinearVelocity().Size() : 0.f;
			if (Speed < 40.f && CaseFramesLeft < Case.MaxFrames - 40 && !bCaseWentThrough)
			{
				FinishActiveCase(TEXT("rim-block-ok"));
				return;
			}
			if (CaseFramesLeft <= 0)
			{
				FinishActiveCase(bCaseWentThrough ? TEXT("unexpected-through") : TEXT("rim-block-ok"));
				return;
			}
		}
		else
		{
			if (Case.bExpectNoEntryGhost && bCaseGhostedEntry)
			{
				FinishActiveCase(TEXT("ghosted-entry"));
				return;
			}
			if (bCaseSank)
			{
				FinishActiveCase(TEXT("sank"));
				return;
			}
			if (CaseFramesLeft <= 0)
			{
				FinishActiveCase(TEXT("rest-ok"));
				return;
			}
		}
	}

	if (CasePhase == ECasePhase::BetweenCases)
	{
		--CaseArmFramesLeft;
		if (CaseArmFramesLeft <= 0)
		{
			BeginNextBatteryCaseOrFinish();
		}
	}
}

void APortalPhysicsProbe::FinishActiveCase(const TCHAR* Reason)
{
	if (!Presets.IsValidIndex(ActivePresetIndex))
	{
		CasePhase = ECasePhase::Idle;
		return;
	}

	const FPortalPhysicsProbeCase& Case = Presets[ActivePresetIndex];
	const bool bCaptureOk = !Case.bExpectCapture || bCaseCaptured;
	const bool bThroughOk = !Case.bExpectThrough || bCaseWentThrough;
	const bool bNoThroughOk = !Case.bExpectNoThrough || !bCaseWentThrough;
	// Mid-flight punch spikes are OK if the body ends blocked and not sunk.
	const bool bBlockOk = !Case.bExpectExitBlock
		|| (bCaseBlockedByExit && !bCaseSank
			&& (CaseMaxExitPenetrationCm <= ExitBlockPenetrationFailCm
				|| FCString::Strcmp(Reason, TEXT("blocked-ok")) == 0));
	const bool bBehindGhostOk = !Case.bExpectNoBehindEntryHit || !bCaseHitBehindEntry;
	const bool bEntryGhostOk = !Case.bExpectNoEntryGhost || !bCaseGhostedEntry;
	const bool bCloneSwapOk = !Case.bExpectCloneSwap
		|| (bCaseSawCloneAtExit && bCaseSawCloneAtEntryAfterSwap);
	// Rim cases: never allow through (already in bNoThroughOk) and never ghost past the face.
	const bool bRimOk = !Case.bExpectNoThrough || (!bCaseWentThrough && !bCaseGhostedEntry);
	const bool bPass = bCaptureOk && bThroughOk && bNoThroughOk && bBlockOk
		&& bBehindGhostOk && bEntryGhostOk && bCloneSwapOk && bRimOk && !bCaseSank;

	UE_LOG(LogPortalPhysicsProbe, Display,
		TEXT("PROBETEST RESULT %s: %s through=%s sank=%s maxSink=%.1fcm captured=%s blocked=%s maxExitPen=%.1fcm behindHit=%s ghost=%s cloneExit=%s cloneEntrySwap=%s maxBehindImpulse=%.1f reason=%s"),
		*Case.Name.ToString(),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bCaseWentThrough ? TEXT("yes") : TEXT("NO"),
		bCaseSank ? TEXT("YES") : TEXT("no"),
		CaseMaxSinkCm,
		bCaseCaptured ? TEXT("yes") : TEXT("NO"),
		bCaseBlockedByExit ? TEXT("yes") : TEXT("NO"),
		CaseMaxExitPenetrationCm,
		bCaseHitBehindEntry ? TEXT("YES") : TEXT("no"),
		bCaseGhostedEntry ? TEXT("YES") : TEXT("no"),
		bCaseSawCloneAtExit ? TEXT("yes") : TEXT("NO"),
		bCaseSawCloneAtEntryAfterSwap ? TEXT("yes") : TEXT("NO"),
		CaseMaxBehindEntryHitImpulse,
		Reason);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0x50524F53, 5.f, bPass ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("%s: %s (through=%s blocked=%s behindHit=%s sank=%s)"),
				*Case.Name.ToString(),
				bPass ? TEXT("PASS") : TEXT("FAIL"),
				bCaseWentThrough ? TEXT("yes") : TEXT("NO"),
				bCaseBlockedByExit ? TEXT("yes") : TEXT("NO"),
				bCaseHitBehindEntry ? TEXT("YES") : TEXT("no"),
				bCaseSank ? TEXT("YES") : TEXT("no")));
	}

	if (bBatteryMode)
	{
		++BatteryIndex;
		CasePhase = ECasePhase::BetweenCases;
		CaseArmFramesLeft = 10;
	}
	else
	{
		CasePhase = ECasePhase::Idle;
	}
}

void APortalPhysicsProbe::BeginNextBatteryCaseOrFinish()
{
	if (!bBatteryMode)
	{
		CasePhase = ECasePhase::Idle;
		return;
	}

	if (BatteryIndex >= Presets.Num())
	{
		CasePhase = ECasePhase::Finished;
		DestroyExitObstacle();
		DestroyBehindEntryObstacle();
		DestroySplitFloors();
		UE_LOG(LogPortalPhysicsProbe, Display, TEXT("PROBETEST BATTERY done (%d presets)"), Presets.Num());
		if (bAutoQuitBattery)
		{
			RequestQuitAfterDelay(1.5f);
		}
		return;
	}

	StartCase(BatteryIndex);
}

void APortalPhysicsProbe::RequestQuitAfterDelay(float Seconds)
{
	if (UWorld* World = GetWorld())
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, []()
		{
			FPlatformMisc::RequestExit(false);
		}, Seconds, false);
	}
}

bool APortalPhysicsProbe::BindPlacedActors()
{
	UWorld* World = GetWorld();
	EntryPortal = Cast<APortal>(FindLabeledActor(World, EntryLabel));
	ExitPortal = Cast<APortal>(FindLabeledActor(World, ExitLabel));
	ProbeCube = FindLabeledActor(World, CubeLabel);

	if (!EntryPortal || !ExitPortal || !ProbeCube)
	{
		return false;
	}

	CubeInitialTransform = ProbeCube->GetActorTransform();
	CubeBaseScale = ProbeCube->GetActorScale3D();

	EntryFloorTopZ = 0.f;
	ExitFloorTopZ = 0.f;
	if (UPrimitiveComponent* Prim = ProbeCube->FindComponentByClass<UPrimitiveComponent>())
	{
		CubeHalfHeight = Prim->Bounds.BoxExtent.Z;
		EntryFloorTopZ = Prim->GetComponentLocation().Z - CubeHalfHeight;
		ExitFloorTopZ = EntryFloorTopZ;
		PrepareCubePhysics();
	}

	UE_LOG(LogPortalPhysicsProbe, Display,
		TEXT("Bound portals. Entry=%s Exit=%s Cube=%s presets=%d"),
		*EntryPortal->GetName(), *ExitPortal->GetName(), *ProbeCube->GetName(), Presets.Num());
	return true;
}

void APortalPhysicsProbe::DumpFrame(bool bSimAdvanced)
{
	if (!ProbeCube || !EntryPortal)
	{
		return;
	}

	UPrimitiveComponent* Prim = ProbeCube->FindComponentByClass<UPrimitiveComponent>();
	const FVector Loc = Prim ? Prim->GetComponentLocation() : ProbeCube->GetActorLocation();
	const float Side = PlaneSide(EntryPortal, Loc);
	const float SinkCm = GetSinkCm(Loc);
	const bool bSank = SinkCm > SinkThresholdCm;
	const bool bBehind = EntryPortal->IsPointBehindPlane(Loc, 2.f);
	const AActor* Clone = EntryPortal->TeleportingObjects.FindRef(ProbeCube);
	const FVector LinVel = Prim ? Prim->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	const bool bPaused = GetWorld() && UGameplayStatics::IsGamePaused(GetWorld());
	const FName ActiveName = GetActivePresetName();
	const float ExitSide = GetCubeExitPlaneSide(Loc);

	const FString Line = FString::Printf(
		TEXT("Portal Probe [%s] f=%d%s\ncube=(%.1f, %.1f, %.1f)\nvel=(%.1f, %.1f, %.1f)\nside=%.1f exitSide=%.1f behind=%d\nsink=%.1f cm  sank=%s\nclone=%s pen=%.1f\n1-5 presets  T all  R/P/N"),
		*ActiveName.ToString(),
		FrameIndex,
		bPaused ? TEXT("  [PAUSED]") : TEXT(""),
		Loc.X, Loc.Y, Loc.Z,
		LinVel.X, LinVel.Y, LinVel.Z,
		Side,
		ExitSide,
		bBehind ? 1 : 0,
		SinkCm,
		bSank ? TEXT("YES") : TEXT("no"),
		Clone ? *Clone->GetName() : TEXT("none"),
		CaseMaxExitPenetrationCm);

	const bool bCaseSample = CasePhase == ECasePhase::Running && (FrameIndex % 5) == 0;
	if (bSimAdvanced && ((FrameIndex % 15) == 0 || bSank || bCaseSample))
	{
		UE_LOG(LogPortalPhysicsProbe, Display, TEXT("%s"), *Line.Replace(TEXT("\n"), TEXT(" | ")));
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0x50524F42, 0.f, bSank ? FColor::Red : FColor::Green, Line);
	}
}

void APortalPhysicsProbe::RunFromConsole(const TArray<FString>& Args, UWorld* World)
{
	if (!World)
	{
		return;
	}

	FPortalPhysicsProbeLaunch::SinkThresholdCm = 5.f;
	if (Args.Num() > 1)
	{
		FPortalPhysicsProbeLaunch::SinkThresholdCm = FCString::Atof(*Args[1]);
	}
	else if (Args.Num() > 0)
	{
		FPortalPhysicsProbeLaunch::SinkThresholdCm = FCString::Atof(*Args[0]);
	}
	FPortalPhysicsProbeLaunch::bRequested = true;
	TravelToProbeLevel(World);
}

void APortalPhysicsProbe::QuitFromConsole(const TArray<FString>& Args, UWorld* World)
{
	if (!World)
	{
		return;
	}

	const TCHAR* ReturnLevel = LevelExists(FPortalPhysicsProbeLaunch::ReturnLevelPath)
		? FPortalPhysicsProbeLaunch::ReturnLevelPath
		: FPortalPhysicsProbeLaunch::FallbackLevelPath;
	UGameplayStatics::OpenLevel(World, FName(ReturnLevel), true);
}

static FAutoConsoleCommandWithWorldAndArgs PortalPhysicsProbeCommand(
	TEXT("Portal.PhysicsProbe"),
	TEXT("Opens L_PortalPhysicsProbe. Prefer opening that map directly."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&APortalPhysicsProbe::RunFromConsole)
);

static FAutoConsoleCommandWithWorldAndArgs PortalPhysicsProbeQuitCommand(
	TEXT("Portal.PhysicsProbeQuit"),
	TEXT("Returns to FirstPersonMap."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&APortalPhysicsProbe::QuitFromConsole)
);
