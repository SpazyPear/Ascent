// Fill out your copyright notice in the Description page of Project Settings.


#include "MazeGenerator.h"
#include "Kismet/KismetSystemLibrary.h"

// Sets default values
AMazeGenerator::AMazeGenerator()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	//Cells = Grid<Cell*>(Length, Width);
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
	for (int32 x = 0; x < TargetDensity; x++)
	{
		uint8 X = FMath::RandRange(0, Length);
		uint8 Y = FMath::RandRange(0, Width);
		Points.Add(FDPoint(X, Y, x));
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

			EdgeQueue.HeapPush(FDEdge::GetInverted(AdjacentEdge), FDEdgeMinComparitor()); // Since Point is always P1, it's possible its being snapped back to somewhere its already been

			if ((FMath::RandRange(0, 1) - AdditionalCorridorChance) > 0 && !Corridors.Contains(AdjacentEdge))
			{
				// Chance to create an additional link that isn't in the MST
				RoomAdjacencies[AdjacentEdge.P1].AddUnique(AdjacentEdge.P2);
				RoomAdjacencies[AdjacentEdge.P2].AddUnique(AdjacentEdge.P1);
				Corridors.Add(AdjacentEdge);
			}
		}
	}

	for (auto& Elem : RoomAdjacencies)
	{
		for (auto& Adj : Elem.Value)
		{
			UKismetSystemLibrary::DrawDebugLine(
				this
				, FVector(Elem.Key.X * CellSize, Elem.Key.Y * CellSize, 0.f)
				, FVector(Adj.X * CellSize, Adj.Y * CellSize, 0.f)
				, FColor::Green
				, 500.f
				, 16.f
			);
		}
	}
}

void AMazeGenerator::DetermineRoomTypes(const TMap<FDPoint, TArray<FDPoint>>& RoomAdjacency)
{
	// Use wave function collapse to determine room types

	TArray<FRoomTile*> RoomTiles;
	for (int x = 0; x < RoomAdjacency.Num(); x++)
	{
		RoomTiles.Add(new FRoomTile(x, FVector2D(0, 0), LayoutRules));
	}

	for (const auto& Point : RoomAdjacency)
	{
		RoomTiles[Point.Key.Id] = new FRoomTile(Point.Key.Id, FVector2D(Point.Key.X, Point.Key.Y), LayoutRules);;
	}

	// Assign adjacancies to room tiles
	for (const auto& Point : RoomAdjacency)
		for (const FDPoint& AdjacentPoint : Point.Value)
		{
			RoomTiles[Point.Key.Id]->Neighbours.Add(RoomTiles[AdjacentPoint.Id]);
		}

	int32 CollapsedRooms = 0;

	//Place spawns first. Then calculate initial entropies
	for (int x = 0; x < PlayerCount; x++)
	{
		uint8 Index = 0;

		// Find a node that allows for a spawn point
		uint8 Attempts = 0;
		while (Attempts < 20)
		{
			Attempts++;
			Index = FMath::RandRange(0, RoomTiles.Num() - 1);
			if (!RoomTiles[Index]->bCollapsed && RoomTiles[Index]->PossibleRoomTypes.Contains(ERoomType::Spawn))
			{
				break;
			}
		}

		RoomTiles[Index]->Collapse(ERoomType::Spawn);
		uint8 NeighboursCollapsed = 0;
		if (!CollapseNeighbours(*RoomTiles[Index], NeighboursCollapsed)) return;
		CollapsedRooms += NeighboursCollapsed;
	}

	// Spawns have been placed, remove them from the possible room types
	for (auto& Room : RoomTiles)
	{
		if (!Room->bCollapsed)
			Room->PossibleRoomTypes.Remove(ERoomType::Spawn);
	}


	// Initially they're all the same, no need to recalculate all of them.
	RoomTiles[0]->RecalculateEntropy();
	for (auto& Room : RoomTiles)
	{
		Room->Entropy = RoomTiles[0]->Entropy;
	}
	
	int32 NextIndex = FMath::RandRange(0, RoomDataCollection.Num() - 1);
	while (CollapsedRooms != RoomTiles.Num())
	{
		// Collapse tile randomly based on room weights
		FRoomTile* Next = RoomTiles[NextIndex];
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

		uint8 NeighboursCollapsed = 0;
		if (!CollapseNeighbours(*Next, NeighboursCollapsed)) return;
		CollapsedRooms += NeighboursCollapsed;

		//Set next as the room with the lowest entropy
		for (int32 i = 0; i < RoomTiles.Num(); i++)
		{
			if (RoomTiles[i]->bCollapsed) continue;
			if (RoomTiles[i]->Entropy < RoomTiles[NextIndex]->Entropy)
			{
				NextIndex = i;
			}
		}
	}

	// Construct room data from tiles
	for (auto& Room : RoomTiles)
	{
		FRoomData Data;
		Data.Id = Room->Id;
		Data.RoomType = Room->PossibleRoomTypes[0];
		Data.GridPos = Room->GridPos;
		Data.Position = FVector(Data.GridPos.X * CellSize, Data.GridPos.Y * CellSize, 0.f);
		RoomDataCollection.Add(&Data);

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
				, FVector(300, 300, 0.f)
				, RoomTypeColours.FindRef(Data.RoomType)
				, FRotator::ZeroRotator
				, 500.f
			);
		}
	}
}

bool AMazeGenerator::CollapseNeighbours(FRoomTile& Tile, uint8& bCollapsed)
{
	uint8 CollapsedRooms = 0;
	// Update neighbours
	for (auto& Neighbour : Tile.Neighbours)
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
			CollapsedRooms++;
			break;
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

}

void AMazeGenerator::BuildLinks()
{
}




