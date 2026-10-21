// Fill out your copyright notice in the Description page of Project Settings.


#include "Portal.h"
#include "../ActionDemoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
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
#include "Materials/MaterialInstanceDynamic.h"

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
	/** Rim frame boxes — contact-mod never disables these (side collision must stay solid). */
	TArray<Chaos::FPhysicsObjectHandle> RimObjects;

	void Reset()
	{
		Bodies.Reset();
		PortalObjects.Reset();
		RimObjects.Reset();
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

	void InjectExitContacts(const TArray<FPortalTrackedBody>& Bodies, Chaos::FPBDRigidsSolver* Solver, const Chaos::FReal Dt, TArray<Chaos::FPBDCollisionConstraintPtr>& OwnedConstraints)
	{
		if (!Solver || !Solver->GetEvolution() || Dt <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		Chaos::FPBDRigidsEvolutionGBF& Evolution = *Solver->GetEvolution();
		Chaos::FPBDCollisionConstraints& Collisions = Evolution.GetCollisionConstraints();
		Chaos::Private::FCollisionConstraintAllocator& Allocator = Collisions.GetConstraintAllocator();
		Chaos::Private::FCollisionContextAllocator* ContextAllocator = Allocator.GetContextAllocator(0);
		if (!ContextAllocator)
		{
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
							}
						}
					}
				}
			}
		}

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

private:
	// Constraints the generic midphase will not own. Freed one step after the graph drops them.
	TArray<Chaos::FPBDCollisionConstraintPtr> OwnedConstraints;
	TArray<Chaos::FPBDCollisionConstraintPtr> RetiredConstraints;

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
			return;
		}

		Chaos::FReadPhysicsObjectInterface_Internal Physics = Chaos::FPhysicsObjectInternalInterface::GetRead();

		auto IsInObjectList = [&Physics](const Chaos::FGeometryParticleHandle* Particle, const TArray<Chaos::FPhysicsObjectHandle>& Objects) mutable
		{
			for (Chaos::FPhysicsObjectHandle Object : Objects)
			{
				if (Physics.GetParticle(Object) == Particle)
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

		for (Chaos::FContactPairModifier& Pair : Modifier)
		{
			const Chaos::TVec2<Chaos::FGeometryParticleHandle*> Particles = Pair.GetParticlePair();
			if (!Particles[0] || !Particles[1])
			{
				continue;
			}

			// Rim colliders stay solid — never strip side/top/bottom hits (prevents rim-jam ghosts).
			if (IsInObjectList(Particles[0], Input->RimObjects) || IsInObjectList(Particles[1], Input->RimObjects))
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
			const bool bPortalSurface = IsInObjectList(Particles[0], Input->PortalObjects) || IsInObjectList(Particles[1], Input->PortalObjects);
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
				// Behind the entry plane: ghost walls/obstacles (incl. behind-entry blockers),
				// keep floor support. Rim pairs never reach here (RimObjects early-out).
				if (Side < 0.f)
				{
					if (FloorAligned <= 0.5f)
					{
						Pair.SetContactPointDisabled(Index);
					}
					continue;
				}
				// Fitting travelers: open the hole. RimObjects + near-plane band keep the frame solid.
				constexpr float ApertureFrontSlackCm = 30.f;
				if (Tracked->bFitsAperture && bInAperture && Side < ApertureFrontSlackCm)
				{
					if (FloorAligned <= 0.5f)
					{
						Pair.SetContactPointDisabled(Index);
					}
					continue;
				}
				// Portal-owned face: only open when the plane-slice fits.
				if (bPortalSurface)
				{
					if (Tracked->bFitsAperture && bInAperture && FloorAligned <= 0.5f
						&& FMath::Abs(WallAligned) > 0.5f)
					{
						Pair.SetContactPointDisabled(Index);
					}
					continue;
				}
				if (Tracked->bFitsAperture && bInAperture
					&& IsPortalSurfaceRejection(OtherSide, PushOnCube, Tracked->PlaneNormal))
				{
					Pair.SetContactPointDisabled(Index);
				}
			}
		}

		InjectExitContacts(Input->Bodies, static_cast<Chaos::FPBDRigidsSolver*>(GetSolver()), GetDeltaTime_Internal(), OwnedConstraints);
	}
};

