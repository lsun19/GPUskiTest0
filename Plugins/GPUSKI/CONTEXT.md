# GPUSKI Plugin - Development Context

## Project Overview
UE5.5 plugin called **GPUSKI** that optimizes rendering of thousands of skeletal mesh instances sharing the same skeleton. Core technique: pre-bake all animation frames into a GPU buffer, then use a custom vertex factory to skin vertices entirely on the GPU, eliminating per-instance CPU skeletal evaluation. Modeled after the **Skelot** plugin in the same project at `Plugins/Skelot/`.

**Project root**: `D:\Game_Projects\Unreal\Testfield\GPUskiTest0\`
**Plugin root**: `D:\Game_Projects\Unreal\Testfield\GPUskiTest0\Plugins\GPUSKI\`
**Reference plugin**: `D:\Game_Projects\Unreal\Testfield\GPUskiTest0\Plugins\Skelot\`
**Plan file**: `C:\Users\Remon\.claude\plans\zippy-fluttering-wombat.md`

## Current Status: MVP (Phase 1) Complete - Runtime Testing

All MVP files are implemented and compiling. The plugin can be tested using `AGPUSKITestActor` which spawns a grid of GPU-skinned instances and optionally CPU comparison instances side by side.

### What Works
- Plugin compiles without errors (two warnings from unrelated SubstanceEditor plugin)
- AnimCollection data asset: bakes animation frames into GPU buffer
- Instance management via SOA (Structure of Arrays) in AGPUSKIWorld singleton
- Custom vertex factory with GPU skinning shader
- GPUScene-based instanced rendering via FInstanceSceneDataBuffers
- Blueprint API via UGPUSKISubsystem
- AGPUSKITestActor for drag-and-drop testing with CPU comparison

### What Needs Testing/Verification
- Visual correctness (scale, material, animation playback)
- The original issues (500-1000x scale, black material) had fixes applied but final verification pending
- Performance comparison GPU vs CPU instances
- Motion vectors / TAA correctness

## File Structure

```
Plugins/GPUSKI/
+-- GPUSKI.uplugin
+-- CONTEXT.md                          (this file)
+-- Shaders/
|   +-- Private/
|       +-- GPUSKIVertexFactory.ush      (GPU skinning vertex factory shader)
+-- Source/
    +-- GPUSKI/
        +-- GPUSKI.Build.cs
        +-- Public/
        |   +-- GPUSKI.h                 (module header, log category, stats group)
        |   +-- GPUSKIBase.h             (FGPUSKIMatrix3x4Half, bounding box types)
        |   +-- GPUSKIAnimCollection.h   (UGPUSKIAnimCollection data asset)
        |   +-- GPUSKIWorldBase.h        (FGPUSKIInstanceHandle, SOA, render desc, cluster structs)
        |   +-- GPUSKIWorld.h            (AGPUSKIWorld singleton, FGPUSKICompDynData)
        |   +-- GPUSKIClusterComponent.h (UGPUSKIClusterComponent)
        |   +-- GPUSKISubsystem.h        (UGPUSKISubsystem - Blueprint API)
        |   +-- GPUSKITestActor.h        (AGPUSKITestActor - test/comparison actor)
        +-- Private/
            +-- GPUSKI.cpp               (module startup, shader dir mapping)
            +-- GPUSKIBase.cpp
            +-- GPUSKIAnimCollection.cpp (animation baking, buffer init)
            +-- GPUSKIWorld.cpp           (instance lifecycle, tick, cluster management)
            +-- GPUSKIClusterComponent.cpp (scene proxy, draw calls, instance buffer updates)
            +-- GPUSKIRenderResources.h   (vertex factory, GPU buffers, shader params)
            +-- GPUSKIRenderResources.cpp (buffer init, VF registration, shader bindings)
            +-- GPUSKISubsystem.cpp
            +-- GPUSKITestActor.cpp
