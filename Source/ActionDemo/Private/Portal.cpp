// Fill out your copyright notice in the Description page of Project Settings.


#include "Portal.h"
#include "../ActionDemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ActionDemo/Public/Teleportor.h"
#include "Components/StaticMeshComponent.h"
#include "Chaos/ContactModification.h"
#include "Chaos/Collision/CollisionConstraintAllocator.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/CollisionKeys.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Collision/ParticlePairMidPhase.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "Chaos/SimCallbackObject.h"
#include "PBDRigidsSolver.h"
#include "Engine/OverlapResult.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Misc/ScopeLock.h"

struct FPortalTrackedBody
{
	Chaos::FPhysicsObjectHandle PhysicsObject = nullptr;
	FVector PlaneOrigin = FVector::ZeroVector;
	FVector PlaneNormal = FVector::ForwardVector;
	FVector PlaneRight = FVector::RightVector;
	FVector PlaneUp = FVector::UpVector;
	float ApertureHalfWidth = 70.f;
	float ApertureHalfHeight = 120.f;
	/** Plane-slice fit — portal face stays solid when false so wide slabs cannot open the hole. */
	bool bFitsAperture = true;
	FVector EntryOrigin = FVector::ZeroVector;
	FVector ExitOrigin = FVector::ZeroVector;
	FVector ExitPlaneNormal = FVector::ForwardVector;
	FQuat TravelQ = FQuat::Identity;
	TArray<Chaos::FPhysicsObjectHandle> ExitBodies;

	void Reset()
	{
		PhysicsObject = nullptr;
		ExitBodies.Reset();
		bFitsAperture = true;
	}

	bool IsInAperture(const FVector& WorldPoint) const
	{
		const FVector Rel = WorldPoint - PlaneOrigin;
		const float AlongRight = FVector::DotProduct(Rel, PlaneRight);
		const float AlongUp = FVector::DotProduct(Rel, PlaneUp);
		return FMath::Abs(AlongRight) <= ApertureHalfWidth && FMath::Abs(AlongUp) <= ApertureHalfHeight;
	}
};

struct FPortalEntryContactInput : public Chaos::FSimCallbackInput
{
	TArray<FPortalTrackedBody> Bodies;
	TArray<Chaos::FPhysicsObjectHandle> PortalObjects;

	void Reset()
	{
		Bodies.Reset();
		PortalObjects.Reset();
	}
};

namespace
{
	Chaos::FRigidTransform3 TranslateShape(const Chaos::FRigidTransform3& ShapeWorld, const FVector& EntryOrigin, const FVector& ExitOrigin, const FQuat& TravelQ)
	{
		const FVector ExitLocation = ExitOrigin + TravelQ.RotateVector(FVector(ShapeWorld.GetTranslation()) - EntryOrigin);
		const FQuat ExitRotation = TravelQ * FQuat(ShapeWorld.GetRotation());
		return Chaos::FRigidTransform3(Chaos::FVec3(ExitLocation), Chaos::FRotation3(ExitRotation), ShapeWorld.GetScale3D());
	}

	Chaos::FRigidTransform3 InverseTravelShape(const Chaos::FRigidTransform3& ShapeWorld, const FVector& ExitOrigin, const FVector& EntryOrigin, const FQuat& InvTravel)
	{
		const FVector EntryLocation = EntryOrigin + InvTravel.RotateVector(FVector(ShapeWorld.GetTranslation()) - ExitOrigin);
		const FQuat EntryRotation = InvTravel * FQuat(ShapeWorld.GetRotation());
		return Chaos::FRigidTransform3(Chaos::FVec3(EntryLocation), Chaos::FRotation3(EntryRotation), ShapeWorld.GetScale3D());
	}

	struct FShapeLeaf
	{
		const Chaos::FImplicitObject* Implicit = nullptr;
		Chaos::FRigidTransform3 World = Chaos::FRigidTransform3::Identity;
	};

	struct FPortalContactKey
	{
		FVector Points[2];
		FVector Normal = FVector::UpVector;
	};

	void AppendShapeLeaves(const Chaos::FImplicitObject* Implicit, const Chaos::FRigidTransform3& World, TArray<FShapeLeaf, TInlineAllocator<16>>& Out)
	{
		if (!Implicit)
		{
			return;
		}

		const Chaos::EImplicitObjectType InnerType = Chaos::GetInnerType(Implicit->GetType());
		if (InnerType == Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>::StaticType())
		{
			const Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>* Transformed = static_cast<const Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>*>(Implicit);
			AppendShapeLeaves(Transformed->GetTransformedObject(), Transformed->GetTransform() * World, Out);
			return;
		}

		if (((uint32)Implicit->GetType() & Chaos::ImplicitObjectType::IsInstanced) != 0)
		{
			const Chaos::FImplicitObjectInstanced* Instanced = Implicit->template AsA<Chaos::FImplicitObjectInstanced>();
			const Chaos::FImplicitObject* Inner = Instanced ? Instanced->GetInnerObject().Get() : nullptr;
			if (Inner && Chaos::GetInnerType(Inner->GetType()) != Chaos::FImplicitConvex3::StaticType())
			{
				AppendShapeLeaves(Inner, World, Out);
				return;
			}
		}

		// A scaled wrapper has the same inner type as the object it contains. Only a real union can be walked.
		if (!Chaos::IsScaled(Implicit->GetType()) && !Chaos::IsInstanced(Implicit->GetType())
			&& (InnerType == Chaos::FImplicitObjectUnion::StaticType() || InnerType == Chaos::FImplicitObjectUnionClustered::StaticType()))
		{
			const Chaos::FImplicitObjectUnion* Union = static_cast<const Chaos::FImplicitObjectUnion*>(Implicit);
			for (const Chaos::FImplicitObjectPtr& Child : Union->GetObjects())
			{
				AppendShapeLeaves(Child.GetReference(), World, Out);
			}
			return;
		}

		FShapeLeaf Leaf;
		Leaf.Implicit = Implicit;
		Leaf.World = World;
		Out.Add(Leaf);
	}

	void CollectShapeLeaves(const Chaos::FGeometryParticleHandle* Particle, const Chaos::FPerShapeData* Shape, const bool bTranslate, const FVector& EntryOrigin, const FVector& ExitOrigin, const FQuat& TravelQ, TArray<FShapeLeaf, TInlineAllocator<16>>& Out)
	{
		if (!Particle || !Shape)
		{
			return;
		}

		const Chaos::FImplicitObject* Leaf = Shape->GetLeafGeometry();
		if (!Leaf)
		{
			return;
		}

		// Same pose the narrow phase uses. Translating the particle and then re-applying
		// child offsets does not match this once a shape has its own relative transform.
		Chaos::FRigidTransform3 World = Shape->GetLeafWorldTransform(Particle);
		if (bTranslate)
		{
			World = TranslateShape(World, EntryOrigin, ExitOrigin, TravelQ);
		}

		const Chaos::EImplicitObjectType InnerType = Chaos::GetInnerType(Leaf->GetType());
		const bool bUnion = !Chaos::IsScaled(Leaf->GetType()) && !Chaos::IsInstanced(Leaf->GetType())
			&& (InnerType == Chaos::FImplicitObjectUnion::StaticType() || InnerType == Chaos::FImplicitObjectUnionClustered::StaticType());
		if (bUnion)
		{
			AppendShapeLeaves(Leaf, World, Out);
			return;
		}

		FShapeLeaf ShapeLeaf;
		ShapeLeaf.Implicit = Leaf;
		ShapeLeaf.World = World;
		Out.Add(ShapeLeaf);
	}

	// The wall face lies on the portal plane, so a behind-the-plane test never sees it.
	// That contact pushes the body back into the entry room and, because it is the deepest,
	// it also fills the manifold and leaves the floor with no slot. A contact in front of
	// the plane still blocks. A contact that does not push back into the room stays.
	bool IsPortalSurfaceRejection(const float OtherSide, const FVector& PushOnCube, const FVector& PlaneNormal)
	{
		// Floor/wall fillets near the throat can look wall-aligned; keep primarily upward support.
		if (FVector::DotProduct(PushOnCube, FVector::UpVector) > 0.5f)
		{
			return false;
		}
		return OtherSide < 1.f && FVector::DotProduct(PushOnCube, PlaneNormal) > 0.7f;
	}

