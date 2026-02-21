// GPUSKI Plugin - Test actor implementation

#include "GPUSKITestActor.h"
#include "GPUSKISubsystem.h"
#include "GPUSKIAnimCollection.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

AGPUSKITestActor::AGPUSKITestActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AGPUSKITestActor::BeginPlay()
{
	Super::BeginPlay();

	SpawnGPUInstances();

	if (bSpawnCPUComparison)
	{
		SpawnCPUInstances();
	}
}

void AGPUSKITestActor::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	DestroyAllInstances();
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void AGPUSKITestActor::RespawnInstances()
{
	DestroyAllInstances();
	SpawnGPUInstances();

	if (bSpawnCPUComparison)
	{
		SpawnCPUInstances();
	}
}
#endif

//----------------------------------------------------------------------
// GPU Instances (GPUSKI)
//----------------------------------------------------------------------
void AGPUSKITestActor::SpawnGPUInstances()
{
	if (!AnimCollection || !Mesh || !Animation)
	{
		UE_LOG(LogGPUSKI, Warning, TEXT("GPUSKITestActor: AnimCollection, Mesh, and Animation must all be set!"));
		return;
	}

	UGPUSKISubsystem* Subsystem = GetWorld()->GetSubsystem<UGPUSKISubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogGPUSKI, Error, TEXT("GPUSKITestActor: Failed to get GPUSKISubsystem!"));
		return;
	}

	const FVector Origin = GetActorLocation();
	const float AnimLength = Animation->GetPlayLength();

	GPUHandles.Reserve(Rows * Columns);

	for (int32 Row = 0; Row < Rows; Row++)
	{
		for (int32 Col = 0; Col < Columns; Col++)
		{
			FVector Location = Origin + FVector(Col * Spacing, Row * Spacing, 0.0f);

			FTransform Transform;
			Transform.SetLocation(Location);

			FGPUSKIInstanceHandle Handle = Subsystem->CreateInstance(Transform, AnimCollection, Mesh);

			if (Handle.IsValid())
			{
				float StartAt = bRandomizeStartTime ? FMath::FRandRange(0.0f, AnimLength) : 0.0f;
				Subsystem->PlayAnimation(Handle, Animation, 1.0f, true, StartAt);
				GPUHandles.Add(Handle);
			}
		}
	}

	GPUInstanceCount = GPUHandles.Num();
	UE_LOG(LogGPUSKI, Log, TEXT("GPUSKITestActor: Spawned %d GPU instances"), GPUInstanceCount);
}

//----------------------------------------------------------------------
// CPU Instances (conventional SkeletalMeshComponent)
//----------------------------------------------------------------------
void AGPUSKITestActor::SpawnCPUInstances()
{
	if (!Mesh || !Animation)
		return;

	const FVector Origin = GetActorLocation();
	// Offset CPU group to the side (negative Y) so both groups are visible side by side
	const float CPUOffsetY = -(Rows * Spacing + Spacing * 2.0f);
	const float AnimLength = Animation->GetPlayLength();

	CPUActors.Reserve(Rows * Columns);

	for (int32 Row = 0; Row < Rows; Row++)
	{
		for (int32 Col = 0; Col < Columns; Col++)
		{
			FVector Location = Origin + FVector(Col * Spacing, Row * Spacing + CPUOffsetY, 0.0f);

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AActor* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
			if (!Actor)
				continue;

			USceneComponent* SceneRoot = NewObject<USceneComponent>(Actor, TEXT("Root"));
			Actor->SetRootComponent(SceneRoot);
			SceneRoot->RegisterComponent();
			Actor->SetActorLocation(Location);

			USkeletalMeshComponent* SKComp = NewObject<USkeletalMeshComponent>(Actor, TEXT("SKMesh"));
			SKComp->SetupAttachment(SceneRoot);
			SKComp->SetSkeletalMesh(Mesh);

			if (MaterialOverride)
			{
				for (int32 i = 0; i < Mesh->GetNumMaterials(); i++)
				{
					SKComp->SetMaterial(i, MaterialOverride);
				}
			}

			SKComp->RegisterComponent();

			// Play animation
			SKComp->PlayAnimation(Animation, true);

			if (bRandomizeStartTime && AnimLength > 0.0f)
			{
				SKComp->SetPosition(FMath::FRandRange(0.0f, AnimLength));
			}

			CPUActors.Add(Actor);
		}
	}

	CPUInstanceCount = CPUActors.Num();
	UE_LOG(LogGPUSKI, Log, TEXT("GPUSKITestActor: Spawned %d CPU comparison instances (offset Y = %.0f)"), CPUInstanceCount, CPUOffsetY);
}

//----------------------------------------------------------------------
// Cleanup
//----------------------------------------------------------------------
void AGPUSKITestActor::DestroyAllInstances()
{
	// Destroy GPU instances
	UGPUSKISubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UGPUSKISubsystem>() : nullptr;
	if (Subsystem)
	{
		for (FGPUSKIInstanceHandle& Handle : GPUHandles)
		{
			Subsystem->DestroyInstance(Handle);
		}
	}
	GPUHandles.Empty();
	GPUInstanceCount = 0;

	// Destroy CPU actors
	for (TObjectPtr<AActor>& Actor : CPUActors)
	{
		if (Actor && IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	CPUActors.Empty();
	CPUInstanceCount = 0;
}
