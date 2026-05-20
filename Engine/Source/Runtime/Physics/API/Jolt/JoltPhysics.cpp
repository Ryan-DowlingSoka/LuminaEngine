#include "pch.h"
#include "JoltPhysics.h"
#include <Core/Console/ConsoleVariable.h>

#include <algorithm>
#include "JoltPhysicsScene.h"
#include "Core/Threading/Thread.h"
#include "Jolt/RegisterTypes.h"
#include "Jolt/Core/Factory.h"
#include "Physics/API/Jolt/JoltUtils.h"
#include "World/World.h"

static_assert(sizeof(JPH::ObjectLayer) == 4);

namespace Lumina::Physics
{
    static TUniquePtr<FJoltData> JoltData;
    #if JPH_DEBUG_RENDERER
    static JPH::BodyManager::DrawSettings DebugDrawSettings;

    static TConsoleVar CVarJoltDebug("Jolt.Debug.Draw", false, "Toggles debug drawing for Jolt Physics, has severe performance impact.");

    static TConsoleVar CVarJoltDebugShapes("Jolt.Debug.Shapes", DebugDrawSettings.mDrawShape, "Toggles debugging shapes for Jolt Physics", [](const auto& Var)
        {
            DebugDrawSettings.mDrawShape = eastl::get<bool>(Var);
        });

    static TConsoleVar CVarJoltDebugShapeWireframe("Jolt.Debug.ShapeWireframe", DebugDrawSettings.mDrawShapeWireframe, "Toggles wireframe rendering for shapes", [](const auto& Var)
        {
            DebugDrawSettings.mDrawShapeWireframe = eastl::get<bool>(Var);
        });

    static TConsoleVar CVarJoltDebugAABB("Jolt.Debug.AABB", DebugDrawSettings.mDrawBoundingBox, "Toggles debugging AABB for Jolt Physics", [](const auto& Var)
        {
            DebugDrawSettings.mDrawBoundingBox = eastl::get<bool>(Var);
        });

    static TConsoleVar CVarJoltDebugVelocity("Jolt.Debug.Velocity", DebugDrawSettings.mDrawVelocity, "Toggles debugging velocity vectors for Jolt Physics", [](const auto& Var)
        {
            DebugDrawSettings.mDrawVelocity = eastl::get<bool>(Var);
        });

    static TConsoleVar CVarJoltDebugCenterOfMass("Jolt.Debug.CenterOfMass", DebugDrawSettings.mDrawCenterOfMassTransform, "Toggles center of mass visualization", [](const auto& Var)
        {
            DebugDrawSettings.mDrawCenterOfMassTransform = eastl::get<bool>(Var);
        });

    static TConsoleVar CVarJoltDebugWorldTransform("Jolt.Debug.WorldTransform", DebugDrawSettings.mDrawWorldTransform, "Toggles world transform axes visualization", [](const auto& Var)
        {
            DebugDrawSettings.mDrawWorldTransform = eastl::get<bool>(Var);
        });

    static TConsoleVar CVarJoltDebugSleepStats("Jolt.Debug.SleepStats", DebugDrawSettings.mDrawSleepStats, "Toggles sleep statistics visualization", [](const auto& Var)
        {
            DebugDrawSettings.mDrawSleepStats = eastl::get<bool>(Var);
        });

    static TConsoleVar CVarJoltDebugGetSupport("Jolt.Debug.GetSupport", DebugDrawSettings.mDrawGetSupportFunction, "Toggles GetSupport function visualization for collision detection", [](const auto& Var)
        {
            DebugDrawSettings.mDrawGetSupportFunction = eastl::get<bool>(Var);
        });

    static TConsoleVar CVarJoltDebugGetSupportDirection("Jolt.Debug.GetSupportDir", DebugDrawSettings.mDrawGetSupportingFace, "Toggles GetSupportingFace visualization", [](const auto& Var)
        {
            DebugDrawSettings.mDrawGetSupportingFace = eastl::get<bool>(Var);
        });
    #endif
    
    #if JPH_ASSERT
    static void JoltTraceCallback(const char* format, ...)
    {
        va_list list;
        va_start(list, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, list);

        if (JoltData)
        {
            JoltData->LastErrorMessage = buffer;
        }
        LOG_TRACE("Jolt Physics - {}", buffer);
    }
    #endif