	// Bounds of a static mesh are already scaled on the implicit. The leaf transform is only position and rotation.
	bool GetLeafPlaneInterval(const Chaos::FImplicitObject* Implicit, const Chaos::FRigidTransform3& World, const FVector& Origin, const FVector& Normal, float& OutMinSide, float& OutMaxSide)
	{
		if (!Implicit || !Implicit->HasBoundingBox() || Normal.IsNearlyZero())
		{
			return false;
		}

		const Chaos::FAABB3 Bounds = Implicit->BoundingBox();
		const FVector Extent(Bounds.Extents());
		if (Extent.IsNearlyZero())
		{
			return false;
		}

		const FVector Min(Bounds.Min());
		const FVector Max(Bounds.Max());
		bool bHaveCorner = false;
		OutMinSide = 0.f;
		OutMaxSide = 0.f;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Local(
				(Corner & 1) ? Max.X : Min.X,
				(Corner & 2) ? Max.Y : Min.Y,
				(Corner & 4) ? Max.Z : Min.Z);
			const float Side = FVector::DotProduct(FVector(World.TransformPositionNoScale(Chaos::FVec3(Local))) - Origin, Normal);
			if (!bHaveCorner)
			{
				OutMinSide = Side;
				OutMaxSide = Side;
				bHaveCorner = true;
			}
			else
			{
				OutMinSide = FMath::Min(OutMinSide, Side);
				OutMaxSide = FMath::Max(OutMaxSide, Side);
			}
		}
		return bHaveCorner;
	}

	void SetExitBodyInfiniteMass(Chaos::FPBDCollisionConstraint& Constraint, Chaos::FGeometryParticleHandle* ExitParticle)
	{
		if (Constraint.GetParticle0() == ExitParticle)
		{
			Constraint.SetInvMassScale0(0);
			Constraint.SetInvInertiaScale0(0);
		}
		else if (Constraint.GetParticle1() == ExitParticle)
		{
			Constraint.SetInvMassScale1(0);
			Constraint.SetInvInertiaScale1(0);
		}
	}

	void ClearInitialOverlap(Chaos::FPBDCollisionConstraint& Constraint)
	{
		Constraint.SetIsInitialContact(false);
		for (int32 Index = 0; Index < Constraint.NumManifoldPoints(); ++Index)
		{
			Chaos::FManifoldPoint& Point = Constraint.GetManifoldPoint(Index);
			Point.Flags.bInitialContact = false;
			Point.InitialPhi = 0.f;
		}
	}

	struct FPortalCopyStats
	{
		int32 Bodies = 0;
		int32 Exits = 0;
		int32 Leaves = 0;
		int32 Unknown = 0;
		int32 Detected = 0;
		int32 Written = 0;
		int32 NewPoints = 0;
		int32 KeptPoints = 0;
		int32 DroppedBehindWall = 0;
		int32 DroppedSurface = 0;
		int32 KeptFloorBehind = 0;
	};

	void NoteWorldContact(const Chaos::FContactPointf& Contact, const Chaos::FRigidTransform3& Shape0, const Chaos::FRigidTransform3& Shape1, const TArray<FPortalContactKey>& PreviousKeys, TArray<FPortalContactKey>& FrameKeys, FPortalCopyStats& Stats)
	{
		FPortalContactKey Key;
		Key.Points[0] = FVector(Shape0.TransformPositionNoScale(Chaos::FVec3(Contact.ShapeContactPoints[0])));
		Key.Points[1] = FVector(Shape1.TransformPositionNoScale(Chaos::FVec3(Contact.ShapeContactPoints[1])));
		Key.Normal = FVector(Shape1.TransformVectorNoScale(Chaos::FVec3(Contact.ShapeContactNormal))).GetSafeNormal();

		bool bKept = false;
		for (const FPortalContactKey& Previous : PreviousKeys)
		{
			if (FVector::DotProduct(Key.Normal, Previous.Normal) < 0.9f)
			{
				continue;
			}
			if (FVector::DistSquared(Key.Points[0], Previous.Points[0]) <= 1.f || FVector::DistSquared(Key.Points[1], Previous.Points[1]) <= 1.f)
			{
				bKept = true;
				break;
			}
		}
		if (bKept)
		{
			++Stats.KeptPoints;
		}
		else
		{
			++Stats.NewPoints;
		}
		FrameKeys.Add(Key);
	}

	void InjectExitContacts(const TArray<FPortalTrackedBody>& Bodies, Chaos::FPBDRigidsSolver* Solver, const Chaos::FReal Dt, TArray<Chaos::FPBDCollisionConstraintPtr>& OwnedConstraints, TArray<FPortalContactKey>& ContactKeys, FPortalCopyStats& Stats)
	{
		TArray<FPortalContactKey> FrameKeys;
		Stats.Bodies = Bodies.Num();
		for (const FPortalTrackedBody& Tracked : Bodies)
		{
			Stats.Exits += Tracked.ExitBodies.Num();
		}
		if (!Solver || !Solver->GetEvolution() || Dt <= KINDA_SMALL_NUMBER)
		{
			ContactKeys.Reset();
			return;
		}

		Chaos::FPBDRigidsEvolutionGBF& Evolution = *Solver->GetEvolution();
		Chaos::FPBDCollisionConstraints& Collisions = Evolution.GetCollisionConstraints();
		Chaos::Private::FCollisionConstraintAllocator& Allocator = Collisions.GetConstraintAllocator();
		Chaos::Private::FCollisionContextAllocator* ContextAllocator = Allocator.GetContextAllocator(0);
		if (!ContextAllocator)
		{
			ContactKeys.Reset();
			return;
		}

		Chaos::FCollisionContext Context;
		Context.SetSettings(Collisions.GetDetectorSettings());
		Context.SetAllocator(ContextAllocator);
		const Chaos::FReal BaseCullDistance = FMath::Max(Collisions.GetDetectorSettings().BoundsExpansion, Chaos::FReal(3));
		Chaos::FReadPhysicsObjectInterface_Internal Physics = Chaos::FPhysicsObjectInternalInterface::GetRead();

		for (const FPortalTrackedBody& Tracked : Bodies)
		{
			if (!Tracked.PhysicsObject || Tracked.ExitBodies.Num() == 0)
			{
				continue;
			}

			Chaos::FGeometryParticleHandle* CubeParticle = Physics.GetParticle(Tracked.PhysicsObject);
			if (!CubeParticle)
			{
				continue;
			}

			const FQuat InvTravel = Tracked.TravelQ.Inverse();
			Chaos::FReal CullDistance = BaseCullDistance;
			if (const Chaos::FImplicitObject* CubeGeometry = CubeParticle->GetGeometry())
			{
				if (CubeGeometry->HasBoundingBox())
				{
					CullDistance = FMath::Max(CullDistance, CubeGeometry->BoundingBox().Extents().GetAbsMax() * Chaos::FReal(0.5));
				}
			}
			for (Chaos::FPhysicsObjectHandle ExitObject : Tracked.ExitBodies)
			{
				Chaos::FGeometryParticleHandle* ExitParticle = Physics.GetParticle(ExitObject);
				if (!ExitParticle || ExitParticle == CubeParticle)
				{
					continue;
				}

				Chaos::FGeometryParticleHandle* Particle0 = CubeParticle;
				Chaos::FGeometryParticleHandle* Particle1 = ExitParticle;
				if (Chaos::ShouldSwapParticleOrder(Particle0, Particle1))
				{
					Swap(Particle0, Particle1);
				}

				Chaos::FParticlePairMidPhase* MidPhase = ContextAllocator->GetMidPhase(Particle0, Particle1, CubeParticle, Context);
				Allocator.ProcessNewMidPhases();
				if (!MidPhase)
				{
					continue;
				}

				const bool bStaticExit = !Chaos::FConstGenericParticleHandle(ExitParticle)->IsDynamic();
				for (const auto& CubeShapePtr : CubeParticle->ShapesArray())
				{
					const Chaos::FPerShapeData* CubeShape = CubeShapePtr.Get();
					if (!CubeShape || !CubeShape->GetSimEnabled())
					{
						continue;
					}

					TArray<FShapeLeaf, TInlineAllocator<16>> CubeLeaves;
					CollectShapeLeaves(CubeParticle, CubeShape, false, Tracked.EntryOrigin, Tracked.ExitOrigin, Tracked.TravelQ, CubeLeaves);

					for (const auto& ExitShapePtr : ExitParticle->ShapesArray())
					{
						const Chaos::FPerShapeData* ExitShape = ExitShapePtr.Get();
						if (!ExitShape || !ExitShape->GetSimEnabled())
						{
							continue;
						}

						TArray<FShapeLeaf, TInlineAllocator<16>> ExitLeaves;
						CollectShapeLeaves(ExitParticle, ExitShape, false, Tracked.EntryOrigin, Tracked.ExitOrigin, Tracked.TravelQ, ExitLeaves);
						Stats.Leaves += CubeLeaves.Num() + ExitLeaves.Num();

						for (const FShapeLeaf& CubeLeaf : CubeLeaves)
						{
							// The body is still entirely on the wall side of the exit, so it has not entered that room.
							if (bStaticExit)
							{
								const Chaos::FRigidTransform3 TraveledCube = TranslateShape(CubeLeaf.World, Tracked.EntryOrigin, Tracked.ExitOrigin, Tracked.TravelQ);
								float MinSide = 0.f;
								float MaxSide = 0.f;
								if (GetLeafPlaneInterval(CubeLeaf.Implicit, TraveledCube, Tracked.ExitOrigin, Tracked.ExitPlaneNormal, MinSide, MaxSide)
									&& MaxSide <= KINDA_SMALL_NUMBER)
								{
									continue;
								}
							}

							for (const FShapeLeaf& ExitLeaf : ExitLeaves)
							{
								// The wall the exit portal is on (thin slab on/behind the plane). Moved into
								// the entry room it shoves long straddling bodies. Floor / room geometry
								// sits well in front (large MaxSide) and must still inject.
								if (bStaticExit)
								{
									float MinSide = 0.f;
									float MaxSide = 0.f;
									constexpr float ExitWallFrontSlackCm = 12.f;
									if (GetLeafPlaneInterval(ExitLeaf.Implicit, ExitLeaf.World, Tracked.ExitOrigin, Tracked.ExitPlaneNormal, MinSide, MaxSide)
										&& MaxSide <= ExitWallFrontSlackCm)
									{
										continue;
									}
								}

								bool bSwapShapes = false;
								const Chaos::EContactShapesType ShapeType = Chaos::Collisions::CalculateShapePairType(CubeParticle, CubeLeaf.Implicit, ExitParticle, ExitLeaf.Implicit, bSwapShapes);
								if (ShapeType == Chaos::EContactShapesType::Unknown || !CubeLeaf.Implicit || !ExitLeaf.Implicit)
								{
									++Stats.Unknown;
									continue;
								}

								// Exit shape posed in the entry room. The traveler stays put. Chaos builds the manifold.
								const Chaos::FRigidTransform3 MovedExit = InverseTravelShape(ExitLeaf.World, Tracked.ExitOrigin, Tracked.EntryOrigin, InvTravel);
								Chaos::FGeometryParticleHandle* P0 = bSwapShapes ? ExitParticle : CubeParticle;
								Chaos::FGeometryParticleHandle* P1 = bSwapShapes ? CubeParticle : ExitParticle;
								const Chaos::FPerShapeData* S0 = bSwapShapes ? ExitShape : CubeShape;
								const Chaos::FPerShapeData* S1 = bSwapShapes ? CubeShape : ExitShape;
								const Chaos::FImplicitObject* Implicit0 = P0 == CubeParticle ? CubeLeaf.Implicit : ExitLeaf.Implicit;
								const Chaos::FImplicitObject* Implicit1 = P1 == CubeParticle ? CubeLeaf.Implicit : ExitLeaf.Implicit;
								const Chaos::FRigidTransform3 Detect0 = P0 == CubeParticle ? CubeLeaf.World : MovedExit;
								const Chaos::FRigidTransform3 Detect1 = P1 == CubeParticle ? CubeLeaf.World : MovedExit;
								const Chaos::FRigidTransform3 Rel0 = Detect0 * Chaos::FConstGenericParticleHandle(P0)->GetTransformPQ().Inverse();
								const Chaos::FRigidTransform3 Rel1 = Detect1 * Chaos::FConstGenericParticleHandle(P1)->GetTransformPQ().Inverse();

								Chaos::FPBDCollisionConstraintPtr Scratch = ContextAllocator->CreateConstraint(
									P0, Implicit0, S0, Chaos::FConstGenericParticleHandle(P0)->CollisionParticles().Get(), Rel0,
									P1, Implicit1, S1, Chaos::FConstGenericParticleHandle(P1)->CollisionParticles().Get(), Rel1,
									CullDistance, true, ShapeType);
								if (!Scratch.IsValid())
								{
									continue;
								}
								Scratch->SetCCDEnabled(false);
								Chaos::Collisions::UpdateConstraint(*Scratch, Detect0, Detect1, Dt);
								if (Scratch->NumManifoldPoints() == 0)
								{
									continue;
								}
								Stats.Detected += Scratch->NumManifoldPoints();

								const int32 KeysBefore = FrameKeys.Num();
								for (int32 PointIndex = 0; PointIndex < Scratch->NumManifoldPoints(); ++PointIndex)
								{
									const Chaos::FManifoldPoint& ManifoldPoint = Scratch->GetManifoldPoint(PointIndex);
									if (!ManifoldPoint.Flags.bDisabled && ManifoldPoint.ContactPoint.IsSet())
									{
										NoteWorldContact(ManifoldPoint.ContactPoint, Detect0, Detect1, ContactKeys, FrameKeys, Stats);
									}
								}

								// A pair that already touches in the entry room keeps that manifold. Injecting
								// here would replace it. This constraint is the exit shape, posed beside the body.
								Chaos::FPBDCollisionConstraint* Existing = nullptr;
								MidPhase->VisitCollisions([&](Chaos::FPBDCollisionConstraint& Constraint)
								{
									const bool bSameOrder = Constraint.GetParticle0() == P0 && Constraint.GetParticle1() == P1 && Constraint.GetImplicit0() == Implicit0 && Constraint.GetImplicit1() == Implicit1;
									const bool bSwappedOrder = Constraint.GetParticle0() == P1 && Constraint.GetParticle1() == P0 && Constraint.GetImplicit0() == Implicit1 && Constraint.GetImplicit1() == Implicit0;
									if (bSameOrder || bSwappedOrder)
									{
										Existing = &Constraint;
										return Chaos::ECollisionVisitorResult::Stop;
									}
									return Chaos::ECollisionVisitorResult::Continue;
								}, Chaos::ECollisionVisitorFlags::VisitAllCurrent);

								Scratch->SetShapeWorldTransforms(Detect0, Detect1);
								bool bInjected = false;
								if (!Existing)
								{
									Context.MidPhase = MidPhase;
									MidPhase->InjectCollision(*Scratch, Context);
									MidPhase->VisitCollisions([&](Chaos::FPBDCollisionConstraint& Constraint)
									{
										if (Constraint.GetParticle0() == P0 && Constraint.GetParticle1() == P1 && Constraint.GetImplicit0() == Implicit0)
										{
											ClearInitialOverlap(Constraint);
											SetExitBodyInfiniteMass(Constraint, ExitParticle);
											Constraint.SetShapeWorldTransforms(Detect0, Detect1);
											bInjected = true;
											return Chaos::ECollisionVisitorResult::Stop;
										}
										return Chaos::ECollisionVisitorResult::Continue;
									}, Chaos::ECollisionVisitorFlags::VisitAllCurrent);
								}

								if (!bInjected)
								{
									ContextAllocator->ActivateConstraint(Scratch.Get());
									ClearInitialOverlap(*Scratch);
									SetExitBodyInfiniteMass(*Scratch, ExitParticle);
									Scratch->SetShapeWorldTransforms(Detect0, Detect1);
									OwnedConstraints.Add(MoveTemp(Scratch));
								}
								Stats.Written += FrameKeys.Num() - KeysBefore;
							}
						}
					}
				}
			}
		}

		ContactKeys = MoveTemp(FrameKeys);
		Allocator.ProcessInjectedConstraints();
	}
}

