// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.
// Peter L. Newton - https://twitter.com/peterlnewton

#pragma once

#include "LocalPhysicsActor.h"
#if WITH_PHYSX
#include "PhysXPublic.h"
#include "LocalPhysicsD6JointData.h"
#endif

#include "LocalPhysicsActor.h"
#include "LocalPhysicsJoint.h"
#include "LocalPhysicsLinearBlockAllocator.h"
#include "LocalPhysicsContactPair.h"
#include "LocalPhysicsPersistentContactPairData.h"
#include "LocalPhysicsCacheAllocator.h"
#include "LocalPhysicsConstraintAllocator.h"


DECLARE_STATS_GROUP(TEXT("Local Physics"), STATGROUP_LocalPhysics, STATCAT_Advanced);

class UBodySetup;
class UPhysicsConstraintTemplate;
class UStaticMeshComponent;

namespace LocalPhysics
{
	struct FActorHandle;
	struct FJointHandle;	
}

namespace LocalPhysics
{
/** Owns all the data associated with the simulation. Can be considered a single scene or world*/
struct LOCALPHYSICS_API FLocalSimulation
{
	bool HandleAvailableToSimulate() const;
public:
	
#if WITH_PHYSX
	/** Create a kinematic body and add it to the simulation */
	FActorHandle* CreateKinematicActor(PxRigidBody* RigidBody, const FTransform& TM);

	/** Create a dynamic body and add it to the simulation */
	FActorHandle* CreateDynamicActor(PxRigidDynamic* RigidDynamic, const FTransform& TM);

	/** Create a static body and add it to the simulation */
	FActorHandle* CreateStaticActor(PxRigidActor* RigidActor, const FTransform& TM);

	/** Create a physical joint and add it to the simulation */
	FJointHandle* CreateJoint(PxD6Joint* Joint, FActorHandle* Body1, FActorHandle* Body2);

	void RemoveActor(FActorHandle* Handle);
	void RemoveJoint(FJointHandle* Handle);
#endif



	/** Sets the number of active bodies. This number is reset any time a new simulated body is created */
	void SetNumActiveBodies(uint32 NumActiveBodies);

	/** An array of actors to ignore. */
	struct FIgnorePair
	{
		FActorHandle* A;
		FActorHandle* B;
	};

	/** Set pair of bodies to ignore collision for */
	void SetIgnoreCollisionPairTable(const TArray<FIgnorePair>& InIgnoreCollisionPairTable);

	/** Set bodies that require no collision */
	void SetIgnoreCollisionActors(const TArray<FActorHandle*>& InIgnoreCollision);

	/** Advance the simulation by DeltaTime */
	void Simulate(float DeltaTime, const FVector& Gravity);

	/** Whether or not an entity is simulated */
	bool IsSimulated(uint32 ActorDataIndex) const
	{
		return ActorDataIndex < NumSimulatedBodies;
	}

	enum class EForceType
	{
		AddForce,	//use mass and delta time
		AddAcceleration, //use delta time, ignore mass
		AddImpulse,	//use mass, ignore delta time
		AddVelocity	//ignore mass, ignore delta time
	};

	/** Add a radial impulse to the given actor */
	void AddRadialForce(int32 ActorDataIndex, const FVector& Origin, float Strength, float Radius, ERadialImpulseFalloff Falloff, EForceType ForceType);

	FLocalSimulation();

	~FLocalSimulation();

private:
	friend FActorHandle;

#if WITH_PHYSX
	const immediate::PxRigidBodyData& GetLowLevelBody(int32 ActorDataIndex) const
	{
		return RigidBodiesData[ActorDataIndex];
	}

	immediate::PxRigidBodyData& GetLowLevelBody(int32 ActorDataIndex)
	{
		return RigidBodiesData[ActorDataIndex];
	}
#endif

	enum class ECreateActorType
	{
		StaticActor,	//collision but no movement
		KinematicActor,	//collision + movement but no dynamics (forces, mass, etc...)
		DynamicActor	//collision + movement + dynamics
	};

#if WITH_PHYSX
	template <ECreateActorType ActorType>
	uint32 CreateActor(PxRigidActor* RigidActor, const FTransform& TM);
#endif

	/** Swap actor data - that is move all data associated with the two actors in the various arrays*/
	void SwapActorData(uint32 Entity1Idx, uint32 Entity2Idx);

