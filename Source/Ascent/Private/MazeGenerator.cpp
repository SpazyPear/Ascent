// Fill out your copyright notice in the Description page of Project Settings.

#include "MazeGenerator.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Math/UnrealMathUtility.h"
#include "Algo/Reverse.h"

bool operator==(const FPathCell A, const FPathCell* B)
{
	return A.GridPos == B->GridPos;
}

bool operator==(const FPathCell A, const FPathCell& B)
{
	return A.GridPos == B.GridPos;
}

#pragma region Pathfinding Helpers

double Distance(FIntPoint A, FIntPoint B)
{
	return FMath::Sqrt((double)FMath::Square(B.X - A.X) + FMath::Square(B.Y - B.X));
}

TArray<FIntPoint> ConstructPath(FPathCell& End)
{
	TArray<FIntPoint> Points;
	FPathCell* SearchNode = &End;
	while (SearchNode)
	{
		Points.Add(SearchNode->GridPos);

		if (SearchNode->Parent != NULL)
		{
			FIntPoint StartPos = SearchNode->GridPos;
			FIntPoint Dist = Distance(SearchNode->Parent->GridPos, SearchNode->GridPos);
			int32 MaxDimension = FMath::Max(FMath::Abs(Dist.X), FMath::Abs(Dist.Y));
			Dist.X = FMath::Clamp(Dist.X, -1, 1);
			Dist.Y = FMath::Clamp(Dist.Y, -1, 1);
			for (int x = 0; x < MaxDimension - 1; x++)
			{
				Points.Add(StartPos += Dist);
			}
		}

		SearchNode = SearchNode->Parent;
	}
	Algo::Reverse(Points);
	Points.RemoveAt(0);
	return Points;
}

bool IsValidPoint(FIntPoint A, const Grid& PathGrid)
{
	return A.X >= 0 && A.X < PathGrid.GetLength(0) && A.Y >= 0 && A.Y < PathGrid.GetLength(1);
}

FPathCell* GetNeighbour(FIntPoint A, FIntPoint Direction, int32 Distance, const Grid& PathGrid)
{
	FIntPoint Vec = A + (Direction * Distance);
	if (IsValidPoint(A, PathGrid)) return &PathGrid[A.X][A.Y];
	return nullptr;
}

int32 DistanceToWall(FPathCell& End, FIntPoint Direction, const Grid& PathGrid)
{
	int32 Distance = 0;
	FPathCell* SearchNode = &End;
	while (SearchNode)
	{
		UE_LOG(LogTemp, Warning, TEXT("dist"))

		SearchNode = GetNeighbour(SearchNode->GridPos, Direction, 1, PathGrid);
		if (SearchNode == nullptr || !SearchNode->IsWalkable) break;
		Distance++;
	}
	return Distance;
}

bool IsInExactDirection(FIntPoint A, FIntPoint B, FIntPoint Direction)
{
	int32 X = FMath::Clamp(B.X - A.X, -1, 1);
	int32 Y = FMath::Clamp(B.Y - A.Y, -1, 1);
	return Direction == FIntPoint(X, Y);
}

bool IsCardinal(FIntPoint Direction)
{
	bool Vertical = Direction.X == 1;
	bool Horizontal = Direction.Y == 1;
	return Vertical ^ Horizontal;
}

bool IsDiagonal(FIntPoint Direction)
{
	return Direction.X != 0 && Direction.Y != 0;
}

FIntPoint GetGeneralDirection(FIntPoint A, FIntPoint B)
{
	TArray<FIntPoint> Directions = { FIntPoint(1,0), FIntPoint(1,1), FIntPoint(0, 1), FIntPoint(-1, 1), FIntPoint(-1, 0), FIntPoint(-1,-1), FIntPoint(0, -1), FIntPoint(1, -1) };

	FVector2D Vec = B - A;
	float Angle = FMath::Atan2(Vec.Y, Vec.X);
	int32 Octant = FMath::RoundToInt(8 * Angle / (2 * PI) + 8) % 8;

	return Directions[Octant];
}