class FPortalEntryContactCallback : public Chaos::TSimCallbackObject<FPortalEntryContactInput, Chaos::FSimCallbackNoOutput, Chaos::ESimCallbackOptions::ContactModification | Chaos::ESimCallbackOptions::PostSolve>
{
public:
	virtual FName GetFNameForStatId() const override
	{
		return TEXT("PortalEntryContact");
	}

	void SetCopyDebug(const FPortalCopyStats& Stats, bool bActive)
	{
		FScopeLock Lock(&CopyDebugLock);
		CopyDebug = Stats;
		bCopyDebugActive = bActive;
	}

	void GetCopyDebug(FPortalCopyStats& Stats, bool& bActive) const
	{
		FScopeLock Lock(&CopyDebugLock);
		Stats = CopyDebug;
		bActive = bCopyDebugActive;
	}

private:
	// Constraints the generic midphase will not own. Freed one step after the graph drops them.
	TArray<Chaos::FPBDCollisionConstraintPtr> OwnedConstraints;
	TArray<Chaos::FPBDCollisionConstraintPtr> RetiredConstraints;
	TArray<FPortalContactKey> ContactKeys;
	mutable FCriticalSection CopyDebugLock;
	FPortalCopyStats CopyDebug;
	bool bCopyDebugActive = false;

	virtual void OnPreSimulate_Internal() override
	{
	}

	virtual void OnPostSolve_Internal() override
	{
		RetiredConstraints.Reset();
		RetiredConstraints = MoveTemp(OwnedConstraints);
	}

