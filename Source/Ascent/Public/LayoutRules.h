// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include <Room.h>
#include "LayoutRules.generated.h"

UENUM(BlueprintType)
enum class ERoomType : uint8
{
	Undetermined UMETA(DisplayName = "Undetermined"),
	UninitialisedRoom UMETA(DisplayName = "UninitialisedRoom"),
	Spawn UMETA(DisplayName = "Spawn"),
	Boss UMETA(DisplayName = "Boss"),
	Treasure UMETA(DisplayName = "Treasure"),
	Normal UMETA(DisplayName = "Normal"),
	AscentPoint UMETA(DisplayName = "Ascent Point"),
	Corridor UMETA(DisplayName = "Corridor")
};

USTRUCT(BlueprintType)
struct FEntropyData
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
		TArray<ERoomType> Possibilities;
};

USTRUCT(BlueprintType)
struct FLayoutRules
{
	GENERATED_BODY()
	
public:
	FLayoutRules()
	{
		// Normalise weights
		float Sum = 0;
		for (auto& RoomTypeWeight : RoomTypeWeights)
		{
			Sum += RoomTypeWeight.Value;
		}

		for (auto& RoomTypeWeight : RoomTypeWeights)
		{
			RoomTypeWeight.Value /= Sum;
		}
	}

	UPROPERTY(EditAnywhere)
		TMap<ERoomType, FEntropyData> RoomEntropy;

	UPROPERTY(EditAnywhere)
		TMap<ERoomType, TSubclassOf<URoom>> RoomBPs;

	UPROPERTY(EditAnywhere)
		TMap<ERoomType, uint32> RoomSizes;

	UPROPERTY(EditAnywhere)
		TMap<ERoomType, float> RoomTypeWeights;
};
