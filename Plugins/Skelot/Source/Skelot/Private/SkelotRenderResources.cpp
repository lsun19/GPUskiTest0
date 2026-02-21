// Copyright 2024 Lazy Marmot Games. All Rights Reserved.

#include "SkelotRenderResources.h"
#include "SkelotAnimCollection.h"
#include "MeshDrawShaderBindings.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Skelot.h"
#include "RenderUtils.h"
#include "ShaderCore.h"
#include "MeshMaterialShader.h"
#include "MaterialDomain.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalRenderResources.h"
#include "SkelotPrivateUtils.h"
#include "Misc/SpinLock.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

#ifndef SKELOT_WITH_MANUAL_VERTEX_FETCH
#define SKELOT_WITH_MANUAL_VERTEX_FETCH 0
#endif


IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSkelotAnimCollectionUniformParams, "SkelotAC");

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSkelotMeshVertexFetchParams, "SkelotVertexFetch");



FSkelotAnimationBuffer::~FSkelotAnimationBuffer()
{
	DestroyBuffer();
}

void FSkelotAnimationBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	const uint32 Stride = bHighPrecision ? sizeof(float[4]) : sizeof(uint16[4]);
	FRHIResourceCreateInfo info(TEXT("FSkelotAnimationBuffer"), Transforms->GetResourceArray());
	//ERHIAccess AM = ERHIAccess::Unknown;// ERHIAccess::SRVGraphics | ERHIAccess::UAVCompute | ERHIAccess::CopyDest;
	Buffer = RHICmdList.CreateVertexBuffer(Transforms->Num() * Transforms->GetStride(), BUF_UnorderedAccess | BUF_ShaderResource, info);
	ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(Buffer, Stride, bHighPrecision ? PF_A32B32G32R32F : PF_FloatRGBA);
	UAV = RHICmdList.CreateUnorderedAccessView(Buffer, bHighPrecision ? PF_A32B32G32R32F : PF_FloatRGBA);

	delete Transforms;
	Transforms = nullptr;
}


void FSkelotAnimationBuffer::ReleaseRHI()
{
	ShaderResourceViewRHI.SafeRelease();
	Buffer.SafeRelease();
}

void FSkelotAnimationBuffer::Serialize(FArchive& Ar)
{
	Ar << bHighPrecision;
	bool bAnyData = Transforms != nullptr;
	Ar << bAnyData;
	if (Ar.IsSaving())
	{
		if (Transforms)
			Transforms->Serialize(Ar);
	}
	else
	{
		if (bAnyData)
		{
			AllocateBuffer();
			Transforms->Serialize(Ar);
		}
	}
}

void FSkelotAnimationBuffer::AllocateBuffer()
{
	if (Transforms)
		delete Transforms;

	if (bHighPrecision)
		Transforms = new TStaticMeshVertexData<FMatrix3x4>();
	else
		Transforms = new TStaticMeshVertexData<FMatrix3x4Half>();
}

void FSkelotAnimationBuffer::InitBuffer(const TArrayView<FTransform> InTransforms, bool InHightPrecision)
{
	InitBuffer(InTransforms.Num(), InHightPrecision, false);

	if (bHighPrecision)
	{
		FMatrix3x4* Dst = (FMatrix3x4*)Transforms->GetDataPointer();
		for (int i = 0; i < InTransforms.Num(); i++)
			Dst[i].SetMatrixTranspose(InTransforms[i].ToMatrixWithScale());

	}
	else
	{
		FMatrix3x4Half* Dst = (FMatrix3x4Half*)Transforms->GetDataPointer();
		for (int i = 0; i < InTransforms.Num(); i++)
			Dst[i].SetMatrixTranspose(FMatrix44f(InTransforms[i].ToMatrixWithScale()));

	}
}

void FSkelotAnimationBuffer::InitBuffer(uint32 NumMatrix, bool InHightPrecision, bool bFillIdentity)
{
	this->bHighPrecision = InHightPrecision;
	this->AllocateBuffer();
	this->Transforms->ResizeBuffer(NumMatrix);

	if(bFillIdentity)
	{
		if (InHightPrecision)
		{
			FMatrix3x4 IdentityMatrix;
			IdentityMatrix.SetMatrixTranspose(FMatrix::Identity);

			FMatrix3x4* Dst = (FMatrix3x4*)Transforms->GetDataPointer();
			for (uint32 i = 0; i < NumMatrix; i++)
				Dst[i] = IdentityMatrix;
		}
		else
		{
			FMatrix3x4Half IdentityMatrix;
			IdentityMatrix.SetMatrixTranspose(FMatrix44f::Identity);

			FMatrix3x4Half* Dst = (FMatrix3x4Half*)Transforms->GetDataPointer();
			for (uint32 i = 0; i < NumMatrix; i++)
				Dst[i] = IdentityMatrix;
		}
	}
}

