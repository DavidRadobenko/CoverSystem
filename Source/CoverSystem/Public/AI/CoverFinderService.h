// Copyright (c) 2018 David Nadaski. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Blackboard/BlackboardKeyAllTypes.h"
#include "AIController.h"
#include "CoverSystem/CoverSubsystem.h"
#include "Debug/CoverFinderVisData.h"
#include "CoverFinderService.generated.h"

/**
 * Finds suitable cover by looking around a unit in a full sphere.
 */
UCLASS()
class COVERSYSTEM_API UCoverFinderService : public UBTService
{
	GENERATED_BODY()
	
	UCoverFinderService();
	
private:
	static FVector GetPerpendicularVector(const FVector& Vector);

	// Gather, filter and sort cover points.
	const void GetCoverPoints(
		TArray<FCoverPointOctreeElement>& OutCoverPoints,
		const FVector& PawnLocation,
		const FVector& EnemyLocation) const;

	// Checks if there's clear line of sight to the enemy when leaning to the right.
	const bool CheckHitByLeaning(const FVector& CoverLocation) const;

	const bool EvaluateCoverPoint(
		const FCoverPointOctreeElement CoverPoint,
		const float CharEyeHeight,
		const FVector& EnemyLocation) const;

	bool FindBestCoverPoint(
		TArray<FCoverPointOctreeElement> CoverPoints,
		FVector& BestCoverPoint) const;

public:
	UPROPERTY(EditAnywhere, Category = "CoverFinderService|Debug")
	bool bDrawDebug;

	UPROPERTY(EditAnywhere, Category = "CoverFinderService|Debug")
	bool bUnitDebug;

	// Stores the calculated result here [vector]
	UPROPERTY(EditAnywhere, Category = "CoverFinderService")
	FBlackboardKeySelector OutputVector;

	// Our target [actor]
	UPROPERTY(EditAnywhere, Category = "CoverFinderService")
	FBlackboardKeySelector Enemy;

	// Our max attack range
	UPROPERTY(EditAnywhere, Category = "CoverFinderService")
	float AttackRange = 1000.0f;

	// Our min attack range
	UPROPERTY(EditAnywhere, Category = "CoverFinderService")
	float MinAttackRange = 100.0f;

	// How much our weapon moves horizontally when we're leaning. 0 = unit can't lean at all.
	UPROPERTY(EditAnywhere, Category = "CoverFinderService")
	float WeaponLeanOffset = 100.0f;

	// How close must the actual cover object be to a cover point. This is to avoid picking a cover point that doesn't provide meaningful cover.
	UPROPERTY(EditAnywhere, Category = "CoverFinderService")
	float CoverPointMaxObjectHitDistance = 310.0f; // was 100.0f

	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	
protected:
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	
private:
	void DebugCoverData();
	
	UCoverSubsystem* CoverSystem;
	
	UBlackboardComponent* BlackBoardComp;
	
	APawn* OwnerPawn;
	AActor* TargetEnemy;

	UNavigationSystemV1* NavSys;
	ANavigationData* NavData;

	UCoverFinderVisData* DebugData;
};