	virtual void OnContactModification_Internal(Chaos::FCollisionContactModifier& Modifier) override
	{
		const FPortalEntryContactInput* Input = GetConsumerInput_Internal();
		if (!Input || Input->Bodies.Num() == 0)
		{
			ContactKeys.Reset();
			SetCopyDebug(FPortalCopyStats(), false);
			return;
		}

		Chaos::FReadPhysicsObjectInterface_Internal Physics = Chaos::FPhysicsObjectInternalInterface::GetRead();

		auto IsPortalParticle = [&Physics, Input](const Chaos::FGeometryParticleHandle* Particle) mutable
		{
			for (Chaos::FPhysicsObjectHandle PortalObject : Input->PortalObjects)
			{
				if (Physics.GetParticle(PortalObject) == Particle)
				{
					return true;
				}
			}
			return false;
		};

		auto FindBody = [&Physics, Input](const Chaos::FGeometryParticleHandle* Particle) mutable -> const FPortalTrackedBody*
		{
			for (const FPortalTrackedBody& Tracked : Input->Bodies)
			{
				if (Physics.GetParticle(Tracked.PhysicsObject) == Particle)
				{
					return &Tracked;
				}
			}
			return nullptr;
		};

		FPortalCopyStats ModStats;
		for (Chaos::FContactPairModifier& Pair : Modifier)
		{
			const Chaos::TVec2<Chaos::FGeometryParticleHandle*> Particles = Pair.GetParticlePair();
			if (!Particles[0] || !Particles[1])
			{
				continue;
			}

			const FPortalTrackedBody* Tracked = FindBody(Particles[0]);
			if (!Tracked)
			{
				Tracked = FindBody(Particles[1]);
			}
			if (!Tracked)
			{
				continue;
			}

			const bool bTrackedIs0 = Particles[0] == Physics.GetParticle(Tracked->PhysicsObject);
			const bool bPortalSurface = IsPortalParticle(Particles[0]) || IsPortalParticle(Particles[1]);
			const int32 NumContacts = Pair.GetNumContacts();
			for (int32 Index = 0; Index < NumContacts; ++Index)
			{
				const FVector Location(Pair.GetWorldContactLocation(Index));
				const float Side = FVector::DotProduct(Location - Tracked->PlaneOrigin, Tracked->PlaneNormal);
				Chaos::FVec3 Location0, Location1;
				Pair.GetWorldContactLocations(Index, Location0, Location1);
				const FVector OtherPoint = bTrackedIs0 ? FVector(Location1) : FVector(Location0);
				const float OtherSide = FVector::DotProduct(OtherPoint - Tracked->PlaneOrigin, Tracked->PlaneNormal);
				const FVector WorldNormal(Pair.GetWorldNormal(Index));
				const FVector PushOnCube = bTrackedIs0 ? WorldNormal : -WorldNormal;
				const float WallAligned = FVector::DotProduct(PushOnCube, Tracked->PlaneNormal);
				const float FloorAligned = FVector::DotProduct(PushOnCube, FVector::UpVector);
				const bool bInAperture = Tracked->IsInAperture(Location);
				// Behind the entry plane: traveler is on the exit side for walls/obstacles, but
				// keep gravity support. When entry/exit floors differ, InjectExitContacts supplies
				// the exit floor; same-height entry floor contacts still prevent straddling sink.
				if (Side < 0.f)
				{
					if (FloorAligned > 0.5f)
					{
						++ModStats.KeptFloorBehind;
					}
					else
					{
						Pair.SetContactPointDisabled(Index);
						++ModStats.DroppedBehindWall;
					}
					continue;
				}
				// Entry face, inside the opening: drop everything except floor (wall volume thrash).
				// Slack covers a typical cube half-depth so high-speed approaches don't bounce off the wall face.
				constexpr float ApertureFrontSlackCm = 30.f;
				if (bInAperture && Side < ApertureFrontSlackCm)
				{
					if (FloorAligned > 0.5f)
					{
						++ModStats.KeptFloorBehind;
					}
					else
					{
						Pair.SetContactPointDisabled(Index);
						++ModStats.DroppedSurface;
					}
					continue;
				}
				// Portal-owned face: only open when the traveler's plane-slice fits. The portal mesh
				// IS the aperture quad — without the fit gate every face contact is "in aperture"
				// and wide slabs would ghost straight through.
				if (bPortalSurface)
				{
					if (Tracked->bFitsAperture && bInAperture && FloorAligned <= 0.5f && FMath::Abs(WallAligned) > 0.5f)
					{
						Pair.SetContactPointDisabled(Index);
						++ModStats.DroppedSurface;
					}
					continue;
				}
				if (Tracked->bFitsAperture && bInAperture
					&& IsPortalSurfaceRejection(OtherSide, PushOnCube, Tracked->PlaneNormal))
				{
					Pair.SetContactPointDisabled(Index);
					++ModStats.DroppedSurface;
				}
			}
		}

		FPortalCopyStats Stats = ModStats;
		InjectExitContacts(Input->Bodies, static_cast<Chaos::FPBDRigidsSolver*>(GetSolver()), GetDeltaTime_Internal(), OwnedConstraints, ContactKeys, Stats);
		SetCopyDebug(Stats, true);
	}
};

static constexpr int32 PortalCopyMessageKey = 0x434F5059;

#define POINT(x, c) DrawDebugPoint(GetWorld(), x, 10, c, !5.0f, 5.0f);
#define LINE(x1, x2, c) DrawDebugLine(GetWorld(), x1, x2, c, !5.0f, 5.0f);
#define PortalChannel ECC_GameTraceChannel2

// Sets default values
APortal::APortal()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	AddOwnedComponent(SceneRoot);
	SetRootComponent(SceneRoot);

	CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("OtherPortalView"));
	AddOwnedComponent(CaptureComponent);
	CaptureComponent->SetupAttachment(SceneRoot);
	//CaptureComponent->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
	CaptureComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f).Quaternion());

	ReverseCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("ReverseCollider"));
	AddOwnedComponent(ReverseCollider);
	ReverseCollider->SetupAttachment(SceneRoot);
	ReverseCollider->SetRelativeLocation(FVector(0.0f, 0.f, 0.0f));
	ReverseCollider->SetRelativeScale3D(FVector(1.f, 1.f, 1.f));
	ReverseCollider->SetGenerateOverlapEvents(true);
	ReverseCollider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ReverseCollider->SetCollisionResponseToAllChannels(ECR_Ignore);
	ReverseCollider->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	ReverseCollider->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	ReverseCollider->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ReverseCollider->OnComponentBeginOverlap.AddDynamic(this, &APortal::OnOverlapBegin);
	ReverseCollider->OnComponentEndOverlap.AddDynamic(this, &APortal::OnOverlapEnd);

	// Exact port of BP_Portal EdgeColliders / Top / Bottom / Left / Right.
	EdgeColliders = CreateDefaultSubobject<USceneComponent>(TEXT("EdgeColliders"));
	AddOwnedComponent(EdgeColliders);
	EdgeColliders->SetupAttachment(SceneRoot);
	EdgeColliders->SetRelativeLocation(FVector::ZeroVector);
	EdgeColliders->SetRelativeRotation(FRotator::ZeroRotator);
	EdgeColliders->SetRelativeScale3D(FVector::OneVector);

	auto MakeRim = [this](const TCHAR* Name) -> UBoxComponent*
	{
		UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(Name);
		AddOwnedComponent(Box);
		Box->SetupAttachment(EdgeColliders);
		Box->SetRelativeRotation(FRotator::ZeroRotator);
		Box->SetRelativeScale3D(FVector::OneVector);
		// Geometry copied from BP_Portal. QueryAndPhysics so the rim actually collides
		// (BP had QueryOnly+Custom; these need to block props that miss the plane mesh).
		Box->SetGenerateOverlapEvents(true);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		return Box;
	};
	Top = MakeRim(TEXT("Top"));
	Bottom = MakeRim(TEXT("Bottom"));
	Left = MakeRim(TEXT("Left"));
	Right = MakeRim(TEXT("Right"));

	// Exact BP_Portal rim layout (aperture cached from these in BeginPlay).
	Top->SetBoxExtent(FVector(2.f, 64.f, 2.f));
	Top->SetRelativeLocation(FVector(-1.f, 0.f, 114.f));
	Bottom->SetBoxExtent(FVector(2.f, 64.f, 2.f));
	Bottom->SetRelativeLocation(FVector(-1.f, 0.f, -114.f));
	Left->SetBoxExtent(FVector(2.f, 2.f, 112.f));
	Left->SetRelativeLocation(FVector(-1.f, 66.f, 0.f));
	Right->SetBoxExtent(FVector(2.f, 2.f, 112.f));
	Right->SetRelativeLocation(FVector(-1.f, -66.f, 0.f));

	Plane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plane"));
	AddOwnedComponent(Plane);
	Plane->SetupAttachment(SceneRoot);
	//Plane->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
	Plane->SetRelativeScale3D(FVector(1.25f, 2.25f, 1.0f));
	Plane->SetRelativeRotation(FRotator(0.0f, -90.0f, 90.0f));
	// Solid for physics props until contact-mod ghosts the face for travelers that fit.
	// Pawns stay ignored — character transit uses movement clipping, not this mesh.
	Plane->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Plane->SetCollisionResponseToAllChannels(ECR_Ignore);
	Plane->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	Plane->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);

	SyncReverseColliderToAperture();
}