void FSkelotAnimationBuffer::DestroyBuffer()
{
	if (Transforms)
	{
		delete Transforms;
		Transforms = nullptr;
	}
}




FMatrix3x4Half* FSkelotAnimationBuffer::GetDataPointerLP() const
{
	check(!this->bHighPrecision);
	return (FMatrix3x4Half*)this->Transforms->GetDataPointer();
}

FMatrix3x4* FSkelotAnimationBuffer::GetDataPointerHP() const
{
	check(this->bHighPrecision);
	return (FMatrix3x4*)this->Transforms->GetDataPointer();
}


void FSkelotMeshDataEx::InitFromMesh(int InBaseLOD, USkeletalMesh* SKMesh, const USkelotAnimCollection* AnimSet)
{
	const FSkeletalMeshRenderData* SKMRenderData = SKMesh->GetResourceForRendering();

	this->LODs.Reserve(SKMRenderData->LODRenderData.Num() - InBaseLOD);

	for (int LODIndex = InBaseLOD; LODIndex < SKMRenderData->LODRenderData.Num(); LODIndex++)	//for each LOD
	{
		const FSkeletalMeshLODRenderData& SKMLODData = SKMRenderData->LODRenderData[LODIndex];
		const FSkinWeightVertexBuffer* SkinVB = SKMLODData.GetSkinWeightVertexBuffer();
		checkf(SkinVB->GetNeedsCPUAccess(), TEXT("SkinWeightVertexBuffer need to be CPU accessible!"));
		

		check(SKMLODData.GetVertexBufferMaxBoneInfluences() <= MAX_INFLUENCE);
		check(SkinVB->GetBoneInfluenceType() == DefaultBoneInfluence);
		check(SkinVB->GetVariableBonesPerVertex() == false);
		check(SkinVB->GetMaxBoneInfluences() == 4 || SkinVB->GetMaxBoneInfluences() == 8);
		
		FSkelotMeshDataEx::FLODData& SkelotLODData = this->LODs.Emplace_GetRef(GMaxRHIFeatureLevel);
		const bool bNeeds16BitBoneIndex = SkinVB->Use16BitBoneIndex() || SKMesh->GetSkeleton()->GetReferenceSkeleton().GetNum() > 255;
		SkelotLODData.BoneData.bIs16BitBoneIndex = bNeeds16BitBoneIndex;
		SkelotLODData.BoneData.MaxBoneInfluences = SkinVB->GetMaxBoneInfluences();
		SkelotLODData.BoneData.NumVertices = SkinVB->GetNumVertices();
		SkelotLODData.BoneData.ResizeBuffer();
		

		for (uint32 VertexIndex = 0; VertexIndex < SkinVB->GetNumVertices(); VertexIndex++)	//for each vertex
		{
			int SectionIndex, SectionVertexIndex;
			SKMLODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);

			const FSkelMeshRenderSection& SectionInfo = SKMLODData.RenderSections[SectionIndex];
			for (uint32 InfluenceIndex = 0; InfluenceIndex < (uint32)SectionInfo.MaxBoneInfluences; InfluenceIndex++)
			{
				uint32 BoneIndex = SkinVB->GetBoneIndex(VertexIndex, InfluenceIndex);
				uint16 BoneWeight = SkinVB->GetBoneWeight(VertexIndex, InfluenceIndex);
				FBoneIndexType MeshBoneIndex = SectionInfo.BoneMap[BoneIndex];
				int SkeletonBoneIndex = AnimSet->Skeleton->GetSkeletonBoneIndexFromMeshBoneIndex(SKMesh, MeshBoneIndex);
				check(SkeletonBoneIndex != INDEX_NONE);
				int RenderBoneIndex = AnimSet->SkeletonBoneToRenderBone[SkeletonBoneIndex];
				check(RenderBoneIndex != INDEX_NONE);

				SkelotLODData.BoneData.SetBoneIndex(VertexIndex, InfluenceIndex, RenderBoneIndex);
			}
		}
	}
}

