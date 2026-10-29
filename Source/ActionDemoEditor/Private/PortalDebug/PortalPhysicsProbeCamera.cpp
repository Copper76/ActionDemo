#include "PortalPhysicsProbeCamera.h"

#include "Components/SphereComponent.h"
#include "GameFramework/PawnMovementComponent.h"

APortalPhysicsProbeCamera::APortalPhysicsProbeCamera()
{
	bAddDefaultMovementBindings = true;
	BaseEyeHeight = 0.f;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	if (USphereComponent* Collision = GetCollisionComponent())
	{
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
		Collision->PrimaryComponentTick.bTickEvenWhenPaused = true;
	}

	if (UPawnMovementComponent* Movement = GetMovementComponent())
	{
		Movement->UpdatedComponent = GetCollisionComponent();
		Movement->PrimaryComponentTick.bTickEvenWhenPaused = true;
	}

	if (UStaticMeshComponent* Mesh = GetMeshComponent())
	{
		Mesh->SetVisibility(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void APortalPhysicsProbeCamera::SetupPlayerInputComponent(UInputComponent* InInputComponent)
{
	Super::SetupPlayerInputComponent(InInputComponent);
	if (!InInputComponent)
	{
		return;
	}

	// DefaultPawn binds move/look without pause execute; keep camera usable while sim is paused.
	for (FInputAxisBinding& Binding : InInputComponent->AxisBindings)
	{
		Binding.bExecuteWhenPaused = true;
	}
	for (FInputAxisKeyBinding& Binding : InInputComponent->AxisKeyBindings)
	{
		Binding.bExecuteWhenPaused = true;
	}
	for (FInputVectorAxisBinding& Binding : InInputComponent->VectorAxisBindings)
	{
		Binding.bExecuteWhenPaused = true;
	}
	for (FInputKeyBinding& Binding : InInputComponent->KeyBindings)
	{
		Binding.bExecuteWhenPaused = true;
	}
}
