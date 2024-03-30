// Fill out your copyright notice in the Description page of Project Settings.


#include "MazeGenerator.h"
#include "Kismet/KismetSystemLibrary.h"

// Sets default values
AMazeGenerator::AMazeGenerator()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AMazeGenerator::BeginPlay()
{
	Super::BeginPlay();
	GenerateMap();
}

// Called every frame
void AMazeGenerator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AMazeGenerator::GenerateMap()
{
	TArray<FDPoint> Points;
	TMap<FDPoint, TArray<FDPoint>> Adjacencies;
	PlacePoints(Points);
	TriangulateLinks(Points, Adjacencies);
	DetermineRoomTypes(Adjacencies);
	//PushRoomsApart();
	//BuildLinks();
}

void AMazeGenerator::PlacePoints(TArray<FDPoint>& Points)
{
	if (bDebug)
	{
		UKismetSystemLibrary::DrawDebugBox(
			this
			, FVector(Length * CellSize / 2, Width * CellSize / 2, 0)
			, FVector(Length * CellSize, Width * CellSize, 0.f)
			, FColor::White
			, FRotator::ZeroRotator
			, 500.f
		);
	
	}

	TArray<F2DRange> RoomSizes;
	LayoutRules.RoomSizes.GenerateValueArray(RoomSizes);
	RoomSizes.Sort([](const F2DRange& A, const F2DRange& B) { return A.MaxX < B.MaxX; });
	const uint8 MAX_BUFFER_X = RoomSizes.Last().MaxX * 4;
	RoomSizes.Sort([](const F2DRange& A, const F2DRange& B) { return A.MaxY < B.MaxY; });
	const uint8 MAX_BUFFER_Y = RoomSizes.Last().MaxY * 4;

	for (int32 i = 0; i < TargetDensity; i++)
	{
		uint8 X = FMath::RandRange(MAX_BUFFER_X, Length - MAX_BUFFER_X);
		uint8 Y = FMath::RandRange(MAX_BUFFER_Y, Width - MAX_BUFFER_Y);
		Points.Add(FDPoint(X, Y, i));
	}
}

void AMazeGenerator::TriangulateLinks(TArray<FDPoint>& Points, OUT TMap<FDPoint, TArray<FDPoint>>& RoomAdjacencies)
{
	FDelaunay Delauney;
	const TArray<FDTriangle> Triangles = Delauney.Triangulate(Points, 1);

	//Prepare to calcule MST by mapping all adjacencies
	TMap<FDPoint, TArray<FDEdge>> RawAdjacencies;
	for (const FDTriangle& Triangle : Triangles)
	{
		for (int32 i = 0; i < 3; i++)
		{
			const FDEdge Edge = (
				i == 0 ? Triangle.E1
				: i == 1 ? Triangle.E2
				: i == 2 ? Triangle.E3
				// Invalid
				: FDEdge(FDPoint(0.f, 0.f, -1), FDPoint(0.f, 0.f, -1))
				);

			// Initiate adjacency matrix to save if checks later
			if (!RawAdjacencies.Contains(Edge.P1))
			{
				RawAdjacencies.Add(Edge.P1);
				RoomAdjacencies.Add(Edge.P1);
			}

			if (!RawAdjacencies.Contains(Edge.P2))
			{
				RawAdjacencies.Add(Edge.P2);
				RoomAdjacencies.Add(Edge.P2);
			}

			RawAdjacencies[Edge.P1].AddUnique(Edge);
			RawAdjacencies[Edge.P2].AddUnique(FDEdge::GetInverted(Edge));

			UKismetSystemLibrary::DrawDebugLine(
				this
				, FVector(Edge.P1.X * CellSize, Edge.P1.Y * CellSize, 0.f)
				, FVector(Edge.P2.X * CellSize, Edge.P2.Y * CellSize, 0.f)
				, FColor::Red
				, 500.f
				, 8.f
			);
		}
	}

	//Prim's algorithm to determine MST

	TArray<bool> Visited;
	Visited.Init(false, RawAdjacencies.Num());
	TArray<FDEdge> EdgeQueue;

	EdgeQueue.HeapPush(FDEdge(Triangles[0].E1));

	while (EdgeQueue.Num() != 0)
	{
		FDEdge Edge;
		EdgeQueue.HeapPop(Edge, FDEdgeMinComparitor());

		FDPoint Point = Edge.P1;

		if (Visited[Point.Id]) continue;
		Visited[Point.Id] = true;

		if (!Corridors.Contains(Edge))
		{
			RoomAdjacencies[Edge.P1].AddUnique(Edge.P2);
			RoomAdjacencies[Edge.P2].AddUnique(Edge.P1);
			Corridors.Add(Edge);
		}

		for (FDEdge& AdjacentEdge : RawAdjacencies[Point])
		{
			if (Visited[AdjacentEdge.P2.Id]) continue;

			EdgeQueue.HeapPush(FDEdge::GetInverted(AdjacentEdge), FDEdgeMinComparitor());

			if ((FMath::RandRange(0, 1) - AdditionalCorridorChance) > 0 && !Corridors.Contains(AdjacentEdge))
			{
				// Chance to create an additional link that isn't in the MST
				RoomAdjacencies[AdjacentEdge.P1].AddUnique(AdjacentEdge.P2);
				RoomAdjacencies[AdjacentEdge.P2].AddUnique(AdjacentEdge.P1);
				Corridors.Add(AdjacentEdge);
			}
		}
	}
}

