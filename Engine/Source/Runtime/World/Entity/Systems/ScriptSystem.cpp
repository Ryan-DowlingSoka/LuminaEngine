#include "pch.h"
#include "ScriptSystem.h"
#include "SystemSingletons.h"
#include "Core/Profiler/CPUProfiler.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "World/World.h"
#include "World/Subsystems/WorldSettings.h"
#include "world/entity/components/entitytags.h"
#include "World/Entity/Components/ScriptComponent.h"


namespace Lumina
{
    // Fixed-rate hook: drains the accumulator into N fixed steps and invokes
    // OnFixedUpdate on every script that defines it. Game/simulation worlds only.
    static void RunFixedUpdate(const FSystemContext& Context)
    {
        const EWorldType WorldType = Context.GetWorldType();
        if (WorldType != EWorldType::Game && WorldType != EWorldType::Simulation)
        {
            return;
        }

        CWorld* World = Context.GetRegistry().ctx().get<CWorld*>();
        const SDefaultWorldSettings& WorldSettings = World->GetDefaultWorldSettings();

        const float Hz       = Math::Max(10.0f, WorldSettings.PhysicsHz);
        const float FixedDt  = 1.0f / Hz;
        const int32 MaxSteps = (int32)WorldSettings.MaxPhysicsSteps;
        const float MaxAccum = (float)MaxSteps * FixedDt;

        FScriptFixedUpdateState& State = Context.GetRegistry().ctx().get<FScriptFixedUpdateState>();
        State.Accumulator += (float)Context.GetDeltaTime();
        // Clamp so a hitch can't trigger a step storm (matches the physics scene).
        State.Accumulator = Math::Min(State.Accumulator, MaxAccum);

        int32 Steps = (State.Accumulator >= FixedDt) ? (int32)(State.Accumulator / FixedDt) : 0;
        if (Steps <= 0)
        {
            return;
        }
        Steps = Math::Min(Steps, MaxSteps);
        State.Accumulator -= (float)Steps * FixedDt;

        auto View = Context.CreateView<SScriptComponent, FScriptHasFixedUpdateFn>(entt::exclude<SDisabledTag>);
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            View.each([&](entt::entity, SScriptComponent& ScriptComponent)
            {
                const TSharedPtr<Lua::FScript>& Script = ScriptComponent.Script;
                if (Script)
                {
                    ScriptComponent.FixedUpdateFunc.Call(Script->Reference, FixedDt);
                }
            });
        }
    }

    // Editor-only per-frame hook: invokes OnEditorUpdate on every script that
    // defines it. Runs in the Paused stage, the only stage an idle editor ticks.
    static void RunEditorUpdate(const FSystemContext& Context)
    {
        if (Context.GetWorldType() != EWorldType::Editor)
        {
            return;
        }

        const float DeltaSeconds = (float)Context.GetDeltaTime();

        auto View = Context.CreateView<SScriptComponent, FScriptHasEditorUpdateFn>(entt::exclude<SDisabledTag>);
        View.each([&](entt::entity, SScriptComponent& ScriptComponent)
        {
            const TSharedPtr<Lua::FScript>& Script = ScriptComponent.Script;
            if (Script)
            {
                ScriptComponent.EditorUpdateFunc.Call(Script->Reference, DeltaSeconds);
            }
        });
    }

    void SScriptSystem::Update(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        CPU_PROFILE_SCOPE_COLOR("Lua Scripts", FColor(0.95f, 0.70f, 0.25f));
        
        if (Lua::FLuaDebugger::Get().IsPaused())
        {
            return;
        }
        
        auto IterateGroup = [&](entt::entity, SScriptComponent& ScriptComponent)
        {
            if (const TSharedPtr<Lua::FScript>& Script = ScriptComponent.Script)
            {
                const float DeltaTime = static_cast<float>(Context.GetDeltaTime());

                if (ScriptComponent.TickRate <= 0.0f)
                {
                    ScriptComponent.UpdateFunc.Call(Script->Reference, DeltaTime);
                }
                else
                {
                    ScriptComponent.AccumulatedTime += DeltaTime;
                    if (ScriptComponent.AccumulatedTime >= ScriptComponent.TickRate)
                    {
                        ScriptComponent.UpdateFunc.Call(Script->Reference, ScriptComponent.AccumulatedTime);
                        ScriptComponent.AccumulatedTime = 0.0f;
                    }
                }
            }
        };

        switch (Context.GetUpdateStage())
        {
        case EUpdateStage::FrameStart:
            {
                auto View = Context.CreateView<SScriptComponent, FScriptHasUpdateFn, FUpdateStage_FrameStart>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);
            }
            break;
        case EUpdateStage::PrePhysics:
            {
                auto View = Context.CreateView<SScriptComponent, FScriptHasUpdateFn, FUpdateStage_PrePhysics>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);
                RunFixedUpdate(Context);
            }
            break;
        case EUpdateStage::DuringPhysics:
            {
                auto View = Context.CreateView<SScriptComponent, FScriptHasUpdateFn, FUpdateStage_DuringPhysics>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);
            }
            break;
        case EUpdateStage::PostPhysics:
            {
                auto View = Context.CreateView<SScriptComponent, FScriptHasUpdateFn, FUpdateStage_PostPhysics>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);
            }
            break;
        case EUpdateStage::FrameEnd:
            {
                auto View = Context.CreateView<SScriptComponent, FScriptHasUpdateFn, FUpdateStage_FrameEnd>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);
            }
            break;
        case EUpdateStage::Paused:
            {
                auto View = Context.CreateView<SScriptComponent, FScriptHasUpdateFn, FUpdateStage_Paused>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);
                RunEditorUpdate(Context);
            }
            break;
        case EUpdateStage::Max:
            break;
        }
    }
}
