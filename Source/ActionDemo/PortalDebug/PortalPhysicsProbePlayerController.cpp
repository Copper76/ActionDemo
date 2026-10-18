#include "PortalPhysicsProbePlayerController.h"

#include "PortalPhysicsProbe.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

APortalPhysicsProbePlayerController::APortalPhysicsProbePlayerController()
{
	bShowMouseCursor = false;
	bShouldPerformFullTickWhenPaused = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void APortalPhysicsProbePlayerController::BeginPlay()
{
	Super::BeginPlay();
	RefreshHelpMessage();
}

void APortalPhysicsProbePlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RefreshHelpMessage();
}

void APortalPhysicsProbePlayerController::SetProbe(APortalPhysicsProbe* InProbe)
{
	Probe = InProbe;
}

void APortalPhysicsProbePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}

	auto BindPausedKey = [this](const FKey& Key, int32 PresetIndex)
	{
		FInputKeyBinding Bind(FInputChord(Key), IE_Pressed);
		Bind.bExecuteWhenPaused = true;
		Bind.KeyDelegate.GetDelegateForManualSet() = FInputActionHandlerSignature::CreateLambda([this, PresetIndex]()
		{
			RunPresetN(PresetIndex);
		});
		InputComponent->KeyBindings.Add(Bind);
	};

	auto BindPausedAction = [this](const FKey& Key, void (APortalPhysicsProbePlayerController::*Fn)())
	{
		FInputKeyBinding& Bind = InputComponent->BindKey(Key, IE_Pressed, this, Fn);
		Bind.bExecuteWhenPaused = true;
	};

	BindPausedAction(EKeys::R, &APortalPhysicsProbePlayerController::RestartSimulation);
	BindPausedAction(EKeys::P, &APortalPhysicsProbePlayerController::TogglePauseSim);
	BindPausedAction(EKeys::N, &APortalPhysicsProbePlayerController::StepOneFrame);
	BindPausedAction(EKeys::T, &APortalPhysicsProbePlayerController::RunAllPresets);

	BindPausedKey(EKeys::One, 1);
	BindPausedKey(EKeys::Two, 2);
	BindPausedKey(EKeys::Three, 3);
	BindPausedKey(EKeys::Four, 4);
	BindPausedKey(EKeys::Five, 5);
	BindPausedKey(EKeys::Six, 6);
	BindPausedKey(EKeys::Seven, 7);
	BindPausedKey(EKeys::Eight, 8);
	BindPausedKey(EKeys::Nine, 9);
	BindPausedKey(EKeys::Zero, 10);
}

void APortalPhysicsProbePlayerController::RefreshHelpMessage() const
{
	if (!GEngine)
	{
		return;
	}

	const FName Active = Probe ? Probe->GetActivePresetName() : NAME_None;
	const int32 Count = Probe ? Probe->GetPresetCount() : 0;
	GEngine->AddOnScreenDebugMessage(0x50524F50, 0.f, FColor::Cyan,
		FString::Printf(
			TEXT("T = full matrix (%d)  | active=%s\nshapes: small/long/wide/wideAngle × motion/exit/spin/behind/floor\nwide=rim-block  wideAngle=through  R restart  P pause  N step"),
			Count, *Active.ToString()));
}

void APortalPhysicsProbePlayerController::RestartSimulation()
{
	if (Probe)
	{
		Probe->RestartSimulation();
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0x50524F51, 1.5f, FColor::Yellow, TEXT("Simulation restarted"));
	}
	RefreshHelpMessage();
}

void APortalPhysicsProbePlayerController::TogglePauseSim()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bSingleFrameStep = false;
	const bool bPaused = !UGameplayStatics::IsGamePaused(World);
	UGameplayStatics::SetGamePaused(World, bPaused);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0x50524F51, 1.5f, FColor::Yellow, bPaused ? TEXT("Paused") : TEXT("Unpaused"));
	}
	RefreshHelpMessage();
}

void APortalPhysicsProbePlayerController::StepOneFrame()
{
	RequestSingleFrameStep();
}

void APortalPhysicsProbePlayerController::RunPresetN(int32 Index1Based)
{
	if (Probe)
	{
		Probe->RunPreset(Index1Based);
	}
}

void APortalPhysicsProbePlayerController::RunAllPresets()
{
	if (Probe)
	{
		Probe->RunAllPresets();
	}
}

void APortalPhysicsProbePlayerController::RequestSingleFrameStep()
{
	UWorld* World = GetWorld();
	if (!World || !UGameplayStatics::IsGamePaused(World))
	{
		return;
	}

	bSingleFrameStep = true;
	SingleStepRequestFrame = GFrameCounter;
	UGameplayStatics::SetGamePaused(World, false);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0x50524F51, 0.75f, FColor::Cyan, TEXT("Step +1 frame"));
	}
	RefreshHelpMessage();
}

bool APortalPhysicsProbePlayerController::TryCompleteSingleFrameStep()
{
	if (!bSingleFrameStep)
	{
		return false;
	}
	if (GFrameCounter <= SingleStepRequestFrame)
	{
		return false;
	}
	bSingleFrameStep = false;
	return true;
}