// Sets default values
APortal::APortal()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = true;

	// CreateDefaultSubobject already owns these — do not AddOwnedComponent (causes
	// template/instance attach ensures when the BP/level is duplicated for PIE).
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	SetRootComponent(SceneRoot);

	CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("OtherPortalView"));
	CaptureComponent->SetupAttachment(SceneRoot);
	CaptureComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f).Quaternion());

	ReverseCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("ReverseCollider"));
	ReverseCollider->SetupAttachment(SceneRoot);
	ReverseCollider->SetRelativeScale3D(FVector(1.f, 1.f, 1.f));
	// Shallow slab in front of the plane — capture/clone only while near the opening.
	constexpr float ReverseHalfDepthCm = 20.f;
	ReverseCollider->SetBoxExtent(FVector(ReverseHalfDepthCm, 64.f, 112.f));
	ReverseCollider->SetRelativeLocation(FVector(ReverseHalfDepthCm, 0.f, 0.f));
	ReverseCollider->SetGenerateOverlapEvents(true);
	ReverseCollider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ReverseCollider->SetCollisionResponseToAllChannels(ECR_Ignore);
	ReverseCollider->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	ReverseCollider->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	ReverseCollider->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ReverseCollider->OnComponentBeginOverlap.AddDynamic(this, &APortal::OnOverlapBegin);
	ReverseCollider->OnComponentEndOverlap.AddDynamic(this, &APortal::OnOverlapEnd);

	// Subobject names must match UPROPERTY names — renaming CreateDefaultSubobject
	// left PortalEdgeColliders/PortalRim* ghosts on BP_Portal beside EdgeColliders/Top/….
	EdgeColliders = CreateDefaultSubobject<USceneComponent>(TEXT("EdgeColliders"));
	EdgeColliders->SetupAttachment(SceneRoot);
	EdgeColliders->SetRelativeLocation(FVector::ZeroVector);
	EdgeColliders->SetRelativeRotation(FRotator::ZeroRotator);
	EdgeColliders->SetRelativeScale3D(FVector::OneVector);

	auto MakeRim = [this](const TCHAR* Name) -> UBoxComponent*
	{
		UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(Name);
		Box->SetupAttachment(EdgeColliders);
		Box->SetRelativeRotation(FRotator::ZeroRotator);
		Box->SetRelativeScale3D(FVector::OneVector);
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
	Plane->SetupAttachment(SceneRoot);
	Plane->SetRelativeScale3D(FVector(1.25f, 2.25f, 1.0f));
	Plane->SetRelativeRotation(FRotator(0.0f, -90.0f, 90.0f));
	// Solid for physics props until contact-mod ghosts the face for travelers that fit.
	// Pawns stay ignored — character transit uses movement clipping, not this mesh.
	Plane->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Plane->SetCollisionResponseToAllChannels(ECR_Ignore);
	Plane->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	Plane->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
}

void APortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APortal, OtherPortal);
}

