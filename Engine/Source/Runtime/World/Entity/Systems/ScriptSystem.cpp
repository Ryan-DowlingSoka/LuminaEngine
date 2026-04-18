#include "pch.h"
#include "ScriptSystem.h"
#include "World/World.h"
#include "world/entity/components/entitytags.h"
#include "World/Entity/Components/ScriptComponent.h"


namespace Lumina
{
    void SScriptSystem::Update(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE(); 
        
        auto IterateGroup = [&](entt::entity, SScriptComponent& ScriptComponent)
        {
            if (const TSharedPtr<Lua::FScript>& Script = ScriptComponent.Script)
            {
                if (ScriptComponent.UpdateFunc.IsValid())
                {
                    const float DeltaTime = static_cast<float>(Context.GetDeltaTime());
                
                    if (ScriptComponent.TickRate <= 0.0f)
                    {
                        ScriptComponent.UpdateFunc(Script->Reference, DeltaTime);
                    }
                    else
                    {
                        ScriptComponent.AccumulatedTime += DeltaTime;
                        if (ScriptComponent.AccumulatedTime >= ScriptComponent.TickRate)
                        {
                            ScriptComponent.UpdateFunc(Script->Reference, ScriptComponent.AccumulatedTime);
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
