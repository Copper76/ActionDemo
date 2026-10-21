#include "PortalFloorWalkZTest.h"

#include "PortalFloorWalkZTestLaunch.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogPortalFloorWalkZ, Log, All);

bool FPortalFloorWalkZTestLaunch::bRequested = false;
bool FPortalFloorWalkZTestLaunch::bAutoQuit = false;
float FPortalFloorWalkZTestLaunch::TimeLimitSec = 8.f;
float FPortalFloorWalkZTestLaunch::MaxStepCm = 1.5f;

namespace PortalFloorWalkZTestPrivate
{
	constexpr TCHAR EntryFloorLabel[] = TEXT("PortalFloorWalkZ_EntryFloor");
	constexpr TCHAR ExitFloorLabel[] = TEXT("PortalFloorWalkZ_ExitFloor");
	constexpr TCHAR ExitWallLabel[] = TEXT("PortalFloorWalkZ_ExitWall");
	constexpr float FloorTopZ = 400.f;
	constexpr float FloorSlabHalfH = 20.f;
	constexpr float SurfaceOffsetCm = 1.f;
	constexpr float ExitWallY = 600.f;
	constexpr float ExitWallCenterZ = 520.f;
	/** Start south of portal; walk +Y toward aperture center. */
	constexpr float StartY = -280.f;
	constexpr float WalkSpeedCmS = 400.f;
	constexpr float PortalReachXYCm = 40.f;

	bool LevelExists(const TCHAR* Path)
	{
		return FPackageName::DoesPackageExist(Path);
	}

	void TravelToTestLevel(UWorld* World)
	{
		const TCHAR* Level = LevelExists(FPortalFloorWalkZTestLaunch::LevelPath)
			? FPortalFloorWalkZTestLaunch::LevelPath
			: FPortalFloorWalkZTestLaunch::FallbackLevelPath;
		const FString Options = FString::Printf(TEXT("game=%s"), FPortalFloorWalkZTestLaunch::GameModePath);
		UGameplayStatics::OpenLevel(World, FName(Level), true, Options);
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
using namespace PortalFloorWalkZTestPrivate;

APortalFloorWalkZTest::APortalFloorWalkZTest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void APortalFloorWalkZTest::BeginPlay()
{
	Super::BeginPlay();

	if (FPortalFloorWalkZTestLaunch::bRequested)
	{
		TimeLimitSec = FPortalFloorWalkZTestLaunch::TimeLimitSec;
		MaxStepCm = FPortalFloorWalkZTestLaunch::MaxStepCm;
		bAutoQuit = FPortalFloorWalkZTestLaunch::bAutoQuit;
		FPortalFloorWalkZTestLaunch::bRequested = false;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PortalFloorWalkZTest")))
	{
		bAutoQuit = true;
		FParse::Value(FCommandLine::Get(), TEXT("FloorWalkTime="), TimeLimitSec);
		FParse::Value(FCommandLine::Get(), TEXT("FloorWalkMaxStep="), MaxStepCm);
	}

	if (!BuildScene())
	{
		Finish(false, TEXT("BuildScene failed"));
		return;
	}

	Phase = EPhase::Running;
	ElapsedSec = 0.f;
	UE_LOG(LogPortalFloorWalkZ, Display,
		TEXT("FLOORWALKZ ready: walk +Y onto floor portal. MaxStep=%.2fcm TimeLimit=%.1f ExpectedFloorZ≈%.1f (plane-1)"),
		MaxStepCm, TimeLimitSec, FloorTopZ);
}

void APortalFloorWalkZTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyScene();
	Super::EndPlay(EndPlayReason);
}

void APortalFloorWalkZTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Phase != EPhase::Running || !TestCharacter || !FloorPortal)
	{
		return;
	}

	ElapsedSec += DeltaTime;
	DriveWalk(DeltaTime);
	SampleZ();

	const FVector Loc = TestCharacter->GetActorLocation();
	if (FVector2D(Loc.X, Loc.Y).Size() <= PortalReachXYCm)
	{
		bReachedPortalXY = true;
	}

