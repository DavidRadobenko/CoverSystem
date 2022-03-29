// Copyright (c) 2018 David Nadaski. All Rights Reserved.

#include "CoverSystem/CoverSubsystem.h"

#include "EngineUtils.h"
#include "Tasks/NavmeshCoverPointGeneratorTask.h"

#if DEBUG_RENDERING
#include "DrawDebugHelpers.h"
#endif

// PROFILER INTEGRATION //
DEFINE_STAT(STAT_GenerateCover);
DEFINE_STAT(STAT_GenerateCoverInBounds);
DEFINE_STAT(STAT_FindCover);

UCoverSubsystem::UCoverSubsystem()
{
	//TODO: take the extents of the underlying navigation mesh instead of using 64000, see NavData->GetBounds() in OnNavmeshUpdated
	CoverOctree = MakeShareable(new TCoverOctree(FVector(0, 0, 0), 64000));
}

UCoverSubsystem::~UCoverSubsystem()
{
	if (CoverOctree.IsValid())
	{
		CoverOctree->Destroy();
		CoverOctree = nullptr;
	}

	ElementToID.Empty();
	CoverObjectToID.Empty();
}

bool ContainsCoverPoint(FCoverPointOctreeElement CoverPoint, TArray<FCoverPointOctreeElement> CoverPoints)
{
	for (FCoverPointOctreeElement cp : CoverPoints)
		if (CoverPoint.Data->Location == cp.Data->Location)
			return true;

	return false;
}

bool UCoverSubsystem::GetElementID(FOctreeElementId2& OutElementID, const FVector ElementLocation) const
{
	const FOctreeElementId2* element = ElementToID.Find(ElementLocation);
	if (!element || !element->IsValidId())
		return false;

	OutElementID = *element;
	return true;
}

void UCoverSubsystem::AssignIDToElement(const FVector ElementLocation, FOctreeElementId2 ID)
{
	ElementToID.Add(ElementLocation, ID);
}

bool UCoverSubsystem::RemoveIDToElementMapping(const FVector ElementLocation)
{
	return ElementToID.Remove(ElementLocation) > 0;
}

void UCoverSubsystem::OnNavMeshTilesUpdated(const TSet<uint32>& UpdatedTiles)
{
	// regenerate cover points within the updated navmesh tiles
	TArray<const AActor*> dirtyCoverActors;
	for (uint32 tileIdx : UpdatedTiles)
	{
#if DEBUG_RENDERING
		// DrawDebugXXX calls may crash UE4 when not called from the main thread, so start synchronous tasks in case we're planning on drawing debug shapes
		if (bDebugDraw)
			(new FAutoDeleteAsyncTask<FNavmeshCoverPointGeneratorTask>(
				CoverPointMinDistance,
				SmallestAgentHeight,
				CoverPointGroundOffset,
				MapBounds,
				tileIdx,
				GetWorld()
				))->StartSynchronousTask();
		else
#endif
			(new FAutoDeleteAsyncTask<FNavmeshCoverPointGeneratorTask>(
				CoverPointMinDistance,
				SmallestAgentHeight,
				CoverPointGroundOffset,
				MapBounds,
				tileIdx,
				GetWorld()
				))->StartBackgroundTask();
	}
}

void UCoverSubsystem::FindCoverPoints(TArray<FCoverPointOctreeElement>& OutCoverPoints, const FBox& QueryBox) const
{
	FRWScopeLock CoverDataLock(CoverDataLockObject, FRWScopeLockType::SLT_ReadOnly);
	CoverOctree->FindCoverPoints(OutCoverPoints, QueryBox);
}

void UCoverSubsystem::FindCoverPoints(TArray<FCoverPointOctreeElement>& OutCoverPoints, const FSphere& QuerySphere) const
{
	FRWScopeLock CoverDataLock(CoverDataLockObject, FRWScopeLockType::SLT_ReadOnly);
	CoverOctree->FindCoverPoints(OutCoverPoints, QuerySphere);
}

void UCoverSubsystem::AddCoverPoints(const TArray<FDTOCoverData>& CoverPointDTOs)
{
	FRWScopeLock CoverDataLock(CoverDataLockObject, FRWScopeLockType::SLT_Write);

	for (FDTOCoverData coverPointDTO : CoverPointDTOs)
		CoverOctree->AddCoverPoint(coverPointDTO, CoverPointMinDistance * 0.9f);

	// optimize the octree
	CoverOctree->ShrinkElements();
}

FBox UCoverSubsystem::EnlargeAABB(FBox Box)
{
	return Box.ExpandBy(FVector(
		(Box.Max.X - Box.Min.X) * 0.5f,
		(Box.Max.Y - Box.Min.Y) * 0.5f,
		(Box.Max.Z - Box.Min.Z) * 0.5f
	));
}