```

## Key Architectural Decisions
- **GPUScene only** - no legacy instancing path (UE5.5 native)
- **Runtime module only** - no editor module yet
- **No Niagara dependency** - self-contained
- **4-bone influence default**, 8-bone via `GPUSKI_WITH_EXTRA_BONE` define
- **Singleton world actor** pattern - one AGPUSKIWorld per UWorld
- **Center-relative instance transforms** - instances offset by cluster center, PrimitiveLocalToWorld set to center position (matching Skelot's approach)

## Bugs Fixed (Chronological)

### 1. Initial compile errors (various)
Multiple compile fixes across all files during initial implementation.

### 2. Mesh rendering 500-1000x too large + black material
**Root causes identified:**
- Missing `PrimitiveUpdates.Enqueue` calls to notify GPUScene
- Missing `InstanceBuffer.Flags` for custom data
- Using `TArray<FMatrix>` instead of `TArray<FRenderTransform>` for instance transforms
- Missing `MATUSAGE_SkeletalMesh` material usage check

**Fixes applied in `GPUSKIClusterComponent.cpp`:**
- Restructured `FGPUSKICompDynData` with Center, FRenderTransform arrays
- Added `#define private public` / `#include "ScenePrivate.h"` for `FScene::PrimitiveUpdates` access
- Added 3x `PrimitiveUpdates.Enqueue` calls in `UpdateInstanceBuffer` (transform, prev transform, instance)
- Added `CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh)` in proxy constructor
- Computed local bounds from mesh bounding boxes instead of hardcoded values
- Center-relative transform computation in `BuildDynamicData`

### 3. FBoneContainer::InitializeTo deprecation warning
**File**: `GPUSKIAnimCollection.cpp:161-162`
**Fix**: Changed from `FCurveEvaluationOption()` to `UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowFiltered)`

### 4. GPUScene crash: `InstanceCustomDataCount == 0` assertion
**File**: `GPUSKIClusterComponent.cpp`
**Root cause**: `FInstanceSceneDataBuffers::Flags` not set before first upload
**Fix**: Created `FGPUSKIInstanceSceneDataBuffers` subclass (matching Skelot's `FSkelotCluserISDB` pattern) with `Init()` method that sets protected `Flags.bHasPerInstanceDynamicData = true` and `Flags.bHasPerInstanceCustomData = true`

### 5. `FInstanceSceneDataBuffers::Flags` protected access error
**File**: `GPUSKIClusterComponent.cpp:65-66`
**Root cause**: Direct access to protected member from proxy constructor
**Fix**: Subclass `FGPUSKIInstanceSceneDataBuffers : public FInstanceSceneDataBuffers` with `Init()` method

### 6. CPU comparison actors spawning at origin (0,0,0)
**File**: `GPUSKITestActor.cpp`
**Root cause**: `SpawnActor<AActor>` with location but AActor has no root component at spawn time, so location is lost
**Fix**: Added `Actor->SetActorLocation(Location)` after creating and registering root component

## Key Patterns from Skelot Reference

### Transform handling
- Skelot stores `TArray<FRenderTransform>` in CompDynData
- Instance positions are offset by cluster center
- `FRenderTransform` constructed from `FMatrix44f` (does NOT transpose)
- PrimitiveLocalToWorld set to center position via `FScaleRotationTranslationMatrix`

### GPUScene integration
- `FSkelotCluserISDB : public FInstanceSceneDataBuffers` with `Init()` for flags
- 3x `PrimitiveUpdates.Enqueue` calls (FUpdateTransformCommand, FUpdateOverridePreviousTransformData, FUpdateInstanceCommand)
- `#define private public` before `ScenePrivate.h` to access `FScene::PrimitiveUpdates`

### Custom data
- Frame indices packed as uint->float reinterpret in PerInstanceCustomData slots 0 and 1
- NumCustomDataFloats aligned to 4

### Vertex factory
- Registered with `IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE` pointing to `.ush` shader
- Shader parameters bound via `BatchElement.UserData` = animation collection uniform buffer
- `bFetchInstanceCountFromScene = true`, `bForceInstanceCulling = true`

## Phase 2 Features (Not Yet Implemented)
- Animation transitions (blend between sequences)
- Multi-LOD support
- Per-instance custom data API
- Animation curves (material-accessible)
- Dynamic pose system (per-instance custom animation)
- Animation notifications
- Instance hierarchy (parent-child attachment)
- Editor module (thumbnails, property customization)
- Developer settings & debug CVars

## Profiling Notes
For comparing GPU vs CPU instance performance:
- `stat unit` - frame time breakdown
- `stat RHI` - draw call count (GPUSKI should be ~1 per mesh vs ~N for CPU)
- `stat gpu` - per-pass GPU timing
- `stat GPUSKI` - custom stat group
- `profilegpu` - single-frame GPU breakdown
- RenderDoc works with UE5 - can verify instanced draws and inspect vertex shader
- Unreal Insights with `-trace=default,gpu` for detailed profiling
- Main win expected on CPU side (game thread + draw thread) and draw call count
