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
#include "Misc/ScopeLock.h"
#include "Camera/CameraComponent.h"

struct FPortalTrackedBody
{
	Chaos::FPhysicsObjectHandle PhysicsObject = nullptr;
	FVector PlaneOrigin = FVector::ZeroVector;
	FVector PlaneNormal = FVector::ForwardVector;
	FVector EntryOrigin = FVector::ZeroVector;
	FVector ExitOrigin = FVector::ZeroVector;
	FVector ExitPlaneNormal = FVector::ForwardVector;
	FQuat TravelQ = FQuat::Identity;
	TArray<Chaos::FPhysicsObjectHandle> ExitBodies;

	void Reset()
	{
		PhysicsObject = nullptr;
		ExitBodies.Reset();
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
	Chaos::FContactPoint CopyContactPoint(const Chaos::FContactPointf& Src)
	{
		Chaos::FContactPoint Out;
		Out.ShapeContactPoints[0] = Chaos::FVec3(Src.ShapeContactPoints[0]);
		Out.ShapeContactPoints[1] = Chaos::FVec3(Src.ShapeContactPoints[1]);
		Out.ShapeContactNormal = Chaos::FVec3(Src.ShapeContactNormal);
		Out.Phi = Src.Phi;
		Out.FaceIndex = Src.FaceIndex;
		Out.ContactType = Src.ContactType;
		return Out;
	}

	Chaos::FContactPoint SwapContactOrder(const Chaos::FContactPoint& In, const Chaos::FRigidTransform3& Shape0, const Chaos::FRigidTransform3& Shape1)
	{
		const Chaos::FVec3 WorldNormal = Shape1.TransformVectorNoScale(In.ShapeContactNormal);
		Chaos::FContactPoint Out;
		Out.ShapeContactPoints[0] = In.ShapeContactPoints[1];
		Out.ShapeContactPoints[1] = In.ShapeContactPoints[0];
		Out.ShapeContactNormal = Shape0.InverseTransformVectorNoScale(-WorldNormal);
		Out.Phi = In.Phi;
		Out.FaceIndex = In.FaceIndex;
		Out.ContactType = In.ContactType;
		return Out;
	}

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

	FPortalContactKey MakeContactKey(const Chaos::FContactPoint& Contact)
	{
		FPortalContactKey Key;
		Key.Points[0] = FVector(Contact.ShapeContactPoints[0]);
		Key.Points[1] = FVector(Contact.ShapeContactPoints[1]);
		Key.Normal = FVector(Contact.ShapeContactNormal).GetSafeNormal();
		return Key;
	}

	bool IsSameContactKey(const Chaos::FContactPoint& Contact, const FPortalContactKey& Key)
	{
		if (FVector::DotProduct(FVector(Contact.ShapeContactNormal).GetSafeNormal(), Key.Normal) < 0.9f)
		{
			return false;
		}

		constexpr float ToleranceSq = 1.f;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (FVector::DistSquared(FVector(Contact.ShapeContactPoints[Index]), Key.Points[Index]) <= ToleranceSq)
			{
				return true;
			}
		}
		return false;
	}

	// The wall face lies on the portal plane, so a behind-the-plane test never sees it.
	// That contact pushes the body back into the entry room and, because it is the deepest,
	// it also fills the manifold and leaves the floor with no slot. A contact in front of
	// the plane still blocks. A contact that does not push back into the room stays.
	bool IsPortalSurfaceRejection(const float OtherSide, const FVector& PushOnCube, const FVector& PlaneNormal)
	{
		return OtherSide < 1.f && FVector::DotProduct(PushOnCube, PlaneNormal) > 0.7f;
	}