void UCoverSubsystem::RemoveStaleCoverPoints(FBox Area)
{
	FRWScopeLock CoverDataLock(CoverDataLockObject, FRWScopeLockType::SLT_Write);

	// enlarge the clean-up area to x1.5 its size
	Area = EnlargeAABB(Area);

	// find all the cover points in the specified area
	TArray<FCoverPointOctreeElement> coverPoints;
	CoverOctree->FindCoverPoints(coverPoints, Area);

	for (FCoverPointOctreeElement coverPoint : coverPoints)
	{
		// check if the cover point still has an owner and still falls on the exact same location on the navmesh as it did when it was generated
		FNavLocation navLocation;
		if (IsValid(coverPoint.GetOwner())
			&& UNavigationSystemV1::GetCurrent(GetWorld())->ProjectPointToNavigation(coverPoint.Data->Location, navLocation, FVector(0.1f, 0.1f, CoverPointGroundOffset)))
			continue;
		
		FOctreeElementId2 id;
		GetElementID(id, coverPoint.Data->Location);

		// remove the cover point from the octree
		if (id.IsValidId())
			CoverOctree->RemoveElement(id);

		// remove the cover point from the element-to-id and object-to-location maps
		RemoveIDToElementMapping(coverPoint.Data->Location);
		CoverObjectToID.RemoveSingle(coverPoint.Data->CoverObject, coverPoint.Data->Location);
	}

	// optimize the octree
	CoverOctree->ShrinkElements();
}

void UCoverSubsystem::RemoveStaleCoverPoints(FVector Origin, FVector Extent)
{
	RemoveStaleCoverPoints(FBoxCenterAndExtent(Origin, Extent * 2.0f).GetBox());
}

void UCoverSubsystem::RemoveCoverPointsOfObject(const AActor* CoverObject)
{
	FRWScopeLock CoverDataLock(CoverDataLockObject, FRWScopeLockType::SLT_Write);

	TArray<FVector> coverPointLocations;
	CoverObjectToID.MultiFind(CoverObject, coverPointLocations, false);

	for (const FVector coverPointLocation : coverPointLocations)
	{
		FOctreeElementId2 elementID;
		GetElementID(elementID, coverPointLocation);
		CoverOctree->RemoveElement(elementID);
		RemoveIDToElementMapping(coverPointLocation);
		CoverObjectToID.Remove(CoverObject);

#if DEBUG_RENDERING
		if (bDebugDraw)
			DrawDebugSphere(GetWorld(), coverPointLocation, 20.0f, 4, FColor::Red, true, -1.0f, 0, 2.0f);
#endif
	}

	// optimize the octree
	CoverOctree->ShrinkElements();
}

void UCoverSubsystem::RemoveAll()
{
	FRWScopeLock CoverDataLock(CoverDataLockObject, FRWScopeLockType::SLT_Write);

	// destroy the octree
	if (CoverOctree.IsValid())
	{
		CoverOctree->Destroy();
		CoverOctree = nullptr;
	}

	// remove the id-to-element mappings
	ElementToID.Empty();

	// make a new octree
	CoverOctree = MakeShareable(new TCoverOctree(FVector(0, 0, 0), 64000));
}

bool UCoverSubsystem::HoldCover(FVector ElementLocation)
{
	FRWScopeLock CoverDataLock(CoverDataLockObject, FRWScopeLockType::SLT_Write);
	
	FOctreeElementId2 elemID;
	if (!GetElementID(elemID, ElementLocation))
		return false;

	return CoverOctree->HoldCover(elemID);
}

bool UCoverSubsystem::ReleaseCover(FVector ElementLocation)
{
	FRWScopeLock CoverDataLock(CoverDataLockObject, FRWScopeLockType::SLT_Write);
	
	FOctreeElementId2 elemID;
	if (!GetElementID(elemID, ElementLocation))
		return false;

	return CoverOctree->ReleaseCover(elemID);
}

void UCoverSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!IsValid(NavSys))
		return;
	
	// subscribe to tile update events on the navmesh
	const ANavigationData* MainNavData = NavSys->MainNavData;
	if (MainNavData && MainNavData->IsA(AChangeNotifyingRecastNavMesh::StaticClass()))
	{
		Navmesh = const_cast<AChangeNotifyingRecastNavMesh*>(Cast<AChangeNotifyingRecastNavMesh>(MainNavData));
		Navmesh->NavmeshTilesUpdatedBufferedDelegate.AddDynamic(this, &UCoverSubsystem::OnNavMeshTilesUpdated);
		
		bool bFoundCoverSystemBoundsActor;
		
		for (FActorIterator It(GetWorld()); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor->ActorHasTag(FName("CoverSystemBounds")))
			{
				FVector Origin, BoxExtent;
				Actor->GetActorBounds(false, Origin, BoxExtent, false);
				MapBounds = FBox(Origin - BoxExtent, Origin + BoxExtent);
				bFoundCoverSystemBoundsActor = true;
				break;
			}
		}
		
		if (bFoundCoverSystemBoundsActor == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("No Actor found with the Actor tag CoverSystemBounds. CoverPoints won't get generated."));
		}
		
		Navmesh->RebuildAll();
	}
}

float UCoverSubsystem::GetCoverPointGroundOffset()
{
	return CoverPointGroundOffset;
}