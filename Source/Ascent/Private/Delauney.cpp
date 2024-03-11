#include "Delauney.h"


TArray<FDTriangle> FDelaunay::Triangulate(TArray<FDPoint>& InPoints, int32 InDelaunayConvexMultiplier) const {
	TArray<FDTriangle> Triangles;
	int32 NPoints = InPoints.Num();
	if (NPoints < 3) {
		UE_LOG(LogActor, Error, TEXT("Triangulate needs at least 3 points."));
		return Triangles;
	}
	if (NPoints == 3) {
		Triangles.Add(FDTriangle(InPoints[0], InPoints[1], InPoints[2]));
		return Triangles;
	}

	// Start (Bowyer Watson) Delaunay triangulation.

	// Get the max amount of expected triangles.
	int32 TrMax = NPoints * 4;
	// Get the min / max dimensions of the grid containing the points.
	float MinX = InPoints[0].X;
	float MinY = InPoints[0].Y;
	float MaxX = MinX;
	float MaxY = MinY;

	for (int32 i = 0; i < NPoints; i++) {
		FDPoint& Point = InPoints[i];
		Point.Id = i;

		MinX = FMath::Min(MinX, Point.X);
		MaxX = FMath::Max(MaxX, Point.X);

		MinY = FMath::Min(MinY, Point.Y);
		MaxY = FMath::Max(MaxY, Point.Y);
	}

	float Dx = ((MaxX - MinX) * InDelaunayConvexMultiplier);
	float Dy = ((MaxY - MinY) * InDelaunayConvexMultiplier);
	float DeltaMax = FMath::Max(Dx, Dy);
	float MidX = (MinX + MaxX) * 0.5f;
	float MidY = (MinY + MaxY) * 0.5f;

	// Add Super Triangle. For simplicity add the generated Super points on top of the point array. 
	FDPoint SuP1 = FDPoint(MidX - 2.f * DeltaMax, MidY - DeltaMax, NPoints);
	FDPoint SuP2 = FDPoint(MidX, MidY + 2.f * DeltaMax, NPoints + 1);
	FDPoint SuP3 = FDPoint(MidX + 2.f * DeltaMax, MidY - DeltaMax, NPoints + 2);
	InPoints.EmplaceAt(SuP1.Id, SuP1);
	InPoints.EmplaceAt(SuP2.Id, SuP2);
	InPoints.EmplaceAt(SuP3.Id, SuP3);
	Triangles.Add(FDTriangle(SuP1, SuP2, SuP3));

	// Use NPoints instead of InPoints.Num() when looping over the original points, to not include the initial super points.
	for (int32 i = 0; i < NPoints; i++) {
		TArray<FDEdge> Edges;

		// For each point, look which triangles their CircumCircle contains this point
		// , in which case we don't want the triangle because it is not a Delaunay triangle.
		for (int32 j = Triangles.Num() - 1; j >= 0; j--) {
			FDTriangle CurTriangle = Triangles[j];
			if (CurTriangle.IsInCircumCircle(InPoints[i])) {

				//// For each edge, add if not yet present by IsSimilar
				//auto AddEdgeIfNotSimilar = [&Edges](const FDEdge& InEdge) {
				//	bool bAdd = true;
				//	for (const FDEdge& EdgeX : Edges) {
				//		if (EdgeX.IsSimilar(InEdge)) {
				//			bAdd = false;
				//			break;
				//		}
				//	}
				//	if (bAdd) {
				//		Edges.Add(InEdge);
				//	}
				//	};
				//AddEdgeIfNotSimilar(CurTriangle.E1);
				//AddEdgeIfNotSimilar(CurTriangle.E2);
				//AddEdgeIfNotSimilar(CurTriangle.E3);

				Edges.Add(CurTriangle.E1);
				Edges.Add(CurTriangle.E2);
				Edges.Add(CurTriangle.E3);

				// Remove the unwanted triangle
				Triangles.RemoveAt(j);
			}
		}

		for (int32 j = Edges.Num() - 2; j >= 0; j--)
		{
			for (int32 k = Edges.Num() - 1; k >= j + 1; k--)
			{
				if (Edges.IsValidIndex(j) && Edges.IsValidIndex(k) && Edges[j].IsSimilar(Edges[k]))
				{
					Edges.RemoveAt(j);
					Edges.RemoveAt(k - 1);
				}
			}
		}

		for (int32 j = 0; j < Edges.Num(); j++) {
			if (Triangles.Num() > TrMax) {
				UE_LOG(LogActor, Error, TEXT("Made more triangles than required. "));
			}

			Triangles.Add(FDTriangle(Edges[j].P1, Edges[j].P2, InPoints[i]));
		}
	}

	// Remove triangles using the initial super points.
	for (int i = Triangles.Num() - 1; i >= 0; i--) {
		FDTriangle Triangle = Triangles[i];
		if (Triangle.P1.Id >= NPoints
			|| Triangle.P2.Id >= NPoints
			|| Triangle.P3.Id >= NPoints
			) {
			Triangles.RemoveAt(i);
		}
	}
	// Remove the initial super points.
	InPoints.Remove(SuP1);
	InPoints.Remove(SuP2);
	InPoints.Remove(SuP3);

	return Triangles;
}