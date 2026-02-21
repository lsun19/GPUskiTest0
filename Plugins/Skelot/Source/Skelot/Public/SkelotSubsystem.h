// Copyright 2024 Lazy Marmot Games. All Rights Reserved.

#pragma once

#include "SkelotWorld.h"
#include "Subsystems/WorldSubsystem.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "SkelotSubsystem.generated.h"


class USkelotWorldSubsystem_Impl;
class UActorComponent;

UCLASS()
class SKELOT_API USkelotWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	UPROPERTY()
	ASkelotWorld* PrimaryInstance;

	USkelotWorldSubsystem_Impl* Impl() { return (USkelotWorldSubsystem_Impl*)this; }

	bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;  }

	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skelot", meta=(WorldContext="WorldContextObject", CompactNodeTitle ="Skelot Singleton", Keywords="Get Skelot World"))
	static ASkelotWorld* GetSingleton(const UObject* WorldContextObject);

	static ASkelotWorld* Internal_GetSingleton(const UWorld* World, bool bCreateIfNotFound);
	static ASkelotWorld* Internal_GetSingleton(const UObject* WorldContextObject, bool bCreateIfNotFound);

	//helper to get singleton and also check validity of the handle
	static ASkelotWorld* GetSingleton(const UObject* WorldContextObject, FSkelotInstanceHandle Handle);
	static ASkelotWorld* GetSingleton(const UObject* WorldContextObject, FSkelotInstanceHandle Handle0, FSkelotInstanceHandle Handle1);

	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Instance", meta=(WorldContext="WorldContextObject"))
	static void Skelot_DestroyInstance(const UObject* WorldContextObject, FSkelotInstanceHandle Handle);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Instance", meta=(WorldContext="WorldContextObject"))
	static void Skelot_DestroyInstances(const UObject* WorldContextObject, const TArray<FSkelotInstanceHandle>& Handles);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Instance", meta=(WorldContext="WorldContextObject", AutoCreateRefTerm = "Transform"))
	static FSkelotInstanceHandle Skelot_CreateInstance(const UObject* WorldContextObject, const FTransform& Transform, USkelotRenderParams* RenderParams, UObject* UserObject);

	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Animation", meta=(WorldContext="WorldContextObject"))
	static float Skelot_PlayAnimation(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, const FSkelotAnimPlayParams& Params);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintPure, Category="Skelot|Animation", meta=(WorldContext="WorldContextObject"))
	static USkelotAnimCollection* Skelot_GetAnimCollection(const UObject* WorldContextObject, FSkelotInstanceHandle Handle);


	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Render", meta=(WorldContext="WorldContextObject", DisplayName = "Skelot Set Render Params (Asset)"))
	static void Skelot_SetRenderParam(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, USkelotRenderParams* RenderParams);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Render", meta=(WorldContext="WorldContextObject", DisplayName="Skelot Set Render Params (Struct)"))
	static void Skelot_SetRenderDesc(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, const FSkelotInstanceRenderDesc& RenderParams);

	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Render", meta=(WorldContext="WorldContextObject"))
	static void Skelot_AttachMesh(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, const USkeletalMesh* Mesh, bool bAttach = true);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Render", meta=(WorldContext="WorldContextObject"))
	static void Skelot_AttachMeshes(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, const TArray<USkeletalMesh*>& Meshes, bool bAttach = true);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Render", meta=(WorldContext="WorldContextObject", AutoCreateRefTerm = "Value"))
	static void Skelot_SetCustomDataFloat4(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, int32 Offset, const FVector4f& Value);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skelot|Render", meta=(WorldContext="WorldContextObject"))
	static float Skelot_GetCustomDataFloat(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, int32 FloatIndex = 0);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Render", meta=(WorldContext="WorldContextObject"))
	static void Skelot_SetMaterial(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, const USkeletalMesh* Mesh, int32 MaterialIndex, FName OrMaterialSlotName, const UMaterialInterface* Material);




	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Utils", meta=(WorldContext="WorldContextObject", AutoCreateRefTerm = "Center"))
	static void SkelotQueryLocationOverlappingSphere(const UObject* WorldContextObject, const FVector& Center, float Radius, TArray<FSkelotInstanceHandle>& Instances);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Utils", meta=(WorldContext="WorldContextObject"))
	static void Skelot_RemoveInvalidHandles(const UObject* WorldContextObject, bool bMaintainOrder, TArray<FSkelotInstanceHandle>& Handles);
	//////////////////////////////////////////////////////////////////////////
	//returns all the valid instance handles
	UFUNCTION(BlueprintCallable, Category="Skelot|Utils", meta=(WorldContext="WorldContextObject"))
	static void Skelot_GetAllHandles(const UObject* WorldContextObject, UPARAM(ref) TArray<FSkelotInstanceHandle>& Handles);
	


	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Hierarchy", meta=(WorldContext="WorldContextObject", AutoCreateRefTerm = "ReletiveTransform"))
	static void SkelotAttachChild(const UObject* WorldContextObject, FSkelotInstanceHandle Parent, FSkelotInstanceHandle Child, FName SocketOrBoneName, const FTransform& ReletiveTransform);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Hierarchy", meta=(WorldContext="WorldContextObject"))
	static void SkelotDetachFromParent(const UObject* WorldContextObject, FSkelotInstanceHandle Handle);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Instance", meta=(WorldContext="WorldContextObject", AutoCreateRefTerm = "ReletiveTransform"))
	static FSkelotInstanceHandle Skelot_CreateInstanceAttached(const UObject* WorldContextObject, USkelotRenderParams* RenderParams, FSkelotInstanceHandle Parent, FName SocketOrBoneName, const FTransform& ReletiveTransform);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Hierarchy", meta=(WorldContext="WorldContextObject"))
	static void SkelotGetChildren(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, TArray<FSkelotInstanceHandle>& Children);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skelot|Hierarchy", meta=(WorldContext="WorldContextObject"))
	static FSkelotInstanceHandle SkelotGetParent(const UObject* WorldContextObject, FSkelotInstanceHandle Handle);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skelot|Hierarchy", meta=(WorldContext="WorldContextObject"))
	static bool SkelotIsChildOf(const UObject* WorldContextObject, FSkelotInstanceHandle Child, FSkelotInstanceHandle Parent);


	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skelot|Transform", meta=(WorldContext="WorldContextObject"))
	static const FTransform& SkelotGetTransform(const UObject* WorldContextObject, FSkelotInstanceHandle Handle);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Transform", meta=(WorldContext="WorldContextObject", AutoCreateRefTerm="Transform"))
	static void SkelotSetTransform(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, const FTransform& Transform, bool bRelative);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintPure, Category="Skelot|Transform", meta=(WorldContext="WorldContextObject"))
	static FTransform SkelotGetSocketTransform(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, FName SocketOrBoneName, USkeletalMesh* InMesh = nullptr, bool bWorldSpace = true);


	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Fragments", meta=(WorldContext="WorldContextObject", ExpandEnumAsExecs = "ExecResult", DeterminesOutputType = "Class"))
	static UObject* SkelotGetUserObject(const UObject* WorldContextObject, ESkelotValidity& ExecResult, FSkelotInstanceHandle Handle, TSubclassOf<UObject> Class);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Fragments", meta=(WorldContext="WorldContextObject"))
	static void SkelotSetUserObject(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, UObject* Object);
	

	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Utils", meta=(WorldContext="WorldContextObject"))
	static void SkelotEnableDynamicPose(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, bool bEnable);
	
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Utils", meta=(WorldContext="WorldContextObject"))
	static void SkelotBindToComponent(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, USkeletalMeshComponent* Component, int32 UserData, bool bCopyTransform);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Utils", meta=(WorldContext="WorldContextObject"))
	static void SkelotUnbindFromComponent(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, bool bKeepDynamicPoseEnabled, USkeletalMeshComponent*& OutComponent, int32& OuUserData);

	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skelot|Utils", meta=(WorldContext="WorldContextObject"))
	static USkeletalMeshComponent* SkelotGetBoundComponent(const UObject* WorldContextObject, FSkelotInstanceHandle Handle);


	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Misc", meta=(WorldContext="WorldContextObject"))
	static void Skelot_SetLifespan(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, float Lifespan);
	//////////////////////////////////////////////////////////////////////////
	//#Note only one timer can be active per every instance.
	UFUNCTION(BlueprintCallable, Category="Skelot|Misc", meta=(WorldContext="WorldContextObject"))
	static void Skelot_SetTimer(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, float Interval, bool bLoop, bool bGameTime, FSkelotGeneralDynamicDelegate Delegate, FName PayloadTag = NAME_None, UObject* PayloadObject = nullptr);
	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, Category="Skelot|Misc", meta=(WorldContext="WorldContextObject"))
	static void Skelot_ClearTimer(const UObject* WorldContextObject, FSkelotInstanceHandle Handle);

	
};


UCLASS()
class SKELOT_API USkelotFunctionLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Skelot|Utils", meta=(WorldContext="WorldContextObject", DeterminesOutputType = "Class"))
	static UActorComponent* SpawnComponent(const UObject* WorldContextObject, TSubclassOf<UActorComponent> Class, const FTransform& Transform);

	//construct skeletal mesh components from an specified Skelot Instance.
	UFUNCTION(BlueprintCallable, Category="Skelot|Misc", meta=(WorldContext="WorldContextObject"))
	static USkeletalMeshComponent* ConstructSkeletalMeshComponents(const UObject* WorldContextObject, FSkelotInstanceHandle Handle, UObject* Outer, bool bSetCustomPrimitiveDataFloat);
};
