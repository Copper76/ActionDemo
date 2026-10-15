#include "PortalTraceLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Portal.h"

namespace
{
	constexpr float PortalExitNudge = 2.f;
	constexpr int32 PortalTraceMessageKey = 0x504F5254;
	constexpr ECollisionChannel PortalObjectChannel = ECC_GameTraceChannel2;

	using FPortalSegmentTrace = TFunctionRef<bool(const FVector& Start, const FVector& End, const FCollisionQueryParams& Params, FHitResult& OutHit)>;

	void ShowPortalTraceMessage(const FString& Message)
	{
#if WITH_EDITOR
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(PortalTraceMessageKey, 2.f, FColor::Yellow, Message);
		}
#endif
	}

	void LogPortalTraceHit(const TCHAR* Stage, const FHitResult& Hit)
	{
#if WITH_EDITOR
		const AActor* Actor = Hit.GetActor();
		const UPrimitiveComponent* Component = Hit.GetComponent();
		ShowPortalTraceMessage(FString::Printf(TEXT("PortalTrace %s: %s / %s  impact=%s  blocking=%d"),
			Stage,
			Actor ? *Actor->GetName() : TEXT("None"),
			Component ? *Component->GetName() : TEXT("None"),
			*Hit.ImpactPoint.ToCompactString(),
			Hit.bBlockingHit ? 1 : 0));
#endif
	}

	bool FindPortal(UWorld* World, const FVector& Start, const FVector& End, const FCollisionQueryParams& Params, FHitResult& OutHit)
	{
		if (!World || !World->LineTraceSingleByObjectType(OutHit, Start, End, FCollisionObjectQueryParams(PortalObjectChannel), Params))
		{
			return false;
		}
		return Cast<APortal>(OutHit.GetActor()) != nullptr;
	}

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
		const bool bHit = SegmentTrace(NextStart, NextEnd, Params, OutHit) && OutHit.bBlockingHit;
		if (bHit)
		{
			LogPortalTraceHit(TEXT("through portal"), OutHit);
		}
		else
		{
#if WITH_EDITOR
			ShowPortalTraceMessage(FString::Printf(TEXT("PortalTrace through portal: nothing on the other side of %s"), *Portal->GetName()));
#endif
		}
		return bHit;
	}

	bool FinishPortalCrossing(const FVector& Start, float SegmentLength, float Traveled, APortal* Portal, const FVector& Direction, FCollisionQueryParams Params, const FPortalSegmentTrace& SegmentTrace, FHitResult& OutHit)
	{
		if (!Portal->OtherPortal)
		{
			return true;
		}

		if (!TraceRemainderThroughPortal(Portal, Direction, SegmentLength - Traveled, Params, SegmentTrace, OutHit))
		{
			OutHit.bBlockingHit = false;
			return false;
		}

		OutHit.TraceStart = Start;
		OutHit.Distance += Traveled + PortalExitNudge;
		OutHit.Time = SegmentLength > KINDA_SMALL_NUMBER ? OutHit.Distance / SegmentLength : 1.f;
		return true;
	}

	bool TraceThroughPortals(UWorld* World, const FVector& Start, const FVector& End, FHitResult& OutHit, FCollisionQueryParams Params, const FPortalSegmentTrace& SegmentTrace)
	{
		const FVector Segment = End - Start;
		const float SegmentLength = Segment.Size();
		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			OutHit.Init();
			return false;
		}

		const FVector Direction = Segment / SegmentLength;
		if (FindPortal(World, Start, End, Params, OutHit))
		{
			LogPortalTraceHit(TEXT("portal"), OutHit);
			return FinishPortalCrossing(Start, SegmentLength, OutHit.Distance, Cast<APortal>(OutHit.GetActor()), Direction, Params, SegmentTrace, OutHit);
		}

		if (!SegmentTrace(Start, End, Params, OutHit) || !OutHit.bBlockingHit)
		{
			return false;
		}

		LogPortalTraceHit(TEXT("hit"), OutHit);
		APortal* Portal = Cast<APortal>(OutHit.GetActor());
		if (!Portal)
		{
			return true;
		}

		return FinishPortalCrossing(Start, SegmentLength, OutHit.Distance, Portal, Direction, Params, SegmentTrace, OutHit);
	}
}

bool UPortalTraceLibrary::LineTraceSingleByChannel(UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParam)
{
	if (!World)
	{
		OutHit.Init();
		return false;
	}

	return TraceThroughPortals(World, Start, End, OutHit, Params, [&](const FVector& SegStart, const FVector& SegEnd, const FCollisionQueryParams& SegParams, FHitResult& SegHit)
	{
		return World->LineTraceSingleByChannel(SegHit, SegStart, SegEnd, TraceChannel, SegParams, ResponseParam);
	});
}

