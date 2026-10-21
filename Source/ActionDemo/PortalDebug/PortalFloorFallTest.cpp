#include "PortalFloorFallTest.h"

#include "PortalFloorFallTestLaunch.h"
#include "ActionDemo/ActionDemoCharacter.h"
#include "Portal.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogPortalFloorFall, Log, All);

bool FPortalFloorFallTestLaunch::bRequested = false;
bool FPortalFloorFallTestLaunch::bAutoQuit = false;
float FPortalFloorFallTestLaunch::DropHeightCm = 0.f;
float FPortalFloorFallTestLaunch::TimeLimitSec = 6.f;

namespace PortalFloorFallTestPrivate
{
	constexpr TCHAR EntryFloorLabel[] = TEXT("PortalFloorFall_EntryFloor");
	constexpr TCHAR ExitFloorLabel[] = TEXT("PortalFloorFall_ExitFloor");
	constexpr TCHAR ExitWallLabel[] = TEXT("PortalFloorFall_ExitWall");
	constexpr float FloorTopZ = 400.f;
	constexpr float FloorSlabHalfH = 20.f;
	constexpr float SurfaceOffsetCm = 1.f;
	/** Entry portal at origin; exit wall stands clear so a through-fall is obvious. */
	constexpr float ExitWallY = 600.f;
	constexpr float ExitWallCenterZ = 520.f;

	bool LevelExists(const TCHAR* Path)
	{
		return FPackageName::DoesPackageExist(Path);
	}

	void TravelToTestLevel(UWorld* World)
	{
		const TCHAR* Level = LevelExists(FPortalFloorFallTestLaunch::LevelPath)
			? FPortalFloorFallTestLaunch::LevelPath
			: FPortalFloorFallTestLaunch::FallbackLevelPath;
		const FString Options = FString::Printf(TEXT("game=%s"), FPortalFloorFallTestLaunch::GameModePath);
		UGameplayStatics::OpenLevel(World, FName(Level), true, Options);
	}

	float PlaneSide(const APortal* Portal, const FVector& Point)
	{
		if (!Portal)
		{
			return 0.f;
		}
		return FVector::DotProduct(Point - Portal->GetPlaneOrigin(), Portal->GetActorForwardVector());
	}

	FString CompName(const UPrimitiveComponent* Comp)
	{
		if (!Comp)
		{
			return TEXT("none");
		}
		const AActor* Owner = Comp->GetOwner();
		return FString::Printf(TEXT("%s.%s"),
			Owner ? *Owner->GetName() : TEXT("?"),
			*Comp->GetName());
	}
}

APortalFloorFallTest::APortalFloorFallTest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void APortalFloorFallTest::BeginPlay()
{
	using namespace PortalFloorFallTestPrivate;
	Super::BeginPlay();

	if (FPortalFloorFallTestLaunch::bRequested)
	{
		DropHeightCm = FPortalFloorFallTestLaunch::DropHeightCm;
		TimeLimitSec = FPortalFloorFallTestLaunch::TimeLimitSec;
		bAutoQuit = FPortalFloorFallTestLaunch::bAutoQuit;
		FPortalFloorFallTestLaunch::bRequested = false;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PortalFloorFallTest")))
	{
		bAutoQuit = true;
		FParse::Value(FCommandLine::Get(), TEXT("FloorFallDrop="), DropHeightCm);
		FParse::Value(FCommandLine::Get(), TEXT("FloorFallTime="), TimeLimitSec);
	}

	if (!BuildScene())
	{
		Finish(false, TEXT("BuildScene failed"));
		return;
	}

	Phase = EPhase::Running;
	ElapsedSec = 0.f;
	UE_LOG(LogPortalFloorFall, Display,
		TEXT("FLOORFALL ready: stand/drop onto floor portal center. DropHeight=%.0f TimeLimit=%.1f"),
		DropHeightCm, TimeLimitSec);
}

void APortalFloorFallTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyScene();
	Super::EndPlay(EndPlayReason);
}

