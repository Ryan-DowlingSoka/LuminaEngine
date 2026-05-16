#include "pch.h"
#include "WorldManager.h"
#include "Core/Profiler/Profile.h"
#include "UI/RmlUiBridge.h"


namespace Lumina
{
    RUNTIME_API FWorldManager* GWorldManager = nullptr;

    FWorldManager::~FWorldManager()
    {
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

    void FWorldManager::RenderWorlds(ICommandList& CmdList)
    {
        LUMINA_PROFILE_SCOPE();

        for (const TUniquePtr<FWorldContext>& Context : Contexts)
        {
            CWorld* World = Context->World.Get();
            if (World == nullptr || World->IsSuspended())
            {
                continue;
            }

            World->Render(CmdList);
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
        Contexts.push_back(Move(Context));

        World->OwningContext = Raw;

        // Spin up the per-world Rml::Context BEFORE InitializeWorld so scripts
        // firing during entity init (OnReady -> UI.LoadDocument) resolve to
        // the new world's context. The bridge sizes against the real RT later
        // inside TickAll once it exists.
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