void FSkelotMeshDataEx::InitResources(FRHICommandListBase& RHICmdList)
{
	check(IsInRenderingThread());
	for(int32 LODIndex = 0; LODIndex < LODs.Num(); LODIndex++)
	{
		LODs[LODIndex].InitResources(RHICmdList);
	}
}

void FSkelotMeshDataEx::ReleaseResources()
{
	check(IsInRenderingThread());
	for (int32 LODIndex = 0; LODIndex < LODs.Num(); LODIndex++)
	{
		LODs[LODIndex].ReleaseResources();
	}
}

void FSkelotMeshDataEx::Serialize(FArchive& Ar)
{
	int NumLOD = this->LODs.Num();
	Ar << NumLOD;

	if (Ar.IsSaving())
	{
		for (int i = 0; i < NumLOD; i++)
			this->LODs[i].Serialize(Ar);
	}
	else
	{
		this->LODs.Reserve(NumLOD);
		for (int i = 0; i < NumLOD; i++)
		{
			this->LODs.Emplace(GMaxRHIFeatureLevel);
			this->LODs.Last().Serialize(Ar);
		}
	}
}

void FSkelotMeshDataEx::InitMeshData(const FSkeletalMeshRenderData* SKRenderData, int InBaseLOD)
{
	check(this->LODs.Num() <= SKRenderData->LODRenderData.Num());
	
	for (int LODIndex = 0; LODIndex < this->LODs.Num(); LODIndex++)
	{
		this->LODs[LODIndex].SkelLODData = &SKRenderData->LODRenderData[LODIndex + InBaseLOD];
	}
}

uint32 FSkelotMeshDataEx::GetTotalBufferSize() const
{
	uint32 Size = 0;
	for (const FLODData& L : LODs)
		Size += L.BoneData.GetBufferSizeInBytes();

	return Size;
}

FSkelotMeshDataEx::FLODData::FLODData(ERHIFeatureLevel::Type InFeatureLevel) 
{
	VertexFactories[0] = TUniquePtr<FSkelotVertexFactory>(new TSkelotVertexFactory<2>(InFeatureLevel));
	VertexFactories[1] = TUniquePtr<FSkelotVertexFactory>(new TSkelotVertexFactory<4>(InFeatureLevel));
#if SKELOT_WITH_EXTRA_BONE
	VertexFactories[2] = TUniquePtr<FSkelotVertexFactory>(new TSkelotVertexFactory<6>(InFeatureLevel));
	VertexFactories[3] = TUniquePtr<FSkelotVertexFactory>(new TSkelotVertexFactory<8>(InFeatureLevel));
#endif 
}