void AMazeGenerator::DetermineRoomTypes(const TMap<FDPoint, TArray<FDPoint>>& RoomAdjacency)
{
	// Use wave function collapse to determine room types

	TArray<FRoomTile> RoomTiles;


	uint8 CollapsedRooms = 0;
	bool bMandatoryRoomsPlaced = false;
	const uint8 MAX_ATTEMPTS = 20;
	int Attempts = 0;
	while (!bMandatoryRoomsPlaced)
	{
		RoomTiles.Init(FRoomTile(), RoomAdjacency.Num());

		for (const auto& Point : RoomAdjacency)
		{
			RoomTiles[Point.Key.Id] = FRoomTile(Point.Key.Id, FVector2D(Point.Key.X, Point.Key.Y), &LayoutRules);
		}

		// Assign adjacancies to room tiles
		for (const auto& Point : RoomAdjacency)
			for (const FDPoint& AdjacentPoint : Point.Value)
			{
				RoomTiles[Point.Key.Id].Neighbours.Add(&RoomTiles[AdjacentPoint.Id]);
			}

		Attempts++;
		if (Attempts >= MAX_ATTEMPTS)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to place mandatory rooms. WFC failed."));
			break;
		}

		//Place spawns first. Then calculate initial entropies
		for (int x = 0; x < PlayerCount; x++)
		{
			uint8 SpawnIndex = 0;
			if (!ForcePlaceRoom(ERoomType::Spawn, RoomTiles, CollapsedRooms, SpawnIndex)) continue;
		}

		// Place boss and ascent points
		uint8 BossIndex = 0;
		if (!ForcePlaceRoom(ERoomType::Boss, RoomTiles, CollapsedRooms, BossIndex)) continue;

		bool bSuccess = false;
		uint8 AscentPointIndex = 0;
		for (auto& Neighbour : RoomTiles[BossIndex].Neighbours)
		{
			if (!Neighbour->bCollapsed)
			{
				Neighbour->Collapse(ERoomType::AscentPoint);
				AscentPointIndex = Neighbour->Id;
				CollapsedRooms++;
				bSuccess = true;
				break;
			}
		}

		if (!bSuccess) continue;

		// Remove links to the ascent point that isn't the boss room
		for (auto& Neighbour : RoomTiles[AscentPointIndex].Neighbours)
		{
			if (Neighbour->Id != BossIndex)
			{
				Neighbour->Neighbours.Remove(&RoomTiles[AscentPointIndex]);
			}
		}
		
		// Spawns, boss and ascent point have been placed, remove them from the possible room types so no more are spawned
		for (auto& Room : RoomTiles)
		{
			if (!Room.bCollapsed)
			{
				Room.PossibleRoomTypes.Remove(ERoomType::Spawn);
				Room.PossibleRoomTypes.Remove(ERoomType::Boss);
				Room.PossibleRoomTypes.Remove(ERoomType::AscentPoint);

				if (Room.PossibleRoomTypes.Num() == 0)
				{
					UE_LOG(LogTemp, Error, TEXT("No possible room types. Retrying."));
					bSuccess = false;
					break;
				}
			}
		}

		if (!bSuccess) continue;

		bMandatoryRoomsPlaced = true;
	}

	if (bDebug)
	{
		for (const auto& Elem : RoomTiles)
		{
			for (const auto& Neighbour : Elem.Neighbours)
			{
				UKismetSystemLibrary::DrawDebugLine(
					this
					, FVector(Elem.GridPos.X * CellSize, Elem.GridPos.Y * CellSize, 0.f)
					, FVector(Neighbour->GridPos.X * CellSize, Neighbour->GridPos.Y * CellSize, 0.f)
					, FColor::Green
					, 500.f
					, 16.f
				);
			}
		}
	}

	for (auto& Room : RoomTiles)
	{
		Room.RecalculateEntropy();
	}
	
	int32 NextIndex = FMath::RandRange(0, RoomDataCollection.Num() - 1);
	while (CollapsedRooms != RoomTiles.Num())
	{
		// Collapse tile randomly based on room weights
		FRoomTile* Next = &RoomTiles[NextIndex];
		float Roll = FMath::RandRange(0.f, 1.f);

		int PossibilityIndex = 0;
		while (Roll > 0)
		{
			ERoomType RoomType = Next->PossibleRoomTypes[PossibilityIndex];
			Roll -= LayoutRules.RoomTypeWeights.FindRef(RoomType);
			PossibilityIndex++;
			if (Roll <= 0 || PossibilityIndex >= Next->PossibleRoomTypes.Num())
			{
				Next->Collapse(RoomType);
				CollapsedRooms++;
				break;
			}
		}

		if (!CollapseNeighbours(*Next, CollapsedRooms)) return;

		//Set next as the room with the lowest entropy
		for (int32 i = 0; i < RoomTiles.Num(); i++)
		{
			if (RoomTiles[i].bCollapsed) continue;
			if (RoomTiles[i].Entropy < RoomTiles[NextIndex].Entropy)
			{
				NextIndex = i;
			}
		}
	}

	// Construct room data from tiles
	for (auto& Room : RoomTiles)
	{
		FRoomData* Data = new FRoomData();
		Data->Id = Room.Id;
		Data->RoomType = Room.PossibleRoomTypes[0];
		Data->GridPos = Room.GridPos;
		Data->Position = FVector(Data->GridPos.X * CellSize, Data->GridPos.Y * CellSize, 0.f);
		RoomDataCollection.Add(Data);

		if (bDebug)
		{
			const TMap<ERoomType, FColor> RoomTypeColours = {
				{ ERoomType::Spawn, FColor::Magenta },
				{ ERoomType::Boss, FColor::Orange },
				{ ERoomType::Treasure, FColor::Yellow },
				{ ERoomType::Normal, FColor::White },
				{ ERoomType::AscentPoint, FColor::Blue },
			};

			UKismetSystemLibrary::DrawDebugBox(
				this
				, Data->Position
				, FVector(300, 300, 0.f)
				, RoomTypeColours.FindRef(Data->RoomType)
				, FRotator::ZeroRotator
				, 500.f
			);
		}
	}

	//SizeRooms();
}

