// GPUSKI Plugin - Render resources implementation

#include "GPUSKIRenderResources.h"
#include "GPUSKIAnimCollection.h"
#include "MeshDrawShaderBindings.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "GPUSKI.h"
#include "RenderUtils.h"
#include "ShaderCore.h"
#include "MeshMaterialShader.h"
#include "MaterialDomain.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalRenderResources.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

#ifndef GPUSKI_WITH_MANUAL_VERTEX_FETCH
#define GPUSKI_WITH_MANUAL_VERTEX_FETCH 0
#endif

//----------------------------------------------------------------------
// Uniform buffer implementations
//----------------------------------------------------------------------
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FGPUSKIAnimCollectionUniformParams, "GPUSKIAC");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FGPUSKIMeshVertexFetchParams, "GPUSKIVertexFetch");

//----------------------------------------------------------------------
// Animation Buffer
//----------------------------------------------------------------------
FGPUSKIAnimationBuffer::~FGPUSKIAnimationBuffer()
{
	DestroyBuffer();
}

void FGPUSKIAnimationBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	const uint32 Stride = bHighPrecision ? sizeof(float[4]) : sizeof(uint16[4]);
	FRHIResourceCreateInfo Info(TEXT("FGPUSKIAnimationBuffer"), Transforms->GetResourceArray());
	Buffer = RHICmdList.CreateVertexBuffer(Transforms->Num() * Transforms->GetStride(), BUF_ShaderResource, Info);
	ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(Buffer, Stride, bHighPrecision ? PF_A32B32G32R32F : PF_FloatRGBA);

	delete Transforms;
	Transforms = nullptr;
}

void FGPUSKIAnimationBuffer::ReleaseRHI()
{
	ShaderResourceViewRHI.SafeRelease();
	Buffer.SafeRelease();
}

void FGPUSKIAnimationBuffer::AllocateBuffer()
{
	if (Transforms)
		delete Transforms;

	if (bHighPrecision)
		Transforms = new TStaticMeshVertexData<FMatrix3x4>();
	else
		Transforms = new TStaticMeshVertexData<FGPUSKIMatrix3x4Half>();
}

void FGPUSKIAnimationBuffer::InitBuffer(const TArrayView<FTransform> InTransforms, bool InHighPrecision)
{
	InitBuffer(InTransforms.Num(), InHighPrecision, false);

	if (bHighPrecision)
	{
		FMatrix3x4* Dst = (FMatrix3x4*)Transforms->GetDataPointer();
		for (int i = 0; i < InTransforms.Num(); i++)
			Dst[i].SetMatrixTranspose(InTransforms[i].ToMatrixWithScale());
	}
	else
	{
		FGPUSKIMatrix3x4Half* Dst = (FGPUSKIMatrix3x4Half*)Transforms->GetDataPointer();
		for (int i = 0; i < InTransforms.Num(); i++)
			Dst[i].SetMatrixTranspose(FMatrix44f(InTransforms[i].ToMatrixWithScale()));
	}
}

void FGPUSKIAnimationBuffer::InitBuffer(uint32 NumMatrix, bool InHighPrecision, bool bFillIdentity)
{
	this->bHighPrecision = InHighPrecision;
	this->AllocateBuffer();
	this->Transforms->ResizeBuffer(NumMatrix);

	if (bFillIdentity)
	{
		if (InHighPrecision)
		{
			FMatrix3x4 IdentityMatrix;
			IdentityMatrix.SetMatrixTranspose(FMatrix::Identity);
			FMatrix3x4* Dst = (FMatrix3x4*)Transforms->GetDataPointer();
			for (uint32 i = 0; i < NumMatrix; i++)
				Dst[i] = IdentityMatrix;
		}
		else
		{
			FGPUSKIMatrix3x4Half IdentityMatrix;
			IdentityMatrix.SetMatrixTranspose(FMatrix44f::Identity);
			FGPUSKIMatrix3x4Half* Dst = (FGPUSKIMatrix3x4Half*)Transforms->GetDataPointer();
			for (uint32 i = 0; i < NumMatrix; i++)
				Dst[i] = IdentityMatrix;
		}
	}
}