// Called when the game starts or when spawned
void APortal::BeginPlay()
{
	Super::BeginPlay();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PlayerController = PC;
		LocalPlayer = Cast<AActionDemoCharacter>(PC->GetCharacter());
	}

	CaptureComponent->bEnableClipPlane = true;
	CaptureComponent->ClipPlaneBase = GetPlaneOrigin();
	CaptureComponent->ClipPlaneNormal = GetActorForwardVector();

	GetApertureHalfExtents(ApertureHalfWidth, ApertureHalfHeight);
	SyncReverseColliderToAperture();

	// BP_Portal still carries SCS copies of EdgeColliders/Top/Bottom/Left/Right that
	// clash with the C++ natives. Kill any extras so only the C++ rims remain.
	{
		TArray<UActorComponent*> Comps;
		GetComponents(Comps);
		for (UActorComponent* Comp : Comps)
		{
			if (!Comp || Comp == EdgeColliders || Comp == Top || Comp == Bottom || Comp == Left || Comp == Right)
			{
				continue;
			}
			const FString Name = Comp->GetName();
			const bool bRimName =
				Name.StartsWith(TEXT("Top")) || Name.StartsWith(TEXT("Bottom"))
				|| Name.StartsWith(TEXT("Left")) || Name.StartsWith(TEXT("Right"))
				|| Name.StartsWith(TEXT("EdgeColliders"));
			if (bRimName)
			{
				Comp->DestroyComponent();
			}
		}
	}

	RegisterPhysicsCallbacks();
	CachePortalPhysicsObjects();
}

void APortal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComp : SimulatingComponents)
	{
		if (UPrimitiveComponent* Comp = WeakComp.Get())
		{
			if (FBodyInstance* Body = Comp->GetBodyInstance())
			{
				Body->SetContactModification(false);
			}
		}
	}
	SimulatingComponents.Reset();
	UnregisterPhysicsCallbacks();
	Super::EndPlay(EndPlayReason);
}

void APortal::RegisterPhysicsCallbacks()
{
	if (EntryContactCallback || !GetWorld())
	{
		return;
	}

	FPhysScene* Scene = GetWorld()->GetPhysicsScene();
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	if (!Solver)
	{
		return;
	}

	EntryContactCallback = Solver->CreateAndRegisterSimCallbackObject_External<FPortalEntryContactCallback>();
}

void APortal::UnregisterPhysicsCallbacks()
{
	if (!GetWorld())
	{
		EntryContactCallback = nullptr;
		return;
	}

	FPhysScene* Scene = GetWorld()->GetPhysicsScene();
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	if (Solver)
	{
		if (EntryContactCallback)
		{
			Solver->UnregisterAndFreeSimCallbackObject_External(EntryContactCallback);
		}
	}

	EntryContactCallback = nullptr;
}

void APortal::SetSimulatingThroughPortal(AActor* Actor, bool bEnable)
{
	if (!Actor)
	{
		return;
	}

	TArray<UPrimitiveComponent*> Primitives;
	Actor->GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!Primitive)
		{
			continue;
		}

		if (bEnable)
		{
			if (!Primitive->IsSimulatingPhysics())
			{
				continue;
			}

			FBodyInstance* Body = Primitive->GetBodyInstance();
			if (!Body || !Body->IsValidBodyInstance())
			{
				continue;
			}

			const bool bAlreadyTracked = SimulatingComponents.ContainsByPredicate([Primitive](const TWeakObjectPtr<UPrimitiveComponent>& Existing)
			{
				return Existing.Get() == Primitive;
			});
			if (bAlreadyTracked)
			{
				continue;
			}

			Body->SetContactModification(true);
			SimulatingComponents.Add(Primitive);
		}
		else
		{
			const int32 Removed = SimulatingComponents.RemoveAll([Primitive](const TWeakObjectPtr<UPrimitiveComponent>& Existing)
			{
				return Existing.Get() == Primitive;
			});
			if (Removed > 0)
			{
				if (FBodyInstance* Body = Primitive->GetBodyInstance())
				{
					Body->SetContactModification(false);
				}
			}
		}
	}

	PushPhysicsCallbackInputs(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f);
}

bool APortal::IsSimulatingThroughPortal(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComp : SimulatingComponents)
	{
		const UPrimitiveComponent* Comp = WeakComp.Get();
		if (Comp && Comp->GetOwner() == Actor)
		{
			return true;
		}
	}
	return false;
}

void APortal::TransferSimulatingThroughPortal(AActor* Actor, APortal* Destination)
{
	if (!Actor || !Destination)
	{
		return;
	}

	TArray<TWeakObjectPtr<UPrimitiveComponent>> Moved;
	for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComp : SimulatingComponents)
	{
		UPrimitiveComponent* Comp = WeakComp.Get();
		if (Comp && Comp->GetOwner() == Actor)
		{
			Moved.Add(WeakComp);
		}
	}

	if (Moved.Num() == 0)
	{
		return;
	}

	for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComp : Moved)
	{
		SimulatingComponents.Remove(WeakComp);
		if (!Destination->SimulatingComponents.Contains(WeakComp))
		{
			Destination->SimulatingComponents.Add(WeakComp);
		}
	}

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	PushPhysicsCallbackInputs(DeltaTime);
	Destination->PushPhysicsCallbackInputs(DeltaTime);
}

void APortal::CachePortalPhysicsObjects()
{
	if (bPortalPhysicsObjectsCached)
	{
		return;
	}

	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Chaos::FPhysicsObject* Object = Primitive ? Primitive->GetPhysicsObjectByName(NAME_None) : nullptr)
		{
			CachedPortalPhysicsObjects.Add(Object);
		}
	}
	bPortalPhysicsObjectsCached = true;
}

void APortal::PushPhysicsCallbackInputs(float DeltaTime)
{
	if (!EntryContactCallback)
	{
		return;
	}

	SimulatingComponents.RemoveAll([](const TWeakObjectPtr<UPrimitiveComponent>& Existing)
	{
		return !Existing.IsValid();
	});

	FPortalEntryContactInput* EntryInput = EntryContactCallback->GetProducerInputData_External();
	if (EntryInput)
	{
		EntryInput->Bodies.Reset();
		EntryInput->PortalObjects.Reset();
		EntryInput->PortalObjects.Append(CachedPortalPhysicsObjects);
		if (OtherPortal)
		{
			OtherPortal->CachePortalPhysicsObjects();
			EntryInput->PortalObjects.Append(OtherPortal->CachedPortalPhysicsObjects);
		}
	}

	if (!OtherPortal)
	{
		return;
	}

	FQuat TravelQ = FQuat::Identity;
	if (!GetTravelQuat(TravelQ))
	{
		return;
	}

	const FVector EntryOrigin = GetPlaneOrigin();
	const FVector EntryNormal = GetActorForwardVector();
	const FVector ExitOrigin = OtherPortal->GetPlaneOrigin();
	const FVector ExitNormal = OtherPortal->GetActorForwardVector();

	for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComp : SimulatingComponents)
	{
		UPrimitiveComponent* Comp = WeakComp.Get();
		FBodyInstance* Body = Comp ? Comp->GetBodyInstance() : nullptr;
		Chaos::FPhysicsObject* PhysicsObject = Comp ? Comp->GetPhysicsObjectByName(NAME_None) : nullptr;
		if (!Comp || !Body || !PhysicsObject || !Comp->IsSimulatingPhysics())
		{
			continue;
		}

		FPortalTrackedBody Tracked;
		Tracked.PhysicsObject = PhysicsObject;
		Tracked.PlaneOrigin = EntryOrigin;
		Tracked.PlaneNormal = EntryNormal;
		Tracked.PlaneRight = GetActorRightVector();
		Tracked.PlaneUp = GetActorUpVector();
		{
			float HalfW = 50.f;
			float HalfH = 50.f;
			GetApertureHalfExtents(HalfW, HalfH);
			// Slightly shrink vs the visual quad so rim contacts just outside the hole stay solid.
			Tracked.ApertureHalfWidth = HalfW * 0.9f;
			Tracked.ApertureHalfHeight = HalfH * 0.9f;
		}
		Tracked.EntryOrigin = EntryOrigin;
		Tracked.ExitOrigin = ExitOrigin;
		Tracked.ExitPlaneNormal = ExitNormal;
		Tracked.TravelQ = TravelQ;
		Tracked.bFitsAperture = ActorFitsAperture(Comp->GetOwner());

		const FTransform BodyTM = Body->GetUnrealWorldTransform();
		FVector ExitLocation = FVector::ZeroVector;
		if (TranslatePoint(BodyTM.GetLocation(), ExitLocation))
		{
			const FQuat ExitRotation = TravelQ * BodyTM.GetRotation();
			const float Inflation = Comp->GetPhysicsLinearVelocity().Size() * FMath::Max(DeltaTime, 0.f) + 1.f;
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PortalExitPhysics), false, Comp->GetOwner());
			QueryParams.AddIgnoredActor(this);
			QueryParams.AddIgnoredActor(OtherPortal);
			const AActor* Clone = TeleportingObjects.FindRef(Comp->GetOwner());
			if (Clone)
			{
				QueryParams.AddIgnoredActor(Clone);
			}

			FCollisionResponseParams ResponseParams;
			ResponseParams.CollisionResponse = Body->GetResponseToChannels();
			TArray<FOverlapResult> Overlaps;
			FComponentQueryParams ShapeParams(SCENE_QUERY_STAT(PortalExitPhysics), Comp->GetOwner());
			ShapeParams.AddIgnoredActor(this);
			ShapeParams.AddIgnoredActor(OtherPortal);
			if (Clone)
			{
				ShapeParams.AddIgnoredActor(Clone);
			}
			Body->OverlapMulti(Overlaps, GetWorld(), nullptr, ExitLocation, ExitRotation, Comp->GetCollisionObjectType(), ShapeParams, ResponseParams);
			TArray<FOverlapResult> BoundsOverlaps;
			GetWorld()->OverlapMultiByChannel(BoundsOverlaps, ExitLocation, ExitRotation, Comp->GetCollisionObjectType(), Comp->GetCollisionShape(Inflation), QueryParams, ResponseParams);
			Overlaps.Append(BoundsOverlaps);

			for (const FOverlapResult& Overlap : Overlaps)
			{
				UPrimitiveComponent* HitComp = Overlap.GetComponent();
				AActor* HitOwner = HitComp ? HitComp->GetOwner() : nullptr;
				if (!HitComp || !HitOwner || HitOwner == Comp->GetOwner() || HitOwner->IsA(APortal::StaticClass()))
				{
					continue;
				}

				Chaos::FPhysicsObject* HitObject = HitComp->GetPhysicsObjectByName(NAME_None);
				if (!HitObject || Tracked.ExitBodies.Contains(HitObject))
				{
					continue;
				}

				Tracked.ExitBodies.Add(HitObject);
			}
		}

		if (EntryInput)
		{
			EntryInput->Bodies.Add(MoveTemp(Tracked));
		}
	}
}