	// After crossing onto the portal XY, give a short settle then judge.
	if (bReachedPortalXY && ElapsedSec > 1.5f)
	{
		const bool bPass = MaxRiseCm <= MaxStepCm;
		Finish(bPass, *FString::Printf(
			TEXT("reached portal XY; maxRise=%.2fcm (limit %.2f) at floor=[%s] Zrange=[%.2f .. %.2f] baseline=%.2f"),
			MaxRiseCm, MaxStepCm, *FloorAtMaxRise, MinZ, MaxZ, BaselineZ));
		return;
	}

	if (ElapsedSec >= TimeLimitSec)
	{
		Finish(false, *FString::Printf(
			TEXT("timeout reachedPortal=%d maxRise=%.2fcm floorAtMax=[%s] Zrange=[%.2f .. %.2f]"),
			bReachedPortalXY ? 1 : 0, MaxRiseCm, *FloorAtMaxRise, MinZ, MaxZ));
	}
}

void APortalFloorWalkZTest::DriveWalk(float DeltaTime)
{
	// Constant walk toward portal center (world +Y).
	TestCharacter->AddMovementInput(FVector::YAxisVector, 1.f);
	if (UCharacterMovementComponent* Move = TestCharacter->GetCharacterMovement())
	{
		// Keep planar speed predictable for the sample window.
		Move->MaxWalkSpeed = WalkSpeedCmS;
	}
}

void APortalFloorWalkZTest::SampleZ()
{
	const FVector Loc = TestCharacter->GetActorLocation();
	float HalfH = 96.f;
	if (UCapsuleComponent* Cap = TestCharacter->GetCapsuleComponent())
	{
		HalfH = Cap->GetScaledCapsuleHalfHeight();
	}
	const float FeetZ = Loc.Z - HalfH;

	if (!bBaselineSet)
	{
		// Wait until grounded before locking baseline.
		const UCharacterMovementComponent* Move = TestCharacter->GetCharacterMovement();
		if (Move && Move->IsMovingOnGround())
		{
			BaselineZ = Loc.Z;
			MinZ = Loc.Z;
			MaxZ = Loc.Z;
			bBaselineSet = true;
			UE_LOG(LogPortalFloorWalkZ, Display,
				TEXT("FLOORWALKZ baseline capsuleZ=%.3f feetZ=%.3f floorTop=%.1f planeZ≈%.1f"),
				BaselineZ, FeetZ, FloorTopZ, FloorTopZ + SurfaceOffsetCm);
		}
		return;
	}

	MinZ = FMath::Min(MinZ, Loc.Z);
	MaxZ = FMath::Max(MaxZ, Loc.Z);
	const float Rise = Loc.Z - BaselineZ;
	if (Rise > MaxRiseCm)
	{
		MaxRiseCm = Rise;
		const UCharacterMovementComponent* Move = TestCharacter->GetCharacterMovement();
		FloorAtMaxRise = (Move && Move->CurrentFloor.bBlockingHit)
			? CompName(Move->CurrentFloor.HitResult.GetComponent())
			: TEXT("nofloor");
	}

	FString FloorInfo = TEXT("nofloor");
	float FloorDist = -1.f;
	if (const UCharacterMovementComponent* Move = TestCharacter->GetCharacterMovement())
	{
		if (Move->CurrentFloor.bBlockingHit)
		{
			FloorDist = Move->CurrentFloor.FloorDist;
			FloorInfo = CompName(Move->CurrentFloor.HitResult.GetComponent());
		}
	}

	const FString Line = FString::Printf(
		TEXT("FLOORWALKZ t=%.2f y=%.1f capZ=%.3f feetZ=%.3f dZ=%.3f floorDist=%.2f floor=[%s] portal=%s"),
		ElapsedSec,
		Loc.Y,
		Loc.Z,
		FeetZ,
		Rise,
		FloorDist,
		*FloorInfo,
		TestCharacter->GetCurrentPortal() ? *TestCharacter->GetCurrentPortal()->GetName() : TEXT("null"));

	UE_LOG(LogPortalFloorWalkZ, Display, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(uint64(0xF100A012), 0.f, FColor::Cyan, Line);
	}
}

