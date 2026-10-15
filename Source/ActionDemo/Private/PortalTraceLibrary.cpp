#include "PortalTraceLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Portal.h"

namespace
{
	constexpr float PortalExitNudge = 2.f;

	using FPortalSegmentTrace = TFunctionRef<bool(const FVector& Start, const FVector& End, const FCollisionQueryParams& Params, FHitResult& OutHit)>;

	bool TraceRemainderThroughPortal(APortal* Portal, const FVector& Direction, float Remaining, FCollisionQueryParams Params, const FPortalSegmentTrace& SegmentTrace, FHitResult& OutHit)
	{
		FVector ExitPoint = FVector::ZeroVector;
		FVector ExitDirection = FVector::ZeroVector;
		if (!Portal->TranslatePoint(OutHit.ImpactPoint, ExitPoint) || !Portal->TranslateDirection(Direction, ExitDirection))
		{
			return false;
		}

		ExitDirection = ExitDirection.GetSafeNormal();
		if (ExitDirection.IsNearlyZero() || Remaining <= PortalExitNudge)
		{
			return false;
		}

		Params.AddIgnoredActor(Portal);
		Params.AddIgnoredActor(Portal->OtherPortal);
		const FVector NextStart = ExitPoint + ExitDirection * PortalExitNudge;
		const FVector NextEnd = NextStart + ExitDirection * (Remaining - PortalExitNudge);
		return SegmentTrace(NextStart, NextEnd, Params, OutHit) && OutHit.bBlockingHit;
	}

	bool TraceThroughPortals(const FVector& Start, const FVector& End, FHitResult& OutHit, FCollisionQueryParams Params, const FPortalSegmentTrace& SegmentTrace)
	{
		const FVector Segment = End - Start;
		const float SegmentLength = Segment.Size();
		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			OutHit.Init();
			return false;
		}

		if (!SegmentTrace(Start, End, Params, OutHit) || !OutHit.bBlockingHit)
		{
			return false;
		}

		APortal* Portal = Cast<APortal>(OutHit.GetActor());
		if (!Portal || !Portal->OtherPortal)
		{
			return true;
		}

		const float Traveled = OutHit.Distance;
		if (!TraceRemainderThroughPortal(Portal, Segment / SegmentLength, SegmentLength - Traveled, Params, SegmentTrace, OutHit))
		{
			OutHit.bBlockingHit = false;
			return false;
		}

		OutHit.TraceStart = Start;
		OutHit.Distance += Traveled + PortalExitNudge;
		OutHit.Time = SegmentLength > KINDA_SMALL_NUMBER ? OutHit.Distance / SegmentLength : 1.f;
		return true;
	}
}

bool UPortalTraceLibrary::LineTraceSingleByChannel(UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParam)
{
	if (!World)
	{
		OutHit.Init();
		return false;
	}

	return TraceThroughPortals(Start, End, OutHit, Params, [&](const FVector& SegStart, const FVector& SegEnd, const FCollisionQueryParams& SegParams, FHitResult& SegHit)
	{
		return World->LineTraceSingleByChannel(SegHit, SegStart, SegEnd, TraceChannel, SegParams, ResponseParam);
	});
}

bool UPortalTraceLibrary::LineTraceSingleByProfile(UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, FName ProfileName, const FCollisionQueryParams& Params)
{
	if (!World)
	{
		OutHit.Init();
		return false;
	}

	return TraceThroughPortals(Start, End, OutHit, Params, [&](const FVector& SegStart, const FVector& SegEnd, const FCollisionQueryParams& SegParams, FHitResult& SegHit)
	{
		return World->LineTraceSingleByProfile(SegHit, SegStart, SegEnd, ProfileName, SegParams);
	});
}

bool UPortalTraceLibrary::LineTraceMultiByProfile(UWorld* World, TArray<FHitResult>& OutHits, const FVector& Start, const FVector& End, FName ProfileName, const FCollisionQueryParams& Params)
{
	OutHits.Reset();
	if (!World)
	{
		return false;
	}

	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByProfile(Hits, Start, End, ProfileName, Params))
	{
		return false;
	}

	const FVector Segment = End - Start;
	const float SegmentLength = Segment.Size();
	const FVector Direction = SegmentLength > KINDA_SMALL_NUMBER ? Segment / SegmentLength : FVector::ZeroVector;
	for (const FHitResult& Hit : Hits)
	{
		APortal* Portal = Cast<APortal>(Hit.GetActor());
		if (Portal && Portal->OtherPortal)
		{
			FHitResult Continued = Hit;
			auto SingleTrace = [&](const FVector& SegStart, const FVector& SegEnd, const FCollisionQueryParams& SegParams, FHitResult& SegHit)
			{
				return World->LineTraceSingleByProfile(SegHit, SegStart, SegEnd, ProfileName, SegParams);
			};
			if (TraceRemainderThroughPortal(Portal, Direction, SegmentLength - Hit.Distance, Params, SingleTrace, Continued))
			{
				OutHits.Add(Continued);
			}
			continue;
		}

		OutHits.Add(Hit);
	}
	return OutHits.Num() > 0;
}

bool UPortalTraceLibrary::LineTraceSingleByObjectType(UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionQueryParams& Params)
{
	if (!World)
	{
		OutHit.Init();
		return false;
	}

	return TraceThroughPortals(Start, End, OutHit, Params, [&](const FVector& SegStart, const FVector& SegEnd, const FCollisionQueryParams& SegParams, FHitResult& SegHit)
	{
		return World->LineTraceSingleByObjectType(SegHit, SegStart, SegEnd, ObjectQueryParams, SegParams);
	});
}

bool UPortalTraceLibrary::LineTraceSingle(const UObject* WorldContextObject, FVector Start, FVector End, ETraceTypeQuery TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, FHitResult& OutHit, bool bIgnoreSelf)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PortalTrace), bTraceComplex);
	Params.AddIgnoredActors(ActorsToIgnore);
	if (bIgnoreSelf)
	{
		if (const AActor* Actor = Cast<AActor>(WorldContextObject))
		{
			Params.AddIgnoredActor(Actor);
		}
		else if (const UActorComponent* Component = Cast<UActorComponent>(WorldContextObject))
		{
			Params.AddIgnoredActor(Component->GetOwner());
		}
	}

	return LineTraceSingleByChannel(World, OutHit, Start, End, UEngineTypes::ConvertToCollisionChannel(TraceChannel), Params);
}