//Setup when there is an existing portal
void APortal::SetUp(AActor* TargetSurface, const bool bIsBlue, APortal* Portal)
{
	if (!bIsBlue)
	{
		Plane->SetMaterial(0, OrangeMaterial);
		CaptureComponent->TextureTarget = OrangeTextureTarget;
	}

	if (Portal)
	{
	Link(Portal);
	OtherPortal->Link(this);
		NotifyCurrentOverlaps();
		OtherPortal->NotifyCurrentOverlaps();
	}
}

void APortal::NotifyCurrentOverlaps()
{
	if (!ReverseCollider || !OtherPortal)
	{
		return;
	}

	ReverseCollider->UpdateOverlaps();
	TArray<UPrimitiveComponent*> OverlappingComponents;
	ReverseCollider->GetOverlappingComponents(OverlappingComponents);
	for (UPrimitiveComponent* Component : OverlappingComponents)
	{
		AActor* Actor = Component ? Component->GetOwner() : nullptr;
		if (!Actor || Actor == this || Actor == OtherPortal)
		{
			continue;
		}

		OnOverlapBegin(ReverseCollider, Actor, Component, INDEX_NONE, false, FHitResult());
	}
}

void APortal::GetApertureHalfExtents(float& OutHalfWidth, float& OutHalfHeight) const
{
	OutHalfWidth = ApertureHalfWidth;
	OutHalfHeight = ApertureHalfHeight;

	const FVector PortalRight = GetActorRightVector();
	const FVector PortalUp = GetActorUpVector();

	auto HalfExtentAlong = [](const UBoxComponent* Box, const FVector& Axis) -> float
	{
		if (!Box || Axis.IsNearlyZero())
		{
			return 0.f;
		}
		const FVector Ext = Box->GetScaledBoxExtent();
		const FTransform TM = Box->GetComponentTransform();
		return FMath::Abs(FVector::DotProduct(TM.GetUnitAxis(EAxis::X), Axis)) * Ext.X
			+ FMath::Abs(FVector::DotProduct(TM.GetUnitAxis(EAxis::Y), Axis)) * Ext.Y
			+ FMath::Abs(FVector::DotProduct(TM.GetUnitAxis(EAxis::Z), Axis)) * Ext.Z;
	};

	// Width of the opening = length of the top/bottom rim strips along portal Right.
	const float TopW = HalfExtentAlong(Top, PortalRight);
	const float BottomW = HalfExtentAlong(Bottom, PortalRight);
	const float Width = FMath::Max(TopW, BottomW);

	// Height of the opening = length of the left/right rim strips along portal Up.
	const float LeftH = HalfExtentAlong(Left, PortalUp);
	const float RightH = HalfExtentAlong(Right, PortalUp);
	const float Height = FMath::Max(LeftH, RightH);

	if (Width > KINDA_SMALL_NUMBER)
	{
		OutHalfWidth = Width;
	}
	if (Height > KINDA_SMALL_NUMBER)
	{
		OutHalfHeight = Height;
	}
}

void APortal::SyncReverseColliderToAperture()
{
	if (!ReverseCollider)
	{
		return;
	}

	float HalfW = ApertureHalfWidth;
	float HalfH = ApertureHalfHeight;
	GetApertureHalfExtents(HalfW, HalfH);
	// Lateral = aperture. Depth must reach past a long/angled body's center while its near
	// face is still in front of the solid portal plane (else it bounces before BeginOverlap).
	const float HalfDepth = FMath::Max(HalfW, HalfH) * 2.f;
	ReverseCollider->SetBoxExtent(FVector(HalfDepth, HalfW, HalfH));
}

float APortal::GetActorHalfExtentAlong(const AActor* Actor, const FVector& Dir) const
{
	if (!Actor || Dir.IsNearlyZero())
	{
		return 0.f;
	}

	const FVector N = Dir.GetSafeNormal();
	float MaxAbs = 0.f;
	TArray<UPrimitiveComponent*> Primitives;
	Actor->GetComponents<UPrimitiveComponent>(Primitives);
	for (const UPrimitiveComponent* Prim : Primitives)
	{
		if (!Prim || (!Prim->IsSimulatingPhysics() && !Prim->GetGenerateOverlapEvents()))
		{
			continue;
		}

		FVector LocalExt = Prim->Bounds.BoxExtent;
		if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Prim))
		{
			if (Mesh->GetStaticMesh())
			{
				LocalExt = Mesh->GetStaticMesh()->GetBoundingBox().GetExtent();
			}
		}
		const FTransform TM = Prim->GetComponentTransform();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector LocalCorner(
				(Corner & 1) ? LocalExt.X : -LocalExt.X,
				(Corner & 2) ? LocalExt.Y : -LocalExt.Y,
				(Corner & 4) ? LocalExt.Z : -LocalExt.Z);
			const FVector WorldCorner = TM.TransformPosition(LocalCorner);
			MaxAbs = FMath::Max(MaxAbs, FMath::Abs(FVector::DotProduct(WorldCorner - Actor->GetActorLocation(), N)));
		}
	}
	return MaxAbs;
}

bool APortal::ShouldKeepTransitAfterOverlapEnd(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	const FVector Origin = GetPlaneOrigin();
	const FVector Normal = GetActorForwardVector();
	const float Side = FVector::DotProduct(Actor->GetActorLocation() - Origin, Normal);
	// Whole body is in front of the plane only once center is farther out than the OBB
	// half-extent along the portal normal. Until then the actor still straddles / crosses.
	const float HalfAlongNormal = GetActorHalfExtentAlong(Actor, Normal);
	return Side <= HalfAlongNormal;
}

void APortal::Link(APortal* Portal)
{
	OtherPortal = Portal;
	UMaterialInstanceDynamic* PortalMID = Plane->CreateDynamicMaterialInstance(0);
	PortalMID->SetScalarParameterValue(TEXT("Linked"), 1.f);
}

bool APortal::GetTravelQuat(FQuat& OutTravelQ) const
{
	if (!OtherPortal)
	{
		return false;
	}
	OutTravelQ = OtherPortal->GetActorQuat() * FQuat(FVector::ZAxisVector, PI) * GetActorQuat().Inverse();
	return true;
}

bool APortal::TranslatePoint(const FVector& WorldPoint, FVector& OutPoint) const
{
	FQuat TravelQ = FQuat::Identity;
	if (!GetTravelQuat(TravelQ) || !Plane || !OtherPortal->Plane)
	{
		return false;
	}

	OutPoint = OtherPortal->GetPlaneOrigin() + TravelQ.RotateVector(WorldPoint - GetPlaneOrigin());
	return true;
}

bool APortal::TranslateDirection(const FVector& WorldDirection, FVector& OutDirection) const
{
	FQuat TravelQ = FQuat::Identity;
	if (!GetTravelQuat(TravelQ))
	{
		return false;
	}

	OutDirection = TravelQ.RotateVector(WorldDirection);
	return true;
}

bool APortal::IsPointBehindPlane(const FVector& Point, const float Cutoff) const
{
	const FVector Origin = Plane ? Plane->GetComponentLocation() : GetActorLocation();
	return FVector::DotProduct(Point - Origin, GetActorForwardVector()) <= Cutoff;
}