bool AMazeGenerator::ForcePlaceRoom(ERoomType RoomType, TArray<FRoomTile>& RoomTiles, uint8& CollapsedRooms, uint8& CollapsedIndex)
{
	// Find a node that allows for a spawn point
	uint8 Attempts = 0;
	while (Attempts < 20)
	{
		Attempts++;
		CollapsedIndex = FMath::RandRange(0, RoomTiles.Num() - 1);
		if (!RoomTiles[CollapsedIndex].bCollapsed && RoomTiles[CollapsedIndex].PossibleRoomTypes.Contains(RoomType))
		{
			break;
		}
	}

	if (Attempts >= 20)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to force place."));
		return false;
	}

	RoomTiles[CollapsedIndex].Collapse(RoomType);
	if (!CollapseNeighbours(RoomTiles[CollapsedIndex], CollapsedRooms)) return false;
	return true;
}

bool AMazeGenerator::CollapseNeighbours(FRoomTile& Tile, uint8& CollapsedRooms)
{
	// Update neighbours
	for (auto Neighbour : Tile.Neighbours)
	{
		if (Neighbour->bCollapsed) continue;
		for (int x = Neighbour->PossibleRoomTypes.Num() - 1; x >= 0; x--)
		{
			ERoomType NeighbourOption = Neighbour->PossibleRoomTypes[x];
			if (!LayoutRules.RoomEntropy.FindRef(NeighbourOption).Possibilities.Contains(Tile.PossibleRoomTypes[0]))
			{
				Neighbour->PossibleRoomTypes.RemoveAt(x);
			}
		}

		Neighbour->RecalculateEntropy();

		// If the neighbour only has one possible room type left, collapse it
		if (Neighbour->PossibleRoomTypes.Num() == 1)
		{
			Neighbour->bCollapsed = true;
			CollapseNeighbours(*Neighbour, CollapsedRooms);
			CollapsedRooms++;
		}
		// If the neighbour has no possible room types left, WFC has failed
		else if (Neighbour->PossibleRoomTypes.Num() == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("No possible room types. WFC failed."));
			return false;
		}
	}
	return true;
}