void APortalFloorFallTest::Tick(float DeltaTime)
{
	using namespace PortalFloorFallTestPrivate;
	Super::Tick(DeltaTime);

	if (Phase != EPhase::Running || !FloorPortal || !TestCharacter)
	{
		return;
	}

	ElapsedSec += DeltaTime;

	const FVector Loc = TestCharacter->GetActorLocation();
	const float Side = PlaneSide(FloorPortal, Loc);
	MinPlaneSide = FMath::Min(MinPlaneSide, Side);

	APortal* Current = TestCharacter->GetCurrentPortal();
	if (Current == FloorPortal)
	{
		bEverHadCurrentPortal = true;
	}
	if (Side <= -2.f)
	{
		bEverBehindPlane = true;
	}

	UCharacterMovementComponent* Move = TestCharacter->GetCharacterMovement();
	if (Move && Move->CurrentFloor.bBlockingHit)
	{
		LastFloorCompName = CompName(Move->CurrentFloor.HitResult.GetComponent());
	}

	DumpFrame(ElapsedSec);

	const bool bNearExit = ExitPortal
		&& FVector::DistSquared(Loc, ExitPortal->GetActorLocation()) < FMath::Square(250.f)
		&& PlaneSide(ExitPortal, Loc) > 10.f;

	if (bNearExit || (bEverBehindPlane && Current != FloorPortal && Side < -50.f))
	{
		Finish(true, TEXT("fell through / reached exit"));
		return;
	}

	if (bEverBehindPlane && Side < -2.f && ElapsedSec > 0.35f)
	{
		// Capsule center behind floor plane — hole opened even if teleport pending.
		Finish(true, TEXT("capsule behind floor portal plane"));
		return;
	}

	if (ElapsedSec >= TimeLimitSec)
	{
		Finish(false, *FString::Printf(
			TEXT("timeout: CurrentPortal=%s everCurrent=%d minSide=%.1f floor=%s mode=%s"),
			Current ? *Current->GetName() : TEXT("null"),
			bEverHadCurrentPortal ? 1 : 0,
			MinPlaneSide,
			*LastFloorCompName,
			Move ? *UEnum::GetValueAsString(Move->MovementMode) : TEXT("?")));
	}
}

