// Copyright (c) 2018 David Nadaski. All Rights Reserved.

#include "AI/CoverFinderService.h"

#include "CoverSystem.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

UCoverFinderService::UCoverFinderService()
{
	bNotifyBecomeRelevant = true;
}

void UCoverFinderService::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);

	CoverSystem = GetWorld()->GetSubsystem<UCoverSubsystem>();

	if (CoverSystem == nullptr)
	{
		COVER_LOG(Error, TEXT("CoverSystem is null. CoverFinderService won't work anymore."));
		SCREEN_LOG(TEXT("CoverSystem is null. CoverFinderService won't work anymore."), FName(""));
		bNotifyTick = false;
		return;
	}

	CoverSystem->GetCoverPointGroundOffset();

#if DEBUG_RENDERING
#elif
	bDrawDebug = false;
	bUnitDebug = false;
#endif
}

void UCoverFinderService::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	// profiling
	SCOPE_CYCLE_COUNTER(STAT_FindCover);
	INC_DWORD_STAT(STAT_FindCoverHistoricalCount);
	SCOPE_SECONDS_ACCUMULATOR(STAT_FindCoverTotalTimeSpent);

	BlackBoardComp = OwnerComp.GetBlackboardComponent();
	TargetEnemy = Cast<
		AActor>(BlackBoardComp->GetValue<UBlackboardKeyType_Object>(Enemy.SelectedKeyName));

	if (TargetEnemy == nullptr)
	{
		return;
	}

	OwnerPawn = OwnerComp.GetAIOwner()->GetPawn();
	const FVector CharacterLocation = OwnerPawn->GetActorLocation();
	const FVector EnemyLocation = TargetEnemy->GetActorLocation();
	DebugData = NewObject<UCoverFinderVisData>(this);

#if DEBUG_RENDERING
	BlackBoardComp->SetValueAsObject(FName("VisData"), DebugData);

	// draw an arrow from our character to the enemy, in red, if the generic debug flag is set
	if (bDrawDebug)
		DebugData->DebugArrows.Add(FDebugArrow(CharacterLocation, EnemyLocation, FColor::Red, true));
#endif

	// release the former cover point, if any
	if (BlackBoardComp->IsVectorValueSet(OutputVector.SelectedKeyName))
	{
		FVector FormerCover = BlackBoardComp->GetValueAsVector(OutputVector.SelectedKeyName);

		CoverSystem->ReleaseCover(FormerCover);
	}

	// get the cover points
	TArray<FCoverPointOctreeElement> CoverPoints;
	GetCoverPoints(CoverPoints, CharacterLocation, EnemyLocation);

	// get navigation data
	NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	NavData = NavSys->MainNavData;

	FVector CoverLocation;
	bool bFoundCover = FindBestCoverPoint(CoverPoints, CoverLocation);
	
	if (bFoundCover)
	{
		// set the cover location in the BB
		BlackBoardComp->SetValueAsVector(OutputVector.SelectedKeyName, CoverLocation);
	}
	else
	{
		// draw a red marker above units that can't find any cover
		#if DEBUG_RENDERING
		if (bDrawDebug)
			DebugData->DebugPoints.Add(FDebugPoint(CharacterLocation + FVector(0.0f, 0.0f, 200.0f), FColor::Red, true));
#endif

		// no cover found: unset the cover location in the BB
		BlackBoardComp->ClearValue(OutputVector.SelectedKeyName);
	}

	DebugCoverData();
}

const void UCoverFinderService::GetCoverPoints(
	TArray<FCoverPointOctreeElement>& OutCoverPoints,
	const FVector& CharacterLocation,
	const FVector& EnemyLocation) const
{
	const float MinAttackRangeSquared = FMath::Square(MinAttackRange);

	// get cover points around the enemy that are inside our attack range
	const FBoxCenterAndExtent CoverScanArea = FBoxCenterAndExtent(EnemyLocation, FVector(AttackRange * 0.5f));
	TArray<FCoverPointOctreeElement> CoverPoints;
	
	CoverSystem->FindCoverPoints(CoverPoints, CoverScanArea.GetBox());

	// filter out cover points that are too close to the enemy based on our min attack range, or already taken; populate a new array with the remaining, valid cover points only
	for (FCoverPointOctreeElement CoverPoint : CoverPoints)
		if (!CoverPoint.Data->bTaken
			&& FVector::DistSquared(EnemyLocation, CoverPoint.Data->Location) >= MinAttackRangeSquared)
		{
			OutCoverPoints.Add(CoverPoint);

#if DEBUG_RENDERING
			if (bUnitDebug)
				DebugData->DebugPoints.Add(FDebugPoint(CoverPoint.Data->Location, FColor::Yellow, false));
#endif
		}
#if DEBUG_RENDERING
		else if (bUnitDebug)
			if (FVector::DistSquared(EnemyLocation, CoverPoint.Data->Location) < MinAttackRangeSquared)
				DebugData->DebugPoints.Add(FDebugPoint(CoverPoint.Data->Location, FColor::Black, false));
#endif

	// sort cover points by their distance to our unit
	OutCoverPoints.Sort([CharacterLocation](const FCoverPointOctreeElement Cp1, const FCoverPointOctreeElement Cp2)
	{
		return FVector::DistSquared(CharacterLocation, Cp1.Data->Location) < FVector::DistSquared(
			CharacterLocation, Cp2.Data->Location);
	});
}