void FGPUSKIAnimationBuffer::DestroyBuffer()
{
	if (Transforms)
	{
		delete Transforms;
		Transforms = nullptr;
	}
}

FMatrix3x4* FGPUSKIAnimationBuffer::GetDataPointerHP() const
{
	check(this->bHighPrecision);
	return (FMatrix3x4*)this->Transforms->GetDataPointer();
}

FGPUSKIMatrix3x4Half* FGPUSKIAnimationBuffer::GetDataPointerLP() const
{
	check(!this->bHighPrecision);
	return (FGPUSKIMatrix3x4Half*)this->Transforms->GetDataPointer();
}

//----------------------------------------------------------------------
// Bone Index Vertex Buffer
//----------------------------------------------------------------------
void FGPUSKIBoneIndexVertexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	check(BoneData);
	uint32 Size = BoneData->Num();
	FRHIResourceCreateInfo Info(TEXT("FGPUSKIBoneIndexVertexBuffer"), BoneData->GetResourceArray());
	VertexBufferRHI = RHICmdList.CreateVertexBuffer(Size, BUF_Static | BUF_ShaderResource, Info);
	ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(
		VertexBufferRHI,
		bIs16BitBoneIndex ? 8 : 4,
		bIs16BitBoneIndex ? PF_R16G16B16A16_UINT : PF_R8G8B8A8_UINT
	);
	delete BoneData;
	BoneData = nullptr;
}

void FGPUSKIBoneIndexVertexBuffer::ReleaseRHI()
{
	VertexBufferRHI.SafeRelease();
}

//----------------------------------------------------------------------
// Vertex Factory: FillData
//----------------------------------------------------------------------
void FGPUSKIVertexFactory::FillData(FRHICommandListBase& RHICmdList, const FGPUSKIBoneIndexVertexBuffer* BoneIndexBuffer, const FSkeletalMeshLODRenderData* LODData)
{
	const FStaticMeshVertexBuffers& SMVB = LODData->StaticVertexBuffers;

	SMVB.PositionVertexBuffer.BindPositionVertexBuffer(this, Data);
	SMVB.StaticMeshVertexBuffer.BindTangentVertexBuffer(this, Data);
	SMVB.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(this, Data);
	SMVB.ColorVertexBuffer.BindColorVertexBuffer(this, Data);

	const FSkinWeightDataVertexBuffer* LODSkinData = LODData->GetSkinWeightVertexBuffer()->GetDataVertexBuffer();
	check(LODSkinData->GetMaxBoneInfluences() == BoneIndexBuffer->MaxBoneInfluences);
	const bool bExtraData = Data.MaxBoneInfluence > 4;

	// Bind bone weights from mesh's skin weight buffer
	{
		EVertexElementType ElemType = LODSkinData->Use16BitBoneWeight() ? VET_UShort4N : VET_UByte4N;
		uint32 Stride = LODSkinData->GetConstantInfluencesVertexStride();
		Data.BoneWeights = FVertexStreamComponent(LODSkinData, LODSkinData->GetConstantInfluencesBoneWeightsOffset(), Stride, ElemType);
		if (bExtraData)
		{
			Data.ExtraBoneWeights = Data.BoneWeights;
			Data.ExtraBoneWeights.Offset += (LODSkinData->GetBoneWeightByteSize() * 4);
		}
		else
		{
			Data.ExtraBoneWeights = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, ElemType);
		}
	}

	// Bind bone indices from our custom bone index buffer
	{
		uint32 Stride = (BoneIndexBuffer->bIs16BitBoneIndex ? 2 : 1) * LODSkinData->GetMaxBoneInfluences();
		EVertexElementType ElemType = BoneIndexBuffer->bIs16BitBoneIndex ? VET_UShort4 : VET_UByte4;
		Data.BoneIndices = FVertexStreamComponent(BoneIndexBuffer, 0, Stride, ElemType);
		if (bExtraData)
		{
			Data.ExtraBoneIndices = Data.BoneIndices;
			Data.ExtraBoneIndices.Offset = Stride / 2;
		}
		else
		{
			Data.ExtraBoneIndices = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, ElemType);
		}
	}
}

