#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PortalTraceLibrary.generated.h"

/**
 * Line traces that continue through a linked portal.
 * The hit's ImpactPoint is on the far side. Distance is the length traveled along the trace,
 * including the part after the portal, so Start + (End - Start) * Time is not the impact once a portal is crossed.
 */
UCLASS()
class ACTIONDEMO_API UPortalTraceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static bool LineTraceSingleByChannel(UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam, const FCollisionResponseParams& ResponseParam = FCollisionResponseParams::DefaultResponseParam);

	static bool LineTraceSingleByProfile(UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, FName ProfileName, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

	static bool LineTraceMultiByProfile(UWorld* World, TArray<FHitResult>& OutHits, const FVector& Start, const FVector& End, FName ProfileName, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

	static bool LineTraceSingleByObjectType(UWorld* World, FHitResult& OutHit, const FVector& Start, const FVector& End, const FCollisionObjectQueryParams& ObjectQueryParams, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam);

	UFUNCTION(BlueprintCallable, Category = "Portal", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "ActorsToIgnore"))
	static bool LineTraceSingle(const UObject* WorldContextObject, FVector Start, FVector End, ETraceTypeQuery TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, FHitResult& OutHit, bool bIgnoreSelf = true);
};
