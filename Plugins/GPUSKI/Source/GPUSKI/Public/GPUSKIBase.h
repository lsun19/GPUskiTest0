// GPUSKI Plugin - Base math types and utilities

#pragma once

#include "CoreMinimal.h"
#include "Matrix3x4.h"
#include "Math/Matrix.h"
#include "RenderTransform.h"

//----------------------------------------------------------------------
// Half-precision 3x4 matrix (same layout as FMatrix3x4 but using FFloat16)
//----------------------------------------------------------------------
struct alignas(8) FGPUSKIMatrix3x4Half
{
	FFloat16 M[3][4];

	FGPUSKIMatrix3x4Half() {}

	FGPUSKIMatrix3x4Half(const FMatrix3x4& From)
	{
		for (int Row = 0; Row < 3; Row++)
			for (int Col = 0; Col < 4; Col++)
				M[Row][Col].Set(From.M[Row][Col]);
	}

	void SetMatrixTranspose(const FMatrix44f& Mat)
	{
		// Transposed storage: row i of output = column i of input
		M[0][0].Set(Mat.M[0][0]); M[0][1].Set(Mat.M[1][0]); M[0][2].Set(Mat.M[2][0]); M[0][3].Set(Mat.M[3][0]);
		M[1][0].Set(Mat.M[0][1]); M[1][1].Set(Mat.M[1][1]); M[1][2].Set(Mat.M[2][1]); M[1][3].Set(Mat.M[3][1]);
		M[2][0].Set(Mat.M[0][2]); M[2][1].Set(Mat.M[1][2]); M[2][2].Set(Mat.M[2][2]); M[2][3].Set(Mat.M[3][2]);
	}
};

inline FArchive& operator<<(FArchive& Ar, FGPUSKIMatrix3x4Half& Data)
{
	Ar.Serialize(&Data, sizeof(FGPUSKIMatrix3x4Half));
	return Ar;
}

inline FArchive& operator<<(FArchive& Ar, FMatrix3x4& Data)
{
	Ar.Serialize(&Data, sizeof(FMatrix3x4));
	return Ar;
}

//----------------------------------------------------------------------
// Vectorized bounding box types
//----------------------------------------------------------------------
namespace GPUSKI
{

struct FBoxCenterExtent
{
	FVector Center, Extent;

	FBoxCenterExtent() {}
	FBoxCenterExtent(EForceInit) : Center(ForceInit), Extent(ForceInit) {}
	FBoxCenterExtent(FVector InCenter, FVector InExtent) : Center(InCenter), Extent(InExtent) {}
	FBoxCenterExtent(const FBox& Box) { Box.GetCenterAndExtents(Center, Extent); }

	FBox GetBox() const { return FBox(Center - Extent, Center + Extent); }
};

struct FBoxMinMax
{
	FVector Min, Max;

	FBoxMinMax() {}
	FBoxMinMax(EForceInit)
		: Min(FVector(TNumericLimits<double>::Max()))
		, Max(FVector(-TNumericLimits<double>::Max()))
	{}
	FBoxMinMax(FVector InMin, FVector InMax) : Min(InMin), Max(InMax) {}

	void Add(const FBoxCenterExtent& B)
	{
		Min = Min.ComponentMin(B.Center - B.Extent);
		Max = Max.ComponentMax(B.Center + B.Extent);
	}
	void Add(const FVector& Point)
	{
		Min = Min.ComponentMin(Point);
		Max = Max.ComponentMax(Point);
	}
	void Add(const FBoxMinMax& Other)
	{
		Min = Min.ComponentMin(Other.Min);
		Max = Max.ComponentMax(Other.Max);
	}

	FBoxSphereBounds ToBoxSphereBounds() const
	{
		return FBoxSphereBounds(FBox(Min, Max));
	}
};

} // namespace GPUSKI

inline FArchive& operator<<(FArchive& Ar, GPUSKI::FBoxCenterExtent& Bound)
{
	Ar << Bound.Center;
	Ar << Bound.Extent;
	return Ar;
}