    void* JPHCustomAllocate(size_t size)
    {
        return Memory::Malloc(size);
    }

    void* JPHCustomReallocate(void* block, size_t oldSize, size_t newSize)
    {
        return Memory::Realloc(block, newSize);
    }

    void JPHCustomFree(void* block)
    {
        Memory::Free(block);
    }

    void* JPHCustomAlignedAllocate(size_t size, size_t alignment)
    {
        return Memory::Malloc(size, alignment);
    }

    void JPHCustomAlignedFree(void* block)
    {
        Memory::Free(block);
    }
    
    static bool JoltAssertionFailed(const char* expr, const char* msg, const char* file, uint32 line)
    {
        LOG_CRITICAL("JOLT ASSERT FAILED: Message {}, File: {} - {}", expr, msg, file, line);
        return true;
    }

    void FJoltPhysicsContext::Initialize()
    {
        #if JPH_ASSERT
        JPH::Trace              = JoltTraceCallback;
        JPH::AssertFailed       = JoltAssertionFailed;
        #endif
        
        JPH::Reallocate         = JPHCustomReallocate;
        JPH::Allocate           = JPHCustomAllocate;
        JPH::Free               = JPHCustomFree;
        JPH::AlignedAllocate    = JPHCustomAlignedAllocate;
        JPH::AlignedFree        = JPHCustomAlignedFree;

        JoltData = MakeUnique<FJoltData>();
        #if JPH_DEBUG_RENDERER
		JoltData->DebugRenderer = MakeUnique<FJoltDebugRenderer>();
        #endif
        JPH::Factory::sInstance = Memory::New<JPH::Factory>();
        
        #if JPH_DEBUG_RENDERER
        JPH::DebugRenderer::sInstance = JoltData->DebugRenderer.get();
        #endif
        JPH::RegisterTypes();
        
        int NumJoltThreads = (int)Threading::GetNumThreads() - 3;
        NumJoltThreads = std::max(NumJoltThreads, 1);
        JoltData->JobThreadPool = MakeUnique<JPH::JobSystemThreadPool>(2048, 8, NumJoltThreads);

    }

    void FJoltPhysicsContext::Shutdown()
    {
        JPH::UnregisterTypes();
        JoltData.reset();
        
        #if JPH_DEBUG_RENDERER
		JPH::DebugRenderer::sInstance = nullptr;
        #endif
        
        Memory::Delete(JPH::Factory::sInstance);
    }

    TUniquePtr<IPhysicsScene> FJoltPhysicsContext::CreatePhysicsScene(CWorld* World)
    {
        return MakeUnique<FJoltPhysicsScene>(World);
    }

    JPH::JobSystemThreadPool* FJoltPhysicsContext::GetThreadPool()
    {
        return JoltData->JobThreadPool.get();
    }

    #if JPH_DEBUG_RENDERER
    FJoltDebugRenderer* FJoltPhysicsContext::GetDebugRenderer()
    {
        return JoltData->DebugRenderer.get();
    }

    void FJoltDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor)
    {
        float DrawDuration = (float)std::max(World->GetWorldDeltaTime(), Duration);
        World->DrawLine(JoltUtils::FromJPHVec3(inFrom), JoltUtils::FromJPHVec3(inTo), glm::vec4(inColor.r, inColor.g, inColor.b, inColor.a), 1.0f, DrawDuration);
    }

    void FJoltDebugRenderer::DrawBodies(JPH::PhysicsSystem* System, CWorld* InWorld)
    {
        World = InWorld;
        
        if (SCameraComponent* Camera = World->GetActiveCamera())
        {
            SetCameraPos(JoltUtils::ToJPHRVec3(Camera->GetPosition()));
            #if JPH_DEBUG_RENDERER
            System->DrawBodies(DebugDrawSettings, this);
            #endif
        }
    }
    #endif
}

namespace JPH
{
    #if JPH_EXTERNAL_PROFILE
    ExternalProfileMeasurement::ExternalProfileMeasurement(const char* inName, uint32 inColor /* = 0 */)
        : mUserData{}
    {
        
    }
    
    ExternalProfileMeasurement::~ExternalProfileMeasurement()
    {
        
    }
    #endif
}