FPathCell& GetLowestCostCell(TArray<FPathCell>& OpenList)
{
	FPathCell* CurMin = nullptr;
	for (FPathCell& Cell : OpenList)
	{
		if (CurMin == nullptr || Cell.fCost() < CurMin->fCost())
		{
			CurMin = &Cell;
		}
	}
	return *CurMin;
}

#pragma endregion

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

	DetermineRoomTypes(Adjacencies, CachedRoomDataCollection);

	BuildLinks(CachedRoomDataCollection);
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

			if (bDebug)
			{
				//UKismetSystemLibrary::DrawDebugLine(
				//	this
				//	, FVector(Edge.P1.X * CellSize, Edge.P1.Y * CellSize, 0.f)
				//	, FVector(Edge.P2.X * CellSize, Edge.P2.Y * CellSize, 0.f)
				//	, FColor::Red
				//	, 500.f
				//	, 8.f
				//);
			}
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

#pragma region Room Typing

void AMazeGenerator::DetermineRoomTypes(const TMap<FDPoint, TArray<FDPoint>>& RoomAdjacency, OUT TArray<FRoomData>& RoomDataCollection)
{
	// Use wave function collapse to determine room types

	TArray<FRoomTile> RoomTiles;


	uint8 CollapsedRooms = 0;
	bool bMandatoryRoomsPlaced = false;
	const uint8 MAX_ATTEMPTS = 50;
	int Attempts = 0;
	while (!bMandatoryRoomsPlaced)
	{
		RoomTiles.Init(FRoomTile(), RoomAdjacency.Num());

		for (const auto& Point : RoomAdjacency)
		{
			RoomTiles[Point.Key.Id] = FRoomTile(Point.Key.Id, FIntPoint(Point.Key.X, Point.Key.Y), &LayoutRules);
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
		for (int X = RoomTiles[AscentPointIndex].Neighbours.Num() - 1; X >= 0; X--)
		{
			FRoomTile* Neighbour = RoomTiles[AscentPointIndex].Neighbours[X];
			if (Neighbour->Id != BossIndex)
			{
				Neighbour->Neighbours.Remove(&RoomTiles[AscentPointIndex]);
				RoomTiles[AscentPointIndex].Neighbours.RemoveAt(X);
			}
		}
		
		if (!IsRoomsConnected(RoomTiles)) continue;

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
	RoomDataCollection.Empty();
	for (auto& Room : RoomTiles)
	{
		FRoomData* Data = new FRoomData();
		Data->Id = Room.Id;
		Data->RoomType = Room.PossibleRoomTypes[0];
		Data->GridPos = Room.GridPos;
		Data->Position = FVector(Data->GridPos.X * CellSize, Data->GridPos.Y * CellSize, 0.f);
		RoomDataCollection.Add(*Data);
	}

	// Assign neighbours
	for (auto& Room : RoomDataCollection)
	{
		for (auto& Neighbour : RoomTiles[Room.Id].Neighbours)
		{
			Room.Neighbours.Add(&RoomDataCollection[Neighbour->Id]);
		}
	}

	SizeRooms(RoomDataCollection);
}

bool AMazeGenerator::IsRoomsConnected(const TArray<FRoomTile>& Rooms)
{
	TArray<FRoomTile> Stack;
	TArray<bool> Visited;
	Visited.Init(false, Rooms.Num());

	Stack.Add(Rooms[0]);

	while (Stack.Num() > 0)
	{
		FRoomTile Current = Stack.Pop();
		Visited[Current.Id] = true;

		for (auto& Neighbour : Current.Neighbours)
		{
			if (!Visited[Neighbour->Id])
			{
				Stack.Add(*Neighbour);
			}
		}
	}

	for (auto& VisitedRoom : Visited)
	{
		if (!VisitedRoom) return false;
	}

	return true;
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

#pragma endregion

#pragma region Sizing

void AMazeGenerator::SizeRooms(TArray<FRoomData>& RoomDataCollection)
{
	UE_LOG(LogTemp, Warning, TEXT("Initial set"))
		// Assign room sizes
		for (FRoomData& Room : RoomDataCollection)
		{
			F2DRange& Corners = Room.Corners;
			const F2DRange& RoomSizeRange = LayoutRules.RoomSizes[Room.RoomType];
			uint32 RoomLength = RoundToOdd(FMath::RandRange(RoomSizeRange.MinX, RoomSizeRange.MaxX));
			uint32 RoomWidth = RoundToOdd(FMath::RandRange(RoomSizeRange.MinY, RoomSizeRange.MaxY));
			Corners.MinX = FMath::Clamp(Room.GridPos.X - ((RoomLength / 2)), 0, Length);
			Corners.MaxX = FMath::Clamp(Room.GridPos.X + ((RoomLength / 2)), 0, Length);
			Corners.MinY = FMath::Clamp(Room.GridPos.Y - ((RoomWidth / 2)), 0, Width);
			Corners.MaxY = FMath::Clamp(Room.GridPos.Y + ((RoomWidth / 2)), 0, Width);

			MoveRoomOnGrid(Room, Room.GridPos);
		}

	uint32 GridLength = Length;
	uint32 GridWidth = Width;

	FIntPoint AveragePos = FIntPoint::ZeroValue;
	for (auto& Room : RoomDataCollection)
	{
		AveragePos += Room.GridPos;
	}
	AveragePos /= RoomDataCollection.Num();

	// Sort by closest to the middle of the clump
	RoomDataCollection.Sort([AveragePos](const FRoomData& A, const FRoomData& B) {
		return Distance(A.GridPos, AveragePos) < Distance(B.GridPos, AveragePos);
		});


	// Move overlapping rooms out towards the edges, from the middle outwards
	bool bOverlapsExist = true;
	uint8 Attempts = 0;
	const uint8 MIN_SPACING = 10;
	const uint8 MAX_ATTEMPTS = 15;
	UE_LOG(LogTemp, Warning, TEXT("Overlap checks"))

	while (bOverlapsExist && Attempts < MAX_ATTEMPTS)
	{
		bOverlapsExist = false;
		for (auto& RoomOne : RoomDataCollection)
		{
			for (auto& RoomTwo : RoomDataCollection)
			{
				if (RoomOne.Id != RoomTwo.Id && 
					RoomOne.GridPos.X < RoomTwo.GridPos.X + RoomTwo.Corners.Length() &&
					RoomOne.GridPos.X + RoomOne.Corners.Length() > RoomTwo.GridPos.X &&
					RoomOne.GridPos.Y < RoomTwo.GridPos.Y + RoomTwo.Corners.Width() &&
					RoomOne.GridPos.Y + RoomOne.Corners.Width() > RoomTwo.GridPos.Y)
				{
					bOverlapsExist = true;

					int MaxLength = FMath::Max(RoomOne.Corners.MaxX - RoomOne.Corners.MinX, RoomTwo.Corners.MaxX - RoomTwo.Corners.MinX);
					int MaxWidth = FMath::Max(RoomOne.Corners.MaxY - RoomOne.Corners.MinY, RoomTwo.Corners.MaxY - RoomTwo.Corners.MinY);

					int XDistance = RoomTwo.GridPos.X - RoomOne.GridPos.X;
					int YDistance = RoomTwo.GridPos.Y - RoomOne.GridPos.Y;
					int TranslationX = ((MaxLength) - FMath::Abs(XDistance)) * FMath::Sign(XDistance);
					int TranslationY = ((MaxWidth) - FMath::Abs(YDistance)) * FMath::Sign(YDistance);

					MoveRoomOnGrid(RoomTwo, RoomTwo.GridPos + FIntPoint(TranslationX, TranslationY));

					RoomDataCollection.Sort([AveragePos](const FRoomData& A, const FRoomData& B) {
						return Distance(A.GridPos, AveragePos) < Distance(B.GridPos, AveragePos);
						});
						
				}
				
			}
		}
		Attempts++;
	}

	if (bOverlapsExist || Attempts >= MAX_ATTEMPTS)
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
				, Data.Position
				, FVector((Data.Corners.Length()) * CellSize / 2, (Data.Corners.Width()) * CellSize / 2, 0.f)
				, RoomTypeColours.FindRef(Data.RoomType)
				, FRotator::ZeroRotator
				, 500.f
			);

		}
	}
}

