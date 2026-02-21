// GPUSKI Plugin - Cluster component for rendering batches of skeletal mesh instances

#pragma once

#include "GPUSKIWorld.h"

#include "GPUSKIClusterComponent.generated.h"

UCLASS(ClassGroup = Rendering, hidecategories = (Object, Activation, Physics, Collision))
class GPUSKI_API UGPUSKIClusterComponent : public UGPUSKIBaseComponent
{
	GENERATED_BODY()

public:
	// Index into AGPUSKIWorld::RenderDescs
	int32 DescIdx = -1;
	// Index into FGPUSKIInstanceRenderDescFinal::Clusters
	int32 ClusterIdx = -1;

	UGPUSKIClusterComponent();

	AGPUSKIWorld* GetGPUSKIWorld() const { return GetOwner<AGPUSKIWorld>(); }

	FGPUSKIInstanceRenderDesc& GetRenderDesc() const;
	FGPUSKICluster& GetClusterStruct() const;

	FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	void SendRenderDynamicData_Concurrent() override;
	void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	void BeginDestroy() override;

	int32 GetNumMaterials() const override;
	UMaterialInterface* GetMaterial(int32 MaterialIndex) const override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	// Builds dynamic data for the scene proxy
	void BuildDynamicData(FGPUSKICompDynData& OutData) const;
};
