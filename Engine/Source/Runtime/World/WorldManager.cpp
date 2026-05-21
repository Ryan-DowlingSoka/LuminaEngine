#include "pch.h"
#include "WorldManager.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Profiler/Profile.h"
#include "Physics/PhysicsThread.h"
#include "Renderer/RenderThread.h"
#include "UI/RmlUiBridge.h"


namespace Lumina
{
    RUNTIME_API FWorldManager* GWorldManager = nullptr;

    // Seconds a world may stay hidden before its render scene is reclaimed. Big
    // enough that tab flicking / brief PIE never crosses it; small enough that a
    // genuinely idle background world doesn't sit on ~hundreds of MB of GPU memory.
    static TConsoleVar<float> CVarIdleReclaimSeconds("Editor.RenderScene.IdleReclaimSeconds", 3.0f,
        "Seconds a hidden world's render scene is kept resident before being freed.");

    FWorldManager::~FWorldManager()
    {
        // Render thread iterates Contexts in RenderWorlds; drain before we touch them.
        FlushRenderingCommands();

        // Worker holds raw CWorld*; drain before teardown.
        WaitForPhysics();

        // Tear down in reverse so PIE/derived contexts go before their source.
        for (auto It = Contexts.rbegin(); It != Contexts.rend(); ++It)
        {
            if ((*It)->World.IsValid())
            {
                (*It)->World->TeardownWorld();
            }
        }
        Contexts.clear();
    }

    void FWorldManager::UpdateWorlds(const FUpdateContext& UpdateContext)
    {
        LUMINA_PROFILE_SCOPE();

        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            CWorld* World = Context->World.Get();
            if (World == nullptr || World->IsSuspended())
            {
                continue;
            }

            World->Update(UpdateContext);
        }
    }

    void FWorldManager::ReclaimIdleRenderers(double NowSeconds)
    {
        LUMINA_PROFILE_SCOPE();

        const double Grace = (double)CVarIdleReclaimSeconds.GetValue();

        // Reclaim at most one world per frame: DestroyRenderer does a full GPU
        // WaitIdle, so freeing a batch of just-suspended worlds in one frame would
        // stall hard. Spreading them keeps any single frame to one stall.
        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            CWorld* World = Context->World.Get();
            if (World != nullptr && World->ReclaimIdleRenderer(NowSeconds, Grace))
            {
                break;
            }
        }
    }

    void FWorldManager::KickPhysics()
    {
        LUMINA_PROFILE_SCOPE();

        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            CWorld* World = Context->World.Get();
            if (World == nullptr || World->IsSuspended())
            {
                continue;
            }

            GPhysicsThread->Enqueue("World::TickPhysics", [World]()
            {
                World->TickPhysics();
            });
        }
    }

    void FWorldManager::WaitForPhysics()
    {
        GPhysicsThread->Flush();
    }

    void FWorldManager::DispatchPhysicsEvents()
    {
        LUMINA_PROFILE_SCOPE();

        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            CWorld* World = Context->World.Get();
            if (World == nullptr || World->IsSuspended())
            {
                continue;
            }

            World->DispatchPhysicsEvents();
        }
    }

    void FWorldManager::ExtractWorlds()
    {
        LUMINA_PROFILE_SCOPE();

        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            CWorld* World = Context->World.Get();
            if (World == nullptr || World->IsSuspended())
            {
                continue;
            }

            World->Extract();
        }
    }

    void FWorldManager::RenderWorlds(ICommandList& CmdList, uint8 FrameIndex)
    {
        LUMINA_PROFILE_SCOPE();

        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            CWorld* World = Context->World.Get();
            if (World == nullptr || World->IsSuspended())
            {
                continue;
            }

            World->Render(CmdList, FrameIndex);
        }
    }

    void FWorldManager::SignalFrameConsumed(uint8 FrameIndex)
    {
        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            CWorld* World = Context->World.Get();
            if (World == nullptr)
            {
                continue;
            }

            if (IRenderScene* Scene = World->GetRenderer())
            {
                Scene->SignalFrameConsumed(FrameIndex);
            }
        }
    }

    FWorldContext* FWorldManager::CreateWorldContext(CWorld* World, EWorldType Type, ENetMode NetMode)
    {
        if (World == nullptr)
        {
            return nullptr;
        }

        if (FWorldContext* Existing = FindContext(World))
        {
            return Existing;
        }

        TUniquePtr<FWorldContext> Context = MakeUnique<FWorldContext>();
        Context->World   = World;
        Context->Type    = Type;
        Context->NetMode = NetMode;

        FWorldContext* Raw = Context.get();
        
        FlushRenderingCommands();
        Contexts.push_back(Move(Context));

        World->OwningContext = Raw;
        
        RmlUi::OnWorldInitialized(World);

        World->InitializeWorld(Type);

        return Raw;
    }

    void FWorldManager::DestroyWorldContext(CWorld* World)
    {
        if (World == nullptr)
        {
            return;
        }

        // RenderWorlds iterates Contexts on the render thread. Flush before any
        // mutation of the vector or destruction of the world it points at.
        FlushRenderingCommands();

        for (size_t i = 0; i < Contexts.size(); ++i)
        {
            if (Contexts[i]->World.Get() == World)
            {
                // Tear down RmlUi context BEFORE the world goes away; the
                // bridge needs the live World pointer for its listener-detach walk.
                RmlUi::OnWorldTornDown(World);

                World->TeardownWorld();
                World->OwningContext = nullptr;

                size_t Last = Contexts.size() - 1;
                if (i != Last)
                {
                    eastl::swap(Contexts[i], Contexts[Last]);
                }
                Contexts.pop_back();
                return;
            }
        }

        // Not registered, tear down anyway so the caller's expectations hold.
        RmlUi::OnWorldTornDown(World);
        World->TeardownWorld();
    }

    FWorldContext* FWorldManager::FindContext(CWorld* World)
    {
        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            if (Context->World.Get() == World)
            {
                return Context.get();
            }
        }
        return nullptr;
    }

    FWorldContext* FWorldManager::GetPrimaryGameContext()
    {
        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            if (Context->Type == EWorldType::Game)
            {
                return Context.get();
            }
        }
        return nullptr;
    }

    CWorld* FWorldManager::StartPIE(CWorld* SourceWorld, EWorldType SessionType, ENetMode NetMode)
    {
        if (SourceWorld == nullptr)
        {
            return nullptr;
        }

        CWorld* PIEWorld = CWorld::DuplicateWorld(SourceWorld);
        if (PIEWorld == nullptr)
        {
            return nullptr;
        }

        FWorldContext* Ctx = CreateWorldContext(PIEWorld, SessionType, NetMode);
        if (Ctx != nullptr)
        {
            Ctx->bPIE = true;
            Ctx->SourceWorld = SourceWorld;
        }

        return PIEWorld;
    }

    void FWorldManager::StopPIE(CWorld* PIEWorld)
    {
        DestroyWorldContext(PIEWorld);
    }
}