void AMazeGenerator::SizeRooms()
{
	Grid<TArray<FRoomData*>> OverlapGrid = Grid<TArray<FRoomData*>>(Length, Width);

	UE_LOG(LogTemp, Warning, TEXT("Initial set"))
	// Assign room sizes
	for (FRoomData* Room : RoomDataCollection)
	{
		F2DRange& Corners = Room->Corners;
		const F2DRange& RoomSizeRange = LayoutRules.RoomSizes[Room->RoomType];
		uint32 RoomLength = RoundToOdd(FMath::RandRange(RoomSizeRange.MinX, RoomSizeRange.MaxX));
		uint32 RoomWidth = RoundToOdd(FMath::RandRange(RoomSizeRange.MinY, RoomSizeRange.MaxY));
		Corners.MinX = FMath::Clamp(Room->GridPos.X - ((RoomLength / 2)), 0, Length);
		Corners.MaxX = FMath::Clamp(Room->GridPos.X + ((RoomLength / 2)), 0, Length);
		Corners.MinY = FMath::Clamp(Room->GridPos.Y - ((RoomWidth / 2)), 0, Width);
		Corners.MaxY = FMath::Clamp(Room->GridPos.Y + ((RoomWidth / 2)), 0, Width);

		MoveRoomOnGrid(Room, Room->GridPos, OverlapGrid);
	}

	//if (bDebug)

	uint32 GridLength = Length;
	uint32 GridWidth = Width;

	FVector2D AveragePos = FVector2D::ZeroVector;
	for (auto& Room : RoomDataCollection)
	{
		AveragePos += Room->GridPos;
	}
	AveragePos /= RoomDataCollection.Num();

	// Sort by closest to the middle of the clump
	RoomDataCollection.Sort([AveragePos](const FRoomData& A, const FRoomData& B) {
		return FVector2D::Distance(A.GridPos, AveragePos) < FVector2D::Distance(B.GridPos, AveragePos);
	});

	// Move overlapping rooms out towards the edges, from the middle outwards
	bool bOverlapsExist = true;
	uint8 Attempts = 0;
	const uint8 MIN_SPACING = 5;
	const uint8 MAX_ATTEMPTS = 50;
	UE_LOG(LogTemp, Warning, TEXT("Overlap checks"))

	while (bOverlapsExist && Attempts < MAX_ATTEMPTS)
	{
		bOverlapsExist = false;
		for (auto& Room : RoomDataCollection)
		{
			for (int X = Room->Corners.MinX; X <= Room->Corners.MaxX; X++)
			{
				for (int Y = Room->Corners.MinY; Y <= Room->Corners.MaxY; Y++)
				{
					if (X < 0 || X >= Length || Y < 0 || Y >= Width) continue;
					if (OverlapGrid[X][Y].Num() > 1)
					{
						bOverlapsExist = true;

						// Move overlapping rooms outwards
						for (int i = OverlapGrid[X][Y].Num() - 1; i >= 0; i--)
						{
							FRoomData* OverlappingRoom = OverlapGrid[X][Y][i];
							if (OverlappingRoom == Room) continue;

							int MaxLength = FMath::Max(Room->Corners.MaxX - Room->Corners.MinX, OverlappingRoom->Corners.MaxX - OverlappingRoom->Corners.MinX);
							int MaxWidth = FMath::Max(Room->Corners.MaxY - Room->Corners.MinY, OverlappingRoom->Corners.MaxY - OverlappingRoom->Corners.MinY);

							int XDistance = OverlappingRoom->GridPos.X - Room->GridPos.X;
							int YDistance = OverlappingRoom->GridPos.Y - Room->GridPos.Y;
							int TranslationX = ((MaxLength / 2) - FMath::Abs(XDistance)) * FMath::Sign(XDistance);
							int TranslationY = ((MaxWidth / 2) - FMath::Abs(YDistance)) * FMath::Sign(YDistance);
							MoveRoomOnGrid(OverlappingRoom, OverlappingRoom->GridPos + FVector2D(TranslationX, TranslationY), OverlapGrid);

							RoomDataCollection.Sort([AveragePos](const FRoomData& A, const FRoomData& B) {
								return FVector2D::Distance(A.GridPos, AveragePos) < FVector2D::Distance(B.GridPos, AveragePos);
								});
						}
					}
				}
			}
		}
		Attempts++;
		if (!bOverlapsExist) break;
	}

	if (bOverlapsExist)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to move rooms apart."));
	}

	for (auto& Data : RoomDataCollection)
	{
		if (bDebug)
		{
			const TMap<ERoomType, FColor> RoomTypeColours = {
				{ ERoomType::Spawn, FColor::Magenta },
				{ ERoomType::Boss, FColor::Orange },
				{ ERoomType::Treasure, FColor::Yellow },
				{ ERoomType::Normal, FColor::White },
				{ ERoomType::AscentPoint, FColor::Blue },
			};

			UKismetSystemLibrary::DrawDebugBox(
				this
				, Data->Position
				, FVector((Data->Corners.MaxX - Data->Corners.MinX) * CellSize, (Data->Corners.MaxY - Data->Corners.MinY) * CellSize, 0.f)
				, RoomTypeColours.FindRef(Data->RoomType)
				, FRotator::ZeroRotator
				, 500.f
			);

		}
	}
}

