// GPUSKI Plugin - Render resources: GPU buffers, vertex factory, shader parameters

#pragma once

#include "Engine/SkeletalMesh.h"
#include "VertexFactory.h"
#include "GPUSKIBase.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "RenderResource.h"
#include "SceneManagement.h"
#include "GPUSKI.h"

class UGPUSKIAnimCollection;
class FSkeletalMeshLODRenderData;
class FGPUSKIBoneIndexVertexBuffer;
class FGPUSKIVertexFactory;

//----------------------------------------------------------------------
// Uniform buffer: shared per animation collection (bound in shader as "GPUSKIAC")
//----------------------------------------------------------------------
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FGPUSKIAnimCollectionUniformParams, GPUSKI_API)
	SHADER_PARAMETER(uint32, BoneCount)
	SHADER_PARAMETER(uint32, Padding0)
	SHADER_PARAMETER(uint32, Padding1)
	SHADER_PARAMETER(uint32, Padding2)
	SHADER_PARAMETER_SRV(Buffer<float4>, AnimationBuffer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FGPUSKIAnimCollectionUniformParams> FGPUSKIAnimCollectionUBRef;

//----------------------------------------------------------------------
// Uniform buffer: per-mesh manual vertex fetch data (bound as "GPUSKIVertexFetch")
//----------------------------------------------------------------------
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FGPUSKIMeshVertexFetchParams, GPUSKI_API)
	SHADER_PARAMETER(uint32, ColorIndexMask)
	SHADER_PARAMETER(uint32, NumTexCoords)
	SHADER_PARAMETER(uint32, NumBoneInfluence)
	SHADER_PARAMETER(uint32, Unused1)
	SHADER_PARAMETER_SRV(Buffer<float2>, VF_TexCoordBuffer)
	SHADER_PARAMETER_SRV(Buffer<float>, VF_PositionBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, VF_PackedTangentsBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, VF_ColorComponentsBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, VF_BoneWeights)
	SHADER_PARAMETER_SRV(Buffer<uint4>, VF_BoneIndices)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FGPUSKIMeshVertexFetchParams> FGPUSKIMeshVertexFetchParamsRef;

//----------------------------------------------------------------------
// Vertex Factory
//----------------------------------------------------------------------
class FGPUSKIVertexFactory : public FVertexFactory
{
public:
	typedef FVertexFactory Super;

	struct FDataType : FStaticMeshDataType
	{
		FVertexStreamComponent BoneIndices;
		FVertexStreamComponent BoneWeights;
		FVertexStreamComponent ExtraBoneIndices;
		FVertexStreamComponent ExtraBoneWeights;
		int32 MaxBoneInfluence;
	};

	FDataType Data;
	FRHIUniformBuffer* VertexFetchUB = nullptr;

	FGPUSKIVertexFactory(ERHIFeatureLevel::Type InFeatureLevel) : Super(InFeatureLevel) {}

	void InitRHI(FRHICommandListBase& RHICmdList) override;
	void FillData(FRHICommandListBase& RHICmdList, const FGPUSKIBoneIndexVertexBuffer* BoneIndexBuffer, const FSkeletalMeshLODRenderData* LODData);
};

// Templated VF for bone influence permutations
template<int MBI>
class TGPUSKIVertexFactory : public FGPUSKIVertexFactory
{
public:
	DECLARE_VERTEX_FACTORY_TYPE(TGPUSKIVertexFactory);

	typedef FGPUSKIVertexFactory Super;

	TGPUSKIVertexFactory(ERHIFeatureLevel::Type InFeatureLevel) : Super(InFeatureLevel)
	{
		Data.MaxBoneInfluence = MBI;
	}

	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	static bool SupportsTessellationShaders() { return false; }
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);
};

//----------------------------------------------------------------------
// Custom Bone Index Vertex Buffer
//----------------------------------------------------------------------
class FGPUSKIBoneIndexVertexBuffer : public FVertexBufferWithSRV
{
public:
	FStaticMeshVertexDataInterface* BoneData = nullptr;
	bool bIs16BitBoneIndex = false;
	int MaxBoneInfluences = 0;
	int NumVertices = 0;