bool APortalFloorFallTest::BuildScene()
{
	using namespace PortalFloorFallTestPrivate;
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	DestroyScene();

	const TSubclassOf<APortal> PortalClass = LoadPortalClass();
	if (!PortalClass)
	{
		UE_LOG(LogPortalFloorFall, Error, TEXT("FLOORFALL missing portal class (BP_Portal / APortal)"));
		return false;
	}

	// Entry floor under origin.
	EntryFloor = SpawnFloorSlab(
		FVector(0.f, 0.f, FloorTopZ - FloorSlabHalfH),
		FloorTopZ,
		FVector(12.f, 12.f, FloorSlabHalfH / 50.f),
		EntryFloorLabel);
	if (!EntryFloor)
	{
		return false;
	}

	// Exit room: wall + landing floor so a successful transit is visible.
	ExitWall = SpawnWall(
		FVector(0.f, ExitWallY, ExitWallCenterZ),
		FRotator::ZeroRotator,
		FVector(4.f, 0.2f, 4.f),
		ExitWallLabel);
	ExitFloor = SpawnFloorSlab(
		FVector(0.f, ExitWallY + 200.f, FloorTopZ - FloorSlabHalfH),
		FloorTopZ,
		FVector(6.f, 6.f, FloorSlabHalfH / 50.f),
		ExitFloorLabel);

	FActorSpawnParameters PortalParams;
	PortalParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Floor portal: forward = world up (enter by going down).
	const FVector FloorPortalLoc = FVector(0.f, 0.f, FloorTopZ + SurfaceOffsetCm);
	const FRotator FloorPortalRot = FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator();
	FloorPortal = World->SpawnActor<APortal>(PortalClass, FloorPortalLoc, FloorPortalRot, PortalParams);
	if (!FloorPortal)
	{
		UE_LOG(LogPortalFloorFall, Error, TEXT("FLOORFALL failed to spawn floor portal"));
		return false;
	}
	FloorPortal->SetActorLabel(TEXT("PortalFloorFall_Floor"));

	// Exit portal on the -Y face of the wall (facing toward entry / -Y).
	const FVector ExitPortalLoc = FVector(0.f, ExitWallY - 10.f - SurfaceOffsetCm, ExitWallCenterZ);
	const FRotator ExitPortalRot = FRotationMatrix::MakeFromXZ(-FVector::YAxisVector, FVector::UpVector).Rotator();
	ExitPortal = World->SpawnActor<APortal>(PortalClass, ExitPortalLoc, ExitPortalRot, PortalParams);
	if (!ExitPortal)
	{
		UE_LOG(LogPortalFloorFall, Error, TEXT("FLOORFALL failed to spawn exit portal"));
		return false;
	}
	ExitPortal->SetActorLabel(TEXT("PortalFloorFall_Exit"));

	FloorPortal->Setup(EntryFloor, true, nullptr);
	ExitPortal->Setup(ExitWall, false, FloorPortal);

	APlayerController* PC = World->GetFirstPlayerController();
	AActionDemoCharacter* Character = PC ? Cast<AActionDemoCharacter>(PC->GetPawn()) : nullptr;
	if (!Character)
	{
		const TSubclassOf<AActionDemoCharacter> CharClass = LoadCharacterClass();
		if (!CharClass || !PC)
		{
			UE_LOG(LogPortalFloorFall, Error, TEXT("FLOORFALL no player character / controller"));
			return false;
		}

		FActorSpawnParameters CharParams;
		CharParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Character = World->SpawnActor<AActionDemoCharacter>(CharClass, FVector::ZeroVector, FRotator::ZeroRotator, CharParams);
		if (!Character)
		{
			return false;
		}
		PC->Possess(Character);
	}

	TestCharacter = Character;
	BindPortalLocals(Character, PC);
	PlaceCharacterAboveFloorPortal();

	// Ensure reverse-collider overlap fires after teleport into the volume.
	if (UCapsuleComponent* Cap = Character->GetCapsuleComponent())
	{
		Cap->UpdateOverlaps();
	}

	UE_LOG(LogPortalFloorFall, Display,
		TEXT("FLOORFALL scene: floorPortal=%s exit=%s char=%s floorTop=%.0f"),
		*FloorPortal->GetName(), *ExitPortal->GetName(), *Character->GetName(), FloorTopZ);
	return true;
}

void APortalFloorFallTest::DestroyScene()
{
	auto KillActor = [](AActor* Actor)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	};

	KillActor(FloorPortal);
	KillActor(ExitPortal);
	KillActor(EntryFloor);
	KillActor(ExitFloor);
	KillActor(ExitWall);
	FloorPortal = nullptr;
	ExitPortal = nullptr;
	EntryFloor = nullptr;
	ExitFloor = nullptr;
	ExitWall = nullptr;
	// Leave TestCharacter — owned by player controller / game.
}

AActor* APortalFloorFallTest::SpawnFloorSlab(const FVector& Center, float TopZ, const FVector& ScaleXYH, const TCHAR* Label)
{
	using namespace PortalFloorFallTestPrivate;
	UWorld* World = GetWorld();
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!World || !CubeMesh)
	{
		return nullptr;
	}

	FVector Loc = Center;
	Loc.Z = TopZ - FloorSlabHalfH;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Slab = World->SpawnActor<AStaticMeshActor>(Loc, FRotator::ZeroRotator, Params);
	if (!Slab)
	{
		return nullptr;
	}

	Slab->SetActorLabel(Label);
	Slab->SetActorScale3D(ScaleXYH);
	if (UStaticMeshComponent* Mesh = Slab->GetStaticMeshComponent())
	{
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(CubeMesh);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetSimulatePhysics(false);
	}
	return Slab;
}