bool UPortalTraceLibrary::LineTraceMultiByChannel(UWorld* World, TArray<FHitResult>& OutHits, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParam)
{
	OutHits.Reset();
	if (!World)
	{
		return false;
	}

	const FVector Segment = End - Start;
	const float SegmentLength = Segment.Size();
	const FVector Direction = SegmentLength > KINDA_SMALL_NUMBER ? Segment / SegmentLength : FVector::ZeroVector;

	FHitResult PortalHit;
	if (FindPortal(World, Start, End, Params, PortalHit))
	{
		LogPortalTraceHit(TEXT("portal"), PortalHit);
		APortal* Portal = Cast<APortal>(PortalHit.GetActor());
		if (Portal && Portal->OtherPortal)
		{
			FHitResult Continued = PortalHit;
			auto SingleTrace = [&](const FVector& SegStart, const FVector& SegEnd, const FCollisionQueryParams& SegParams, FHitResult& SegHit)
			{
				return World->LineTraceSingleByChannel(SegHit, SegStart, SegEnd, TraceChannel, SegParams, ResponseParam);
			};
			if (TraceRemainderThroughPortal(Portal, Direction, SegmentLength - PortalHit.Distance, Params, SingleTrace, Continued))
			{
				Continued.TraceStart = Start;
				Continued.Distance += PortalHit.Distance + PortalExitNudge;
				Continued.Time = SegmentLength > KINDA_SMALL_NUMBER ? Continued.Distance / SegmentLength : 1.f;
				OutHits.Add(Continued);
			}
		}
		else if (Portal)
		{
			OutHits.Add(PortalHit);
		}
		return OutHits.Num() > 0;
	}

	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByChannel(Hits, Start, End, TraceChannel, Params, ResponseParam))
	{
		return false;
	}

	for (const FHitResult& Hit : Hits)
	{
		LogPortalTraceHit(TEXT("hit"), Hit);
		OutHits.Add(Hit);
	}
	return OutHits.Num() > 0;
}

bool UPortalTraceLibrary::LineTraceSingleByProfile(UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, FName ProfileName, const FCollisionQueryParams& Params)
{
	if (!World)
	{
		OutHit.Init();
		return false;
	}

	return TraceThroughPortals(World, Start, End, OutHit, Params, [&](const FVector& SegStart, const FVector& SegEnd, const FCollisionQueryParams& SegParams, FHitResult& SegHit)
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

	const FVector Segment = End - Start;
	const float SegmentLength = Segment.Size();
	const FVector Direction = SegmentLength > KINDA_SMALL_NUMBER ? Segment / SegmentLength : FVector::ZeroVector;

	FHitResult PortalHit;
	if (FindPortal(World, Start, End, Params, PortalHit))
	{
		LogPortalTraceHit(TEXT("portal"), PortalHit);
		APortal* Portal = Cast<APortal>(PortalHit.GetActor());
		if (Portal && Portal->OtherPortal)
		{
			FHitResult Continued = PortalHit;
			auto SingleTrace = [&](const FVector& SegStart, const FVector& SegEnd, const FCollisionQueryParams& SegParams, FHitResult& SegHit)
			{
				return World->LineTraceSingleByProfile(SegHit, SegStart, SegEnd, ProfileName, SegParams);
			};
			if (TraceRemainderThroughPortal(Portal, Direction, SegmentLength - PortalHit.Distance, Params, SingleTrace, Continued))
			{
				Continued.TraceStart = Start;
				Continued.Distance += PortalHit.Distance + PortalExitNudge;
				Continued.Time = SegmentLength > KINDA_SMALL_NUMBER ? Continued.Distance / SegmentLength : 1.f;
				OutHits.Add(Continued);
			}
		}
		else if (Portal)
		{
			OutHits.Add(PortalHit);
		}
		return OutHits.Num() > 0;
	}

	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByProfile(Hits, Start, End, ProfileName, Params))
	{
		return false;
	}

	for (const FHitResult& Hit : Hits)
	{
		LogPortalTraceHit(TEXT("hit"), Hit);
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

	return TraceThroughPortals(World, Start, End, OutHit, Params, [&](const FVector& SegStart, const FVector& SegEnd, const FCollisionQueryParams& SegParams, FHitResult& SegHit)
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
