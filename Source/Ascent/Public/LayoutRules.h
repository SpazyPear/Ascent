// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <Room.h>
#include "LayoutRules.generated.h"

USTRUCT(BlueprintType)	
struct F2DRange
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere)
	int32 MinX;

	UPROPERTY(EditAnywhere)
	int32 MaxX;

	UPROPERTY(EditAnywhere)
	int32 MinY;

	UPROPERTY(EditAnywhere)
	int32 MaxY;

	F2DRange()
	{
		MinX = 0;
		MaxX = 0;
		MinY = 0;
		MaxY = 0;
	}

	int32 Length() const
	{
		return MaxX - MinX;
	}

	int32 Width() const
	{
		return MaxY - MinY;
	}
};

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
		TMap<ERoomType, F2DRange> RoomSizes;

	UPROPERTY(EditAnywhere)
		TMap<ERoomType, float> RoomTypeWeights;

};