bool APortalFloorWalkZTest::BuildScene()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	DestroyScene();

	const TSubclassOf<APortal> PortalClass = LoadPortalClass();
	if (!PortalClass)
	{
		UE_LOG(LogPortalFloorWalkZ, Error, TEXT("FLOORWALKZ missing portal class"));
		return false;
	}

	EntryFloor = SpawnFloorSlab(
		FVector(0.f, 0.f, FloorTopZ - FloorSlabHalfH),
		FloorTopZ,
		FVector(12.f, 12.f, FloorSlabHalfH / 50.f),
		EntryFloorLabel);
	if (!EntryFloor)
	{
		return false;
	}

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

	const FVector FloorPortalLoc = FVector(0.f, 0.f, FloorTopZ + SurfaceOffsetCm);
	const FRotator FloorPortalRot = FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator();
	FloorPortal = World->SpawnActor<APortal>(PortalClass, FloorPortalLoc, FloorPortalRot, PortalParams);
	if (!FloorPortal)
	{
		return false;
	}
	FloorPortal->SetActorLabel(TEXT("PortalFloorWalkZ_Floor"));

	const FVector ExitPortalLoc = FVector(0.f, ExitWallY - 10.f - SurfaceOffsetCm, ExitWallCenterZ);
	const FRotator ExitPortalRot = FRotationMatrix::MakeFromXZ(-FVector::YAxisVector, FVector::UpVector).Rotator();
	ExitPortal = World->SpawnActor<APortal>(PortalClass, ExitPortalLoc, ExitPortalRot, PortalParams);
	if (!ExitPortal)
	{
		return false;
	}
	ExitPortal->SetActorLabel(TEXT("PortalFloorWalkZ_Exit"));

	FloorPortal->Setup(EntryFloor, true, nullptr);
	ExitPortal->Setup(ExitWall, false, FloorPortal);

	APlayerController* PC = World->GetFirstPlayerController();
	AActionDemoCharacter* Character = PC ? Cast<AActionDemoCharacter>(PC->GetPawn()) : nullptr;
	if (!Character)
	{
		const TSubclassOf<AActionDemoCharacter> CharClass = LoadCharacterClass();
		if (!CharClass || !PC)
		{
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
	FloorPortal->LocalPlayer = Character;
	FloorPortal->PlayerController = PC;
	ExitPortal->LocalPlayer = Character;
	ExitPortal->PlayerController = PC;
	PlaceAndAimCharacter();

	if (UCapsuleComponent* Cap = Character->GetCapsuleComponent())
	{
		Cap->UpdateOverlaps();
	}

	UE_LOG(LogPortalFloorWalkZ, Display,
		TEXT("FLOORWALKZ scene: floorPortal=%s startY=%.0f floorTop=%.0f"),
		*FloorPortal->GetName(), StartY, FloorTopZ);
	return true;
}

void APortalFloorWalkZTest::DestroyScene()
{
	auto Kill = [](AActor* Actor)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	};
	Kill(FloorPortal);
	Kill(ExitPortal);
	Kill(EntryFloor);
	Kill(ExitFloor);
	Kill(ExitWall);
	FloorPortal = nullptr;
	ExitPortal = nullptr;
	EntryFloor = nullptr;
	ExitFloor = nullptr;
	ExitWall = nullptr;
}