bool AMazeGenerator::MoveRoomOnGrid(FRoomData* Tile, FVector2D NewGridPos, Grid<TArray<FRoomData*>>& Grid)
{
	uint32 RoomLength = Tile->Corners.MaxX - Tile->Corners.MinX;
	uint32 RoomWidth = Tile->Corners.MaxY - Tile->Corners.MinY;

	if (NewGridPos.X - RoomLength < 0 || NewGridPos.X + RoomLength >= Grid.GetLength(0) || NewGridPos.Y - RoomWidth < 0 || NewGridPos.Y + RoomWidth >= Grid.GetLength(1))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to move room out of bounds."))
		return false;
	}

	for (int X = Tile->Corners.MinX; X <= Tile->Corners.MaxX; X++)
	{
		for (int Y = Tile->Corners.MinY; Y <= Tile->Corners.MaxY; Y++)
		{
			Grid[X][Y].Remove(Tile);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Moved room from %d %d to %d %d"), Tile->GridPos.X, Tile->GridPos.Y, NewGridPos.X, NewGridPos.Y)
	Tile->GridPos = NewGridPos;
	Tile->Position = FVector(Tile->GridPos.X * CellSize, Tile->GridPos.Y * CellSize, 0.f);

	Tile->Corners.MinX = Tile->GridPos.X - (RoomLength / 2);
	Tile->Corners.MaxX = Tile->GridPos.X + (RoomLength / 2);
	Tile->Corners.MinY = Tile->GridPos.Y - (RoomWidth / 2);
	Tile->Corners.MaxY = Tile->GridPos.Y + (RoomWidth / 2);

	for (int X = Tile->Corners.MinX; X <= Tile->Corners.MaxX; X++)
	{
		for (int Y = Tile->Corners.MinY; Y <= Tile->Corners.MaxY; Y++)
		{
			Grid[X][Y].Add(Tile);

			if (bDebug)
			{
				if (Grid[X][Y].Num() > 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Room overlap detected at %i %i"), X, Y)

					for (auto& Room : Grid[X][Y])
					{
						UE_LOG(LogTemp, Warning, TEXT("%i with ID: %i , %i with ID: %i"), Room->RoomType, Room->Id, Room->RoomType, Room->Id)
					}
				}
			}
		}
	}
	return true;
}

int32 AMazeGenerator::RoundToOdd(int32 Value)
{
	return Value % 2 == 0 ? Value + 1 : Value;
}

void AMazeGenerator::BuildLinks()
{
}