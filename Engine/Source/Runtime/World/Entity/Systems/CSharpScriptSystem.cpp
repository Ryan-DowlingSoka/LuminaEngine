#include "pch.h"
#include "CSharpScriptSystem.h"

#include "Scripting/DotNet/DotNetHost.h"
#include "Scripting/ScriptExports.h"
#include "World/Entity/Components/CSharpScriptComponent.h"
#include "World/Entity/Components/EntityTags.h"
#include "World/Entity/Components/InputComponent.h"
#include "Input/InputEvent.h"

namespace Lumina
{
    class CWorld;

    void SCSharpScriptSystem::Update(const FSystemContext& Context) noexcept
    {
        if (!DotNet::IsInitialized())
        {
            return;
        }

        const uint64 World        = reinterpret_cast<uint64>(Context.GetRegistry().ctx().get<CWorld*>());
        const int32  Generation   = DotNet::GetScriptGeneration();
        const float  DeltaSeconds = (float)Context.GetDeltaTime();

        auto View = Context.CreateView<SCSharpScriptComponent>(entt::exclude<SDisabledTag>);
        
        View.each([&](entt::entity Entity, SCSharpScriptComponent& Component)
        {
            if (Component.ScriptClass.empty())
            {
                return;
            }
            if (Component.Generation == Generation && Component.Instance != nullptr)
            {
                return;
            }

            // Generation changed, managed already freed the old instance handle on unload, so drop our
            // stale pointer WITHOUT calling destroy (that would touch a freed GCHandle).
            Component.Instance = nullptr;
            Component.BindState = ECSharpBindState::Unbound;
            Component.Generation = Generation;

            const FStringView ClassName(Component.ScriptClass.c_str(), Component.ScriptClass.size());
            void* Instance = DotNet::CreateEntityScript(ClassName, World, (uint32)entt::to_integral(Entity));
            if (Instance == nullptr)
            {
                Component.CallbackFlags = 0;
                return;
            }

            Component.Instance = Instance;
            Component.CallbackFlags = DotNet::GetScriptCallbackFlags(Instance);
            Component.BindState = ECSharpBindState::Attached;

            // Reconcile saved overrides against the script's current [Property] schema (drop drift, fill
            // missing from defaults), then push them onto the fresh instance.
            Scripting::FScriptExportSchema Schema;
            TVector<Scripting::FScriptPropertyEntry> Defaults;
            if (DotNet::GatherScriptSchema(ClassName, Schema, Defaults))
            {
                Scripting::ReconcileOverrides(Schema, Defaults, Component.PropertyOverrides.Items);
            }
            DotNet::ApplyScriptProperties(Instance, Component.PropertyOverrides);
        });
        
        {
            CWorld* CW = Context.GetRegistry().ctx().get<CWorld*>();
            const FInputViewport* V = FInputViewportRegistry::Get().FindViewportForWorld(CW);
            const TVector<SInputEvent>* Events = (V != nullptr) ? &V->GetContext().GetFrameEvents() : nullptr;
            if (Events != nullptr && !Events->empty())
            {
                constexpr int32 OnInputBit = 1 << 4;
                View.each([&](entt::entity Entity, SCSharpScriptComponent& Component)
                {
                    if (Component.Instance == nullptr || (Component.CallbackFlags & OnInputBit) == 0)
                    {
                        return;
                    }
                    const SInputComponent* Input = Context.GetRegistry().try_get<SInputComponent>(Entity);
                    if (Input == nullptr || !Input->bReceivingInput)
                    {
                        return;
                    }
                    for (const SInputEvent& E : *Events)
                    {
                        const int32 bMouse  = (E.Key.Device == EKeyDevice::Mouse) ? 1 : 0;
                        const int32 KeyCode = bMouse ? (int32)E.Key.MouseButton : (int32)E.Key.Key;
                        const int32 Mods    = (E.Key.bShift ? 1 : 0) | (E.Key.bCtrl ? 2 : 0) | (E.Key.bAlt ? 4 : 0);
                        DotNet::DispatchScriptInput(Component.Instance, (int32)E.Type, KeyCode, bMouse, Mods,
                            E.bRepeat ? 1 : 0, E.MouseX, E.MouseY, E.DeltaX, E.DeltaY, E.Scroll);
                    }
                });
            }
        }
        
        TVector<void*> Ready;
        Ready.reserve(View.size_hint());
        View.each([&](entt::entity, SCSharpScriptComponent& Component)
        {
            if (Component.Instance == nullptr)
            {
                return;
            }
            if (Component.BindState == ECSharpBindState::Attached)
            {
                DotNet::OnReadyScript(Component.Instance);
                Component.BindState = ECSharpBindState::Ready;
            }
            if (Component.BindState == ECSharpBindState::Ready)
            {
                Ready.push_back(Component.Instance);
            }
        });

        if (!Ready.empty())
        {
            DotNet::UpdateScripts(Ready.data(), (int32)Ready.size(), DeltaSeconds);
        }
    }
}