	FVector InverseTravelPoint(const FVector& ExitPoint, const FVector& ExitOrigin, const FVector& EntryOrigin, const FQuat& InvTravel)
	{
		return EntryOrigin + InvTravel.RotateVector(ExitPoint - ExitOrigin);
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

	void AppendRewrittenExitContacts(
		const Chaos::FPBDCollisionConstraint& Detected,
		Chaos::FGeometryParticleHandle* CubeParticle,
		const Chaos::FRigidTransform3& ExitShape0,
		const Chaos::FRigidTransform3& ExitShape1,
		const Chaos::FRigidTransform3& RealShape0,
		const Chaos::FRigidTransform3& RealShape1,
		const FVector& ExitOrigin,
		const FVector& EntryOrigin,
		const FVector& EntryPlaneNormal,
		const FQuat& InvTravel,
		const Chaos::FReal CullDistance,
		const float MaxPenetration,
		TArray<Chaos::FContactPoint, TInlineAllocator<8>>& OutSupport)
	{
		const int32 CubeIndex = Detected.GetParticle0() == CubeParticle ? 0 : 1;
		const int32 OtherIndex = 1 - CubeIndex;

		for (int32 Index = 0; Index < Detected.NumManifoldPoints(); ++Index)
		{
			const Chaos::FManifoldPoint& ManifoldPoint = Detected.GetManifoldPoint(Index);
			if (ManifoldPoint.Flags.bDisabled || !ManifoldPoint.ContactPoint.IsSet() || ManifoldPoint.ContactPoint.Phi >= CullDistance)
			{
				continue;
			}

			const Chaos::FVec3 ExitWorld0 = ExitShape0.TransformPositionNoScale(Chaos::FVec3(ManifoldPoint.ContactPoint.ShapeContactPoints[0]));
			const Chaos::FVec3 ExitWorld1 = ExitShape1.TransformPositionNoScale(Chaos::FVec3(ManifoldPoint.ContactPoint.ShapeContactPoints[1]));
			FVector EntryWorld0 = InverseTravelPoint(FVector(ExitWorld0), ExitOrigin, EntryOrigin, InvTravel);
			FVector EntryWorld1 = InverseTravelPoint(FVector(ExitWorld1), ExitOrigin, EntryOrigin, InvTravel);
			const Chaos::FVec3 ExitWorldNormal = ExitShape1.TransformVectorNoScale(Chaos::FVec3(ManifoldPoint.ContactPoint.ShapeContactNormal));
			const FVector EntryNormal = InvTravel.RotateVector(FVector(ExitWorldNormal)).GetSafeNormal();
			if (EntryNormal.IsNearlyZero())
			{
				continue;
			}

			// In front of the entry plane this is the exit wall. Behind it is the exit floor.
			const FVector ExitOtherPoint = OtherIndex == 0 ? FVector(ExitWorld0) : FVector(ExitWorld1);
			const FVector EntryOther = InverseTravelPoint(ExitOtherPoint, ExitOrigin, EntryOrigin, InvTravel);
			const float EntrySide = FVector::DotProduct(EntryOther - EntryOrigin, EntryPlaneNormal);
			const FVector PushOnCube = CubeIndex == 0 ? EntryNormal : -EntryNormal;
			if (EntrySide > 1.f || IsPortalSurfaceRejection(EntrySide, PushOnCube, EntryPlaneNormal))
			{
				continue;
			}

			// These contacts are rebuilt every frame, so Chaos does not treat the overlap as initial
			// and turns the whole penetration into velocity. A tip makes that penetration deep, and
			// the body is launched. Leave a shallow contact alone and only shorten a deep one.
			FVector& CubePoint = CubeIndex == 0 ? EntryWorld0 : EntryWorld1;
			const FVector& OtherPoint = CubeIndex == 0 ? EntryWorld1 : EntryWorld0;
			const float Separation = FVector::DotProduct(CubePoint - OtherPoint, PushOnCube);
			if (Separation < -MaxPenetration)
			{
				CubePoint += PushOnCube * (-MaxPenetration - Separation);
			}

			// Shape points are in the real shape frames, so the solver feels this contact
			// behind the entry portal even when it rebuilds the world points from the bodies.
			Chaos::FContactPoint Contact = CopyContactPoint(ManifoldPoint.ContactPoint);
			Contact.ShapeContactPoints[0] = RealShape0.InverseTransformPositionNoScale(Chaos::FVec3(EntryWorld0));
			Contact.ShapeContactPoints[1] = RealShape1.InverseTransformPositionNoScale(Chaos::FVec3(EntryWorld1));
			Contact.ShapeContactNormal = RealShape1.InverseTransformVectorNoScale(Chaos::FVec3(EntryNormal));
			Contact.Phi = FMath::Max(ManifoldPoint.ContactPoint.Phi, Chaos::FReal(-MaxPenetration));
			OutSupport.Add(Contact);
		}
	}

	void WriteContacts(Chaos::FPBDCollisionConstraint& Constraint, TArray<Chaos::FContactPoint, TInlineAllocator<8>>& Contacts, const int32 NumSupport, const int32 NumExit, const TArray<FPortalContactKey>& PreviousKeys, TArray<FPortalContactKey>& FrameKeys, int32& OutNew, int32& OutKept)
	{
		if (Contacts.Num() == 0)
		{
			return;
		}

		// The manifold holds 4 points and keeps the deepest. A wall penetration is deeper than the
		// floor, so a plain sort drops the exit floor and the body never feels it.
		TArray<int32, TInlineAllocator<8>> Order;
		auto TakeDeepest = [&Contacts, &Order](const int32 Begin, const int32 End)
		{
			TArray<int32, TInlineAllocator<8>> Indices;
			for (int32 Index = Begin; Index < End; ++Index)
			{
				Indices.Add(Index);
			}
			Indices.Sort([&Contacts](const int32 A, const int32 B)
			{
				return Contacts[A].Phi < Contacts[B].Phi;
			});
			for (const int32 Index : Indices)
			{
				if (Order.Num() >= 4)
				{
					return;
				}
				Order.Add(Index);
			}
		};
		TakeDeepest(0, NumSupport);
		TakeDeepest(NumSupport, NumExit);
		TakeDeepest(NumExit, Contacts.Num());

		for (const int32 Index : Order)
		{
			bool bKept = false;
			for (const FPortalContactKey& Key : PreviousKeys)
			{
				if (IsSameContactKey(Contacts[Index], Key))
				{
					bKept = true;
					break;
				}
			}
			if (bKept)
			{
				++OutKept;
			}
			else
			{
				++OutNew;
			}
			FrameKeys.Add(MakeContactKey(Contacts[Index]));
		}

		Constraint.SetCullDistance(FMath::Max(Constraint.GetCullDistance(), Chaos::FReal(1)));
		Constraint.ResetActiveManifoldContacts();
		for (const int32 Index : Order)
		{
			Constraint.AddOneshotManifoldContact(Contacts[Index]);
		}
		Constraint.SetDisabled(false);
		Constraint.SetCCDEnabled(false);
		Constraint.SetIsInitialContact(false);
		for (int32 Index = 0; Index < Constraint.NumManifoldPoints(); ++Index)
		{
			Chaos::FManifoldPoint& Point = Constraint.GetManifoldPoint(Index);
			Point.Flags.bInitialContact = false;
			Point.InitialPhi = 0.f;
		}
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
	};

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
					CollectShapeLeaves(CubeParticle, CubeShape, true, Tracked.EntryOrigin, Tracked.ExitOrigin, Tracked.TravelQ, CubeLeaves);

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
							// Pulling the body out of the wall to find contacts drives a tipped corner into
							// the static geometry. That contact is copied back onto the real body and the
							// solver turns it into an upward velocity. Detect at the real overlap instead.
							if (bStaticExit)
							{
								float MinSide = 0.f;
								float MaxSide = 0.f;
								if (GetLeafPlaneInterval(CubeLeaf.Implicit, CubeLeaf.World, Tracked.ExitOrigin, Tracked.ExitPlaneNormal, MinSide, MaxSide)
									&& MaxSide <= KINDA_SMALL_NUMBER)
								{
									continue;
								}
							}

							for (const FShapeLeaf& ExitLeaf : ExitLeaves)
							{
								bool bSwapShapes = false;
								const Chaos::EContactShapesType ShapeType = Chaos::Collisions::CalculateShapePairType(CubeParticle, CubeLeaf.Implicit, ExitParticle, ExitLeaf.Implicit, bSwapShapes);
								if (ShapeType == Chaos::EContactShapesType::Unknown || !CubeLeaf.Implicit || !ExitLeaf.Implicit)
								{
									++Stats.Unknown;
									continue;
								}

								Chaos::FGeometryParticleHandle* P0 = bSwapShapes ? ExitParticle : CubeParticle;
								Chaos::FGeometryParticleHandle* P1 = bSwapShapes ? CubeParticle : ExitParticle;
								const Chaos::FPerShapeData* S0 = bSwapShapes ? ExitShape : CubeShape;
								const Chaos::FPerShapeData* S1 = bSwapShapes ? CubeShape : ExitShape;
								const Chaos::FImplicitObject* Implicit0 = P0 == CubeParticle ? CubeLeaf.Implicit : ExitLeaf.Implicit;
								const Chaos::FImplicitObject* Implicit1 = P1 == CubeParticle ? CubeLeaf.Implicit : ExitLeaf.Implicit;
								const Chaos::FRigidTransform3 Detect0 = P0 == CubeParticle ? CubeLeaf.World : ExitLeaf.World;
								const Chaos::FRigidTransform3 Detect1 = P1 == CubeParticle ? CubeLeaf.World : ExitLeaf.World;
								// The cube leaf was detected at the exit. Its real pose is the inverse travel of the
								// unshifted leaf. The other body stays where it is; the contact points move instead.
								const Chaos::FRigidTransform3 Real0 = P0 == CubeParticle ? InverseTravelShape(CubeLeaf.World, Tracked.ExitOrigin, Tracked.EntryOrigin, InvTravel) : ExitLeaf.World;
								const Chaos::FRigidTransform3 Real1 = P1 == CubeParticle ? InverseTravelShape(CubeLeaf.World, Tracked.ExitOrigin, Tracked.EntryOrigin, InvTravel) : ExitLeaf.World;
								const Chaos::FRigidTransform3 Rel0 = Real0 * Chaos::FConstGenericParticleHandle(P0)->GetTransformPQ().Inverse();
								const Chaos::FRigidTransform3 Rel1 = Real1 * Chaos::FConstGenericParticleHandle(P1)->GetTransformPQ().Inverse();

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
								Stats.Detected += Scratch->NumManifoldPoints();

								TArray<Chaos::FContactPoint, TInlineAllocator<8>> SupportContacts;
								const float MaxPenetration = FMath::Max(static_cast<float>(Dt) * 50.f, 0.5f);
								AppendRewrittenExitContacts(*Scratch, CubeParticle, Detect0, Detect1, Real0, Real1, Tracked.ExitOrigin, Tracked.EntryOrigin, Tracked.PlaneNormal, InvTravel, CullDistance, MaxPenetration, SupportContacts);
								if (SupportContacts.Num() == 0)
								{
									continue;
								}

								TArray<Chaos::FContactPoint, TInlineAllocator<8>> Ordered;
								Ordered.Append(SupportContacts);
								const int32 NumSupport = Ordered.Num();
								const int32 NumExit = Ordered.Num();

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

								const int32 KeysBefore = FrameKeys.Num();
								if (Existing)
								{
									const bool bSwapped = Existing->GetParticle0() != P0;
									if (bSwapped)
									{
										for (Chaos::FContactPoint& Contact : Ordered)
										{
											Contact = SwapContactOrder(Contact, Real0, Real1);
										}
									}
									WriteContacts(*Existing, Ordered, NumSupport, NumExit, ContactKeys, FrameKeys, Stats.NewPoints, Stats.KeptPoints);
									Existing->SetShapeWorldTransforms(bSwapped ? Real1 : Real0, bSwapped ? Real0 : Real1);
									ClearInitialOverlap(*Existing);
								}
								else
								{
									WriteContacts(*Scratch, Ordered, NumSupport, NumExit, ContactKeys, FrameKeys, Stats.NewPoints, Stats.KeptPoints);
									if (Scratch->NumManifoldPoints() == 0)
									{
										continue;
									}

									Scratch->SetShapeWorldTransforms(Real0, Real1);
									Context.MidPhase = MidPhase;
									MidPhase->InjectCollision(*Scratch, Context);

									bool bInjected = false;
									MidPhase->VisitCollisions([&](Chaos::FPBDCollisionConstraint& Constraint)
									{
										if (Constraint.GetParticle0() == P0 && Constraint.GetParticle1() == P1 && Constraint.GetImplicit0() == Implicit0)
										{
											// Activate() marks every unmatched point as an initial overlap, which
											// allows the whole penetration and never pushes the body back out.
											ClearInitialOverlap(Constraint);
											SetExitBodyInfiniteMass(Constraint, ExitParticle);
											Constraint.SetShapeWorldTransforms(Real0, Real1);
											bInjected = true;
											return Chaos::ECollisionVisitorResult::Stop;
										}
										return Chaos::ECollisionVisitorResult::Continue;
									}, Chaos::ECollisionVisitorFlags::VisitAllCurrent);

									// Generic midphases ignore InjectCollision, so the scratch constraint has to
									// be activated directly or the exit contact never reaches the solver.
									if (!bInjected)
									{
										ContextAllocator->ActivateConstraint(Scratch.Get());
										ClearInitialOverlap(*Scratch);
										SetExitBodyInfiniteMass(*Scratch, ExitParticle);
										Scratch->SetShapeWorldTransforms(Real0, Real1);
										OwnedConstraints.Add(MoveTemp(Scratch));
									}
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

		const float MaxPenetration = FMath::Max(static_cast<float>(GetDeltaTime_Internal()) * 50.f, 0.5f);
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
				// The frame is what the body stands on and what blocks the sides. Dropping an upward
				// frame hit, or a floor point a few centimetres behind the plane, removes that support.
				// The wall face on the plane is still ignored. A deep overlap is shortened below.
				if (!bPortalSurface && (Side < 0.f || IsPortalSurfaceRejection(OtherSide, PushOnCube, Tracked->PlaneNormal)))
				{
					Pair.SetContactPointDisabled(Index);
					continue;
				}

				const float Separation = Pair.GetSeparation(Index);
				if (Separation < -MaxPenetration)
				{
					const FVector CubePoint = bTrackedIs0 ? FVector(Location0) : FVector(Location1);
					const FVector Corrected = CubePoint + PushOnCube * (-MaxPenetration - Separation);
					if (bTrackedIs0)
					{
						Pair.ModifyWorldContactLocations(Chaos::FVec3(Corrected), Location1, Index);
					}
					else
					{
						Pair.ModifyWorldContactLocations(Location0, Chaos::FVec3(Corrected), Index);
					}
				}
			}
		}

		FPortalCopyStats Stats;
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
	ReverseCollider->SetRelativeScale3D(FVector(0.1f, 1.5f, 3.25f));
	ReverseCollider->SetGenerateOverlapEvents(true);

	Plane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plane"));
	AddOwnedComponent(Plane);
	Plane->SetupAttachment(SceneRoot);
	//Plane->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
	Plane->SetRelativeScale3D(FVector(1.25f, 2.25f, 1.0f));
	Plane->SetRelativeRotation(FRotator(0.0f, -90.0f, 90.0f));

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
	//These are set in setup later
	ReverseCollider->OnComponentBeginOverlap.AddDynamic(this, &APortal::OnOverlapBegin);
	ReverseCollider->OnComponentEndOverlap.AddDynamic(this, &APortal::OnOverlapEnd);

	CaptureComponent->bEnableClipPlane = true;
	CaptureComponent->ClipPlaneBase = GetPlaneOrigin();
	CaptureComponent->ClipPlaneNormal = GetActorForwardVector();

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
		Tracked.EntryOrigin = EntryOrigin;
		Tracked.ExitOrigin = ExitOrigin;
		Tracked.ExitPlaneNormal = ExitNormal;
		Tracked.TravelQ = TravelQ;

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
	}
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
		const FQuat EntryQ = GetActorQuat();
		const FQuat ExitQ = OtherPortal->GetActorQuat();
		const FQuat CamQ = PlayerController->PlayerCameraManager->GetCameraRotation().Quaternion();
		const FQuat FlipQ = FQuat(FVector::ZAxisVector, PI);

