// GPUSKI Plugin - Test actor implementation

#include "GPUSKITestActor.h"
#include "GPUSKISubsystem.h"
#include "GPUSKIAnimCollection.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

AGPUSKITestActor::AGPUSKITestActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // enabled only in pool mode

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

	const bool bPoolMode = bEnableTransition && AnimationPool.Num() >= 2;

	// A/B shared mode — all instances switch together on a single timer
	if (bEnableTransition && !bPoolMode && AnimationB && GPUHandles.Num() > 0)
	{
		bTransitionOnB = false;
		GetWorldTimerManager().SetTimer(
			TransitionTimerHandle,
			this, &AGPUSKITestActor::OnTransitionTimer,
			TransitionInterval,
			/*bLoop=*/true
		);
	}

	// Pool mode — each instance transitions independently via Tick
	if (bPoolMode && GPUHandles.Num() > 0)
	{
		SetActorTickEnabled(true);
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
	GetWorldTimerManager().ClearTimer(TransitionTimerHandle);

	DestroyAllInstances();
	SpawnGPUInstances();

	if (bSpawnCPUComparison)
	{
		SpawnCPUInstances();
	}

	const bool bPoolMode = bEnableTransition && AnimationPool.Num() >= 2;

	if (bEnableTransition && !bPoolMode && AnimationB && GPUHandles.Num() > 0)
	{
		bTransitionOnB = false;
		GetWorldTimerManager().SetTimer(
			TransitionTimerHandle,
			this, &AGPUSKITestActor::OnTransitionTimer,
			TransitionInterval,
			/*bLoop=*/true
		);
	}

	if (bPoolMode && GPUHandles.Num() > 0)
	{
		SetActorTickEnabled(true);
	}
}
#endif

//----------------------------------------------------------------------
// GPU Instances (GPUSKI)
//----------------------------------------------------------------------
void AGPUSKITestActor::SpawnGPUInstances()
{
	const bool bPoolMode = bEnableTransition && AnimationPool.Num() >= 2;

	if (!AnimCollection || !Mesh || (!Animation && !bPoolMode))
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
	const float Now = GetWorld()->GetTimeSeconds();

	GPUHandles.Reserve(Rows * Columns);
	if (bPoolMode)
	{
		InstanceCurrentAnimIndices.Reserve(Rows * Columns);
		InstanceNextTransitionTimes.Reserve(Rows * Columns);
	}

	for (int32 Row = 0; Row < Rows; Row++)
	{
		for (int32 Col = 0; Col < Columns; Col++)
		{
			FVector Location = Origin + FVector(Col * Spacing, Row * Spacing, 0.0f);

			FTransform Transform;
			Transform.SetLocation(Location);

			FGPUSKIInstanceHandle Handle = Subsystem->CreateInstance(Transform, AnimCollection, Mesh);
			if (!Handle.IsValid())
				continue;

			if (bPoolMode)
			{
				// Each instance starts on a randomly chosen animation from the pool
				const int32 StartIdx = FMath::RandRange(0, AnimationPool.Num() - 1);
				UAnimSequenceBase* StartAnim = AnimationPool[StartIdx].Get();
				if (StartAnim)
				{
					float StartAt = bRandomizeStartTime ? FMath::FRandRange(0.0f, StartAnim->GetPlayLength()) : 0.0f;
					Subsystem->PlayAnimation(Handle, StartAnim, 1.0f, true, StartAt);
				}
				InstanceCurrentAnimIndices.Add(StartIdx);
				// Stagger first transitions so they don't all fire at once
				InstanceNextTransitionTimes.Add(Now + FMath::FRandRange(TransitionIntervalMin, TransitionIntervalMax));
			}
			else
			{
				float StartAt = bRandomizeStartTime ? FMath::FRandRange(0.0f, Animation->GetPlayLength()) : 0.0f;
				Subsystem->PlayAnimation(Handle, Animation, 1.0f, true, StartAt);
			}

			GPUHandles.Add(Handle);
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
// Transition Timer
//----------------------------------------------------------------------
void AGPUSKITestActor::OnTransitionTimer()
{
	UGPUSKISubsystem* Subsystem = GetWorld()->GetSubsystem<UGPUSKISubsystem>();
	if (!Subsystem)
		return;

	// Alternate between Animation and AnimationB
	bTransitionOnB = !bTransitionOnB;
	UAnimSequenceBase* NextAnim = bTransitionOnB ? AnimationB.Get() : Animation.Get();
	if (!NextAnim)
		return;

	for (FGPUSKIInstanceHandle& Handle : GPUHandles)
	{
		Subsystem->PlayAnimation(Handle, NextAnim, 1.0f, true, 0.0f, TransitionBlendTime);
	}
}

//----------------------------------------------------------------------
// Pool mode: per-instance independent transitions
//----------------------------------------------------------------------
void AGPUSKITestActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UGPUSKISubsystem* Subsystem = GetWorld()->GetSubsystem<UGPUSKISubsystem>();
	if (!Subsystem)
		return;

	const float Now = GetWorld()->GetTimeSeconds();

	for (int32 i = 0; i < GPUHandles.Num(); i++)
	{
		if (Now < InstanceNextTransitionTimes[i])
			continue;

		// Pick a different animation from the pool than the one currently playing
		int32 NextIdx = InstanceCurrentAnimIndices[i];
		do { NextIdx = FMath::RandRange(0, AnimationPool.Num() - 1); }
		while (NextIdx == InstanceCurrentAnimIndices[i] && AnimationPool.Num() > 1);

		UAnimSequenceBase* NextAnim = AnimationPool[NextIdx].Get();
		if (NextAnim)
		{
			Subsystem->PlayAnimation(GPUHandles[i], NextAnim, 1.0f, true, 0.0f, TransitionBlendTime);
			InstanceCurrentAnimIndices[i] = NextIdx;
		}

		InstanceNextTransitionTimes[i] = Now + FMath::FRandRange(TransitionIntervalMin, TransitionIntervalMax);
	}
}

//----------------------------------------------------------------------
// Cleanup
//----------------------------------------------------------------------
void AGPUSKITestActor::DestroyAllInstances()
{
	GetWorldTimerManager().ClearTimer(TransitionTimerHandle);
	bTransitionOnB = false;

	SetActorTickEnabled(false);
	InstanceNextTransitionTimes.Empty();
	InstanceCurrentAnimIndices.Empty();

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
