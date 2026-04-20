// GPUSKI Plugin - World subsystem implementation

#include "GPUSKISubsystem.h"
#include "GPUSKIWorld.h"
#include "GPUSKIAnimCollection.h"

#include "Engine/SkeletalMesh.h"

void UGPUSKISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UGPUSKISubsystem::Deinitialize()
{
	CachedWorld = nullptr;
	Super::Deinitialize();
}

AGPUSKIWorld* UGPUSKISubsystem::GetOrCreateWorld()
{
	if (!CachedWorld || !IsValid(CachedWorld))
	{
		CachedWorld = AGPUSKIWorld::GetInstance(GetWorld());
	}
	return CachedWorld;
}

//----------------------------------------------------------------------
// Instance Management
//----------------------------------------------------------------------
FGPUSKIInstanceHandle UGPUSKISubsystem::CreateInstance(const FTransform& Transform, UGPUSKIAnimCollection* AnimCollection, USkeletalMesh* Mesh)
{
	AGPUSKIWorld* World = GetOrCreateWorld();
	if (!World || !AnimCollection || !Mesh)
		return FGPUSKIInstanceHandle();

	// Build anim collection if not already built
	if (!AnimCollection->bIsBuilt)
	{
		AnimCollection->BuildData();
	}

	// Construct render descriptor
	FGPUSKIInstanceRenderDesc Desc;
	Desc.AnimCollection = AnimCollection;

	FGPUSKIMeshRenderDesc MeshDesc;
	MeshDesc.Mesh = Mesh;
	Desc.Meshes.Add(MeshDesc);

	return World->CreateInstance(Transform, Desc);
}

void UGPUSKISubsystem::DestroyInstance(FGPUSKIInstanceHandle& Handle)
{
	AGPUSKIWorld* World = GetOrCreateWorld();
	if (World)
	{
		World->DestroyInstance(Handle);
	}
}

bool UGPUSKISubsystem::IsHandleValid(const FGPUSKIInstanceHandle& Handle) const
{
	if (!CachedWorld || !IsValid(CachedWorld))
		return false;

	return CachedWorld->IsHandleValid(Handle);
}

//----------------------------------------------------------------------
// Animation
//----------------------------------------------------------------------
void UGPUSKISubsystem::PlayAnimation(const FGPUSKIInstanceHandle& Handle, UAnimSequenceBase* Animation, float PlayRate, bool bLoop, float StartAt, float BlendInTime)
{
	AGPUSKIWorld* World = GetOrCreateWorld();
	if (!World)
		return;

	FGPUSKIAnimPlayParams Params;
	Params.Animation = Animation;
	Params.PlayScale = PlayRate;
	Params.bLoop = bLoop;
	Params.StartAt = StartAt;
	Params.BlendInTime = BlendInTime;

	World->PlayAnimation(Handle, Params);
}

//----------------------------------------------------------------------
// Transform
//----------------------------------------------------------------------
void UGPUSKISubsystem::SetInstanceTransform(const FGPUSKIInstanceHandle& Handle, const FTransform& Transform)
{
	AGPUSKIWorld* World = GetOrCreateWorld();
	if (World)
	{
		World->SetInstanceTransform(Handle, Transform);
	}
}

FTransform UGPUSKISubsystem::GetInstanceTransform(const FGPUSKIInstanceHandle& Handle) const
{
	if (!CachedWorld || !IsValid(CachedWorld))
		return FTransform::Identity;

	return CachedWorld->GetInstanceTransform(Handle);
}