bool UCoverFinderService::FindBestCoverPoint(TArray<FCoverPointOctreeElement> CoverPoints, FVector& BestCoverPoint) const
{
	const FVector CharacterLocation = OwnerPawn->GetActorLocation();
	const FVector EnemyLocation = TargetEnemy->GetActorLocation();
	
	// find the first adequate cover point
	for (const FCoverPointOctreeElement CoverPoint : CoverPoints)
	{
		BestCoverPoint = CoverPoint.Data->Location;

		// our unit must be able to reach the cover point
		// TODO: this is a relatively expensive operation, consider implementing an async query instead?
		if (!NavSys->TestPathSync(FPathFindingQuery(OwnerPawn, *NavData, CharacterLocation, BestCoverPoint)))
		{
#if DEBUG_RENDERING
			if (bUnitDebug)
				DebugData->DebugPoints.Add(FDebugPoint(CoverPoint.Data->Location, FColor::Red, false));
#endif

			continue;
		}

		float CharEyeHeightStanding = OwnerPawn->BaseEyeHeight;
		bool bFoundCover;

		if (const ACharacter* Character = Cast<ACharacter>(OwnerPawn))
		{
			// calculate the character's standing and crouched eye height offsets
			const float CapsuleHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float CharEyeHeightCrouched = CapsuleHalfHeight + Character->CrouchedEyeHeight;
			CharEyeHeightStanding = CapsuleHalfHeight + Character->BaseEyeHeight;

			bFoundCover = EvaluateCoverPoint(CoverPoint, CharEyeHeightCrouched, EnemyLocation);
		}

		// check from a standing position and if that fails then from a crouched one
		bFoundCover = bFoundCover
			|| EvaluateCoverPoint(CoverPoint, CharEyeHeightStanding, EnemyLocation);

		if (bFoundCover)
		{
			// mark the cover point as taken
			CoverSystem->HoldCover(BestCoverPoint);

			// draw an arrow from the cover point to the enemy, in green (success), if the unit debug flag is set
#if DEBUG_RENDERING
			if (bUnitDebug)
				DebugData->DebugArrows.Add(FDebugArrow(BestCoverPoint, EnemyLocation, FColor::Green, false));
#endif
			
			return true;
		}
	}
	return false;
}

const bool UCoverFinderService::EvaluateCoverPoint(
	const FCoverPointOctreeElement CoverPoint,
	const float CharEyeHeight,
	const FVector& EnemyLocation) const
{
	const FVector CoverLocation = CoverPoint.Data->Location;
	const FVector CoverLocationInEyeHeight = FVector(CoverLocation.X, CoverLocation.Y,
	                                                 CoverLocation.Z - CoverSystem->GetCoverPointGroundOffset() + CharEyeHeight);

	FHitResult Hit;
	FCollisionShape SphereColl;
	SphereColl.SetSphere(5.0f);
	FCollisionQueryParams CollQueryParamsExclCharacter;
	CollQueryParamsExclCharacter.AddIgnoredActor(OwnerPawn);
	CollQueryParamsExclCharacter.TraceTag = "CoverPointFinder_EvaluateCoverPoint";

	// check if we can hit the enemy straight from the cover point. if we can, then the cover point is no good
	if (!GetWorld()->SweepSingleByChannel(Hit, CoverLocationInEyeHeight, EnemyLocation, FQuat::Identity,
	                                 ECollisionChannel::ECC_Camera, SphereColl, CollQueryParamsExclCharacter))
		return false;

	// if the cover point is behind a shield then we shouldn't do any leaning checks, however we must be able to hit the enemy directly and through the shield
	// for this, we will need a second raycast to determine if we're hitting the shield, which has a different collision response than regular objects
	const AActor* HitActor = Hit.GetActor();
	if (CoverPoint.Data->bForceField // cover is a force field (shield)
		&& HitActor == TargetEnemy) // should be able to hit the enemy directly
	{
		CollQueryParamsExclCharacter.TraceTag = "CoverPointFinder_HitShieldFromCover";
		FHitResult ShieldHit;
		bool bHitShield = GetWorld()->SweepSingleByChannel(ShieldHit, CoverLocationInEyeHeight, EnemyLocation,
		                                              FQuat::Identity, ECollisionChannel::ECC_GameTraceChannel2,
		                                              SphereColl, CollQueryParamsExclCharacter);
		if (bHitShield // we must hit the shield
			&& ShieldHit.Distance <= CoverPointMaxObjectHitDistance)
			// cover point and cover object must be close to one another
			return true;

#if DEBUG_RENDERING
		if (bUnitDebug)
			if (!bHitShield)
				DebugData->DebugArrows.Add(FDebugArrow(CoverLocationInEyeHeight, EnemyLocation, FColor::Red, false));
			else if (ShieldHit.Distance > CoverPointMaxObjectHitDistance)
				DebugData->DebugArrows.Add(FDebugArrow(CoverLocationInEyeHeight, EnemyLocation, FColor::Black, false));
#endif
	}
	// if the cover point is not behind a shield then check if we can hit the enemy by leaning out of cover
	else if (!CoverPoint.Data->bForceField // cover is not a force field (shield)
		&& Hit.Distance <= CoverPointMaxObjectHitDistance // cover point and cover object must be close to one another
		&& HitActor != TargetEnemy // shouldn't be able to hit the enemy directly
		&& !HitActor->IsA<APawn>() // can't hide behind other units, for now
		&& CheckHitByLeaning(CoverLocationInEyeHeight))
		// we should only be able to hit the enemy by leaning out of cover
		return true;

#if DEBUG_RENDERING
	if (bUnitDebug && !CoverPoint.Data->bForceField)
		if (HitActor == TargetEnemy)
			DebugData->DebugArrows.Add(FDebugArrow(CoverLocationInEyeHeight, EnemyLocation, FColor::Purple, false));
		else if (Hit.Distance > CoverPointMaxObjectHitDistance)
			DebugData->DebugArrows.Add(FDebugArrow(CoverLocationInEyeHeight, EnemyLocation, FColor::Blue, false));
#endif

	return false;
}