AActor* APortalFloorFallTest::SpawnWall(const FVector& Center, const FRotator& Rotation, const FVector& Scale, const TCHAR* Label)
{
	UWorld* World = GetWorld();
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!World || !CubeMesh)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(Center, Rotation, Params);
	if (!Wall)
	{
		return nullptr;
	}

	Wall->SetActorLabel(Label);
	Wall->SetActorScale3D(Scale);
	if (UStaticMeshComponent* Mesh = Wall->GetStaticMeshComponent())
	{
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(CubeMesh);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetSimulatePhysics(false);
	}
	return Wall;
}

TSubclassOf<APortal> APortalFloorFallTest::LoadPortalClass() const
{
	if (UClass* BP = LoadClass<APortal>(nullptr, TEXT("/Game/PortalGun/BP_Portal.BP_Portal_C")))
	{
		return BP;
	}
	return APortal::StaticClass();
}

TSubclassOf<AActionDemoCharacter> APortalFloorFallTest::LoadCharacterClass() const
{
	if (UClass* BP = LoadClass<AActionDemoCharacter>(nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C")))
	{
		return BP;
	}
	return AActionDemoCharacter::StaticClass();
}

void APortalFloorFallTest::PlaceCharacterAboveFloorPortal()
{
	using namespace PortalFloorFallTestPrivate;
	if (!TestCharacter || !FloorPortal)
	{
		return;
	}

	float Radius = 42.f;
	float HalfHeight = 96.f;
	if (UCapsuleComponent* Cap = TestCharacter->GetCapsuleComponent())
	{
		Cap->GetScaledCapsuleSize(Radius, HalfHeight);
	}

	// Center of aperture, feet on floor (or DropHeight above). Portal forward = up.
	const FVector PlaneOrigin = FloorPortal->GetPlaneOrigin();
	const FVector SpawnLoc = PlaneOrigin
		+ FVector::UpVector * (HalfHeight + DropHeightCm);

	TestCharacter->SetActorLocationAndRotation(SpawnLoc, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Move = TestCharacter->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->Velocity = FVector::ZeroVector;
		Move->SetMovementMode(DropHeightCm > KINDA_SMALL_NUMBER ? MOVE_Falling : MOVE_Walking);
	}

	if (APlayerController* PC = Cast<APlayerController>(TestCharacter->GetController()))
	{
		PC->SetControlRotation(FRotator(-40.f, 0.f, 0.f));
	}

	UE_LOG(LogPortalFloorFall, Display,
		TEXT("FLOORFALL placed character at %s (halfH=%.0f drop=%.0f)"),
		*SpawnLoc.ToCompactString(), HalfHeight, DropHeightCm);
}

void APortalFloorFallTest::BindPortalLocals(AActionDemoCharacter* Character, APlayerController* PC)
{
	if (FloorPortal)
	{
		FloorPortal->LocalPlayer = Character;
		FloorPortal->PlayerController = PC;
	}
	if (ExitPortal)
	{
		ExitPortal->LocalPlayer = Character;
		ExitPortal->PlayerController = PC;
	}
}

void APortalFloorFallTest::DumpFrame(float InElapsedSec) const
{
	using namespace PortalFloorFallTestPrivate;
	if (!TestCharacter || !FloorPortal)
	{
		return;
	}

	const FVector Loc = TestCharacter->GetActorLocation();
	const float Side = PlaneSide(FloorPortal, Loc);
	APortal* Current = TestCharacter->GetCurrentPortal();

	UCharacterMovementComponent* Move = TestCharacter->GetCharacterMovement();
	FString FloorInfo = TEXT("nofloor");
	FString Mode = TEXT("?");
	if (Move)
	{
		Mode = UEnum::GetValueAsString(Move->MovementMode);
		if (Move->CurrentFloor.bBlockingHit)
		{
			FloorInfo = FString::Printf(TEXT("%s dist=%.1f walk=%d"),
				*CompName(Move->CurrentFloor.HitResult.GetComponent()),
				Move->CurrentFloor.FloorDist,
				Move->CurrentFloor.IsWalkableFloor() ? 1 : 0);
		}
	}

	const FString Line = FString::Printf(
		TEXT("FLOORFALL t=%.2f side=%.1f current=%s mode=%s floor=[%s] loc=%s"),
		InElapsedSec,
		Side,
		Current ? *Current->GetName() : TEXT("null"),
		*Mode,
		*FloorInfo,
		*Loc.ToCompactString());

	UE_LOG(LogPortalFloorFall, Display, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(uint64(0xF100FALL), 0.f, FColor::Yellow, Line);
	}
}

void APortalFloorFallTest::Finish(bool bPass, const TCHAR* Reason)
{
	if (Phase == EPhase::Passed || Phase == EPhase::Failed || Phase == EPhase::Finished)
	{
		return;
	}

	Phase = bPass ? EPhase::Passed : EPhase::Failed;
	const FString Msg = FString::Printf(
		TEXT("FLOORFALL %s: %s (everCurrent=%d minSide=%.1f lastFloor=%s)"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		Reason,
		bEverHadCurrentPortal ? 1 : 0,
		MinPlaneSide,
		*LastFloorCompName);

	UE_LOG(LogPortalFloorFall, Display, TEXT("%s"), *Msg);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(uint64(0xF100FALL), 30.f, bPass ? FColor::Green : FColor::Red, Msg);
	}

	Phase = EPhase::Finished;

	if (bAutoQuit)
	{
		if (UWorld* World = GetWorld())
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([]()
			{
				FPlatformMisc::RequestExit(false);
			}), 1.0f, false);
		}
	}
}