AActor* APortalFloorWalkZTest::SpawnFloorSlab(const FVector& Center, float TopZ, const FVector& ScaleXYH, const TCHAR* Label)
{
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

AActor* APortalFloorWalkZTest::SpawnWall(const FVector& Center, const FRotator& Rotation, const FVector& Scale, const TCHAR* Label)
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

TSubclassOf<APortal> APortalFloorWalkZTest::LoadPortalClass() const
{
	if (UClass* BP = LoadClass<APortal>(nullptr, TEXT("/Game/PortalGun/BP_Portal.BP_Portal_C")))
	{
		return BP;
	}
	return APortal::StaticClass();
}

TSubclassOf<AActionDemoCharacter> APortalFloorWalkZTest::LoadCharacterClass() const
{
	if (UClass* BP = LoadClass<AActionDemoCharacter>(nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C")))
	{
		return BP;
	}
	return AActionDemoCharacter::StaticClass();
}

void APortalFloorWalkZTest::PlaceAndAimCharacter()
{
	if (!TestCharacter)
	{
		return;
	}

	float Radius = 42.f;
	float HalfHeight = 96.f;
	if (UCapsuleComponent* Cap = TestCharacter->GetCapsuleComponent())
	{
		Cap->GetScaledCapsuleSize(Radius, HalfHeight);
	}

	const FVector SpawnLoc(0.f, StartY, FloorTopZ + HalfHeight + 2.f);
	TestCharacter->SetActorLocationAndRotation(SpawnLoc, FRotator(0.f, 90.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Move = TestCharacter->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->SetMovementMode(MOVE_Walking);
		Move->MaxWalkSpeed = WalkSpeedCmS;
	}
	if (APlayerController* PC = Cast<APlayerController>(TestCharacter->GetController()))
	{
		PC->SetControlRotation(FRotator(-15.f, 90.f, 0.f));
	}
}

void APortalFloorWalkZTest::Finish(bool bPass, const TCHAR* Reason)
{
	if (Phase == EPhase::Finished)
	{
		return;
	}
	Phase = EPhase::Finished;

	const FString Msg = FString::Printf(
		TEXT("FLOORWALKZ %s: %s"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		Reason);
	UE_LOG(LogPortalFloorWalkZ, Display, TEXT("%s"), *Msg);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(uint64(0xF100A012), 30.f, bPass ? FColor::Green : FColor::Red, Msg);
	}

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

void APortalFloorWalkZTest::RunFromConsole(const TArray<FString>& Args, UWorld* World)
{
	if (!World)
	{
		return;
	}

	FPortalFloorWalkZTestLaunch::bRequested = true;
	FPortalFloorWalkZTestLaunch::bAutoQuit = false;
	FPortalFloorWalkZTestLaunch::TimeLimitSec = 8.f;
	FPortalFloorWalkZTestLaunch::MaxStepCm = 1.5f;

	for (const FString& Arg : Args)
	{
		if (Arg.Equals(TEXT("auto"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
		{
			FPortalFloorWalkZTestLaunch::bAutoQuit = true;
		}
		else if (Arg.StartsWith(TEXT("time="), ESearchCase::IgnoreCase))
		{
			FPortalFloorWalkZTestLaunch::TimeLimitSec = FCString::Atof(*Arg.Mid(5));
		}
		else if (Arg.StartsWith(TEXT("maxstep="), ESearchCase::IgnoreCase))
		{
			FPortalFloorWalkZTestLaunch::MaxStepCm = FCString::Atof(*Arg.Mid(8));
		}
	}

	UE_LOG(LogPortalFloorWalkZ, Display,
		TEXT("FLOORWALKZ launching (time=%.1f maxstep=%.2f autoQuit=%d)"),
		FPortalFloorWalkZTestLaunch::TimeLimitSec,
		FPortalFloorWalkZTestLaunch::MaxStepCm,
		FPortalFloorWalkZTestLaunch::bAutoQuit ? 1 : 0);
	TravelToTestLevel(World);
}

void APortalFloorWalkZTest::QuitFromConsole(const TArray<FString>& Args, UWorld* World)
{
	if (World)
	{
		UGameplayStatics::OpenLevel(World, FName(FPortalFloorWalkZTestLaunch::ReturnLevelPath));
	}
}

static FAutoConsoleCommandWithWorldAndArgs PortalFloorWalkZTestCommand(
	TEXT("Portal.FloorWalkZTest"),
	TEXT("Walk onto a floor portal and track capsule Z. Args: time=<sec> maxstep=<cm> auto|quit"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&APortalFloorWalkZTest::RunFromConsole)
);

static FAutoConsoleCommandWithWorldAndArgs PortalFloorWalkZTestQuitCommand(
	TEXT("Portal.FloorWalkZTestQuit"),
	TEXT("Returns to FirstPersonMap."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&APortalFloorWalkZTest::QuitFromConsole)
);