	/** Swap joint data - that is move all data associated with the two joints in the various arrays*/
	void SwapJointData(uint32 Joint1Idx, uint32 Joint2Idx);

	/** Ensure arrays are valid */
	void ValidateArrays() const;

	/** Constructs solver bodies */
	void ConstructSolverBodies(float DeltaTime, const FVector& Gravity);

	/** Generate contacts*/
	void GenerateContacts();

	/** Batch constraints and re-order them for optimal processing */
	void BatchConstraints();

	/** Prepares the various constraints (contact,joints) for the solver */
	void PrepareConstraints(float DeltaTime);

	/** Solve constraints and integrate velocities */
	void SolveAndIntegrate(float DeltaTime);

	/** Prepares iteration cache for generating contacts */
	void PrepareIterationCache();

	//void EvictCache();

private:

	/** Mapping from entity index to handle */
	TArray<FActorHandle*>	ActorHandles;

	/** Mapping from constraint index to handle */
	TArray<FJointHandle*> JointHandles;

	/** Entities holding loose data. NOTE: for performance reasons we don't automatically cleanup on destructor (needed for tarray swaps etc...) it's very important that Terminate is called */
	TArray<FActor> Actors;

	TArray<FJoint> Joints;

	/** Workspace memory that we use for per frame allocations */
	FLinearBlockAllocator Workspace;

#if WITH_PHYSX
	/** Low level rigid body data */
	TArray<immediate::PxRigidBodyData> RigidBodiesData;
	
	/** Low level solver bodies data */
	TArray<PxSolverBodyData> SolverBodiesData;

	TArray<PxVec3> PendingAcceleration;

	/** Low level contact points generated for this frame. Points are grouped together by pairs */
	TArray<Gu::ContactPoint> ContactPoints;

	/** Shapes used in the entire simulation. Shapes are sorted in the same order as actors. Note that an actor can have multiple shapes which will be adjacent*/
	struct FShapeSOA
	{
		TArray<PxTransform> LocalTMs;
		TArray<const PxGeometry*> Geometries;
		TArray<float> Bounds;
		TArray<PxVec3> BoundsOffsets;
		TArray<int32> OwningActors;
#if PERSISTENT_CONTACT_PAIRS
		TArray<FPersistentContactPairData> ContactPairData;
#endif
	} ShapeSOA;
	
	/** Low level solver bodies */
	PxSolverBody* SolverBodies;

	/** Low level constraint descriptors.*/
	TArray<PxSolverConstraintDesc> OrderedDescriptors;
	TArray<PxConstraintBatchHeader> BatchHeaders;

	/** JointData as passed in from physics constraint template */
	TArray<D6JointData> JointData;

	/** When new joints are created we have to update the processing order */
	bool bDirtyJointData;

	PxU32 NumContactHeaders;
	PxU32 NumJointHeaders;
	uint32 NumActiveJoints;
#endif

	/** Contact pairs generated for this frame */
	TArray<FContactPair> ContactPairs;

	/** Number of dynamic bodies associated with the simulation */
	uint32 NumSimulatedBodies;

	/** Number of dynamic bodies that are actually active */
	uint32 NumActiveSimulatedBodies;

	/** Number of kinematic bodies (dynamic but not simulated) associated with the simulation */
	uint32 NumKinematicBodies;

	/** Total number of simulated shapes in the scene */
	uint32 NumSimulatedShapesWithCollision;

	/** Number of position iterations used by solver */
	uint32 NumPositionIterations;

	/** Number of velocity iterations used by solver */
	uint32 NumVelocityIterations;

	/** Count of how many times we've ticked. Useful for cache invalidation */
	uint32 SimCount;

	/** Both of these are slow to access. Make sure to use iteration cache when possible */
	TMap<FActorHandle*, TSet<FActorHandle*>> IgnoreCollisionPairTable;
	TSet<FActorHandle*> IgnoreCollisionActors;

	/** This cache is used to record which generate contact iteration we can skip. This assumes the iteration order has not changed (add/remove/swap actors must invalidate this) */
	bool bRecreateIterationCache;

	TArray<int32> SkipCollisionCache;	//Holds the iteration count that we should skip due to ignore filtering

	friend struct FContactPointRecorder;

	FCacheAllocator CacheAllocator;
	FConstraintAllocator ConstraintAllocator;
};

}