// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <LayoutRules.h>
#include "Delauney.h"
#include "MazeGenerator.generated.h"

struct FPathCell
{
public:
	FPathCell() 
	{
		IsWalkable = true;
		gCost = INFINITY;
	}

	FPathCell(FIntPoint InGridPos, bool InIsWalkable)
	{
		GridPos = InGridPos;
		IsWalkable = InIsWalkable;
		gCost = INFINITY;
		Parent = nullptr;
	}

	FIntPoint GridPos;
	bool IsWalkable;
	double hCost;
	double gCost;
	FPathCell* Parent;

	double fCost()
	{
		return hCost + gCost;
	}
};

class FRoomData;

class FLinkData
{
public:

	FLinkData()
	{

	}

	FLinkData(FRoomData* A, FRoomData* B)
	{
		RoomA = A;
		RoomB = B;
	}

	FRoomData* RoomA;
	FRoomData* RoomB;

	TArray<FIntPoint> Path;
	TArray<FVector> WorldPath;

	bool operator==(FLinkData const& B) const
	{
		return (RoomA == B.RoomA && RoomB == B.RoomB) || (RoomA == B.RoomB && RoomB == B.RoomA);
	}
};

class FRoomData
{
public:

	FRoomData()
	{

	}

	ERoomType RoomType;
	FIntPoint GridPos;
	FVector Position;
	F2DRange Corners;
	int32 Id;
	TArray<FRoomData*> Neighbours;

	friend FORCEINLINE uint32 GetTypeHash(const FRoomData& s)
	{
		return s.Id;
	}

	float GetWidth() const
	{
		return Corners.MaxX - Corners.MinX;
	}

	float GetHeight() const
	{
		return Corners.MaxY - Corners.MinY;
	}

	bool operator== (const FRoomData& B) const
	{
		GridPos == B.GridPos;
	}
};

class FRoomTile
{
public:
	int32 Id;
	TArray<FRoomTile*> Neighbours;
	TArray<ERoomType> PossibleRoomTypes;
	float Entropy;
	bool bCollapsed;
	FIntPoint GridPos;
	FLayoutRules* LayoutRules;

	FRoomTile() { }

	FRoomTile(int32 Id, FIntPoint GridPos, FLayoutRules* InLayoutRules)
	{
		this->Id = Id;
		this->LayoutRules = InLayoutRules;
		this->GridPos = GridPos;
		Entropy = 0;
		PossibleRoomTypes.Add(ERoomType::Treasure);
		PossibleRoomTypes.Add(ERoomType::Boss);
		PossibleRoomTypes.Add(ERoomType::Normal);
		PossibleRoomTypes.Add(ERoomType::AscentPoint);
		PossibleRoomTypes.Add(ERoomType::Spawn);

		bCollapsed = false;

		// WFC requires the room types to be sorted by their weights in decending order
		PossibleRoomTypes.Sort([InLayoutRules](const ERoomType& A, const ERoomType& B) {
			return InLayoutRules->RoomTypeWeights.FindRef(A) > InLayoutRules->RoomTypeWeights.FindRef(B);
		});
	}

	void Collapse(ERoomType RoomType)
	{
		bCollapsed = true;
		PossibleRoomTypes = TArray<ERoomType> { RoomType };
	}

	void RecalculateEntropy()
	{
		float Sum = 0;
		float LogSum = 0;
		for (ERoomType RoomType : PossibleRoomTypes)
		{
			float Weight = LayoutRules->RoomTypeWeights.FindRef(RoomType);
			Sum += Weight;
			LogSum += FMath::Log2(Weight) * Weight;
		}
		Entropy = FMath::Log2(Sum) - (LogSum / Sum);
	}
};

class Grid
{
	typedef FPathCell* iterator;
	typedef const FPathCell* const_iterator;
	uint32 Width;
	uint32 Length;

	FPathCell** Arr;

public:

	iterator begin() { return Arr[0]; }
	const_iterator begin() const { return Arr[0]; }
	iterator end() { return Arr[Width * Length]; }
	const_iterator end() const { return Arr[Length * Width]; }

	Grid() {}

	Grid(uint32 L, uint32 W)
	{
		Length = L;
		Width = W;
		Arr = new FPathCell*[L];
		for (uint32 X = 0; X < L; X++)
		{
			Arr[X] = new FPathCell[W];
			for (uint32 Y = 0; Y < W; Y++)
			{
				Arr[X][Y] = FPathCell(FIntPoint(X, Y), true);
			}
		}
	}

	~Grid()
	{
		for (uint32 X = 0; X < Length; X++)
		{
			delete[] Arr[X];
		}
		delete[] Arr;
	}

	int32 GetLength(uint8 Axis) const
	{
		return Axis == 0 ? Length : Width;
	}

	FPathCell* operator[](uint32 const& X) const
	{
		return Arr[X];
	}
};

UCLASS()
class ASCENT_API AMazeGenerator : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AMazeGenerator();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere)
		FLayoutRules LayoutRules;

	UPROPERTY(EditAnywhere)
		int32 Width;

	UPROPERTY(EditAnywhere)
		int32 Length;

	UPROPERTY(EditAnywhere)
		float CellSize;

	UPROPERTY(EditAnywhere)
		uint8 TargetDensity;

	UPROPERTY(EditAnywhere)
		uint8 PlayerCount;

	UPROPERTY(EditAnywhere, meta=(ClampMin="0", ClampMax="1"))
		float AdditionalCorridorChance;

	UPROPERTY(EditAnywhere)
		bool bDebug;

	UFUNCTION(BlueprintCallable)
		void GenerateMap();

private:

	TArray<FDEdge> Corridors;
	TArray<FRoomData> CachedRoomDataCollection;

	void PlacePoints(TArray<FDPoint>& Points);
	void TriangulateLinks(TArray<FDPoint>& Points, OUT TMap<FDPoint, TArray<FDPoint>>& Adjacencies);
	void DetermineRoomTypes(const TMap<FDPoint, TArray<FDPoint>>& Adjacencies, OUT TArray<FRoomData>& RoomDataCollection);
	bool CollapseNeighbours(FRoomTile& Tile, uint8& bCollapsed);
	bool ForcePlaceRoom(ERoomType RoomType, TArray<FRoomTile>& RoomTiles, uint8& CollapsedRooms, uint8& CollapsedIndex);
	void SizeRooms(TArray<FRoomData>& RoomDataCollection);
	bool MoveRoomOnGrid(FRoomData& Tile, FIntPoint NewGridPos);
	int32 RoundToOdd(int32 Value);
	bool IsRoomsConnected(const TArray<FRoomTile>& Rooms);
	void BuildLinks(TArray<FRoomData>& Rooms);
	void PopulateLinkPath(FLinkData& Link, Grid& Grid);
};