bool AMazeGenerator::MoveRoomOnGrid(FRoomData& Tile, FIntPoint NewGridPos)
{
	uint32 RoomLength = Tile.Corners.MaxX - Tile.Corners.MinX;
	uint32 RoomWidth = Tile.Corners.MaxY - Tile.Corners.MinY;

	UE_LOG(LogTemp, Warning, TEXT("Moved room from %f %f to %f %f"), Tile.GridPos.X, Tile.GridPos.Y, NewGridPos.X, NewGridPos.Y)
	Tile.GridPos = NewGridPos;
	Tile.Position = FVector(Tile.GridPos.X * CellSize, Tile.GridPos.Y * CellSize, 0.f);

	Tile.Corners.MinX = Tile.GridPos.X - (RoomLength / 2);
	Tile.Corners.MaxX = Tile.GridPos.X + (RoomLength / 2);
	Tile.Corners.MinY = Tile.GridPos.Y - (RoomWidth / 2);
	Tile.Corners.MaxY = Tile.GridPos.Y + (RoomWidth / 2);

	return true;
}

int32 AMazeGenerator::RoundToOdd(int32 Value)
{
	return Value % 2 == 0 ? Value + 1 : Value;
}

#pragma endregion

#pragma region Build Corridors

void AMazeGenerator::BuildLinks(TArray<FRoomData>& RoomDataCollection)
{
	TArray<FRoomData> Stack;
	TArray<bool> Visited;
	TArray<FLinkData> Links;
	Visited.Init(false, RoomDataCollection.Num());

	Stack.Add(RoomDataCollection[0]);

	while (Stack.Num() > 0)
	{
		FRoomData Current = Stack.Pop();
		Visited[Current.Id] = true;

		for (auto& Neighbour : Current.Neighbours)
		{
			Links.AddUnique(FLinkData(&Current, Neighbour));

			if (!Visited[Neighbour->Id])
			{
				Stack.Add(*Neighbour);
			}
		}
	}

	Grid PathGrid = Grid(Length, Width); // Rooms can be pushed out of bounds of this
	
	for (auto& Link : Links)
	{
		PopulateLinkPath(Link, PathGrid);

		if (bDebug)
		{
			FIntPoint Prev = FIntPoint::ZeroValue;
			for (FIntPoint Point : Link.Path)
			{
				if (Prev == FIntPoint::ZeroValue) continue;

				UKismetSystemLibrary::DrawDebugLine(
					this
					, FVector(Point.X * CellSize, Point.Y * CellSize, 0.f)
					, FVector(Prev.X * CellSize, Prev.Y * CellSize, 0.f)
					, FColor::Emerald
					, 500.f
					, 16.f
				);
				Prev = Point;
			}
		}
	}
}