bool APortal::ActorFitsAperture(const AActor* Actor) const
{
	if (!Actor || !Plane)
	{
		return true;
	}

	float HalfW = 50.f;
	float HalfH = 50.f;
	GetApertureHalfExtents(HalfW, HalfH);
	const FVector Origin = GetPlaneOrigin();
	const FVector Normal = GetActorForwardVector();
	const FVector PlaneRight = GetActorRightVector();
	const FVector PlaneUp = GetActorUpVector();
	// Cross-section at the portal plane (not full silhouette) so long bodies can thread.
	constexpr float SliceHalfDepthCm = 8.f;

	float SliceMaxRight = 0.f;
	float SliceMaxUp = 0.f;
	float FullMaxRight = 0.f;
	float FullMaxUp = 0.f;
	bool bHaveSlice = false;
	bool bHaveFull = false;

	auto ConsiderFull = [&](const FVector& WorldPoint)
	{
		bHaveFull = true;
		FullMaxRight = FMath::Max(FullMaxRight, FMath::Abs(FVector::DotProduct(WorldPoint - Origin, PlaneRight)));
		FullMaxUp = FMath::Max(FullMaxUp, FMath::Abs(FVector::DotProduct(WorldPoint - Origin, PlaneUp)));
	};
	auto ConsiderSlice = [&](const FVector& WorldPoint)
	{
		const float Side = FVector::DotProduct(WorldPoint - Origin, Normal);
		if (FMath::Abs(Side) > SliceHalfDepthCm)
		{
			return;
		}
		bHaveSlice = true;
		SliceMaxRight = FMath::Max(SliceMaxRight, FMath::Abs(FVector::DotProduct(WorldPoint - Origin, PlaneRight)));
		SliceMaxUp = FMath::Max(SliceMaxUp, FMath::Abs(FVector::DotProduct(WorldPoint - Origin, PlaneUp)));
	};

	TArray<UPrimitiveComponent*> Primitives;
	Actor->GetComponents<UPrimitiveComponent>(Primitives);
	for (const UPrimitiveComponent* Prim : Primitives)
	{
		if (!Prim || !Prim->IsSimulatingPhysics())
		{
			continue;
		}

		FVector LocalExt = Prim->Bounds.BoxExtent;
		if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Prim))
		{
			if (Mesh->GetStaticMesh())
			{
				LocalExt = Mesh->GetStaticMesh()->GetBoundingBox().GetExtent();
			}
		}
		const FTransform TM = Prim->GetComponentTransform();
		FVector Corners[8];
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector LocalCorner(
				(Corner & 1) ? LocalExt.X : -LocalExt.X,
				(Corner & 2) ? LocalExt.Y : -LocalExt.Y,
				(Corner & 4) ? LocalExt.Z : -LocalExt.Z);
			Corners[Corner] = TM.TransformPosition(LocalCorner);
			ConsiderFull(Corners[Corner]);
			ConsiderSlice(Corners[Corner]);
		}

		// Edge ∩ portal plane samples for a true cross-section while straddling.
		static const int32 Edges[12][2] = {
			{0,1},{2,3},{4,5},{6,7},
			{0,2},{1,3},{4,6},{5,7},
			{0,4},{1,5},{2,6},{3,7}
		};
		for (const int32* Edge : Edges)
		{
			const FVector& A = Corners[Edge[0]];
			const FVector& B = Corners[Edge[1]];
			const float SideA = FVector::DotProduct(A - Origin, Normal);
			const float SideB = FVector::DotProduct(B - Origin, Normal);
			if (SideA * SideB > 0.f && FMath::Abs(SideA) > SliceHalfDepthCm && FMath::Abs(SideB) > SliceHalfDepthCm)
			{
				continue;
			}
			const float Denom = SideA - SideB;
			if (FMath::Abs(Denom) <= KINDA_SMALL_NUMBER)
			{
				ConsiderSlice(A);
				ConsiderSlice(B);
				continue;
			}
			const float T = SideA / Denom;
			if (T >= 0.f && T <= 1.f)
			{
				ConsiderSlice(FMath::Lerp(A, B, T));
			}
		}
	}

	if (bHaveSlice)
	{
		return SliceMaxRight <= HalfW && SliceMaxUp <= HalfH;
	}
	if (!bHaveFull)
	{
		return true;
	}
	return FullMaxRight <= HalfW && FullMaxUp <= HalfH;
}

// Called every frame
void APortal::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (OtherPortal == nullptr)
	{
		return;
	}

	if (!(PlayerController == nullptr || LocalPlayer == nullptr))
	{
		FQuat TravelQ = FQuat::Identity;
		FVector ExitCameraLocation = FVector::ZeroVector;
		const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
		const FQuat CameraRotation = PlayerController->PlayerCameraManager->GetCameraRotation().Quaternion();
		if (GetTravelQuat(TravelQ) && TranslatePoint(CameraLocation, ExitCameraLocation))
		{
			OtherPortal->CaptureComponent->SetWorldLocationAndRotation(ExitCameraLocation, TravelQ * CameraRotation);
		}

		// The game locks vertical FOV and lets horizontal FOV follow the viewport. A single FOV
		// number on the capture keeps horizontal FOV instead, so the portal picture squeezes sideways.
		if (ULocalPlayer* ViewportPlayer = PlayerController->GetLocalPlayer())
		{
			if (UGameViewportClient* GameViewport = ViewportPlayer->ViewportClient)
			{
				if (FViewport* Viewport = GameViewport->Viewport)
				{
					FSceneViewProjectionData ProjectionData;
					if (ViewportPlayer->GetProjectionData(Viewport, ProjectionData))
					{
						OtherPortal->CaptureComponent->bUseCustomProjectionMatrix = true;
						OtherPortal->CaptureComponent->CustomProjectionMatrix = ProjectionData.ProjectionMatrix;
					}
				}
			}
		}
	}

	if (LocalPlayer && LocalPlayer->GetCurrentPortal() == this)
	{
		if (IsPointBehindPlane(LocalPlayer->GetActorLocation(), -2.0f))
		{
			Teleport(LocalPlayer);
		}
	}

	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		UpdateCloneTransform(Pair.Key, Pair.Value);
		// Deferred ghosting: enable once the body fits. Skip if already latched.
		if (Pair.Key && !Pair.Key->IsA<AActionDemoCharacter>()
			&& !IsSimulatingThroughPortal(Pair.Key)
			&& ActorFitsAperture(Pair.Key))
		{
			SetSimulatingThroughPortal(Pair.Key, true);
		}
	}

	TArray<AActor*> ToHandOff;
	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		if (Pair.Key && Pair.Value && !Pair.Key->IsA<AActionDemoCharacter>()
			&& IsPointBehindPlane(Pair.Key->GetActorLocation(), -2.0f)
			&& ActorFitsAperture(Pair.Key))
		{
			ToHandOff.Add(Pair.Key);
		}
	}
	for (AActor* Original : ToHandOff)
	{
		HandOffTeleportingObject(Original);
	}

	const FLinearColor Origin = FLinearColor(Plane->GetComponentLocation());
	const FLinearColor Normal = FLinearColor(GetActorForwardVector());

	FLinearColor OtherOrigin;
	FLinearColor OtherNormal;

	if (OtherPortal)
	{
		OtherOrigin = FLinearColor(OtherPortal->Plane->GetComponentLocation());
		OtherNormal = FLinearColor(OtherPortal->GetActorForwardVector());
	}

	CaptureComponent->ClipPlaneBase = GetPlaneOrigin();
	CaptureComponent->ClipPlaneNormal = GetActorForwardVector();

	if (LocalPlayer && LocalPlayer->GetCurrentPortal() == this && PlayerController && PlayerController->PlayerCameraManager)
	{
		const FVector CamLoc = PlayerController->PlayerCameraManager->GetCameraLocation();
		const bool bCamBehind = IsPointBehindPlane(CamLoc, 0.f);
		SetActorClip(LocalPlayer, Origin, Normal, bCamBehind);
	}

	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		if (!Pair.Key->IsA<AActionDemoCharacter>())
	{
		SetActorClip(Pair.Key, Origin, Normal, true);
		}
		if (OtherPortal && Pair.Value)
		{
			SetActorClip(Pair.Value, OtherOrigin, OtherNormal, true);
		}
	}

	PushPhysicsCallbackInputs(DeltaTime);
#ifdef WITH_EDITOR
	if (EntryContactCallback && GEngine)
	{
		FPortalCopyStats Stats;
		bool bActive = false;
		EntryContactCallback->GetCopyDebug(Stats, bActive);
		if (bActive)
		{
			const FColor Color = Stats.NewPoints > 0 ? FColor::Orange : (Stats.Detected > 0 ? FColor::Green : FColor::Red);
			const TCHAR* State = Stats.NewPoints > 0 ? TEXT("new points added") : (Stats.Written > 0 ? TEXT("no new points") : TEXT("no contacts"));
			GEngine->AddOnScreenDebugMessage(PortalCopyMessageKey, 0.f, Color,
				FString::Printf(TEXT("%s copy: %s  bodies=%d exits=%d leaves=%d unk=%d det=%d new=%d kept=%d dropW=%d dropS=%d keepF=%d"),
					*GetName(), State, Stats.Bodies, Stats.Exits, Stats.Leaves, Stats.Unknown, Stats.Detected, Stats.NewPoints, Stats.KeptPoints,
					Stats.DroppedBehindWall, Stats.DroppedSurface, Stats.KeptFloorBehind));
		}
	}
#endif
}