void FSkelotMeshDataEx::FLODData::InitResources(FRHICommandListBase& RHICmdList)
{
	check(IsInRenderingThread());
	BoneData.InitResource(RHICmdList);

	for(TUniquePtr<FSkelotVertexFactory>& VF : VertexFactories)
	{
		VF->FillData(RHICmdList, &BoneData, SkelLODData);
		VF->InitResource(RHICmdList);
	}

	//lets create uniform buffer for MVF if needed
	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform);
	const bool bUseManualVertexFetch = RHISupportsManualVertexFetch(GMaxRHIShaderPlatform);
	if (SKELOT_WITH_MANUAL_VERTEX_FETCH && bUseManualVertexFetch && bCanUseGPUScene)
	{
		FSkelotVertexFactory* FirstVF = VertexFactories[0].Get();
		const FSkinWeightDataVertexBuffer* LODSkinData = this->SkelLODData->GetSkinWeightVertexBuffer()->GetDataVertexBuffer();
		check(LODSkinData->GetMaxBoneInfluences() == this->BoneData.MaxBoneInfluences);
		check(LODSkinData->VertexBufferRHI);

		FSkelotMeshVertexFetchParams VertexFetchParams;
		VertexFetchParams.VF_PositionBuffer = FirstVF->Data.PositionComponentSRV;
		check(VertexFetchParams.VF_PositionBuffer);
		VertexFetchParams.VF_PackedTangentsBuffer = FirstVF->Data.TangentsSRV;
		check(VertexFetchParams.VF_PackedTangentsBuffer);
		VertexFetchParams.VF_TexCoordBuffer = FirstVF->Data.TextureCoordinatesSRV;
		check(VertexFetchParams.VF_TexCoordBuffer);
		VertexFetchParams.VF_ColorComponentsBuffer = FirstVF->Data.ColorComponentsSRV;
		check(VertexFetchParams.VF_ColorComponentsBuffer);

		VertexFetchParams.VF_BoneIndices = this->BoneData.ShaderResourceViewRHI;
		//VertexFetchParams.VF_BoneWeights = LODSkinData->GetSRV();	//SRV is R32_UINT
		checkf(LODSkinData->Use16BitBoneIndex() == LODSkinData->Use16BitBoneWeight(), TEXT("BoneIndex and BoneWeight must be is limited to be same size right now."));
		auto Desc = FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed)
			.SetFormat(LODSkinData->Use16BitBoneWeight() ? PF_R16G16B16A16_UNORM : PF_R8G8B8A8);

		auto SRV = RHICmdList.CreateShaderResourceView(LODSkinData->VertexBufferRHI, Desc);
		VertexFetchParams.VF_BoneWeights = SRV;

		//const int32 EffectiveBaseVertexIndex = RHISupportsAbsoluteVertexID(GMaxRHIShaderPlatform) ? 0 : BaseVertexIndex;
		VertexFetchParams.ColorIndexMask = FirstVF->Data.ColorIndexMask;
		VertexFetchParams.NumTexCoords = FirstVF->Data.NumTexCoords;
		VertexFetchParams.NumBoneInfluence = LODSkinData->GetMaxBoneInfluences();
		check(VertexFetchParams.NumBoneInfluence == 4 || VertexFetchParams.NumBoneInfluence == 8);

		VertexFetchUB = FSkelotMeshVertexFetchParamsRef::CreateUniformBufferImmediate(VertexFetchParams, EUniformBufferUsage::UniformBuffer_MultiFrame);
	}

	//all permutations of VB use same vertex fetch
	for (TUniquePtr<FSkelotVertexFactory>& VF : VertexFactories)
	{
		VF->VertexFetchUB = VertexFetchUB.GetReference();
	}
}

void FSkelotMeshDataEx::FLODData::ReleaseResources()
{
	check(IsInRenderingThread());
	
	for (TUniquePtr<FSkelotVertexFactory>& VF : VertexFactories)
	{
		VF->ReleaseResource();
		VF = nullptr;
	}

	BoneData.ReleaseResource();
}

FSkelotVertexFactory* FSkelotMeshDataEx::FLODData::GetVertexFactory(int32 MBI)
{
	switch (MBI)
	{
	case 1:
	case 2: return VertexFactories[0].Get();
	case 3:
	case 4: return VertexFactories[1].Get();
#if SKELOT_WITH_EXTRA_BONE
	case 5:
	case 6: return VertexFactories[2].Get();
	case 7:
	case 8: return VertexFactories[3].Get();
#endif
	}
	return nullptr;
}


void FSkelotMeshDataEx::FLODData::Serialize(FArchive& Ar)
{
	BoneData.Serialize(Ar);
}






void FSkelotBoneIndexVertexBuffer::Serialize(FArchive& Ar)
{
	Ar << bIs16BitBoneIndex << MaxBoneInfluences << NumVertices;
	if (Ar.IsLoading())
	{
		ResizeBuffer();
	}
	
	BoneData->Serialize(Ar);
}

void FSkelotBoneIndexVertexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	check(BoneData);
	uint32 size = BoneData->Num();
	FRHIResourceCreateInfo info(TEXT("FSkelotBoneIndexVertexBuffer"), BoneData->GetResourceArray());
	VertexBufferRHI = RHICmdList.CreateVertexBuffer(size, BUF_Static | BUF_ShaderResource, info);
	ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(VertexBufferRHI, bIs16BitBoneIndex ? 8 : 4, bIs16BitBoneIndex ? PF_R16G16B16A16_UINT : PF_R8G8B8A8_UINT);
	delete BoneData;
	BoneData = nullptr;
}

void FSkelotBoneIndexVertexBuffer::ReleaseRHI()
{
	VertexBufferRHI.SafeRelease();
}

void FSkelotCurveBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	const uint32 Stride = 2;
	FRHIResourceCreateInfo info(TEXT("FSkelotCurveBuffer"), &Values);
	ERHIAccess AM = ERHIAccess::Unknown;// ERHIAccess::SRVGraphics | ERHIAccess::UAVCompute | ERHIAccess::CopyDest;
	Buffer = RHICmdList.CreateVertexBuffer(Values.Num() * Stride, BUF_UnorderedAccess | BUF_ShaderResource, AM, info);
	//const EPixelFormat LUT[] = {PF_Unknown, PF_R16F,  PF_G16R16F, PF_Unknown, PF_FloatRGBA};
	const EPixelFormat PXFormat = PF_R16F;
	ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(Buffer, Stride, PXFormat);
	UAV = RHICmdList.CreateUnorderedAccessView(Buffer, PXFormat);

	Values.Empty();
}

void FSkelotCurveBuffer::ReleaseRHI()
{
	ShaderResourceViewRHI.SafeRelease();
	Buffer.SafeRelease();
}



template<int MBI> bool TSkelotVertexFactory<MBI>::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return Parameters.MaterialParameters.MaterialDomain == MD_Surface && (Parameters.MaterialParameters.bIsUsedWithSkeletalMesh || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

template<int MBI> void TSkelotVertexFactory<MBI>::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);

	const FStaticFeatureLevel MaxSupportedFeatureLevel = GetMaxSupportedFeatureLevel(Parameters.Platform);
	const bool bUseGPUScene = UseGPUScene(Parameters.Platform, MaxSupportedFeatureLevel) && (MaxSupportedFeatureLevel > ERHIFeatureLevel::ES3_1);
	const bool bSupportsPrimitiveIdStream = Parameters.VertexFactoryType->SupportsPrimitiveIdStream();
	checkf(bUseGPUScene && bSupportsPrimitiveIdStream, TEXT("Only supported by GPUScene!"));

	check(MBI > 0 && MBI <= 8);
	OutEnvironment.SetDefine(TEXT("MAX_BONE_INFLUENCE"), MBI);
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	

	//#Note material overrides them!! wanned to fetch per instance data through HLSL without using PerIsntanceCustomData Material Node. Parameters.MaterialParameters.bHasPerInstanceCustomData must be set !
	OutEnvironment.SetDefine(TEXT("VF_REQUIRES_PER_INSTANCE_CUSTOM_DATA"), 1);
	OutEnvironment.SetDefine(TEXT("USES_PER_INSTANCE_CUSTOM_DATA"), 1);

	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_SPEEDTREE_WIND"), 0);
	OutEnvironment.SetDefine(TEXT("USE_DITHERED_LOD_TRANSITION_FOR_INSTANCED"), 0);
	OutEnvironment.SetDefine(TEXT("VF_SKELOT"), 0);
	OutEnvironment.SetDefine(TEXT("VF_SKELOT2"), 1); 
	OutEnvironment.SetDefine(TEXT("USE_DITHERED_LOD_TRANSITION"), 0);
	
	bool bSupportMVF = RHISupportsManualVertexFetch(Parameters.Platform);
	OutEnvironment.SetDefineIfUnset(TEXT("MANUAL_VERTEX_FETCH"), SKELOT_WITH_MANUAL_VERTEX_FETCH && bSupportMVF);
}

template<int MBI> void TSkelotVertexFactory<MBI>::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{

}