	~FGPUSKIBoneIndexVertexBuffer()
	{
		if (BoneData)
		{
			delete BoneData;
			BoneData = nullptr;
		}
	}

	void InitRHI(FRHICommandListBase& RHICmdList) override;
	void ReleaseRHI() override;

	void ResizeBuffer()
	{
		if (!BoneData)
		{
			if (bIs16BitBoneIndex)
				BoneData = new TStaticMeshVertexData<uint16>();
			else
				BoneData = new TStaticMeshVertexData<uint8>();
		}
		check(MaxBoneInfluences == 4 || MaxBoneInfluences == 8);
		BoneData->ResizeBuffer(NumVertices * MaxBoneInfluences);
		FMemory::Memzero(BoneData->GetDataPointer(), BoneData->GetResourceSize());
	}

	void SetBoneIndex(uint32 VertexIdx, uint32 InfluenceIdx, uint32 BoneIdx)
	{
		if (bIs16BitBoneIndex)
		{
			FBoneIndex16* Data = ((FBoneIndex16*)BoneData->GetDataPointer());
			Data[VertexIdx * MaxBoneInfluences + InfluenceIdx] = static_cast<FBoneIndex16>(BoneIdx);
		}
		else
		{
			FBoneIndex8* Data = ((FBoneIndex8*)BoneData->GetDataPointer());
			Data[VertexIdx * MaxBoneInfluences + InfluenceIdx] = static_cast<FBoneIndex8>(BoneIdx);
		}
	}

	uint32 GetBufferSizeInBytes() const
	{
		return NumVertices * MaxBoneInfluences * (bIs16BitBoneIndex ? 2u : 1u);
	}
};

//----------------------------------------------------------------------
// Animation Buffer (stores all baked bone transforms)
//----------------------------------------------------------------------
class FGPUSKIAnimationBuffer : public FRenderResource
{
public:
	FStaticMeshVertexDataInterface* Transforms = nullptr;

	FBufferRHIRef Buffer;
	FShaderResourceViewRHIRef ShaderResourceViewRHI;
	bool bHighPrecision = false;

	~FGPUSKIAnimationBuffer();
	void InitRHI(FRHICommandListBase& RHICmdList) override;
	void ReleaseRHI() override;

	void AllocateBuffer();
	void InitBuffer(const TArrayView<FTransform> InTransforms, bool InHighPrecision);
	void InitBuffer(uint32 NumMatrix, bool InHighPrecision, bool bFillIdentity);
	void DestroyBuffer();

	FMatrix3x4* GetDataPointerHP() const;
	FGPUSKIMatrix3x4Half* GetDataPointerLP() const;
};

//----------------------------------------------------------------------
// Per-Mesh Data (bone index buffers + vertex factories per LOD)
//----------------------------------------------------------------------
class FGPUSKIMeshDataEx : public TSharedFromThis<FGPUSKIMeshDataEx>
{
public:
	struct FLODData
	{
		FGPUSKIBoneIndexVertexBuffer BoneData;

		static const int32 NUM_VERTEX_FACTORY = GPUSKI_WITH_EXTRA_BONE ? 4 : 2;
		TUniquePtr<FGPUSKIVertexFactory> VertexFactories[NUM_VERTEX_FACTORY];

		const FSkeletalMeshLODRenderData* SkelLODData = nullptr;
		FGPUSKIMeshVertexFetchParamsRef VertexFetchUB;

		FLODData(ERHIFeatureLevel::Type InFeatureLevel);
		void InitResources(FRHICommandListBase& RHICmdList);
		void ReleaseResources();

		FGPUSKIVertexFactory* GetVertexFactory(int32 MBI);
	};

	TArray<FLODData, TFixedAllocator<GPUSKI_MAX_LOD>> LODs;

	void InitFromMesh(int InBaseLOD, USkeletalMesh* SKMesh, const UGPUSKIAnimCollection* AnimSet);
	void InitResources(FRHICommandListBase& RHICmdList);
	void ReleaseResources();

	void InitMeshData(const FSkeletalMeshRenderData* SKRenderData, int InBaseLOD);
};

typedef TSharedPtr<FGPUSKIMeshDataEx, ESPMode::ThreadSafe> FGPUSKIMeshDataExPtr;
