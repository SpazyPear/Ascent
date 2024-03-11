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
	RoomTiles.Init(nullptr, RoomAdjacency.Num());
	for (const auto& Point : RoomAdjacency)
	{
		FRoomTile RoomData = FRoomTile(Point.Key.Id, FVector2D(Point.Key.X, Point.Key.Y), LayoutRules);
		RoomTiles[Point.Key.Id] = &RoomData;
	}

	for (const auto& Point : RoomAdjacency)
		for (const FDPoint& AdjacentPoint : Point.Value)
		{
			RoomTiles[Point.Key.Id]->Neighbours.Add(RoomTiles[AdjacentPoint.Id]);
		}

	// Initially they're all the same, no need to recalculate all of them.
	RoomTiles[0]->RecalculateEntropy();
	for (auto& Room : RoomTiles)
	{
		Room->Entropy = RoomTiles[0]->Entropy;
	}

	//Place spawns now? Then calculate initial entropies
	
	int32 NextIndex = FMath::RandRange(0, RoomDataCollection.Num() - 1);
	int32 CollapsedRooms = 0;
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
				Next->PossibleRoomTypes = TArray<ERoomType> { RoomType };
				Next->Collapsed = true;
				CollapsedRooms++;
				break;
			}
		}

		ERoomType CollapsedRoomType = Next->PossibleRoomTypes[0];
		Next->Collapsed = true;

		//Update neighbours
		for (auto& Neighbour : Next->Neighbours)
		{
			for (ERoomType NeighbourOption : Neighbour->PossibleRoomTypes)
			{
				if (!LayoutRules.RoomEntropy[NeighbourOption].Possibilities.Find(CollapsedRoomType))
				{
					Neighbour->PossibleRoomTypes.Remove(NeighbourOption);

					if (Neighbour->PossibleRoomTypes.Num() == 1)
					{
						Neighbour->Collapsed = true;
						CollapsedRooms++;
						break;
					}
					else if (Neighbour->PossibleRoomTypes.Num() == 0)
					{
						UE_LOG(LogTemp, Error, TEXT("No possible room types. WFC failed."))
					}
				}
			}
			Neighbour->RecalculateEntropy();
		}

		//Set next as the room with the lowest entropy
		for (int32 i = 0; i < RoomTiles.Num(); i++)
		{
			if (RoomTiles[i]->Collapsed) continue;
			if (RoomTiles[i]->Entropy < RoomTiles[NextIndex]->Entropy)
			{
				NextIndex = i;
			}
		}
	}

	//Construct room data from tiles
	for (auto& Room : RoomTiles)
	{
		FRoomData Data;
		Data.Id = Room->Id;
		Data.RoomType = Room->PossibleRoomTypes[0];
		Data.GridPos = Room->GridPos;
		Data.Position = FVector(Data.GridPos.X * CellSize, Data.GridPos.Y * CellSize, 0.f);
		RoomDataCollection.Add(&Data);

		if (Debug)
		{
			const TMap<ERoomType, FColor> RoomTypeColours = {
				{ ERoomType::Spawn, FColor::Green },
				{ ERoomType::Boss, FColor::Red },
				{ ERoomType::Treasure, FColor::Yellow },
				{ ERoomType::Normal, FColor::White },
				{ ERoomType::AscentPoint, FColor::Blue },
			};

			UKismetSystemLibrary::DrawDebugBox(
				this
				, Data.Position
				, FVector(300, 300, 0.f)
				, RoomTypeColours[Data.RoomType]
				, FRotator::ZeroRotator
				, 500.f
			);
		}
	}


}

void AMazeGenerator::PushRoomsApart()
{
}

void AMazeGenerator::BuildLinks()
{
}