void FSkelotVertexFactory::FillData(FRHICommandListBase& RHICmdList, const FSkelotBoneIndexVertexBuffer* BoneIndexBuffer, const FSkeletalMeshLODRenderData* LODData)
{
	const FStaticMeshVertexBuffers& smvb = LODData->StaticVertexBuffers;

	smvb.PositionVertexBuffer.BindPositionVertexBuffer(this, Data);
	smvb.StaticMeshVertexBuffer.BindTangentVertexBuffer(this, Data);
	smvb.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(this, Data);
	smvb.ColorVertexBuffer.BindColorVertexBuffer(this, Data);

	//see InitGPUSkinVertexFactoryComponents we can take weights from the mesh since we just have different bone indices

	const FSkinWeightDataVertexBuffer* LODSkinData = LODData->GetSkinWeightVertexBuffer()->GetDataVertexBuffer();
	check(LODSkinData->GetMaxBoneInfluences() == BoneIndexBuffer->MaxBoneInfluences);
	const bool bExtraData = Data.MaxBoneInfluence > 4; //LODSkinData->GetMaxBoneInfluences() > 4;

	//bind bone weights
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
	//bind bone indices
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

void FSkelotVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	FDataType& data = Data;

	FVertexDeclarationElementList OutElements;
	//position
	OutElements.Add(AccessStreamComponent(data.PositionComponent, 0));
	// tangent basis vector decls
	OutElements.Add(AccessStreamComponent(data.TangentBasisComponents[0], 1));
	OutElements.Add(AccessStreamComponent(data.TangentBasisComponents[1], 2));

	// Texture coordinates
	if (data.TextureCoordinates.Num())
	{
		const uint8 BaseTexCoordAttribute = 5;
		for (int32 CoordinateIndex = 0; CoordinateIndex < data.TextureCoordinates.Num(); ++CoordinateIndex)
		{
			OutElements.Add(AccessStreamComponent(
				data.TextureCoordinates[CoordinateIndex],
				BaseTexCoordAttribute + CoordinateIndex
			));
		}

		for (int32 CoordinateIndex = data.TextureCoordinates.Num(); CoordinateIndex < MAX_TEXCOORDS; ++CoordinateIndex)
		{
			OutElements.Add(AccessStreamComponent(
				data.TextureCoordinates[data.TextureCoordinates.Num() - 1],
				BaseTexCoordAttribute + CoordinateIndex
			));
		}
	}

	OutElements.Add(AccessStreamComponent(data.ColorComponent, 13));

	// bone indices decls
	OutElements.Add(AccessStreamComponent(data.BoneIndices, 3));
	// bone weights decls
	OutElements.Add(AccessStreamComponent(data.BoneWeights, 4));

	if (Data.MaxBoneInfluence > 4)
	{
		OutElements.Add(AccessStreamComponent(data.ExtraBoneIndices, 14));
		OutElements.Add(AccessStreamComponent(data.ExtraBoneWeights, 15));
	}

	this->AddPrimitiveIdStreamElement(EVertexInputStreamType::Default, OutElements, 16, 16);

	InitDeclaration(OutElements);
	check(GetDeclaration());
}


//////////////////////////////////////////////////////////////////////////
class FSkelotShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FSkelotShaderParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
	}
	void GetElementShaderBindings(const class FSceneInterface* Scene, const FSceneView* View, const FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType, ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory, const FMeshBatchElement& BatchElement, class FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const
	{
		const FSkelotVertexFactory* VF = static_cast<const FSkelotVertexFactory*>(VertexFactory);
		const FRHIUniformBuffer* ACUniformBuffer = reinterpret_cast<const FRHIUniformBuffer*>(BatchElement.UserData);

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FSkelotAnimCollectionUniformParams>(), ACUniformBuffer);
		
		if(VF->VertexFetchUB)
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FSkelotMeshVertexFetchParams>(), VF->VertexFetchUB);
	}

};


IMPLEMENT_TYPE_LAYOUT(FSkelotShaderParameters);

constexpr EVertexFactoryFlags VFFlags = EVertexFactoryFlags::UsedWithMaterials | EVertexFactoryFlags::SupportsDynamicLighting | EVertexFactoryFlags::SupportsPrecisePrevWorldPos 
	| EVertexFactoryFlags::SupportsPrimitiveIdStream /* | EVertexFactoryFlags::SupportsCachingMeshDrawCommands */
	| (SKELOT_WITH_MANUAL_VERTEX_FETCH ? EVertexFactoryFlags::SupportsManualVertexFetch : EVertexFactoryFlags::None);


#define IMPL_NEWVF(MBI) \
	using SkelotVF##MBI = TSkelotVertexFactory<MBI>;\
	IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE(template<>, SkelotVF##MBI, "/Plugin/Skelot/Private/SkelotVertexFactory.ush", VFFlags);\
	IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(SkelotVF##MBI, SF_Vertex, FSkelotShaderParameters);\
	IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(SkelotVF##MBI, SF_Pixel, FSkelotShaderParameters);\


IMPL_NEWVF(2);
IMPL_NEWVF(4);
#if SKELOT_WITH_EXTRA_BONE
IMPL_NEWVF(6);
IMPL_NEWVF(8);
#endif