void APortalFloorFallTest::RunFromConsole(const TArray<FString>& Args, UWorld* World)
{
	using namespace PortalFloorFallTestPrivate;
	if (!World)
	{
		return;
	}

	FPortalFloorFallTestLaunch::bRequested = true;
	FPortalFloorFallTestLaunch::bAutoQuit = false;
	FPortalFloorFallTestLaunch::DropHeightCm = 0.f;
	FPortalFloorFallTestLaunch::TimeLimitSec = 6.f;

	for (const FString& Arg : Args)
	{
		if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
		{
			FPortalFloorFallTestLaunch::bAutoQuit = true;
		}
		else if (Arg.StartsWith(TEXT("drop="), ESearchCase::IgnoreCase))
		{
			FPortalFloorFallTestLaunch::DropHeightCm = FCString::Atof(*Arg.Mid(5));
		}
		else if (Arg.StartsWith(TEXT("time="), ESearchCase::IgnoreCase))
		{
			FPortalFloorFallTestLaunch::TimeLimitSec = FCString::Atof(*Arg.Mid(5));
		}
	}

	UE_LOG(LogPortalFloorFall, Display,
		TEXT("FLOORFALL launching (drop=%.0f time=%.1f autoQuit=%d)"),
		FPortalFloorFallTestLaunch::DropHeightCm,
		FPortalFloorFallTestLaunch::TimeLimitSec,
		FPortalFloorFallTestLaunch::bAutoQuit ? 1 : 0);
	TravelToTestLevel(World);
}

void APortalFloorFallTest::QuitFromConsole(const TArray<FString>& Args, UWorld* World)
{
	if (World)
	{
		UGameplayStatics::OpenLevel(World, FName(FPortalFloorFallTestLaunch::ReturnLevelPath));
	}
}

static FAutoConsoleCommandWithWorldAndArgs PortalFloorFallTestCommand(
	TEXT("Portal.FloorFallTest"),
	TEXT("Linked floor portal under player. Args: drop=<cm> time=<sec> auto|quit"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&APortalFloorFallTest::RunFromConsole)
);

static FAutoConsoleCommandWithWorldAndArgs PortalFloorFallTestQuitCommand(
	TEXT("Portal.FloorFallTestQuit"),
	TEXT("Returns to FirstPersonMap."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&APortalFloorFallTest::QuitFromConsole)
);