AActor* APortal::CreatePortalClone(AActor* Original, const FTransform& SpawnTransform)
{
	if (!Original || !GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	AActor* Clone = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform, Params);
	if (!Clone)
	{
		return nullptr;
	}

	USceneComponent* Root = NewObject<USceneComponent>(Clone, TEXT("CloneRoot"));
	Clone->SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
	Clone->FinishSpawning(SpawnTransform);
	Root->RegisterComponent();
	Clone->SetActorTickEnabled(false);

	TArray<UStaticMeshComponent*> Meshes;
	Original->GetComponents<UStaticMeshComponent>(Meshes);
	for (UStaticMeshComponent* Source : Meshes)
	{
		if (!Source || !Source->GetStaticMesh())
		{
			continue;
		}

		UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(Clone);
		Copy->SetStaticMesh(Source->GetStaticMesh());
		Copy->SetHiddenInGame(Source->bHiddenInGame);
		Copy->SetMobility(EComponentMobility::Movable);
		const FTransform Relative = Source->GetComponentTransform().GetRelativeTransform(Original->GetActorTransform());
		Copy->SetupAttachment(Root);
		Copy->SetRelativeTransform(Relative);
		const int32 NumMaterials = Source->GetNumMaterials();
		for (int32 Index = 0; Index < NumMaterials; ++Index)
		{
			Copy->SetMaterial(Index, Source->GetMaterial(Index));
		}

		Copy->SetCollisionProfileName(Source->GetCollisionProfileName());
		Copy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Copy->SetCollisionObjectType(Source->GetCollisionObjectType());
		Copy->SetCollisionResponseToChannels(Source->GetCollisionResponseToChannels());
		Copy->CanCharacterStepUpOn = Source->CanCharacterStepUpOn;
		Copy->SetSimulatePhysics(false);
		Copy->SetEnableGravity(false);
		Copy->SetGenerateOverlapEvents(false);
		Copy->SetNotifyRigidBodyCollision(false);
		Copy->RegisterComponent();
		Clone->AddInstanceComponent(Copy);
	}

	return Clone;
}

void APortal::UpdateCloneTransform(AActor* Original, AActor* Clone)
{
	if (!Original || !Clone)
	{
		return;
	}

	FQuat TravelQ = FQuat::Identity;
	FVector ExitLocation = FVector::ZeroVector;
	if (!GetTravelQuat(TravelQ) || !TranslatePoint(Original->GetActorLocation(), ExitLocation))
	{
		return;
	}

	const FQuat ExitRotation = TravelQ * Original->GetActorQuat();
	Clone->SetActorTransform(FTransform(ExitRotation, ExitLocation, Original->GetActorScale3D()), false, nullptr, ETeleportType::TeleportPhysics);
}

void APortal::HandOffTeleportingObject(AActor* Original)
{
	if (!OtherPortal || !Original || !TeleportingObjects.Contains(Original))
	{
		return;
	}

	AActor* Clone = TeleportingObjects[Original];
	if (!Clone)
	{
		TeleportingObjects.Remove(Original);
		return;
	}

	UpdateCloneTransform(Original, Clone);

	UPrimitiveComponent* OriginalComp = Original->FindComponentByClass<UPrimitiveComponent>();
	const FTransform OriginalTransform = Original->GetActorTransform();
	FVector LinVel = OriginalComp ? OriginalComp->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	FVector AngVel = OriginalComp ? OriginalComp->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
	FQuat TravelQ = FQuat::Identity;
	if (GetTravelQuat(TravelQ))
	{
		LinVel = TravelQ.RotateVector(LinVel);
		AngVel = TravelQ.RotateVector(AngVel);
	}

	TeleportingObjects.Remove(Original);
	OtherPortal->TeleportingObjects.Add(Original, Clone);

	Original->SetActorTransform(Clone->GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
	Clone->SetActorTransform(OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
	if (OriginalComp && OriginalComp->IsSimulatingPhysics())
	{
		OriginalComp->SetPhysicsLinearVelocity(LinVel);
		OriginalComp->SetPhysicsAngularVelocityInRadians(AngVel);
	}

	TransferSimulatingThroughPortal(Original, OtherPortal);
}

void APortal::Teleport(AActionDemoCharacter* Character)
{
	const FQuat ExitQ = OtherPortal->GetActorQuat();
	FQuat TravelQ = FQuat::Identity;
	if (!GetTravelQuat(TravelQ))
	{
		return;
	}

	FVector ExitLocation = FVector::ZeroVector;
	if (!TranslatePoint(Character->GetActorLocation(), ExitLocation))
	{
		return;
	}

	const FQuat CaptureQ = OtherPortal->CaptureComponent->GetComponentQuat();
	PlayerController->SetControlRotation(CaptureQ.Rotator());

	// Move the existing image to this side before the character arrives, so the exit overlap does not spawn another one in front of the camera.
	if (AActor* Clone = TeleportingObjects.FindRef(Character))
	{
		const FTransform StayTransform = Character->GetActorTransform();
		TeleportingObjects.Remove(Character);
		OtherPortal->TeleportingObjects.Add(Character, Clone);
		Clone->SetActorTransform(StayTransform, false, nullptr, ETeleportType::TeleportPhysics);
		SetActorClip(Clone, FLinearColor(GetPlaneOrigin()), FLinearColor(GetActorForwardVector()), true);
		TransferSimulatingThroughPortal(Character, OtherPortal);
	}

	const FQuat ExitRotation = TravelQ * Character->GetActorQuat();
	Character->SetActorLocationAndRotation(ExitLocation, ExitRotation, false, nullptr, ETeleportType::TeleportPhysics);

	Character->AlignMovement(TravelQ, ExitQ);

	if (PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->UpdateCamera(0.f);
	}
}

void APortal::SetActorClip(AActor* Actor, const FLinearColor Origin, const FLinearColor Normal, const bool bEnable)
{
	if (!Actor) return;
	TArray<UMeshComponent*> Meshes;
	Actor->GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		const int32 Num = Mesh->GetNumMaterials();
		for (int32 i = 0; i < Num; ++i)
		{
			UMaterialInterface* Mat = Mesh->GetMaterial(i);
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat);
			if (!MID)
			{
				MID = Mesh->CreateDynamicMaterialInstance(i, Mat);
			}

			if (!MID)
			{
				continue;
			}

			MID->SetScalarParameterValue(TEXT("ClipEnabled"), bEnable ? 1.f : 0.f);
			MID->SetVectorParameterValue(TEXT("ClipPlaneOrigin"), Origin);
			MID->SetVectorParameterValue(TEXT("ClipPlaneNormal"), Normal);
		}
	}
}

void APortal::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherPortal != nullptr)
	{
		if (AActionDemoCharacter* OverlappedCharacter = Cast<AActionDemoCharacter>(OtherActor))
		{
			if (OverlappedCharacter == LocalPlayer)
			{
				OverlappedCharacter->SetCurrentPortal(this);
			}
		}

		if (TeleportingObjects.Contains(OtherActor))
		{
			return;
		}

		for (const TPair<AActor*, AActor*>& Pair : OtherPortal->TeleportingObjects)
		{
			if (Pair.Value == OtherActor) //This is a clone of the original
			{
				return;
			}
		}

		FQuat TravelQ = FQuat::Identity;
		FVector Location = FVector::ZeroVector;
		if (!GetTravelQuat(TravelQ) || !TranslatePoint(OtherActor->GetActorLocation(), Location))
		{
			return;
		}

		const FTransform NewTrans(TravelQ * OtherActor->GetActorQuat(), Location, OtherActor->GetActorScale3D());
		if (AActor* ClonedActor = CreatePortalClone(OtherActor, NewTrans))
		{
			TeleportingObjects.Add(OtherActor, ClonedActor);
			// Defer contact-mod until the body fits — otherwise a captured wide slab ghosts the
			// aperture volume and falls through the wall hole without ever handing off.
			if (ActorFitsAperture(OtherActor))
			{
				SetSimulatingThroughPortal(OtherActor, true);
			}
		}
	}
}

void APortal::OnOverlapEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AActionDemoCharacter* OverlappedCharacter = Cast<AActionDemoCharacter>(OtherActor))
	{
		if (OverlappedCharacter == LocalPlayer && OverlappedCharacter->GetCurrentPortal() == this)
		{
			OverlappedCharacter->SetCurrentPortal(nullptr);
		}
	}

	if (!OtherActor || !TeleportingObjects.Contains(OtherActor))
	{
		return;
	}

	// Overlap volume is finite; keep capture while the body still straddles the portal plane.
	if (ShouldKeepTransitAfterOverlapEnd(OtherActor))
	{
		return;
	}

	AActor* Clone = TeleportingObjects[OtherActor];
	if (Clone)
	{
		VDestroy(Clone);
	}

	SetActorClip(OtherActor, FLinearColor(Plane->GetComponentLocation()), FLinearColor(GetActorForwardVector()), false);
	SetSimulatingThroughPortal(OtherActor, false);
	TeleportingObjects.Remove(OtherActor);
}

void APortal::VDestroy(AActor* ToDestroy)
{
	if (!ToDestroy) return;
	if (!ToDestroy->IsValidLowLevel()) return;

	ToDestroy->K2_DestroyActor();
	ToDestroy = NULL;

	GEngine->ForceGarbageCollection(true);
}
