#include "pch.h"
#include "ScriptSystem.h"
#include "Core/Profiler/CPUProfiler.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "World/World.h"
#include "world/entity/components/entitytags.h"
#include "World/Entity/Components/ScriptComponent.h"


namespace Lumina
{
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
                    #if USING(WITH_EDITOR)
                    (void)Script->InvokeAsCoroutine(ScriptComponent.UpdateFunc, Script->Reference, DeltaTime);
                    #else
                    (void)ScriptComponent.UpdateFunc.Invoke(Script->Reference, DeltaTime);
                    #endif
                }
                else
                {
                    ScriptComponent.AccumulatedTime += DeltaTime;
                    if (ScriptComponent.AccumulatedTime >= ScriptComponent.TickRate)
                    {
                        #if USING(WITH_EDITOR)
                        (void)Script->InvokeAsCoroutine(ScriptComponent.UpdateFunc, Script->Reference, ScriptComponent.AccumulatedTime);
                        #else
                        (void)ScriptComponent.UpdateFunc.Invoke(Script->Reference, ScriptComponent.AccumulatedTime);
                        #endif
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
            }
            break;
        case EUpdateStage::Max:
            break;
        }
    }
}