const bool UCoverFinderService::CheckHitByLeaning(
	const FVector& CoverLocation) const
{
	const FVector EnemyLocation = TargetEnemy->GetActorLocation();

	FHitResult Hit;
	FCollisionShape SphereColl;
	SphereColl.SetSphere(5.0f);
	FCollisionQueryParams CollQueryParamsExclCharacter;
	CollQueryParamsExclCharacter.AddIgnoredActor(OwnerPawn);
	CollQueryParamsExclCharacter.TraceTag = "CoverPointFinder_CheckHitByLeaning";

	// check leaning left and right
	for (int DirectionMultiplier = 1; DirectionMultiplier > -2; DirectionMultiplier -= 2)
	{
		// calculate our reach for when leaning out of cover
		const FVector CoverEdge = EnemyLocation - CoverLocation;
		const FVector CoverEdgeDir = CoverEdge.GetUnsafeNormal();
		FVector2D CoverLean2D = FVector2D(GetPerpendicularVector(CoverEdgeDir)) * DirectionMultiplier;
		CoverLean2D *= WeaponLeanOffset;
		const FVector CoverLean = FVector(CoverLocation.X + CoverLean2D.X, CoverLocation.Y + CoverLean2D.Y,
										  CoverLocation.Z);

		// check if we can hit our target by leaning out of cover
		GetWorld()->SweepSingleByChannel(Hit, CoverLean, EnemyLocation, FQuat::Identity, ECollisionChannel::ECC_Camera,
									SphereColl, CollQueryParamsExclCharacter);
		if (Hit.GetActor() == TargetEnemy)
		{
#if DEBUG_RENDERING
			if (bUnitDebug)
			{
				DebugData->DebugArrows.Add(FDebugArrow(CoverLocation, CoverLean, FColor::Orange, false));
				DebugData->DebugArrows.Add(FDebugArrow(CoverLean, Hit.Location, FColor::Orange, false));
			}
#endif

			return true;
		}

#if DEBUG_RENDERING
		if (bUnitDebug)
		{
			DebugData->DebugArrows.Add(FDebugArrow(CoverLocation, CoverLean, FColor::Cyan, false));
			DebugData->DebugArrows.Add(FDebugArrow(CoverLean, Hit.Location, FColor::Cyan, false));
		}
#endif
	}

	// can't hit the enemy by leaning out of cover
	return false;
}

FVector UCoverFinderService::GetPerpendicularVector(const FVector& Vector)
{
	return FVector(Vector.Y, -Vector.X, Vector.Z);
}

void UCoverFinderService::DebugCoverData()
{
#if DEBUG_RENDERING
	if (!bDrawDebug && !bUnitDebug)
		return;

	if (!IsValid(DebugData))
		return;

	for (FDebugPoint debugPoint : DebugData->DebugPoints)
		if ((debugPoint.bGenericDebugData && bDrawDebug)
			|| (!debugPoint.bGenericDebugData && bUnitDebug))
				DrawDebugSphere(GetWorld(), debugPoint.Location, 20.0f, 4, debugPoint.Color, false, Interval);

	for (FDebugArrow debugArrow : DebugData->DebugArrows)
		if ((debugArrow.bGenericOrUnitDebugData && bDrawDebug)
			|| (!debugArrow.bGenericOrUnitDebugData && bUnitDebug))
				DrawDebugDirectionalArrow(GetWorld(), debugArrow.Start, debugArrow.End, 50.0f, debugArrow.Color, false, Interval, 0, 2.0f);
#endif
}