		FVector PlayerOffset = Plane->GetComponentLocation() - PlayerController->PlayerCameraManager->GetCameraLocation();
		PlayerOffset = EntryQ.UnrotateVector(PlayerOffset);

		const FVector RefUp = LocalPlayer ? LocalPlayer->GetActorUpVector() : FVector::UpVector;
		if (FMath::Abs(GetActorForwardVector() | RefUp.GetSafeNormal()) < 1.0f)
		{
			PlayerOffset.Z = -PlayerOffset.Z;
		}

		const FQuat CaptureWorldQ = ExitQ * FlipQ * EntryQ.Inverse() * CamQ;
		OtherPortal->CaptureComponent->SetWorldRotation(CaptureWorldQ);
		OtherPortal->CaptureComponent->SetRelativeLocation(PlayerOffset);
	}

	if (LocalPlayer && LocalPlayer->GetCurrentPortal() == this && IsPointBehindPlane(LocalPlayer->GetActorLocation(), -2.0f))
	{
		Teleport(LocalPlayer);
	}

	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		UpdateCloneTransform(Pair.Key, Pair.Value);
	}

	TArray<AActor*> ToHandOff;
	for (const TPair<AActor*, AActor*>& Pair : TeleportingObjects)
	{
		if (Pair.Key && Pair.Value && !Pair.Key->IsA<AActionDemoCharacter>() && IsPointBehindPlane(Pair.Key->GetActorLocation(), -2.0f))
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
				FString::Printf(TEXT("%s copy: %s  bodies=%d exits=%d leaves=%d unk=%d det=%d new=%d kept=%d"),
					*GetName(), State, Stats.Bodies, Stats.Exits, Stats.Leaves, Stats.Unknown, Stats.Detected, Stats.NewPoints, Stats.KeptPoints));
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

	// Enter the exit portal before the move, so the placement resolves against that plane.
	// OtherPortal->OnOverlapBegin(OtherPortal->ReverseCollider, Character, Character->GetCapsuleComponent(), INDEX_NONE, false, FHitResult());

	const FQuat ExitRotation = TravelQ * Character->GetActorQuat();
	Character->SetActorLocationAndRotation(ExitLocation, ExitRotation, false, nullptr, ETeleportType::TeleportPhysics);

	Character->AlignMovement(TravelQ, ExitQ);
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
			SetSimulatingThroughPortal(OtherActor, true);
		}
	}
}

void APortal::OnOverlapEnd(class UPrimitiveComponent* OverlappedComp, class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("EXIT: %s"), *OtherActor->GetActorLabel())

	if (AActionDemoCharacter* OverlappedCharacter = Cast<AActionDemoCharacter>(OtherActor))
	{
		if (OverlappedCharacter == LocalPlayer && OverlappedCharacter->GetCurrentPortal() == this)
		{
			OverlappedCharacter->SetCurrentPortal(nullptr);
		}
	}

	if (TeleportingObjects.Contains(OtherActor))
	{
		AActor* Clone = TeleportingObjects[OtherActor];
		if (Clone)
		{
			VDestroy(Clone);
		}

		SetActorClip(OtherActor, FLinearColor(Plane->GetComponentLocation()), FLinearColor(GetActorForwardVector()), false);
		SetSimulatingThroughPortal(OtherActor, false);

		TeleportingObjects.Remove(OtherActor);
	}
}

void APortal::VDestroy(AActor* ToDestroy)
{
	if (!ToDestroy) return;
	if (!ToDestroy->IsValidLowLevel()) return;

	ToDestroy->K2_DestroyActor();
	ToDestroy = NULL;

	GEngine->ForceGarbageCollection(true);
}