//----------------------------------------------------------------------
// Vertex Factory: InitRHI (vertex declaration)
//----------------------------------------------------------------------
void FGPUSKIVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	FDataType& D = Data;

	FVertexDeclarationElementList OutElements;
	OutElements.Add(AccessStreamComponent(D.PositionComponent, 0));
	OutElements.Add(AccessStreamComponent(D.TangentBasisComponents[0], 1));
	OutElements.Add(AccessStreamComponent(D.TangentBasisComponents[1], 2));

	if (D.TextureCoordinates.Num())
	{
		const uint8 BaseTexCoordAttribute = 5;
		for (int32 CoordinateIndex = 0; CoordinateIndex < D.TextureCoordinates.Num(); ++CoordinateIndex)
		{
			OutElements.Add(AccessStreamComponent(D.TextureCoordinates[CoordinateIndex], BaseTexCoordAttribute + CoordinateIndex));
		}
		for (int32 CoordinateIndex = D.TextureCoordinates.Num(); CoordinateIndex < MAX_TEXCOORDS; ++CoordinateIndex)
		{
			OutElements.Add(AccessStreamComponent(D.TextureCoordinates[D.TextureCoordinates.Num() - 1], BaseTexCoordAttribute + CoordinateIndex));
		}
	}

	OutElements.Add(AccessStreamComponent(D.ColorComponent, 13));
	OutElements.Add(AccessStreamComponent(D.BoneIndices, 3));
	OutElements.Add(AccessStreamComponent(D.BoneWeights, 4));

	if (Data.MaxBoneInfluence > 4)
	{
		OutElements.Add(AccessStreamComponent(D.ExtraBoneIndices, 14));
		OutElements.Add(AccessStreamComponent(D.ExtraBoneWeights, 15));
	}

	this->AddPrimitiveIdStreamElement(EVertexInputStreamType::Default, OutElements, 16, 16);

	InitDeclaration(OutElements);
	check(GetDeclaration());
}

//----------------------------------------------------------------------
// Vertex Factory: Template specializations
//----------------------------------------------------------------------
template<int MBI>
bool TGPUSKIVertexFactory<MBI>::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return Parameters.MaterialParameters.MaterialDomain == MD_Surface
		&& (Parameters.MaterialParameters.bIsUsedWithSkeletalMesh || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

template<int MBI>
void TGPUSKIVertexFactory<MBI>::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);

	const FStaticFeatureLevel MaxSupportedFeatureLevel = GetMaxSupportedFeatureLevel(Parameters.Platform);
	const bool bUseGPUScene = UseGPUScene(Parameters.Platform, MaxSupportedFeatureLevel) && (MaxSupportedFeatureLevel > ERHIFeatureLevel::ES3_1);
	const bool bSupportsPrimitiveIdStream = Parameters.VertexFactoryType->SupportsPrimitiveIdStream();
	checkf(bUseGPUScene && bSupportsPrimitiveIdStream, TEXT("GPUSKI only supports GPUScene!"));

	check(MBI > 0 && MBI <= 8);
	OutEnvironment.SetDefine(TEXT("MAX_BONE_INFLUENCE"), MBI);
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	OutEnvironment.SetDefine(TEXT("VF_REQUIRES_PER_INSTANCE_CUSTOM_DATA"), 1);
	OutEnvironment.SetDefine(TEXT("USES_PER_INSTANCE_CUSTOM_DATA"), 1);
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_SPEEDTREE_WIND"), 0);
	OutEnvironment.SetDefine(TEXT("USE_DITHERED_LOD_TRANSITION_FOR_INSTANCED"), 0);
	OutEnvironment.SetDefine(TEXT("USE_DITHERED_LOD_TRANSITION"), 0);

	bool bSupportMVF = RHISupportsManualVertexFetch(Parameters.Platform);
	OutEnvironment.SetDefineIfUnset(TEXT("MANUAL_VERTEX_FETCH"), GPUSKI_WITH_MANUAL_VERTEX_FETCH && bSupportMVF);
}