// Called when the game starts or when spawned
void APortal::BeginPlay()
{
	Super::BeginPlay();

	// Scene capture is render-only; dedicated server never needs it active.
	if (CaptureComponent)
	{
		if (GetNetMode() == NM_DedicatedServer)
		{
			CaptureComponent->bCaptureEveryFrame = false;
			CaptureComponent->bCaptureOnMovement = false;
			CaptureComponent->Deactivate();
		}
		else
		{
			CaptureComponent->bEnableClipPlane = true;
			CaptureComponent->ClipPlaneBase = GetPlaneOrigin();
			CaptureComponent->ClipPlaneNormal = GetActorForwardVector();
		}
	}

	GetApertureHalfExtents(ApertureHalfWidth, ApertureHalfHeight);

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
	CachedPortalPhysicsObjects.Reset();
	CachedRimPhysicsObjects.Reset();

	// Face only in PortalObjects (aperture-gated). Rims listed separately so contact-mod
	// never disables them — otherwise Side≈-1 rim hits get stripped and bodies jam/ghost.
	if (Plane)
	{
		if (Chaos::FPhysicsObject* Object = Plane->GetPhysicsObjectByName(NAME_None))
		{
			CachedPortalPhysicsObjects.Add(Object);
		}
	}

	auto AddRimObject = [this](UBoxComponent* Rim)
	{
		if (!Rim)
		{
			return;
		}
		if (Chaos::FPhysicsObject* Object = Rim->GetPhysicsObjectByName(NAME_None))
		{
			CachedRimPhysicsObjects.AddUnique(Object);
		}
	};

	for (UBoxComponent* Rim : { Top, Bottom, Left, Right })
	{
		AddRimObject(Rim);
	}

	// BP_Portal may still own SCS duplicates (Top/Bottom/Left/Right) alongside PortalRim*.
	// Those are what props actually hit — include every rim-shaped box on this actor.
	// Re-scan each push: SCS bodies can appear after BeginPlay.
	TArray<UBoxComponent*> Boxes;
	GetComponents<UBoxComponent>(Boxes);
	for (UBoxComponent* Box : Boxes)
	{
		if (!Box || Box == ReverseCollider)
		{
			continue;
		}
		const FString Name = Box->GetName();
		const bool bLooksLikeRim =
			Name.Contains(TEXT("Rim"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("Top")) || Name.Equals(TEXT("Bottom"))
			|| Name.Equals(TEXT("Left")) || Name.Equals(TEXT("Right"))
			|| Name.Contains(TEXT("PortalRim"), ESearchCase::IgnoreCase);
		if (bLooksLikeRim)
		{
			AddRimObject(Box);
		}
	}
	bPortalPhysicsObjectsCached = CachedPortalPhysicsObjects.Num() > 0 || CachedRimPhysicsObjects.Num() > 0;
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
		EntryInput->RimObjects.Reset();
		CachePortalPhysicsObjects();
		EntryInput->PortalObjects.Append(CachedPortalPhysicsObjects);
		EntryInput->RimObjects.Append(CachedRimPhysicsObjects);
		if (OtherPortal)
		{
			OtherPortal->CachePortalPhysicsObjects();
			EntryInput->PortalObjects.Append(OtherPortal->CachedPortalPhysicsObjects);
			EntryInput->RimObjects.Append(OtherPortal->CachedRimPhysicsObjects);
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
	const FVector EntryRight = GetActorRightVector();
	const FVector EntryUp = GetActorUpVector();
	const FVector ExitOrigin = OtherPortal->GetPlaneOrigin();
	const FVector ExitNormal = OtherPortal->GetActorForwardVector();
	float HalfW = ApertureHalfWidth;
	float HalfH = ApertureHalfHeight;
	GetApertureHalfExtents(HalfW, HalfH);
	// Slightly shrink vs the visual quad so rim contacts just outside the hole stay solid.
	const float ApertureHalfW = HalfW * 0.9f;
	const float ApertureHalfH = HalfH * 0.9f;

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
		Tracked.PlaneRight = EntryRight;
		Tracked.PlaneUp = EntryUp;
		Tracked.ApertureHalfWidth = ApertureHalfW;
		Tracked.ApertureHalfHeight = ApertureHalfH;
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
void APortal::Setup(AActor* TargetSurface, const bool bIsBlue, APortal* Portal)
{
	SetupVisual(bIsBlue);

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
	const float HalfAlongNormal = GetActorHalfExtentAlong(Actor, Normal);
	// Reverse is shallow; once EndOverlap fires, keep only while the body still straddles
	// the plane (crossing / just behind). Fully back in the room → drop clone.
	return Side <= HalfAlongNormal;
}

void APortal::Link(APortal* Portal)
{
	OtherPortal = Portal;
	// Authority does not get OnRep; apply linked look locally. Clients use OnRep_OtherPortal.
	OnRep_OtherPortal();
}

void APortal::AbortAllTransit()
{
	TArray<AActor*> Originals;
	TArray<AActor*> Clones;
	Originals.Reserve(TeleportingObjects.Num());
	Clones.Reserve(TeleportingObjects.Num());
	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		if (Pair.Key)
		{
			Originals.Add(Pair.Key);
		}
		if (Pair.Value)
		{
			Clones.Add(Pair.Value);
		}
	}
	TeleportingObjects.Reset();
	for (AActor* Original : Originals)
	{
		SetSimulatingThroughPortal(Original, false);
		if (Plane)
		{
			SetActorClip(Original, FLinearColor(Plane->GetComponentLocation()), FLinearColor(GetActorForwardVector()), false);
		}
	}
	for (AActor* Clone : Clones)
	{
		if (IsValid(Clone))
		{
			Clone->Destroy();
		}
	}
}

bool APortal::GetTravelQuat(FQuat& OutTravelQ) const
{
	if (!OtherPortal)
	{
		return false;
	}
	// Live from current poses (same as pre-cache). Flip is 180° about portal local up
	// so handoff stays correct when portals sit on differently oriented walls.
	OutTravelQ = OtherPortal->GetActorQuat() * FQuat(FVector::UpVector, PI) * GetActorQuat().Inverse();
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

	float HalfW = ApertureHalfWidth;
	float HalfH = ApertureHalfHeight;
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

void APortal::SetupVisual_Implementation(bool bInIsBlue)
{
	if (bInIsBlue)
	{
		return;
	}

	if (Plane)
	{
		Plane->SetMaterial(0, OrangeMaterial);
	}

	if (CaptureComponent && GetNetMode() != NM_DedicatedServer)
	{
		CaptureComponent->TextureTarget = OrangeTextureTarget;
	}
}

void APortal::OnRep_OtherPortal()
{
	if (!OtherPortal || !Plane)
	{
		return;
	}

	UMaterialInterface* Current = Plane->GetMaterial(0);
	UMaterialInstanceDynamic* PortalMID = Cast<UMaterialInstanceDynamic>(Current);
	if (!PortalMID)
	{
		PortalMID = Plane->CreateDynamicMaterialInstance(0, Current);
	}
	if (PortalMID)
	{
		PortalMID->SetScalarParameterValue(TEXT("Linked"), 1.f);
	}
}

// Called every frame
void APortal::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (OtherPortal == nullptr)
	{
		return;
	}

	// Local viewport + owning pawn only (dedicated server has neither).
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (GetNetMode() != NM_DedicatedServer && PC && PC->IsLocalController())
	{
		if (OtherPortal->CaptureComponent)
		{
			if (ULocalPlayer* LocalViewportPlayer = PC->GetLocalPlayer())
			{
				if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
				{
					FQuat TravelQ = FQuat::Identity;
					FVector ExitCameraLocation = FVector::ZeroVector;
					const FVector CameraLocation = CameraManager->GetCameraLocation();
					const FQuat CameraRotation = CameraManager->GetCameraRotation().Quaternion();
					if (GetTravelQuat(TravelQ) && TranslatePoint(CameraLocation, ExitCameraLocation))
					{
						OtherPortal->CaptureComponent->SetWorldLocationAndRotation(ExitCameraLocation, TravelQ * CameraRotation);
					}
				}

				// The game locks vertical FOV and lets horizontal FOV follow the viewport. A single FOV
				// number on the capture keeps horizontal FOV instead, so the portal picture squeezes sideways.
				if (UGameViewportClient* GameViewport = LocalViewportPlayer->ViewportClient)
				{
					if (FViewport* Viewport = GameViewport->Viewport)
					{
						FSceneViewProjectionData ProjectionData;
						if (LocalViewportPlayer->GetProjectionData(Viewport, ProjectionData))
						{
							OtherPortal->CaptureComponent->bUseCustomProjectionMatrix = true;
							OtherPortal->CaptureComponent->CustomProjectionMatrix = ProjectionData.ProjectionMatrix;
						}
					}
				}
			}
		}

		// Owning client detects crossing and predicts; server commits via ServerTeleportThroughPortal.
		if (AActionDemoCharacter* LocalCharacter = Cast<AActionDemoCharacter>(PC->GetCharacter()))
		{
			if (LocalCharacter->GetCurrentPortal() == this
				&& IsPointBehindPlane(LocalCharacter->GetActorLocation(), -2.0f))
			{
				LocalCharacter->RequestPortalTeleport(this);
			}
		}
	}

	if (CaptureComponent && GetNetMode() != NM_DedicatedServer)
	{
		CaptureComponent->ClipPlaneBase = GetPlaneOrigin();
		CaptureComponent->ClipPlaneNormal = GetActorForwardVector();

		const FLinearColor Origin = FLinearColor(Plane->GetComponentLocation());
		const FLinearColor Normal = FLinearColor(GetActorForwardVector());
		const FLinearColor OtherOrigin = FLinearColor(OtherPortal->Plane->GetComponentLocation());
		const FLinearColor OtherNormal = FLinearColor(OtherPortal->GetActorForwardVector());

		for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
		{
			SetActorClip(Pair.Key, Origin, Normal, true);
			if (Pair.Value)
			{
				SetActorClip(Pair.Value, OtherOrigin, OtherNormal, true);
			}
		}
	}

	TArray<AActor*> ToHandOff;
	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		UpdateCloneTransform(Pair.Key, Pair.Value);
		if (!Pair.Key || Pair.Key->IsA<AActionDemoCharacter>())
		{
			continue;
		}

		// Keep contact-mod on for all captured props (behind-entry ghost + rim-safe aperture).
		if (!IsSimulatingThroughPortal(Pair.Key))
		{
			SetSimulatingThroughPortal(Pair.Key, true);
		}
		if (Pair.Value && IsPointBehindPlane(Pair.Key->GetActorLocation(), -2.0f) && ActorFitsAperture(Pair.Key))
		{
			ToHandOff.Add(Pair.Key);
		}
	}
	for (AActor* Original : ToHandOff)
	{
		HandOffTeleportingObject(Original);
	}

	PushPhysicsCallbackInputs(DeltaTime);
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
	if (!Character || !OtherPortal)
	{
		return;
	}

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

	if (APlayerController* CharacterController = Cast<APlayerController>(Character->GetController()))
	{
		const FQuat NewViewQ = TravelQ * CharacterController->GetControlRotation().Quaternion();
		CharacterController->SetControlRotation(NewViewQ.Rotator());
		if (CharacterController->PlayerCameraManager)
		{
			CharacterController->PlayerCameraManager->UpdateCamera(0.f);
		}
	}
	Character->SetCurrentPortal(OtherPortal);
}

void APortal::SetActorClip(AActor* Actor, const FLinearColor Origin, const FLinearColor Normal, const bool bEnable)
{
	// Clip MIDs are view-only. Physics/handoff run on every machine; visuals stay local.
	if (!Actor || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

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
	if (OtherPortal == nullptr)
	{
		return;
	}

	if (AActionDemoCharacter* OverlappedCharacter = Cast<AActionDemoCharacter>(OtherActor))
	{
		OverlappedCharacter->SetCurrentPortal(this);
	}

	if (!OtherActor || TeleportingObjects.Contains(OtherActor))
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
		// Always enable contact-mod while captured so behind-entry obstacles ghost.
		// Aperture opening still only softens when ActorFitsAperture (see contact mod).
		SetSimulatingThroughPortal(OtherActor, true);
	}
}

void APortal::OnOverlapEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AActionDemoCharacter* OverlappedCharacter = Cast<AActionDemoCharacter>(OtherActor))
	{
		if (OverlappedCharacter->GetCurrentPortal() == this)
		{
			OverlappedCharacter->SetCurrentPortal(nullptr);
		}
	}

	// Overlap volume is finite; keep capture while the body still straddles the portal plane.
	if (!OtherActor || !TeleportingObjects.Contains(OtherActor) || ShouldKeepTransitAfterOverlapEnd(OtherActor))
	{
		return;
	}

	AActor* Clone = TeleportingObjects[OtherActor];
	if (IsValid(Clone))
	{
		Clone->Destroy();
	}

	SetActorClip(OtherActor, FLinearColor(Plane->GetComponentLocation()), FLinearColor(GetActorForwardVector()), false);
	SetSimulatingThroughPortal(OtherActor, false);
	TeleportingObjects.Remove(OtherActor);
}
