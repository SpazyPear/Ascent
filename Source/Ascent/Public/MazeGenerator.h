// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <LayoutRules.h>
#include "Delauney.h"
#include "MazeGenerator.generated.h"

class FRoomData
{
public:

	FRoomData()
	{

	}
	
	ERoomType RoomType;
	FVector2D GridPos;
	FVector Position;
	//TArray<int32> Neighbours;
	int32 Id;

	friend FORCEINLINE uint32 GetTypeHash(const FRoomData& s)
	{
		return s.Id;
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
	FVector2D GridPos;
	FLayoutRules& LayoutRules;

	FRoomTile(int32 Id, FVector2D GridPos, FLayoutRules& InLayoutRules) : LayoutRules(InLayoutRules)
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
			return InLayoutRules.RoomTypeWeights.FindRef(A) > InLayoutRules.RoomTypeWeights.FindRef(B);
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
			float Weight = LayoutRules.RoomTypeWeights.FindRef(RoomType);
			Sum += Weight;
			LogSum += FMath::Log2(Weight) * Weight;
		}
		Entropy = FMath::Log2(Sum) - (LogSum / Sum);
	}
};

class Cell
{
public:
	ERoomType RoomType;
};

template <typename T> class Grid
{
	typedef T* iterator;
	typedef const T* const_iterator;
	uint32 Width;
	uint32 Length;

	T* Arr;

public:

	Grid() {}

	Grid(uint32 L, uint32 W)
	{
		Length = W;
		Width = W;
		Arr = new T[L * W];
		for (auto elem : *this)
		{
			elem = T();
		}
	}

	~Grid()
	{
		delete[] Arr;
	}

	T& ElementAt(uint32 X, uint32 Y)
	{
		if (X >= Length || X < 0 || Y >= Width || Y < 0) return nullptr;
		return Arr[X][Y];
	}

	T* operator[](uint32 const& v1)
	{
		return Arr[Arr + v1 * Width];
	}

	iterator begin() { return &Arr[0]; }
	const_iterator begin() const { return &Arr[0]; }
	iterator end() { return &Arr[Width * Length]; }
	const_iterator end() const { return &Arr[Length * Width]; }
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
		uint32 Width;

	UPROPERTY(EditAnywhere)
		uint32 Length;

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
	TArray<FRoomData*> RoomDataCollection;
	//Grid<Cell*> Cells;

	void PlacePoints(TArray<FDPoint>& Points);
	void TriangulateLinks(TArray<FDPoint>& Points, OUT TMap<FDPoint, TArray<FDPoint>>& Adjacencies);
	void DetermineRoomTypes(const TMap<FDPoint, TArray<FDPoint>>& Adjacencies);
	bool CollapseNeighbours(FRoomTile& Tile, uint8& bCollapsed);
	void SizeRooms();
	void BuildLinks();
};