// Jump Point Search algorithm to find the shortest path between two rooms
void AMazeGenerator::PopulateLinkPath(FLinkData& Link, Grid& PathGrid) 
{
	TMap<FIntPoint, TArray<FIntPoint>> ValidDirections = { 
		{ FIntPoint(0, -1), TArray<FIntPoint> { FIntPoint(-1, 0), FIntPoint(-1, -1), FIntPoint(0, -1), FIntPoint(1, -1), FIntPoint(1, 0)} },
		{ FIntPoint(1, -1), TArray<FIntPoint> { FIntPoint(0, -1), FIntPoint(1, -1), FIntPoint(1, 0) } },
		{ FIntPoint(1, 0), TArray<FIntPoint> { FIntPoint(0, -1), FIntPoint(1, -1), FIntPoint(1, 0), FIntPoint(1, 1), FIntPoint(0, 1) } },
		{ FIntPoint(1, 1), TArray<FIntPoint> { FIntPoint(1, 0), FIntPoint(1, 1), FIntPoint(0, 1) } },
		{ FIntPoint(0, 1), TArray<FIntPoint> { FIntPoint(1, 0), FIntPoint(1, 1), FIntPoint(0, 1), FIntPoint(-1, 1), FIntPoint(-1, 0) }},
		{ FIntPoint(-1, 1), TArray<FIntPoint> { FIntPoint(0, 1), FIntPoint(-1, 1), FIntPoint(-1, 0)}},
		{ FIntPoint(-1, 0), TArray<FIntPoint> { FIntPoint(0, 1), FIntPoint(-1, 1), FIntPoint(-1, 0), FIntPoint(-1, -1), FIntPoint(0, -1) } },
		{ FIntPoint(-1, -1), TArray<FIntPoint> { FIntPoint(-1, 0), FIntPoint(-1, -1), FIntPoint(0, -1)} }
	};

	FIntPoint StartPoint = Link.RoomA->GridPos;
	FIntPoint EndPoint = Link.RoomB->GridPos;
	FPathCell StartNode = PathGrid[StartPoint.X][StartPoint.Y];
	FPathCell EndNode = PathGrid[EndPoint.X][EndPoint.Y];
	TArray<FPathCell> OpenList;
	OpenList.Add(StartNode);

	while (OpenList.Num() > 0)
	{
		FPathCell CurNode = GetLowestCostCell(OpenList);
		FPathCell* ParentNode = CurNode.Parent;
		UE_LOG(LogTemp, Warning, TEXT("%i, %i"), CurNode.GridPos.X, CurNode.GridPos.Y)
		if (CurNode.GridPos == EndNode.GridPos)
		{
			Link.Path = ConstructPath(EndNode);
			return;
		}

		OpenList.Remove(CurNode);
		FIntPoint MovingDirection = ParentNode == nullptr ? GetGeneralDirection(CurNode.GridPos, EndNode.GridPos) : GetGeneralDirection(ParentNode->GridPos, CurNode.GridPos);
		for (FIntPoint Direction : ValidDirections[MovingDirection])
		{
			double GCost = 0.0;
			FPathCell* NewSuccesor = nullptr;
			UE_LOG(LogTemp, Warning, TEXT("Is cardinal %i, Is diagonal %i, Is in exact direction %i, Distance %f, Distance to wall %f"), IsCardinal(Direction), IsDiagonal(Direction), IsInExactDirection(CurNode.GridPos, EndNode.GridPos, Direction), Distance(CurNode.GridPos, EndNode.GridPos), DistanceToWall(CurNode, Direction, PathGrid))
			if (IsCardinal(Direction) && IsInExactDirection(CurNode.GridPos, EndNode.GridPos, Direction) && Distance(CurNode.GridPos, EndNode.GridPos) <= DistanceToWall(CurNode, Direction, PathGrid))
			{
				UE_LOG(LogTemp, Warning, TEXT("1"))
				NewSuccesor = &EndNode;
				GCost = CurNode.gCost + (Distance(CurNode.GridPos, EndNode.GridPos));
			}
			else if (IsDiagonal(Direction) && IsInExactDirection(CurNode.GridPos, EndNode.GridPos, Direction) && (CurNode.GridPos.X - EndNode.GridPos.X <= DistanceToWall(CurNode, Direction, PathGrid) || CurNode.GridPos.Y - EndNode.GridPos.Y <= DistanceToWall(CurNode, Direction, PathGrid)))
			{
				UE_LOG(LogTemp, Warning, TEXT("2"))
				double MinDiff = FMath::Min(FMath::Abs(CurNode.GridPos.X - EndNode.GridPos.X), FMath::Abs(CurNode.GridPos.Y - EndNode.GridPos.Y));
				NewSuccesor = GetNeighbour(CurNode.GridPos, Direction, MinDiff, PathGrid);
				GCost = CurNode.gCost + FMath::Sqrt(MinDiff);
			}
			else if (DistanceToWall(CurNode, Direction, PathGrid) > 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("3"))
				NewSuccesor = GetNeighbour(CurNode.GridPos, Direction, 1, PathGrid);
				GCost = Distance(CurNode.GridPos, NewSuccesor->GridPos);
				if (IsDiagonal(Direction)) GCost = FMath::Sqrt(GCost);
				else GCost += CurNode.gCost;
			}

			if (NewSuccesor != nullptr)
			{
				if (GCost < NewSuccesor->gCost)
				{
					NewSuccesor->Parent = &CurNode;
					NewSuccesor->gCost = GCost;
					NewSuccesor->hCost = Distance(EndNode.GridPos, NewSuccesor->GridPos);

					if (!OpenList.Contains(NewSuccesor))
					{
						OpenList.Add(*NewSuccesor);
					}
				}
			}
		}
	}

}

#pragma endregion	