template<int MBI>
void TGPUSKIVertexFactory<MBI>::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
}

//----------------------------------------------------------------------
// Shader Parameters (binds UB per draw call)
//----------------------------------------------------------------------
class FGPUSKIShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FGPUSKIShaderParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap) {}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		const FGPUSKIVertexFactory* VF = static_cast<const FGPUSKIVertexFactory*>(VertexFactory);
		const FRHIUniformBuffer* ACUniformBuffer = reinterpret_cast<const FRHIUniformBuffer*>(BatchElement.UserData);

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FGPUSKIAnimCollectionUniformParams>(), ACUniformBuffer);

		if (VF->VertexFetchUB)
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FGPUSKIMeshVertexFetchParams>(), VF->VertexFetchUB);
	}
};

IMPLEMENT_TYPE_LAYOUT(FGPUSKIShaderParameters);

//----------------------------------------------------------------------
// Vertex Factory Type Registration
//----------------------------------------------------------------------
constexpr EVertexFactoryFlags GPUSKIVFFlags =
	EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
	| (GPUSKI_WITH_MANUAL_VERTEX_FETCH ? EVertexFactoryFlags::SupportsManualVertexFetch : EVertexFactoryFlags::None);

#define GPUSKI_IMPL_VF(MBI) \
	using GPUSKIVF##MBI = TGPUSKIVertexFactory<MBI>; \
	IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE(template<>, GPUSKIVF##MBI, "/Plugin/GPUSKI/Private/GPUSKIVertexFactory.ush", GPUSKIVFFlags); \
	IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(GPUSKIVF##MBI, SF_Vertex, FGPUSKIShaderParameters); \
	IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(GPUSKIVF##MBI, SF_Pixel, FGPUSKIShaderParameters);

GPUSKI_IMPL_VF(2);
GPUSKI_IMPL_VF(4);
#if GPUSKI_WITH_EXTRA_BONE
GPUSKI_IMPL_VF(6);
GPUSKI_IMPL_VF(8);
#endif

//----------------------------------------------------------------------
// Mesh Data Ex
//----------------------------------------------------------------------
FGPUSKIMeshDataEx::FLODData::FLODData(ERHIFeatureLevel::Type InFeatureLevel)
{
	VertexFactories[0] = TUniquePtr<FGPUSKIVertexFactory>(new TGPUSKIVertexFactory<2>(InFeatureLevel));
	VertexFactories[1] = TUniquePtr<FGPUSKIVertexFactory>(new TGPUSKIVertexFactory<4>(InFeatureLevel));
#if GPUSKI_WITH_EXTRA_BONE
	VertexFactories[2] = TUniquePtr<FGPUSKIVertexFactory>(new TGPUSKIVertexFactory<6>(InFeatureLevel));
	VertexFactories[3] = TUniquePtr<FGPUSKIVertexFactory>(new TGPUSKIVertexFactory<8>(InFeatureLevel));
#endif
}

void FGPUSKIMeshDataEx::FLODData::InitResources(FRHICommandListBase& RHICmdList)
{
	check(IsInRenderingThread());
	BoneData.InitResource(RHICmdList);

	for (TUniquePtr<FGPUSKIVertexFactory>& VF : VertexFactories)
	{
		VF->FillData(RHICmdList, &BoneData, SkelLODData);
		VF->InitResource(RHICmdList);
	}

	// Create manual vertex fetch UB if supported
	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform);
	const bool bUseManualVertexFetch = RHISupportsManualVertexFetch(GMaxRHIShaderPlatform);
	if (GPUSKI_WITH_MANUAL_VERTEX_FETCH && bUseManualVertexFetch && bCanUseGPUScene)
	{
		FGPUSKIVertexFactory* FirstVF = VertexFactories[0].Get();
		const FSkinWeightDataVertexBuffer* LODSkinData = this->SkelLODData->GetSkinWeightVertexBuffer()->GetDataVertexBuffer();
		check(LODSkinData->GetMaxBoneInfluences() == this->BoneData.MaxBoneInfluences);
		check(LODSkinData->VertexBufferRHI);

		FGPUSKIMeshVertexFetchParams VFParams;
		VFParams.VF_PositionBuffer = FirstVF->Data.PositionComponentSRV;
		VFParams.VF_PackedTangentsBuffer = FirstVF->Data.TangentsSRV;
		VFParams.VF_TexCoordBuffer = FirstVF->Data.TextureCoordinatesSRV;
		VFParams.VF_ColorComponentsBuffer = FirstVF->Data.ColorComponentsSRV;
		VFParams.VF_BoneIndices = this->BoneData.ShaderResourceViewRHI;

		checkf(LODSkinData->Use16BitBoneIndex() == LODSkinData->Use16BitBoneWeight(),
			TEXT("BoneIndex and BoneWeight must be same size right now."));

		auto Desc = FRHIViewDesc::CreateBufferSRV()
			.SetType(FRHIViewDesc::EBufferType::Typed)
			.SetFormat(LODSkinData->Use16BitBoneWeight() ? PF_R16G16B16A16_UNORM : PF_R8G8B8A8);
		auto SRV = RHICmdList.CreateShaderResourceView(LODSkinData->VertexBufferRHI, Desc);
		VFParams.VF_BoneWeights = SRV;

		VFParams.ColorIndexMask = FirstVF->Data.ColorIndexMask;
		VFParams.NumTexCoords = FirstVF->Data.NumTexCoords;
		VFParams.NumBoneInfluence = LODSkinData->GetMaxBoneInfluences();
		check(VFParams.NumBoneInfluence == 4 || VFParams.NumBoneInfluence == 8);

		VertexFetchUB = FGPUSKIMeshVertexFetchParamsRef::CreateUniformBufferImmediate(VFParams, EUniformBufferUsage::UniformBuffer_MultiFrame);
	}

	for (TUniquePtr<FGPUSKIVertexFactory>& VF : VertexFactories)
	{
		VF->VertexFetchUB = VertexFetchUB.GetReference();
	}
}

void FGPUSKIMeshDataEx::FLODData::ReleaseResources()
{
	check(IsInRenderingThread());
	for (TUniquePtr<FGPUSKIVertexFactory>& VF : VertexFactories)
	{
		VF->ReleaseResource();
		VF = nullptr;
	}
	BoneData.ReleaseResource();
}

FGPUSKIVertexFactory* FGPUSKIMeshDataEx::FLODData::GetVertexFactory(int32 MBI)
{
	switch (MBI)
	{
	case 1:
	case 2: return VertexFactories[0].Get();
	case 3:
	case 4: return VertexFactories[1].Get();
#if GPUSKI_WITH_EXTRA_BONE
	case 5:
	case 6: return VertexFactories[2].Get();
	case 7:
	case 8: return VertexFactories[3].Get();
#endif
	}
	return nullptr;
}

void FGPUSKIMeshDataEx::InitFromMesh(int InBaseLOD, USkeletalMesh* SKMesh, const UGPUSKIAnimCollection* AnimSet)
{
	const FSkeletalMeshRenderData* SKMRenderData = SKMesh->GetResourceForRendering();
	this->LODs.Reserve(SKMRenderData->LODRenderData.Num() - InBaseLOD);

	for (int LODIndex = InBaseLOD; LODIndex < SKMRenderData->LODRenderData.Num(); LODIndex++)
	{
		const FSkeletalMeshLODRenderData& SKMLODData = SKMRenderData->LODRenderData[LODIndex];
		const FSkinWeightVertexBuffer* SkinVB = SKMLODData.GetSkinWeightVertexBuffer();
		checkf(SkinVB->GetNeedsCPUAccess(), TEXT("SkinWeightVertexBuffer needs CPU access!"));

		check(SkinVB->GetBoneInfluenceType() == DefaultBoneInfluence);
		check(SkinVB->GetMaxBoneInfluences() == 4 || SkinVB->GetMaxBoneInfluences() == 8);

		FGPUSKIMeshDataEx::FLODData& LODData = this->LODs.Emplace_GetRef(GMaxRHIFeatureLevel);
		const bool bNeeds16Bit = SkinVB->Use16BitBoneIndex() || SKMesh->GetSkeleton()->GetReferenceSkeleton().GetNum() > 255;
		LODData.BoneData.bIs16BitBoneIndex = bNeeds16Bit;
		LODData.BoneData.MaxBoneInfluences = SkinVB->GetMaxBoneInfluences();
		LODData.BoneData.NumVertices = SkinVB->GetNumVertices();
		LODData.BoneData.ResizeBuffer();

		// Remap vertex bone indices to render bone indices
		for (uint32 VertexIndex = 0; VertexIndex < SkinVB->GetNumVertices(); VertexIndex++)
		{
			int SectionIndex, SectionVertexIndex;
			SKMLODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);

			const FSkelMeshRenderSection& SectionInfo = SKMLODData.RenderSections[SectionIndex];
			for (uint32 InfluenceIndex = 0; InfluenceIndex < (uint32)SectionInfo.MaxBoneInfluences; InfluenceIndex++)
			{
				uint32 BoneIndex = SkinVB->GetBoneIndex(VertexIndex, InfluenceIndex);
				FBoneIndexType MeshBoneIndex = SectionInfo.BoneMap[BoneIndex];
				int SkeletonBoneIndex = AnimSet->Skeleton->GetSkeletonBoneIndexFromMeshBoneIndex(SKMesh, MeshBoneIndex);
				check(SkeletonBoneIndex != INDEX_NONE);
				int RenderBoneIndex = AnimSet->SkeletonBoneToRenderBone[SkeletonBoneIndex];
				check(RenderBoneIndex != INDEX_NONE);

				LODData.BoneData.SetBoneIndex(VertexIndex, InfluenceIndex, RenderBoneIndex);
			}
		}
	}
}

void FGPUSKIMeshDataEx::InitResources(FRHICommandListBase& RHICmdList)
{
	check(IsInRenderingThread());
	for (int32 LODIndex = 0; LODIndex < LODs.Num(); LODIndex++)
	{
		LODs[LODIndex].InitResources(RHICmdList);
	}
}

void FGPUSKIMeshDataEx::ReleaseResources()
{
	check(IsInRenderingThread());
	for (int32 LODIndex = 0; LODIndex < LODs.Num(); LODIndex++)
	{
		LODs[LODIndex].ReleaseResources();
	}
}

void FGPUSKIMeshDataEx::InitMeshData(const FSkeletalMeshRenderData* SKRenderData, int InBaseLOD)
{
	check(this->LODs.Num() <= SKRenderData->LODRenderData.Num());
	for (int LODIndex = 0; LODIndex < this->LODs.Num(); LODIndex++)
	{
		this->LODs[LODIndex].SkelLODData = &SKRenderData->LODRenderData[LODIndex + InBaseLOD];
	}
}
