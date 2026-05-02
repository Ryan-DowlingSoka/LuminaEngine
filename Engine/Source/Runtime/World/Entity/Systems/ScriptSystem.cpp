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

        // Hold every script Update while the debugger is parked at a
        // breakpoint. Without this, the entity that broke would re-enter
        // Update on the next frame and re-hit the same line, plus every
        // other entity would tick on top of the paused thread, making the
        // pause useless. The paused thread itself stays alive (anchored by
        // FLuaDebugger) and only resumes when the user clicks Continue.
        if (Lua::FLuaDebugger::Get().IsPaused())
        {
            return;
        }

        auto IterateGroup = [&](entt::entity, SScriptComponent& ScriptComponent)
        {
            if (const TSharedPtr<Lua::FScript>& Script = ScriptComponent.Script)
            {
                if (ScriptComponent.UpdateFunc.IsValid())
                {
                    const float DeltaTime = static_cast<float>(Context.GetDeltaTime());
                    
                    if (ScriptComponent.TickRate <= 0.0f)
                    {
                        #if USING(WITH_EDITOR)
                        (void)ScriptComponent.UpdateFunc.InvokeAsCoroutine(Script->Reference, DeltaTime);
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
                            (void)ScriptComponent.UpdateFunc.InvokeAsCoroutine(Script->Reference, ScriptComponent.AccumulatedTime);
                            #else
                            (void)ScriptComponent.UpdateFunc.Invoke(Script->Reference, ScriptComponent.AccumulatedTime);
                            #endif
                            ScriptComponent.AccumulatedTime = 0.0f;
                        }
                    }
                }
            }
        };

        switch (Context.GetUpdateStage())
        {
        case EUpdateStage::FrameStart:
            {
                auto View = Context.CreateView<SScriptComponent, FUpdateStage_FrameStart>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);   
            }
            break;
        case EUpdateStage::PrePhysics:
            {
                auto View = Context.CreateView<SScriptComponent, FUpdateStage_PrePhysics>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);   
            }
            break;
        case EUpdateStage::DuringPhysics:
            {
                auto View = Context.CreateView<SScriptComponent, FUpdateStage_DuringPhysics>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);   
            }
            break;
        case EUpdateStage::PostPhysics:
            {
                auto View = Context.CreateView<SScriptComponent, FUpdateStage_PostPhysics>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);  
            }
            break;
        case EUpdateStage::FrameEnd:
            {
                auto View = Context.CreateView<SScriptComponent, FUpdateStage_FrameEnd>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);  
            }
            break;
        case EUpdateStage::Paused:
            {
                auto View = Context.CreateView<SScriptComponent, FUpdateStage_Paused>(entt::exclude<SDisabledTag>);
                View.each(IterateGroup);
            }
            break;
        case EUpdateStage::Max:
            break;
        }
    }
}
