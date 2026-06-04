#include "FSceneEditorTool.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
#include "Assets/AssetTypes/Prefabs/PrefabComponents.h"
#include "Components/EditorEntityTags.h"
#include "Config/Config.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Math/Math.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Object/Package/Package.h"
#include "EASTL/sort.h"
#include "Input/InputViewport.h"
#include "World/Scene/RenderScene/RenderScene.h"
#include "World/Scene/RenderScene/SceneRenderTypes.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Traits.h"
#include "Tools/ComponentVisualizers/ComponentVisualizer.h"
#include "Tools/Dialogs/Dialogs.h"
#include "Tools/PrimitiveManager/PrimitiveManager.h"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "UI/Properties/EntityPropertyContext.h"
#include "UI/Tools/EditorEntityUtils.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/EntityTags.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/ScriptComponent.h"
#include "World/Entity/Components/TagComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Entity/EntityUtils.h"
#include "World/World.h"

namespace Lumina
{
    // Route through the asset-tool's display-name-only ctor so the inherited PropertyTable
    // stays unbound (neither subclass uses it; they drive their own per-component tables).
    // Asset + World are assigned directly; FEditorTool's ctor only stores World, so setting
    // it post-construction is safe.
    FSceneEditorTool::FSceneEditorTool(IEditorToolContext* Context, const FString& DisplayName, CObject* InAsset, CWorld* InWorld)
        : FAssetEditorTool(Context, DisplayName)
    {
        Asset = InAsset;
        World = InWorld;
    }

    FSceneEditorTool::FSceneEditorTool(IEditorToolContext* Context, const FString& DisplayName, CWorld* InWorld)
        : FAssetEditorTool(Context, DisplayName)
    {
        // World path: the live CWorld is the document, but it is NOT held as the FAssetEditorTool
        // Asset, its lifetime is owned by the editor (created/swapped/destroyed elsewhere), so a
        // second owning ref here would corrupt the refcount on swap. Asset stays null; saving and
        // dirtying go through GetScenePackage()/OnSave overrides.
        World = InWorld;
    }

    void FSceneEditorTool::OnSave()
    {
        CommitScene();
        FAssetEditorTool::OnSave();
    }

    void FSceneEditorTool::OnAssetLoadFinished()
    {
        OnSceneLoaded();
    }

    CPackage* FSceneEditorTool::GetScenePackage() const
    {
        return Asset != nullptr ? Asset->GetPackage() : nullptr;
    }

    void FSceneEditorTool::MarkSceneDirty()
    {
        if (CPackage* Package = GetScenePackage())
        {
            Package->MarkDirty();
        }
    }

    void FSceneEditorTool::SetSingleSelectedEntity(entt::entity Entity)
    {
        FEntityRegistry& Registry = GetSceneRegistry();
        if (Entity != entt::null && !Registry.valid(Entity))
        {
            Entity = entt::null;
        }

        // Fast-path: clicking the already-singularly-selected entity is a no-op.
        if (Entity == LastSelectedEntity && SelectedEntities.size() == (Entity == entt::null ? 0 : 1)
            && (Entity == entt::null || SelectedEntities.find(Entity) != SelectedEntities.end()))
        {
            return;
        }

        // Drop tags from entities no longer selected so render highlighting matches the canonical set.
        for (entt::entity Old : SelectedEntities)
        {
            if (Old != Entity && Registry.valid(Old))
            {
                Registry.remove<FSelectedInEditorComponent>(Old);
                SyncOutlinerRowSelection(Old, false);
            }
        }
        SelectedEntities.clear();

        // Clear last-selected tag unconditionally; re-emplace below if new selection isn't empty.
        Registry.clear<FLastSelectedTag>();

        if (Entity != entt::null)
        {
            SelectedEntities.insert(Entity);
            Registry.emplace_or_replace<FSelectedInEditorComponent>(Entity);
            Registry.emplace_or_replace<FLastSelectedTag>(Entity);
            SyncOutlinerRowSelection(Entity, true);
        }

        if (LastSelectedEntity != Entity)
        {
            LastSelectedEntity = Entity;
            OnSelectionChanged();
        }
    }

    void FSceneEditorTool::AddSelectedEntity(entt::entity Entity, bool /*bRebuild*/)
    {
        FEntityRegistry& Registry = GetSceneRegistry();
        if (Entity == entt::null || !Registry.valid(Entity))
        {
            return;
        }

        const bool bWasAlreadySelected = SelectedEntities.find(Entity) != SelectedEntities.end();
        if (!bWasAlreadySelected)
        {
            SelectedEntities.insert(Entity);
            Registry.emplace_or_replace<FSelectedInEditorComponent>(Entity);
            SyncOutlinerRowSelection(Entity, true);
        }

        // Always promote to last-selected so clicking a row in a multi-select focuses details.
        if (LastSelectedEntity != Entity)
        {
            Registry.clear<FLastSelectedTag>();
            Registry.emplace_or_replace<FLastSelectedTag>(Entity);
            LastSelectedEntity = Entity;
            OnSelectionChanged();
        }
    }

    void FSceneEditorTool::RemoveSelectedEntity(entt::entity Entity, bool /*bRebuild*/)
    {
        if (Entity == entt::null)
        {
            return;
        }

        auto SetIt = SelectedEntities.find(Entity);
        if (SetIt == SelectedEntities.end())
        {
            return;
        }

        SelectedEntities.erase(SetIt);

        FEntityRegistry& Registry = GetSceneRegistry();
        if (Registry.valid(Entity))
        {
            Registry.remove<FSelectedInEditorComponent>(Entity);
        }

        SyncOutlinerRowSelection(Entity, false);

        // If the deselected entity was the focus, pick a new one so "last" isn't stale.
        if (LastSelectedEntity == Entity)
        {
            Registry.clear<FLastSelectedTag>();
            entt::entity NewLast = entt::null;
            for (entt::entity Candidate : SelectedEntities)
            {
                if (Registry.valid(Candidate))
                {
                    NewLast = Candidate;
                    break;
                }
            }
            if (NewLast != entt::null)
            {
                Registry.emplace_or_replace<FLastSelectedTag>(NewLast);
            }
            LastSelectedEntity = NewLast;
            OnSelectionChanged();
        }
    }

    void FSceneEditorTool::ToggleSelectedEntity(entt::entity Entity)
    {
        FEntityRegistry& Registry = GetSceneRegistry();
        if (Entity == entt::null || !Registry.valid(Entity))
        {
            return;
        }

        if (SelectedEntities.find(Entity) != SelectedEntities.end())
        {
            RemoveSelectedEntity(Entity, false);
        }
        else
        {
            AddSelectedEntity(Entity, false);
        }
    }

    void FSceneEditorTool::ResyncSelectionFromRegistry()
    {
        // Clear old outliner row state; re-mark below from the post-resync set.
        for (entt::entity Old : SelectedEntities)
        {
            SyncOutlinerRowSelection(Old, false);
        }
        SelectedEntities.clear();
        LastSelectedEntity = entt::null;

        FEntityRegistry& Registry = GetSceneRegistry();

        Registry.view<FSelectedInEditorComponent>().each([&](entt::entity Entity)
        {
            SelectedEntities.insert(Entity);
            SyncOutlinerRowSelection(Entity, true);
        });

        // FLastSelectedTag should be serialized; fall back to first selected if it's missing.
        Registry.view<FLastSelectedTag>().each([&](entt::entity Entity)
        {
            LastSelectedEntity = Entity;
        });

        if (LastSelectedEntity == entt::null && !SelectedEntities.empty())
        {
            entt::entity First = *SelectedEntities.begin();
            LastSelectedEntity = First;
            Registry.emplace_or_replace<FLastSelectedTag>(First);
        }

        OnSelectionChanged();
    }

    void FSceneEditorTool::ClearSelectedEntities()
    {
        FEntityRegistry& Registry = GetSceneRegistry();

        for (entt::entity Entity : SelectedEntities)
        {
            SyncOutlinerRowSelection(Entity, false);
        }

        SelectedEntities.clear();

        // Bulk-erase via registry clear<>(); cheaper than walking SelectedEntities.
        Registry.clear<FSelectedInEditorComponent>();
        Registry.clear<FLastSelectedTag>();

        if (LastSelectedEntity != entt::null)
        {
            LastSelectedEntity = entt::null;
            OnSelectionChanged();
        }
    }

    bool FSceneEditorTool::IsComponentHiddenInDetails(const CStruct* Type) const
    {
        // Tags render as chips in their own section, not as a component row.
        return Type == STagComponent::StaticStruct();
    }

    void FSceneEditorTool::DrawComponentList(entt::entity Entity)
    {
        for (FComponentTableEntry& Entry : PropertyTables)
        {
            DrawComponentHeader(Entry, Entity);
            ImGui::Spacing();
        }

        // Deferred so we never remove a storage element while iterating / drawing its table.
        if (PendingRuntimeRemove != nullptr)
        {
            ECS::Utils::RemoveRuntimeComponent(GetSceneRegistry(), Entity, PendingRuntimeRemove);
            PendingRuntimeRemove = nullptr;
            bDetailsDirty = true;
            MarkSceneDirty();
        }
    }

    void FSceneEditorTool::DrawComponentHeader(FComponentTableEntry& Entry, entt::entity Entity)
    {
        using namespace entt::literals;

        const bool bRuntime = Entry.bRuntime;

        // The live runtime type is fetched from the storage (which strong-refs it) -- never from a
        // cached pointer, which could dangle if the type asset was deleted.
        CEntityComponentType* RuntimeType = nullptr;

        // Existence check + re-point. Reflected components verify via meta; runtime ones verify
        // storage still holds the entity and re-bind the table if it moved (realloc/relayout).
        if (!bRuntime)
        {
            if (IsComponentHiddenInDetails(Entry.ReflectedType))
            {
                return;
            }

            entt::meta_type MetaType = entt::resolve(entt::hashed_string(Entry.ReflectedType->GetName().c_str()));
            if (!ECS::Utils::HasComponent(GetSceneRegistry(), Entity, MetaType))
            {
                return;
            }
        }
        else
        {
            FRuntimeComponentStorage* Storage = ECS::Utils::FindRuntimeStorageById(GetSceneRegistry(), Entry.RuntimeStorageId);
            // Missing / invalidated (type deleted) / no longer on the entity -> drop this row.
            if (Storage == nullptr || !Storage->IsBound() || !Storage->contains(Entity))
            {
                bDetailsDirty = true;
                return;
            }
            RuntimeType = Storage->GetSchemaType();
            if (Entry.Table != nullptr)
            {
                void* CurrentData = Storage->value(Entity);
                CStruct* CurrentLayout = Storage->GetLayout();
                if (CurrentData != Entry.BoundData || CurrentLayout != Entry.BoundLayout)
                {
                    Entry.Table->SetObject(CurrentData, CurrentLayout);
                    Entry.BoundData = CurrentData;
                    Entry.BoundLayout = CurrentLayout;
                }
            }
        }

        const bool bIsRequired = !bRuntime
            && (Entry.ReflectedType == STransformComponent::StaticStruct() || Entry.ReflectedType == SNameComponent::StaticStruct());

        ImGui::PushID(&Entry);

        constexpr ImGuiTableFlags Flags =
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_NoBordersInBodyUntilResize |
        ImGuiTableFlags_SizingFixedFit;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 10.0f)); // increase Y for taller header
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
        bool bIsOpen = false;
        if (ImGui::BeginTable("GridTable", 1, Flags))
        {
            ImGui::TableSetupColumn("##Header", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();

            ImGui::PushStyleColor(ImGuiCol_Header, 0xFF3A3A3A);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0xFF484848);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0xFF404040);
            ImGui::SetNextItemAllowOverlap();
            // Runtime rows show the live type name so renaming the type asset updates the row
            // immediately (the cached title is only refreshed on a details rebuild).
            const char* HeaderTitle = (bRuntime && RuntimeType != nullptr) ? RuntimeType->GetName().c_str() : Entry.Title.c_str();
            bIsOpen = ImGui::CollapsingHeader(HeaderTitle, ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, 0xFF1C1C1C);

            ImGui::PopStyleColor(3);

            if (!bIsRequired)
            {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 28.0f);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.25f, 0.25f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.2f, 0.2f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

                if (ImGui::SmallButton(LE_ICON_TRASH_CAN "##RemoveComponent"))
                {
                    if (bRuntime)
                    {
                        PendingRuntimeRemove = RuntimeType;
                    }
                    else
                    {
                        ComponentDestroyRequests.push(FComponentDestroyRequest{ Entry.ReflectedType, Entity });
                    }
                }

                ImGuiX::TextTooltip("{}", "Remove Component");

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
            }

            ImGui::EndTable();
        }

        ImGui::PopStyleVar(2);


        if (bIsOpen && Entry.Table != nullptr)
        {
            ImGui::Spacing();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.015f, 0.015f, 0.015f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));

            ImGui::Indent(8.0f);

            // Make this component's world resolvable to any PROPERTY(Entity) picker in the table.
            {
                FScopedEntityPropertyContext EntityContext(World);
                Entry.Table->DrawTree();
            }

            ImGui::Unindent(8.0f);

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);

            ImGui::Spacing();
        }

        ImGui::PopID();
    }

    void FSceneEditorTool::RemoveComponent(entt::entity Entity, const CStruct* ComponentType)
    {
        bool bWasRemoved = false;

        if (ComponentType == nullptr)
        {
            return;
        }

        ECS::Utils::ForEachComponent(GetSceneRegistry(), Entity, [&](void* Component, entt::basic_sparse_set<>& Set, const entt::meta_type& Type)
        {
            using namespace entt::literals;

            if (entt::meta_any ReturnValue = ECS::Utils::InvokeMetaFunc(Type, "static_struct"_hs))
            {
                CStruct* StructType = ReturnValue.cast<CStruct*>();

                if (StructType == ComponentType)
                {
                    Set.remove(Entity);
                    bWasRemoved = true;
                }
            }
        });


        if (bWasRemoved)
        {
            // Mark dirty; next DrawEntityEditor pass rebuilds PropertyTables. Avoids tearing down handles mid-draw.
            if (Entity == DetailsEntity)
            {
                bDetailsDirty = true;
            }
            MarkSceneDirty();
        }
        else
        {
            ImGuiX::Notifications::NotifyError("Failed to remove component: {0}", ComponentType->GetName().c_str());
        }
    }

    void FSceneEditorTool::ProcessComponentEditRequests()
    {
        if (!ComponentDestroyRequests.empty())
        {
            BeginTransaction();
            while (!ComponentDestroyRequests.empty())
            {
                FComponentDestroyRequest Request = ComponentDestroyRequests.front();
                ComponentDestroyRequests.pop();

                RemoveComponent(Request.EntityID, Request.Type);
            }
            EndTransaction("Remove Component");
        }
    }

    void FSceneEditorTool::OnPrePropertyChangeEvent(const FPropertyChangedEvent& Event)
    {
    }

    void FSceneEditorTool::OnPostPropertyChangeEvent(const FPropertyChangedEvent& Event)
    {
        using namespace entt::literals;

        // A reset-to-default (and some nested rows) can fire with no outer component type; there's
        // nothing to resolve/patch then, so bail before dereferencing it.
        if (Event.OuterType == nullptr)
        {
            return;
        }

        FEntityRegistry& Registry = GetSceneRegistry();
        entt::id_type TypeID = ECS::Utils::GetTypeID(Event.OuterType->GetName().c_str());
        entt::meta_type WantType = entt::resolve(TypeID);
        if (!WantType)
        {
            return;
        }

        // Multi-edit source: the focus entity is what the bound property table edited. Locate its
        // component instance so the change can be replicated onto the rest of the selection.
        void* FocusInstance = nullptr;
        if (Event.Property != nullptr && Registry.valid(DetailsEntity))
        {
            ECS::Utils::ForEachComponent(Registry, DetailsEntity, [&](void* Component, entt::basic_sparse_set<>&, const entt::meta_type& Type)
            {
                if (FocusInstance == nullptr && Type == WantType)
                {
                    FocusInstance = Component;
                }
            });
        }

        auto View = Registry.view<FSelectedInEditorComponent>();
        View.each([&](entt::entity Entity)
        {
            entt::meta_any Has = ECS::Utils::InvokeMetaFunc(TypeID, "has"_hs, entt::forward_as_meta(Registry), Entity);
            if (!Has || !Has.cast<bool>())
            {
                return;
            }

            // Copy the edited property from the focus instance onto this one (only uniform, editable
            // properties reach here; "(Multiple Values)" rows are non-editable), then patch to fire on_update.
            if (FocusInstance != nullptr && Entity != DetailsEntity)
            {
                void* DestInstance = nullptr;
                ECS::Utils::ForEachComponent(Registry, Entity, [&](void* Component, entt::basic_sparse_set<>&, const entt::meta_type& Type)
                {
                    if (DestInstance == nullptr && Type == WantType)
                    {
                        DestInstance = Component;
                    }
                });

                if (DestInstance != nullptr && DestInstance != FocusInstance)
                {
                    Event.Property->CopyCompleteValue_InContainer(DestInstance, FocusInstance);
                }
            }

            entt::meta_any Component = ECS::Utils::InvokeMetaFunc(TypeID, "get"_hs, entt::forward_as_meta(Registry), Entity);
            ECS::Utils::InvokeMetaFunc(TypeID, "patch"_hs, entt::forward_as_meta(Registry), Entity, entt::forward_as_meta(Component));
        });
    }

    void FSceneEditorTool::RebuildPropertyTables(entt::entity Entity)
    {
        using namespace entt::literals;

        PropertyTables.clear();

        // Track owning entity so the details panel can detect staleness; null on invalid input forces a rebuild next time.
        DetailsEntity = (Entity != entt::null && GetSceneRegistry().valid(Entity)) ? Entity : entt::null;
        bDetailsDirty = false;

        if (GetSceneRegistry().valid(Entity))
        {
            FEntityRegistry& Registry = GetSceneRegistry();

            // One intermediate row for both reflected and runtime components so they sort together.
            struct FPendingRow
            {
                void*                 Data = nullptr;
                CStruct*              Layout = nullptr;        // reflected CStruct, or runtime layout
                const CStruct*        ReflectedType = nullptr; // null for runtime
                CEntityComponentType* RuntimeType = nullptr;   // null for reflected
                FString               Title;
                TVector<void*>        OtherInstances;          // same component on the other selected entities (multi-edit)
            };

            // Multi-edit: when more than one entity is selected, the details panel shows the intersection
            // of components and compares each property's value across the whole selection.
            const bool bMultiSelect = SelectedEntities.size() > 1 && IsEntitySelected(Entity);

            TVector<entt::entity> OtherTargets;
            TVector<THashMap<const CStruct*, void*>> OtherReflected;
            TVector<THashMap<uint32, void*>>         OtherRuntime;
            if (bMultiSelect)
            {
                for (entt::entity Selected : SelectedEntities)
                {
                    if (Selected == Entity || !Registry.valid(Selected))
                    {
                        continue;
                    }
                    OtherTargets.push_back(Selected);

                    THashMap<const CStruct*, void*> ReflectedMap;
                    ECS::Utils::ForEachComponent(Registry, Selected, [&](void* Component, entt::basic_sparse_set<>&, const entt::meta_type& Type)
                    {
                        entt::meta_any Any = ECS::Utils::InvokeMetaFunc(Type, "static_struct"_hs);
                        if (Any)
                        {
                            ReflectedMap[Any.cast<CStruct*>()] = Component;
                        }
                    });
                    OtherReflected.push_back(Move(ReflectedMap));

                    THashMap<uint32, void*> RuntimeMap;
                    ECS::Utils::ForEachRuntimeComponent(Registry, Selected, [&](CEntityComponentType* Type, CStruct*, void* Data)
                    {
                        if (Type != nullptr)
                        {
                            RuntimeMap[Type->GetStorageId()] = Data;
                        }
                    });
                    OtherRuntime.push_back(Move(RuntimeMap));
                }
            }

            TVector<FPendingRow> Pending;

            ECS::Utils::ForEachComponent(Registry, Entity, [&](void* Component, entt::basic_sparse_set<>& Set, const entt::meta_type& Type)
            {
                entt::meta_any Any = ECS::Utils::InvokeMetaFunc(Type, "static_struct"_hs);
                if (!Any)
                {
                    return;
                }

                CStruct* Struct = Any.cast<CStruct*>();

                // Components that never show in the details panel (tags, prefab bookkeeping).
                if (IsComponentHiddenInDetails(Struct))
                {
                    return;
                }

                // Intersection: drop components not present on every selected entity, gathering the
                // matching instance pointers for the multi-value compare.
                TVector<void*> Others;
                for (const THashMap<const CStruct*, void*>& Map : OtherReflected)
                {
                    auto It = Map.find(Struct);
                    if (It == Map.end())
                    {
                        return;
                    }
                    Others.push_back(It->second);
                }

                Pending.push_back({ Component, Struct, Struct, nullptr, Struct->MakeDisplayName().c_str(), Move(Others) });
            });

            ECS::Utils::ForEachRuntimeComponent(Registry, Entity,
                [&](CEntityComponentType* Type, CStruct* Layout, void* Data)
            {
                if (Type == nullptr)
                {
                    return;
                }

                TVector<void*> Others;
                for (const THashMap<uint32, void*>& Map : OtherRuntime)
                {
                    auto It = Map.find(Type->GetStorageId());
                    if (It == Map.end())
                    {
                        return;
                    }
                    Others.push_back(It->second);
                }

                Pending.push_back({ Data, Layout, nullptr, Type, Type->GetName().ToString(), Move(Others) });
            });

            eastl::sort(Pending.begin(), Pending.end(), [&](const FPendingRow& LHS, const FPendingRow& RHS)
            {
                // Name first, Transform second, everything else (incl. runtime) alphabetical.
                auto Priority = [](const FPendingRow& Row) -> uint32
                {
                    if (Row.ReflectedType == SNameComponent::StaticStruct())      { return 0; }
                    if (Row.ReflectedType == STransformComponent::StaticStruct()) { return 1; }
                    return 2;
                };

                const uint32 APriority = Priority(LHS);
                const uint32 BPriority = Priority(RHS);
                if (APriority != BPriority)
                {
                    return APriority < BPriority;
                }
                return LHS.Title < RHS.Title;
            });

            for (const FPendingRow& Row : Pending)
            {
                FComponentTableEntry Entry;
                Entry.ReflectedType = Row.ReflectedType;
                Entry.bRuntime      = (Row.RuntimeType != nullptr);
                Entry.RuntimeStorageId = (Row.RuntimeType != nullptr) ? Row.RuntimeType->GetStorageId() : 0;
                Entry.Title         = Row.Title;

                if (Row.RuntimeType != nullptr)
                {
                    // Runtime row: a field-less type has no value buffer, so leave Table null (the
                    // header still draws, just with no body).
                    if (Row.Data != nullptr && Row.Layout != nullptr)
                    {
                        Entry.Table = MakeUnique<FPropertyTable>(Row.Data, Row.Layout);

                        void* FocusData = Row.Data;
                        TVector<void*> Others = Row.OtherInstances;
                        Entry.Table->SetPostEditCallback([this, FocusData, Others](const FPropertyChangedEvent& Event)
                        {
                            // Multi-edit: copy the edited property to every other selected instance.
                            if (Event.Property != nullptr)
                            {
                                for (void* Other : Others)
                                {
                                    Event.Property->CopyCompleteValue_InContainer(Other, FocusData);
                                }
                            }
                            // Values live in the storage and serialize with the scene; just mark dirty.
                            MarkSceneDirty();
                        });
                        if (!Others.empty())
                        {
                            Entry.Table->ChangeEventCallbacks.IsMultiValueFn = [FocusData, Others](FProperty* Property) -> bool
                            {
                                for (void* Other : Others)
                                {
                                    if (!Property->Identical_InContainer(FocusData, Other))
                                    {
                                        return true;
                                    }
                                }
                                return false;
                            };
                        }
                        Entry.Table->MarkDirty();
                        Entry.BoundLayout = Row.Layout;
                        Entry.BoundData   = Row.Data;
                    }
                }
                else
                {
                    Entry.Table = MakeUnique<FPropertyTable>(Row.Data, Row.Layout);
                    Entry.Table->SetPreEditCallback([&](const FPropertyChangedEvent& Event)    { OnPrePropertyChangeEvent(Event); });
                    Entry.Table->SetPostEditCallback([&](const FPropertyChangedEvent& Event)   { OnPostPropertyChangeEvent(Event); MarkSceneDirty(); });
                    Entry.Table->SetStartEditCallback([&](const FPropertyChangedEvent& Event)  { BeginTransaction(); });
                    Entry.Table->SetFinishEditCallback([&](const FPropertyChangedEvent& Event) { EndTransaction(Event.PropertyName); });

                    // Multi-edit value compare; propagation is handled in OnPostPropertyChangeEvent, which
                    // also patches each entity so on_update (physics rebuild, etc.) fires.
                    if (!Row.OtherInstances.empty())
                    {
                        void* FocusData = Row.Data;
                        TVector<void*> Others = Row.OtherInstances;
                        Entry.Table->ChangeEventCallbacks.IsMultiValueFn = [FocusData, Others](FProperty* Property) -> bool
                        {
                            for (void* Other : Others)
                            {
                                if (!Property->Identical_InContainer(FocusData, Other))
                                {
                                    return true;
                                }
                            }
                            return false;
                        };
                    }
                    Entry.Table->MarkDirty();
                }

                PropertyTables.emplace_back(Move(Entry));
            }
        }
    }

    bool FSceneEditorTool::IsOutlinerEntityVisible(entt::entity Entity) const
    {
        FEntityRegistry& Registry = GetSceneRegistry();
        return Registry.valid(Entity)
            && Registry.all_of<SNameComponent>(Entity)
            && !Registry.any_of<FHideInSceneOutliner>(Entity);
    }

    void FSceneEditorTool::SyncOutlinerRowSelection(entt::entity Entity, bool bSelected)
    {
        auto It = EntityToTreeNode.find(Entity);
        if (It != EntityToTreeNode.end() && OutlinerListView.IsValid(It->second))
        {
            OutlinerListView.Get<FTreeNodeState>(It->second).bSelected = bSelected;
        }
    }

    void FSceneEditorTool::RebuildSceneOutliner(FTreeListView& Tree)
    {
        // Incremental tree: rebuild just resets the map and re-adds roots. Children fill lazily on expand.
        EntityToTreeNode.clear();
        PendingOutlinerAdds.clear();

        FEntityRegistry& Registry = GetSceneRegistry();
        TVector<entt::entity> Roots;
        auto View = Registry.view<SNameComponent>(entt::exclude<FHideInSceneOutliner>);
        for (entt::entity Entity : View)
        {
            if (!IsOutlinerEntityVisible(Entity))
            {
                continue;
            }
            if (FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity))
            {
                if (Rel->Parent != entt::null)
                {
                    continue;
                }
            }

            Roots.push_back(Entity);
        }

        eastl::sort(Roots.begin(), Roots.end(), [&](entt::entity LHS, entt::entity RHS)
        {
            const FFixedString A = View.get<SNameComponent>(LHS).Name.c_str();
            const FFixedString B = View.get<SNameComponent>(RHS).Name.c_str();

            if (A != B)
            {
                return A < B;
            }
            return LHS < RHS;
        });

        for (entt::entity Root : Roots)
        {
            AddEntityToOutliner(Root);
        }
    }

    FTreeNodeID FSceneEditorTool::AddEntityToOutliner(entt::entity Entity)
    {
        FEntityRegistry& Registry = GetSceneRegistry();
        if (!IsOutlinerEntityVisible(Entity))
        {
            return InvalidTreeNode;
        }

        auto Existing = EntityToTreeNode.find(Entity);
        if (Existing != EntityToTreeNode.end())
        {
            return Existing->second;
        }

        // Attach under parent if it's already in the tree.
        FTreeNodeID ParentNode = InvalidTreeNode;
        if (FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity))
        {
            if (Rel->Parent != entt::null)
            {
                auto ParentIt = EntityToTreeNode.find(Rel->Parent);
                if (ParentIt != EntityToTreeNode.end())
                {
                    ParentNode = ParentIt->second;
                }
                else
                {
                    // Parent not in tree yet; defer to avoid attaching as root then relocating.
                    return InvalidTreeNode;
                }
            }
        }

        SNameComponent& NameComponent = Registry.get<SNameComponent>(Entity);
        const SPrefabInstanceComponent* PrefabInstance = Registry.try_get<SPrefabInstanceComponent>(Entity);
        const bool bIsPrefabInstanceRoot = PrefabInstance != nullptr && PrefabInstance->bIsRoot;
        const bool bIsLockedPrefabChild = PrefabInstance != nullptr && !PrefabInstance->bIsRoot;

        FFixedString Name;
        if (bIsPrefabInstanceRoot)
        {
            Name.append(LE_ICON_PACKAGE_VARIANT_CLOSED).append(" ");
        }
        else if (bIsLockedPrefabChild)
        {
            Name.append(LE_ICON_LOCK).append(" ");
        }
        else
        {
            Name.append(LE_ICON_CUBE).append(" ");
        }
        Name.append(NameComponent.Name.c_str()).append_convert(FString(" - (" + eastl::to_string(entt::to_integral(Entity)) + ")"));

        FTreeNodeID ItemEntity = OutlinerListView.CreateNode(ParentNode, FStringView(Name.data(), Name.length()));
        EntityToTreeNode[Entity] = ItemEntity;

        FTreeNodeDisplay& Display = OutlinerListView.Get<FTreeNodeDisplay>(ItemEntity);

        // Styled hover tooltip: title (type icon + name), a dim subtitle, then component chips.
        const char* TypeIcon = bIsPrefabInstanceRoot ? LE_ICON_PACKAGE_VARIANT_CLOSED
                             : bIsLockedPrefabChild   ? LE_ICON_LOCK
                                                      : LE_ICON_CUBE;
        Display.TooltipTitle = FString(TypeIcon) + " " + NameComponent.Name.c_str();

        if (bIsLockedPrefabChild)
        {
            Display.TooltipSubtitle = FString("Prefab child #" + eastl::to_string(entt::to_integral(Entity)) + " — hierarchy locked");
        }
        else if (bIsPrefabInstanceRoot)
        {
            Display.TooltipSubtitle = FString("Prefab instance #" + eastl::to_string(entt::to_integral(Entity)));
        }
        else
        {
            Display.TooltipSubtitle = FString("Entity #" + eastl::to_string(entt::to_integral(Entity)));
        }

        // Components shown on hover only, they no longer clutter the outliner tree.
        Display.TooltipChipHeader = "COMPONENTS";
        Display.TooltipChips.clear();
        ECS::Utils::ForEachComponent(Registry, Entity, [&](void*, const entt::basic_sparse_set<>& /*Set*/, entt::meta_type Meta)
        {
            using namespace entt::literals;
            FString Chip = LE_ICON_PUZZLE " ";
            if (entt::meta_any Resolved = ECS::Utils::InvokeMetaFunc(Meta, "static_struct"_hs))
            {
                if (CStruct* StructType = Resolved.cast<CStruct*>())
                {
                    Chip += StructType->MakeDisplayName().c_str();
                    Display.TooltipChips.emplace_back(eastl::move(Chip));
                    return;
                }
            }
            Chip += Meta.name();
            Display.TooltipChips.emplace_back(eastl::move(Chip));
        });
        if (Display.TooltipChips.empty())
        {
            Display.TooltipChips.emplace_back("(none)");
        }

        Display.bShowDisabledIcon = true;
        Display.bAllowRenaming = !bIsLockedPrefabChild;

        // Per-entity script enable toggle: only shown when the entity carries a script.
        if (Registry.any_of<SScriptComponent>(Entity))
        {
            Display.bShowSecondaryIcon = true;
            Display.SecondaryIconOn    = LE_ICON_SCRIPT_TEXT;
            Display.SecondaryIconOff   = LE_ICON_SCRIPT_TEXT_OUTLINE;
            Display.SecondaryTooltip   = "Toggle this entity's script on/off (the entity stays active).";
        }

        OutlinerListView.EmplaceUserData<FEntityListViewItemData>(ItemEntity).Entity = Entity;

        if (Registry.any_of<FSelectedInEditorComponent>(Entity))
        {
            OutlinerListView.Get<FTreeNodeState>(ItemEntity).bSelected = true;
        }

        if (Registry.any_of<SDisabledTag>(Entity))
        {
            OutlinerListView.Get<FTreeNodeState>(ItemEntity).bDisabled = true;
        }

        if (Registry.any_of<SScriptDisabledTag>(Entity))
        {
            OutlinerListView.Get<FTreeNodeState>(ItemEntity).bSecondaryToggled = true;
        }

        // Only show an expander if the entity actually has child entities; lazy expansion populates them.
        const FRelationshipComponent* RelForChildren = Registry.try_get<FRelationshipComponent>(Entity);
        const bool bHasChildren = RelForChildren != nullptr && RelForChildren->Children > 0;
        OutlinerListView.MarkHasLazyChildren(ItemEntity, bHasChildren);

        return ItemEntity;
    }

    void FSceneEditorTool::RemoveEntityFromOutliner(entt::entity Entity)
    {
        auto It = EntityToTreeNode.find(Entity);
        if (It == EntityToTreeNode.end())
        {
            return;
        }

        // RemoveNode tears down the subtree; walk hierarchy first to clear EntityToTreeNode for descendants.
        FEntityRegistry& Registry = GetSceneRegistry();
        if (Registry.valid(Entity))
        {
            ECS::Utils::ForEachChild(Registry, Entity, [&](entt::entity Child)
            {
                RemoveEntityFromOutliner(Child);
            });
        }

        OutlinerListView.RemoveNode(It->second);
        EntityToTreeNode.erase(It);
    }

    void FSceneEditorTool::ReparentEntityInOutliner(entt::entity Entity)
    {
        // Remember the old tree parent before the move; it may lose its last child here.
        entt::entity OldParent = entt::null;
        if (auto It = EntityToTreeNode.find(Entity); It != EntityToTreeNode.end())
        {
            FTreeNodeID ParentNode = OutlinerListView.GetParentNode(It->second);
            if (ParentNode.IsValid())
            {
                OldParent = OutlinerListView.Get<FEntityListViewItemData>(ParentNode).Entity;
            }
        }

        // Drop and re-add the row; new parent's lazy children rebuild on next expand.
        RemoveEntityFromOutliner(Entity);
        AddEntityToOutliner(Entity);

        // The old parent's expander is stale if that was its only child.
        RefreshOutlinerExpander(OldParent);
    }

    void FSceneEditorTool::RefreshOutlinerExpander(entt::entity Entity)
    {
        if (Entity == entt::null)
        {
            return;
        }
        auto It = EntityToTreeNode.find(Entity);
        if (It == EntityToTreeNode.end())
        {
            return;
        }

        FEntityRegistry& Registry = GetSceneRegistry();
        const FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity);
        const bool bHasChildren = Rel != nullptr && Rel->Children > 0;
        OutlinerListView.MarkHasLazyChildren(It->second, bHasChildren);
    }

    void FSceneEditorTool::BuildEntityChildren(FTreeListView& Tree, FTreeNodeID Item)
    {
        FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
        FEntityRegistry& Registry = GetSceneRegistry();
        if (!Registry.valid(Data.Entity))
        {
            return;
        }

        // Child entity rows: skip filtered-out ones and ones already present (on_construct race).
        ECS::Utils::ForEachChild(Registry, Data.Entity, [&](entt::entity Child)
        {
            if (!IsOutlinerEntityVisible(Child))
            {
                return;
            }
            if (EntityToTreeNode.find(Child) != EntityToTreeNode.end())
            {
                return;
            }

            AddEntityToOutliner(Child);
        });
    }

    void FSceneEditorTool::OnOutlinerEntityConstructed(entt::registry& Registry, entt::entity Entity)
    {
        if (Registry.any_of<FHideInSceneOutliner>(Entity))
        {
            return;
        }
        // Defer to next flush; FRelationshipComponent may not be set yet.
        PendingOutlinerAdds.push_back(Entity);
    }

    void FSceneEditorTool::OnOutlinerEntityDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        (void)Registry;
        RemoveEntityFromOutliner(Entity);
        PendingOutlinerAdds.erase(eastl::remove(PendingOutlinerAdds.begin(), PendingOutlinerAdds.end(), Entity), PendingOutlinerAdds.end());
    }

    void FSceneEditorTool::FlushOutlinerPending()
    {
        if (PendingOutlinerAdds.empty())
        {
            return;
        }

        // Iterate by index; AddEntityToOutliner could grow the queue.
        for (int32 i = 0; i < static_cast<int32>(PendingOutlinerAdds.size()); ++i)
        {
            AddEntityToOutliner(PendingOutlinerAdds[i]);
        }
        PendingOutlinerAdds.clear();
    }

    FTransform FSceneEditorTool::GetNewEntitySpawnTransform() const
    {
        return GetCameraSpawnTransform();
    }

    void FSceneEditorTool::CreateEntity()
    {
        BeginTransaction();
        entt::entity NewEntity = World->ConstructEntity("Entity", GetNewEntitySpawnTransform());
        if (NewEntity != entt::null)
        {
            OnEntityCreatedInScene(NewEntity);
        }
        EndTransaction("New Entity");

        if (NewEntity != entt::null)
        {
            SetSingleSelectedEntity(NewEntity);
            MarkSceneDirty();
        }
    }

    void FSceneEditorTool::CreateEntityWithComponent(const CStruct* Component)
    {
        using namespace entt::literals;

        entt::meta_type MetaType = entt::resolve(entt::hashed_string(Component->GetName().c_str()));

        BeginTransaction();
        entt::entity CreatedEntity = World->ConstructEntity(Component->MakeDisplayName(), GetNewEntitySpawnTransform());
        if (CreatedEntity != entt::null)
        {
            ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs, entt::forward_as_meta(GetSceneRegistry()), CreatedEntity, entt::forward_as_meta(entt::meta_any{}));
            OnEntityCreatedInScene(CreatedEntity);
        }
        EndTransaction("New Entity");

        if (CreatedEntity != entt::null)
        {
            SetSingleSelectedEntity(CreatedEntity);
            MarkSceneDirty();
        }
    }

    void FSceneEditorTool::CreatePrimitiveEntity(CStaticMesh* PrimitiveMesh, const char* DisplayName)
    {
        if (PrimitiveMesh == nullptr)
        {
            return;
        }

        BeginTransaction();
        entt::entity CreatedEntity = World->ConstructEntity(DisplayName, GetNewEntitySpawnTransform());
        if (CreatedEntity != entt::null)
        {
            GetSceneRegistry().emplace<SStaticMeshComponent>(CreatedEntity).StaticMesh = PrimitiveMesh;
            OnEntityCreatedInScene(CreatedEntity);
        }
        EndTransaction("New Primitive");

        if (CreatedEntity != entt::null)
        {
            SetSingleSelectedEntity(CreatedEntity);
            MarkSceneDirty();
        }
    }

    TVector<entt::entity> FSceneEditorTool::GetComponentEditTargets(entt::entity Entity)
    {
        TVector<entt::entity> Targets;

        FEntityRegistry& Registry = GetSceneRegistry();
        if (!Registry.valid(Entity))
        {
            return Targets;
        }

        // Apply to the whole selection when more than one entity is selected; otherwise just the focus entity.
        if (SelectedEntities.size() > 1 && IsEntitySelected(Entity))
        {
            Targets.reserve(SelectedEntities.size());
            for (entt::entity Selected : SelectedEntities)
            {
                if (Registry.valid(Selected))
                {
                    Targets.push_back(Selected);
                }
            }
        }
        else
        {
            Targets.push_back(Entity);
        }

        return Targets;
    }

    void FSceneEditorTool::ApplyAddComponentToTargets(const TVector<entt::entity>& Targets, entt::meta_type PickedMetaType, CEntityComponentType* PickedRuntime)
    {
        using namespace entt::literals;

        if (Targets.empty())
        {
            return;
        }

        FEntityRegistry& Registry = GetSceneRegistry();

        BeginTransaction();
        for (entt::entity Target : Targets)
        {
            // AddRuntimeComponent is idempotent; the reflected "emplace" uses emplace_or_replace for data
            // components (would reset existing data) and plain emplace for tags (would assert if present),
            // so skip targets that already hold the component.
            if (PickedRuntime != nullptr)
            {
                ECS::Utils::AddRuntimeComponent(Registry, Target, PickedRuntime);
            }
            else if (!ECS::Utils::HasComponent(Registry, Target, PickedMetaType))
            {
                ECS::Utils::InvokeMetaFunc(PickedMetaType, "emplace"_hs, entt::forward_as_meta(Registry), Target, entt::forward_as_meta(entt::meta_any{}));
            }
        }
        EndTransaction(PickedRuntime != nullptr ? "Add Runtime Component" : "Add Component");

        MarkSceneDirty();
        OutlinerListView.MarkTreeDirty();
        bDetailsDirty = true;
    }

    bool FSceneEditorTool::DrawAddableComponentList(const ImGuiTextFilter& Filter, entt::meta_type& OutMetaType, CStruct*& OutStruct, CEntityComponentType*& OutRuntimeType)
    {
        OutRuntimeType = nullptr;

        struct FComponentEntry
        {
            entt::meta_type MetaType;
            CStruct*        Struct = nullptr;   // reflected component
            // Data-authored type: listed straight from the asset registry (so it shows whether or
            // not it is loaded) and loaded on pick, exactly like every other asset reference.
            bool            bRuntime = false;
            FGuid           RuntimeGuid;
            FString         RuntimeName;
        };

        struct FComponentCategory
        {
            FString                  Name;
            TVector<FComponentEntry> Entries;
        };

        TVector<FComponentCategory> Categories;
        auto FindOrAddCategory = [&Categories](const FString& Name) -> FComponentCategory&
        {
            for (FComponentCategory& Cat : Categories)
            {
                if (Cat.Name == Name)
                {
                    return Cat;
                }
            }
            FComponentCategory& Added = Categories.emplace_back();
            Added.Name = Name;
            return Added;
        };

        static const FString DefaultCategoryName = "General";

        for(auto &&[ID, MetaType]: entt::resolve())
        {
            ECS::ETraits Traits = MetaType.traits<ECS::ETraits>();
            if (!EnumHasAllFlags(Traits, ECS::ETraits::Component))
            {
                continue;
            }

            using namespace entt::literals;
            entt::meta_any Any = ECS::Utils::InvokeMetaFunc(MetaType, "static_struct"_hs);
            CStruct* Struct = Any.cast<CStruct*>();
            ASSERT(Struct);

            if (Struct->HasMeta("HideInComponentList"))
            {
                continue;
            }

            FFixedString DisplayName = Struct->MakeDisplayName();
            if (!Filter.PassFilter(DisplayName.c_str()))
            {
                continue;
            }

            FString CategoryName = Struct->HasMeta("Category")
                ? Struct->GetMeta("Category")
                : DefaultCategoryName;

            FComponentEntry NewEntry;
            NewEntry.MetaType = MetaType;
            NewEntry.Struct   = Struct;
            FindOrAddCategory(CategoryName).Entries.push_back(NewEntry);
        }

        // Runtime (data-authored) component types appear under "Data", enumerated from the asset
        // registry (not GObjectArray) so unloaded ones still show; the pick loads on demand.
        TVector<FAssetData*> RuntimeTypes = FAssetRegistry::Get().FindByPredicate([](const FAssetData& Data)
        {
            CClass* DataClass = FindObject<CClass>(Data.AssetClass);
            return DataClass != nullptr && DataClass->IsChildOf(CEntityComponentType::StaticClass());
        });

        for (const FAssetData* Data : RuntimeTypes)
        {
            if (!Filter.PassFilter(Data->AssetName.c_str()))
            {
                continue;
            }

            FComponentEntry NewEntry;
            NewEntry.bRuntime    = true;
            NewEntry.RuntimeGuid = Data->AssetGUID;
            NewEntry.RuntimeName = Data->AssetName.ToString();
            FindOrAddCategory("Data").Entries.push_back(NewEntry);
        }

        eastl::sort(Categories.begin(), Categories.end(), [](const FComponentCategory& LHS, const FComponentCategory& RHS)
        {
            // Push "General" to the bottom so categorized buckets surface first.
            const bool bLhsGeneral = (LHS.Name == DefaultCategoryName);
            const bool bRhsGeneral = (RHS.Name == DefaultCategoryName);
            if (bLhsGeneral != bRhsGeneral)
            {
                return !bLhsGeneral;
            }
            return LHS.Name < RHS.Name;
        });

        bool bPicked = false;
        for (FComponentCategory& Category : Categories)
        {
            auto EntryName = [](const FComponentEntry& E) -> FString
            {
                return E.bRuntime ? E.RuntimeName : E.Struct->GetName().ToString();
            };
            eastl::sort(Category.Entries.begin(), Category.Entries.end(), [&](const FComponentEntry& LHS, const FComponentEntry& RHS)
            {
                return EntryName(LHS) < EntryName(RHS);
            });

            ImGui::PushID(Category.Name.c_str());

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
            FFixedString Header;
            Header.append(LE_ICON_FOLDER " ");
            Header.append(Category.Name.c_str());
            ImGui::TextUnformatted(Header.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();

            for (const FComponentEntry& Entry : Category.Entries)
            {
                // Stable per-item ID across frames (the entry list is rebuilt every frame, so its
                // address is not stable -- an unstable ID breaks click press/release matching).
                if (Entry.bRuntime)
                {
                    ImGui::PushID(static_cast<int>(Entry.RuntimeGuid.Hash()));
                }
                else
                {
                    ImGui::PushID((void*)Entry.Struct);
                }

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.21f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.45f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.3f, 0.4f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));

                const float ButtonWidth = ImGui::GetContentRegionAvail().x;

                FFixedString DisplayName = Entry.bRuntime ? FFixedString(Entry.RuntimeName.c_str()) : Entry.Struct->MakeDisplayName();
                if (ImGui::Button(DisplayName.c_str(), ImVec2(ButtonWidth, 0.0f)))
                {
                    if (Entry.bRuntime)
                    {
                        // Load on pick (returns the cached instance if already loaded).
                        OutRuntimeType = LoadObject<CEntityComponentType>(Entry.RuntimeGuid);
                    }
                    else
                    {
                        OutMetaType = Entry.MetaType;
                        OutStruct   = Entry.Struct;
                    }
                    bPicked = true;
                }

                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(3);

                ImGui::PopID();
                ImGui::Spacing();
            }

            ImGui::PopID();
            ImGui::Spacing();
        }

        return bPicked;
    }

    void FSceneEditorTool::AddEntityToCopies(entt::entity Entity)
    {
        GetSceneRegistry().emplace_or_replace<FCopiedTag>(Entity);
    }

    void FSceneEditorTool::RemoveEntityFromCopies(entt::entity Entity)
    {
        GetSceneRegistry().remove<FCopiedTag>(Entity);
    }

    void FSceneEditorTool::ClearCopies() const
    {
        GetSceneRegistry().clear<FCopiedTag>();
    }

    void FSceneEditorTool::CopyEntity(entt::entity& To, entt::entity From)
    {
        World->DuplicateEntity(To, From, &EditorEntityUtils::DefaultDuplicateFilter);
    }

    void FSceneEditorTool::CycleGuizmoOp()
    {
        EditorEntityUtils::CycleGizmoOp(GuizmoOp);
    }

    void FSceneEditorTool::DrawViewportToolbar(const FUpdateContext& UpdateContext)
    {
        // While the game viewport has input focus (an immersive PIE session), hide the whole toolbar so it
        // doesn't block the view. Whenever the editor is focused -- including during play, once you release the
        // game's mouse capture -- show the FULL toolbar so every control (view modes, debug toggles, etc.) stays
        // reachable instead of collapsing to just the Stop button.
        if (FInputViewportRegistry::Get().IsGameInputFocused())
        {
            return;
        }

        const float Scale = ImGuiX::GetUIScale();
        const float Padding = 8.0f * Scale;
        const float ItemSpacing = 6.0f * Scale;
        const float ButtonSize = (IsViewportPlaying() ? 24.0f : 32.0f) * Scale;
        constexpr float CornerRounding = 8.0f;

        ImVec2 Pos = ImGui::GetWindowPos();
        ImGui::SetNextWindowPos(Pos + ImVec2(Padding, Padding));
        ImGui::SetNextWindowBgAlpha(IsViewportPlaying() ? 1.0f : 0.85f);

        ImGuiWindowFlags WindowFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Padding, Padding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CornerRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ItemSpacing, ItemSpacing));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

        if (ImGui::Begin("##ViewportToolbar", nullptr, WindowFlags))
        {
            ImGui::BeginGroup();

            // Leading play/simulate controls (world only); draws its own trailing separator.
            DrawViewportToolbarPlayControls(ButtonSize);

            // The rest of the bar is shown whenever the editor is focused, even mid-play, so debug/view
            // controls stay reachable (the game-focused case returned early above).
            DrawCameraControls(ButtonSize);

            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            DrawViewportOptions(ButtonSize);

            // Trailing editor-mode selector + active-mode toolbar (world only).
            DrawViewportToolbarModeSelector(ButtonSize);

            ImGui::EndGroup();
        }
        ImGui::End();

        ImGui::PopStyleVar(4);
    }

    void FSceneEditorTool::DrawCameraControls(float ButtonSize)
    {
        const ImVec2 BtnSize = ImVec2(ButtonSize, ButtonSize);
        float Speed = CameraState.Speed;

        if (ImGuiX::IconButton(LE_ICON_CAMERA, "##Camera", 0xFFFFFFFF, BtnSize))
        {
            ImGui::OpenPopup("CameraSettings");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Camera Speed: %.1fx", Speed);
        }

        if (ImGui::BeginPopup("CameraSettings", ImGuiWindowFlags_NoMove))
        {
            STransformComponent& CameraTransform = GetSceneRegistry().get<STransformComponent>(EditorEntity);

            ImGui::SeparatorText(LE_ICON_VIDEO " Camera Settings");

            ImGui::Text("Movement Speed");
            if (ImGui::SliderFloat("##Speed", &Speed, 0.1f, 100.0f, "%.1fx"))
            {
                CameraState.Speed = Speed;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##Speed"))
            {
                Speed = 1.0f;
                CameraState.Speed = 1.0f;
            }

            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
            ImGui::TextUnformatted(LE_ICON_AXIS_ARROW);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGuiX::TextTooltip("Translation (Location)");
            ImGui::DragFloat3("T", Math::ValuePtr(CameraTransform.WorldTransform.Location), 0.01f);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.7f, 1.0f));
            ImGui::TextUnformatted(LE_ICON_ROTATE_360);
            ImGui::PopStyleColor();
            ImGuiX::TextTooltip("Rotation (Euler Angles)");
            ImGui::SameLine();
            FVector3 EulerRotation = CameraTransform.GetRotationAsEuler();
            if (ImGui::DragFloat3("R", Math::ValuePtr(EulerRotation), 0.01f))
            {
                CameraTransform.SetRotationFromEuler(EulerRotation);
            }

            ImGui::Separator();
            if (ImGui::Button("Reset Position", ImVec2(-1, 0)))
            {
                GetSceneRegistry().get<STransformComponent>(EditorEntity).SetLocation(FVector3(0.0f));
            }
            if (ImGui::Button("Reset Rotation", ImVec2(-1, 0)))
            {
                GetSceneRegistry().get<STransformComponent>(EditorEntity).SetRotation(FQuat(1.0f, 0.0f, 0.0f, 0.0f));
            }
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(-1, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if (ImGuiX::IconButton(LE_ICON_CROSSHAIRS, "##FocusSelection", 0xFFFFFFFF, BtnSize))
        {
            FocusViewportToEntity(GetLastSelectedEntity());
        }
        ImGuiX::TextTooltip("Focus on Selection (F)");
    }

    void FSceneEditorTool::DrawSnapSettingsPopup()
    {
        ImGui::Text("Snap Settings");
        ImGuiX::HelpMarker(
            "Constrains gizmo drags to fixed steps. Translate = world units. Rotate = degrees. "
            "Scale = multiplicative factor. Toggle quickly with the Snap button on the toolbar.");
        ImGui::Separator();

        if (ImGui::Checkbox("Enable Snap", &bGuizmoSnapEnabled))
        {
            PersistGizmoSettings();
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.4f, 0.6f, 0.3f));
        bool bAnySettingDirty = false;

        if (ImGui::CollapsingHeader("Translation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("Translate");
            ImGui::Indent();
            ImGui::BeginDisabled(!bGuizmoSnapEnabled);
            ImGui::Text("Presets:"); ImGui::SameLine();
            if (ImGui::Button("0.1")) { GuizmoSnapTranslate = 0.1f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("1.0")) { GuizmoSnapTranslate = 1.0f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("5.0")) { GuizmoSnapTranslate = 5.0f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("10"))  { GuizmoSnapTranslate = 10.0f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("50"))  { GuizmoSnapTranslate = 50.0f; bAnySettingDirty = true; }
            if (ImGui::DragFloat("Value##Translation", &GuizmoSnapTranslate, 0.1f, 0.01f, 1000.0f, "%.2f units")) { bAnySettingDirty = true; }
            ImGui::EndDisabled();
            ImGui::Unindent();
            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("Rotation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("Rotate");
            ImGui::Indent();
            ImGui::BeginDisabled(!bGuizmoSnapEnabled);
            ImGui::Text("Presets:"); ImGui::SameLine();
            if (ImGui::Button("1 " LE_ICON_ANGLE_ACUTE))  { GuizmoSnapRotate = 1.0f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("5 " LE_ICON_ANGLE_ACUTE))  { GuizmoSnapRotate = 5.0f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("15 " LE_ICON_ANGLE_ACUTE)) { GuizmoSnapRotate = 15.0f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("45 " LE_ICON_ANGLE_ACUTE)) { GuizmoSnapRotate = 45.0f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("90 " LE_ICON_ANGLE_ACUTE)) { GuizmoSnapRotate = 90.0f; bAnySettingDirty = true; }
            if (ImGui::DragFloat("Value##Rotation", &GuizmoSnapRotate, 0.5f, 0.1f, 180.0f, "%.1f " LE_ICON_ANGLE_ACUTE)) { bAnySettingDirty = true; }
            ImGui::EndDisabled();
            ImGui::Unindent();
            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader("Scale", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("Scale");
            ImGui::Indent();
            ImGui::BeginDisabled(!bGuizmoSnapEnabled);
            ImGui::Text("Presets:"); ImGui::SameLine();
            if (ImGui::Button("0.1"))  { GuizmoSnapScale = 0.1f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("0.25")) { GuizmoSnapScale = 0.25f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("0.5"))  { GuizmoSnapScale = 0.5f; bAnySettingDirty = true; } ImGui::SameLine();
            if (ImGui::Button("1.0"))  { GuizmoSnapScale = 1.0f; bAnySettingDirty = true; }
            if (ImGui::DragFloat("Value##Scale", &GuizmoSnapScale, 0.01f, 0.01f, 10.0f, "%.2f")) { bAnySettingDirty = true; }
            ImGui::EndDisabled();
            ImGui::Unindent();
            ImGui::PopID();
        }

        if (bAnySettingDirty)
        {
            PersistGizmoSettings();
        }

        ImGui::PopStyleColor();
    }

    void FSceneEditorTool::DrawViewportOptions(float ButtonSize)
    {
        const ImVec2 BtnSize = ImVec2(ButtonSize, ButtonSize);

        ImColor IconColor = bWorldGridEnabled ? ImVec4(0.2f, 0.6f, 1.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        if (ImGuiX::IconButton(LE_ICON_GRID, "##GridToggle", IconColor, BtnSize))
        {
            bWorldGridEnabled = !bWorldGridEnabled;
        }
        ImGuiX::TextTooltip("Toggle Grid");

        ImGui::SameLine();

        const char* Icon = nullptr;
        switch (GuizmoOp)
        {
        case ImGuizmo::OPERATION::TRANSLATE: Icon = LE_ICON_AXIS_ARROW; break;
        case ImGuizmo::OPERATION::ROTATE:    Icon = LE_ICON_ROTATE_360; break;
        case ImGuizmo::OPERATION::SCALE:     Icon = LE_ICON_ARROW_TOP_RIGHT_BOTTOM_LEFT; break;
        }

        if (ImGuiX::IconButton(Icon, "##GizmoMode", 0xFFFFFFFF, BtnSize))
        {
            CycleGuizmoOp();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Gizmo: %s (R)", ImGuiX::ImGuizmoOpToString(GuizmoOp).data());
        }

        ImGui::SameLine();

        const bool bIsLocalMode = (GuizmoMode == ImGuizmo::LOCAL);
        const char* ModeIcon = bIsLocalMode ? LE_ICON_AXIS_ARROW : LE_ICON_EARTH;
        const ImColor ModeIconColor = bIsLocalMode ? ImVec4(0.2f, 0.6f, 1.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        if (ImGuiX::IconButton(ModeIcon, "##GizmoSpace", ModeIconColor, BtnSize))
        {
            ToggleGuizmoMode();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Gizmo Space: %s (X)", bIsLocalMode ? "Local" : "World");
        }

        if (bGuizmoSnapEnabled)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 0.6f));
        }
        ImGui::SameLine();
        bool bSnapWasEnabled = bGuizmoSnapEnabled;
        if (ImGuiX::IconButton(LE_ICON_MAGNET, "##SnapToggle", 0xFFFFFFFF, BtnSize))
        {
            bGuizmoSnapEnabled = !bGuizmoSnapEnabled;
            PersistGizmoSettings();
        }
        if (bSnapWasEnabled)
        {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Snap Settings (Click to toggle) (Right click for config)");
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
        {
            ImGui::OpenPopup("SnapSettingsPopup");
        }
        if (ImGui::BeginPopup("SnapSettingsPopup", ImGuiWindowFlags_NoMove))
        {
            DrawSnapSettingsPopup();
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if (ImGuiX::IconButton(LE_ICON_EYE, "##ViewMode", 0xFFFFFFFF, BtnSize))
        {
            ImGui::OpenPopup("ViewModePopup");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("View Mode Options");
        }

        ImGui::SameLine();
        if (ImGuiX::IconButton(LE_ICON_PLUS, "##AddToWorld", 0xFFFFFFFF, BtnSize))
        {
            ImGui::OpenPopup("AddToEntityMenu");
        }
        DrawAddToEntityOrWorldPopup();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Add something to the scene.");
        }

        IRenderScene* RenderScene = World->GetRenderer();
        if (ImGui::BeginPopup("ViewModePopup", ImGuiWindowFlags_NoMove))
        {
            ImGui::Text("Visualizations");
            ImGui::Separator();

            if (ImGui::BeginMenu("Components"))
            {
                ImGui::Checkbox("Show All", &bShowComponentVisualizers);
                ImGui::BeginDisabled(!bShowComponentVisualizers);
                for (auto&& [Struct, Visualizer] : CComponentVisualizerRegistry::Get().GetVisualizers())
                {
                    bool bFoobar = false;
                    ImGui::Checkbox(Struct->MakeDisplayName().c_str(), &bFoobar);
                }
                ImGui::EndDisabled();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Physics"))
            {
                if (const bool* bValue = FConsoleRegistry::Get().TryGetAs<bool>("Jolt.Debug.Draw"))
                {
                    bool bProxy = *bValue;
                    if (ImGui::MenuItem("Toggle Collision", nullptr, &bProxy))
                    {
                        FConsoleRegistry::Get().SetAs("Jolt.Debug.Draw", bProxy);
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Navigation"))
            {
                if (const bool* bValue = FConsoleRegistry::Get().TryGetAs<bool>("Nav.DrawDebug"))
                {
                    bool bProxy = *bValue;
                    if (ImGui::MenuItem("Draw NavMesh", nullptr, &bProxy))
                    {
                        FConsoleRegistry::Get().SetAs("Nav.DrawDebug", bProxy);
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(LE_ICON_BONE " Skeleton"))
            {
                DrawSkeletonDebugMenuItems();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Rendering"))
            {
                FSceneRenderSettings& Settings = RenderScene->GetSceneRenderSettings();

                if (ImGui::BeginMenu("View Mode"))
                {
                    struct FViewModeEntry { ERenderSceneDebugFlags Mode; const char* Label; };

                    static const FViewModeEntry Shading[] =
                    {
                        { ERenderSceneDebugFlags::None,  "Lit"   },
                        { ERenderSceneDebugFlags::Unlit, "Unlit" },
                    };
                    static const FViewModeEntry Buffers[] =
                    {
                        { ERenderSceneDebugFlags::BaseColor,         "Base Color"        },
                        { ERenderSceneDebugFlags::WorldNormal,       "World Normal"      },
                        { ERenderSceneDebugFlags::ShadingNormal,     "Shading Normal"    },
                        { ERenderSceneDebugFlags::Roughness,         "Roughness"         },
                        { ERenderSceneDebugFlags::Metallic,          "Metallic"          },
                        { ERenderSceneDebugFlags::AmbientOcclusion,  "Ambient Occlusion" },
                        { ERenderSceneDebugFlags::Emissive,          "Emissive"          },
                        { ERenderSceneDebugFlags::UV,                "UV"                },
                    };
                    static const FViewModeEntry Geometry[] =
                    {
                        { ERenderSceneDebugFlags::Meshlets, "Meshlets" },
                    };
                    static const FViewModeEntry Lighting[] =
                    {
                        { ERenderSceneDebugFlags::LightComplexity, "Light Complexity" },
                        { ERenderSceneDebugFlags::ClusterGrid,     "Light Clusters"   },
                        { ERenderSceneDebugFlags::ShadowCascades,  "Shadow Cascades"  },
                        { ERenderSceneDebugFlags::ShadowPenumbra,  "Shadow Penumbra"  },
                    };

                    auto DrawGroup = [&](const char* Header, const FViewModeEntry* Entries, size_t Count)
                    {
                        ImGui::TextDisabled("%s", Header);
                        ImGui::Separator();
                        for (size_t i = 0; i < Count; ++i)
                        {
                            bool bSelected = Settings.Flags == Entries[i].Mode;
                            if (ImGui::MenuItem(Entries[i].Label, nullptr, bSelected))
                            {
                                Settings.Flags = Entries[i].Mode;
                            }
                        }
                    };

                    DrawGroup("Shading", Shading, sizeof(Shading) / sizeof(Shading[0]));
                    ImGui::Spacing();
                    DrawGroup("Buffers", Buffers, std::size(Buffers));
                    ImGui::Spacing();
                    DrawGroup("Geometry", Geometry, std::size(Geometry));
                    ImGui::Spacing();
                    DrawGroup("Lighting", Lighting, std::size(Lighting));

                    ImGui::EndMenu();
                }

                ImGui::Separator();

                bool bWireframe = Settings.bWireframe;
                if (ImGui::MenuItem("Wireframe", nullptr, &bWireframe))
                {
                    Settings.bWireframe = bWireframe;
                }

                bool bDrawBillboards = Settings.bDrawBillboards;
                if (ImGui::MenuItem("Draw Billboards", nullptr, &bDrawBillboards))
                {
                    Settings.bDrawBillboards = bDrawBillboards;
                }

                bool bDrawAABB = Settings.bDrawAABB;
                if (ImGui::MenuItem("Draw Bounds", nullptr, &bDrawAABB))
                {
                    Settings.bDrawAABB = bDrawAABB;
                }

                // Tool-specific view-mode items (world: Entity Debug Info, Game View).
                DrawViewModeExtraItems();

                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }
    }

    void FSceneEditorTool::ToggleGuizmoMode()
    {
        EditorEntityUtils::ToggleGizmoMode(GuizmoMode);
    }

    void FSceneEditorTool::EndFrame()
    {
        using namespace entt::literals;

        if (!bShowComponentVisualizers)
        {
            return;
        }

        CComponentVisualizerRegistry& ComponentVisualizerRegistry = CComponentVisualizerRegistry::Get();
        FEntityRegistry& Registry = GetSceneRegistry();

        // Resolve which component storages actually have a visualizer ONCE per frame.
        TFixedVector<eastl::pair<entt::sparse_set*, CComponentVisualizer*>, 16> VisualizerStorages;
        for (auto&& [ID, Storage] : Registry.storage())
        {
            if (entt::meta_type MetaType = entt::resolve(Storage.info()))
            {
                if (entt::meta_any ReturnValue = ECS::Utils::InvokeMetaFunc(MetaType, "static_struct"_hs))
                {
                    if (CComponentVisualizer* Visualizer = ComponentVisualizerRegistry.GetComponentVisualizer(ReturnValue.cast<CStruct*>()))
                    {
                        VisualizerStorages.emplace_back(&Storage, Visualizer);
                    }
                }
            }
        }

        if (VisualizerStorages.empty())
        {
            return;
        }

        // Flatten the selection (view, not SelectedEntities, so entt::exclude<SDisabledTag> applies) into a
        // contiguous list to index from worker threads. Bail before the resolve when nothing is selected.
        TVector<entt::entity> SelectedEntities;
        auto SelectedView = Registry.view<FSelectedInEditorComponent>(entt::exclude<SDisabledTag>);
        SelectedEntities.reserve(SelectedView.size_hint());
        SelectedView.each([&](entt::entity SelectedEntity)
        {
            SelectedEntities.push_back(SelectedEntity);
        });

        if (SelectedEntities.empty())
        {
            return;
        }
        
        ECS::Utils::ResolveAllDirtyTransforms(Registry);

        auto DrawFor = [&](entt::entity Entity)
        {
            for (auto& [Storage, Visualizer] : VisualizerStorages)
            {
                if (Storage->contains(Entity))
                {
                    Visualizer->Draw(World, Registry, Entity);
                }
            }
        };
        
        Task::ParallelFor((uint32)SelectedEntities.size(), [&](uint32 Index)
        {
            const entt::entity SelectedEntity = SelectedEntities[Index];
            DrawFor(SelectedEntity);
            ECS::Utils::ForEachChild(Registry, SelectedEntity, [&](entt::entity Child)
            {
                DrawFor(Child);
            });
        }, 32);
    }

    void FSceneEditorTool::DrawDetailsPanel(bool bFocused)
    {
        const entt::entity Entity = GetLastSelectedEntity();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);

        ImGui::BeginChild("Property Editor", ImVec2(0, 0), true);

        // PropertyTables hold raw component pointers; rebuild before drawing on focus change, invalidation, or dirty mark.
        const bool bEntityValid = (Entity != entt::null) && GetSceneRegistry().valid(Entity);

        if (!bEntityValid)
        {
            if (DetailsEntity != entt::null || !PropertyTables.empty())
            {
                PropertyTables.clear();
                DetailsEntity = entt::null;
            }
            bDetailsDirty = false;
        }
        else if (DetailsEntity != Entity || bDetailsDirty)
        {
            RebuildPropertyTables(Entity);
            DetailsEntity = Entity;
            bDetailsDirty = false;
        }

        if (bEntityValid)
        {
            DrawEntityProperties(Entity);
        }
        else
        {
            DrawEmptyState();
        }

        ImGui::EndChild();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void FSceneEditorTool::DrawEntityProperties(entt::entity Entity)
    {
        const bool bMultiSelect = SelectedEntities.size() > 1 && IsEntitySelected(Entity);

        SNameComponent* NameComponent = GetSceneRegistry().try_get<SNameComponent>(Entity);
        FName EntityName = NameComponent ? NameComponent->Name : eastl::to_string((uint32)Entity);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        constexpr ImGuiTableFlags Flags =
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_NoBordersInBodyUntilResize |
        ImGuiTableFlags_SizingFixedFit;

        if (ImGui::BeginTable("##EntityName", 1, Flags))
        {
            ImGui::TableSetupColumn("##Editor", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(35, 35, 35, 255));

            ImGui::BeginHorizontal(EntityName.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f), 0.5f);

            ImGuiX::TextColoredUnformatted(ImVec4(0.40f, 0.70f, 1.0f, 1.0f), LE_ICON_CUBE);

            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::LargeBold);
            if (bMultiSelect)
            {
                ImGuiX::Text("{} Entities Selected", (uint32)SelectedEntities.size());
            }
            else
            {
                ImGuiX::Text("{}", EntityName);
            }
            ImGui::PopFont();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.55f, 1.0f));
            if (bMultiSelect)
            {
                ImGuiX::Text("Editing {}", EntityName);
            }
            else
            {
                ImGuiX::Text("ID {}", entt::to_integral(Entity));
            }
            ImGui::PopStyleColor();

            ImGui::Spring(1.0f);

            const float ActionDim = ImGui::GetFrameHeight();
            const ImVec2 ActionSize(ActionDim, ActionDim);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.65f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.25f, 1.0f));

            if (ImGui::Button(LE_ICON_PLUS, ActionSize))
            {
                AddEntityComponentFilter.Clear();
                ImGui::OpenPopup("AddToEntityMenu");
            }
            DrawAddToEntityOrWorldPopup(Entity);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Add Component");
            }

            // Tool-specific header buttons (world: Add Tag).
            DrawDetailsHeaderExtraButtons(Entity);

            ImGui::PopStyleColor(3);

            const bool bCanDelete = bMultiSelect || CanDeleteEntity(Entity);
            ImGui::BeginDisabled(!bCanDelete);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));

            if (ImGui::Button(LE_ICON_TRASH_CAN, ActionSize))
            {
                if (bMultiSelect)
                {
                    if (Dialogs::Confirmation("Confirm Deletion",
                        "Are you sure you want to delete {0} selected entities?\n\nThis action cannot be undone.",
                        (uint32)SelectedEntities.size()))
                    {
                        for (entt::entity Selected : SelectedEntities)
                        {
                            if (CanDeleteEntity(Selected))
                            {
                                EntityDestroyRequests.push(Selected);
                            }
                        }
                    }
                }
                else if (Dialogs::Confirmation("Confirm Deletion",
                    "Are you sure you want to delete entity \"{0}\"?\n\nThis action cannot be undone.",
                    (uint32)Entity))
                {
                    EntityDestroyRequests.push(Entity);
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip(bMultiSelect ? "Delete Selected" : "Delete Entity");
            }

            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
            ImGui::PopStyleVar();

            ImGui::EndHorizontal();
            ImGui::PopStyleVar(3);

            ImGui::EndTable();
        }

        ImGui::SeparatorText("Details");

        // Tool-specific sections above the component list (world: Tags).
        DrawDetailsExtraSections(Entity);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(LE_ICON_CUBE " Components");
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        DrawComponentList(Entity);
    }

    void FSceneEditorTool::DrawEmptyState()
    {
        ImVec2 WindowSize = ImGui::GetWindowSize();
        ImVec2 CenterPos = ImVec2(WindowSize.x * 0.5f, WindowSize.y * 0.5f);

        ImGui::SetCursorPos(ImVec2(CenterPos.x - 100.0f, CenterPos.y - 40.0f));

        ImGui::BeginGroup();
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));

            const char* EmptyIcon = LE_ICON_INBOX;
            ImVec2 IconSize = ImGui::CalcTextSize(EmptyIcon);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (200.0f - IconSize.x) * 0.5f);

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
            ImGui::TextUnformatted(EmptyIcon);
            ImGui::PopFont();

            ImGui::Spacing();

            const char* EmptyText = "Nothing selected";
            ImVec2 TextSize = ImGui::CalcTextSize(EmptyText);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (200.0f - TextSize.x) * 0.5f);
            ImGui::TextUnformatted(EmptyText);

            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
            const char* HintText = "Select an entity to view properties";
            ImVec2 HintSize = ImGui::CalcTextSize(HintText);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (200.0f - HintSize.x) * 0.5f);
            ImGui::TextUnformatted(HintText);
            ImGui::PopStyleColor();

            ImGui::PopStyleColor();
        }
        ImGui::EndGroup();
    }

    size_t FSceneEditorTool::CountOutlinerEntities() const
    {
        size_t Count = 0;
        GetSceneRegistry().view<SNameComponent>().each([&](entt::entity E, const SNameComponent&)
        {
            if (IsOutlinerEntityVisible(E))
            {
                ++Count;
            }
        });
        return Count;
    }

    void FSceneEditorTool::DrawFilterOptions()
    {
        using namespace entt::literals;

        if (ImGui::Button("Reset Filters", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
        {
            EntityFilterState.ComponentFilters.clear();
        }

        if (ImGui::BeginTable("ComponentFilters", 1,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchSame |
            ImGuiTableFlags_ScrollY, ImVec2(0.0f, 400.0f)))
        {
            ImGui::TableSetupColumn("Component Type");
            ImGui::TableHeadersRow();

            for (auto&& [ID, Storage] : GetSceneRegistry().storage())
            {
                if (entt::meta_type MetaType = entt::resolve(Storage.info()))
                {
                    if (entt::meta_any ReturnValue = ECS::Utils::InvokeMetaFunc(MetaType, "static_struct"_hs))
                    {
                        CStruct* StructType = ReturnValue.cast<CStruct*>();

                        if (StructType->HasMeta("HideInComponentList"))
                        {
                            continue;
                        }

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();

                        auto It = eastl::find(EntityFilterState.ComponentFilters.begin(),
                            EntityFilterState.ComponentFilters.end(), StructType->GetName());

                        bool bIsFiltered = (It != EntityFilterState.ComponentFilters.end());
                        if (ImGui::Checkbox(StructType->MakeDisplayName().c_str(), &bIsFiltered))
                        {
                            if (bIsFiltered)
                            {
                                EntityFilterState.ComponentFilters.emplace_back(StructType->GetName());
                            }
                            else
                            {
                                EntityFilterState.ComponentFilters.erase(It);
                            }
                        }
                    }
                }
            }

            ImGui::EndTable();
        }
    }

    void FSceneEditorTool::DrawOutliner(bool bFocused)
    {
        // Track focus/hover so Delete (and other selection shortcuts) work from here too.
        bOutlinerActive = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                       || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

        {
            const ImGuiStyle& Style = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

            const float ButtonWidth = ImGui::GetFrameHeight(); // square, matches the search field height

            // Shared "Add" menu (empty / primitives / components / prefabs), identical in both tools.
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
            if (ImGuiX::IconButton(LE_ICON_PLUS, "##AddToSceneGraph", 0xFFFFFFFF, ImVec2(ButtonWidth, ButtonWidth)))
            {
                ImGui::OpenPopup("AddToEntityMenu");
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Add a new entity.");
            }
            DrawAddToEntityOrWorldPopup();

            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ButtonWidth - Style.FramePadding.x);
            EntityFilterState.FilterName.Draw("##Search");

            ImGui::PopStyleVar();

            if (!EntityFilterState.FilterName.IsActive())
            {
                ImDrawList* DrawList = ImGui::GetWindowDrawList();
                ImVec2 TextPos = ImGui::GetItemRectMin();
                TextPos.x += Style.FramePadding.x + 2.0f;
                TextPos.y += Style.FramePadding.y;
                DrawList->AddText(TextPos, IM_COL32(100, 100, 110, 255), LE_ICON_FILE_SEARCH " Search entities...");
            }

            ImGui::SameLine();

            const bool bFilterActive = EntityFilterState.FilterName.IsActive() || !EntityFilterState.ComponentFilters.empty();
            ImGui::PushStyleColor(ImGuiCol_Button,
                bFilterActive ? ImVec4(0.4f, 0.45f, 0.65f, 1.0f) : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                bFilterActive ? ImVec4(0.5f, 0.55f, 0.75f, 1.0f) : ImVec4(0.25f, 0.25f, 0.27f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

            if (ImGui::Button(LE_ICON_FILTER_SETTINGS "##ComponentFilter", ImVec2(ButtonWidth, ButtonWidth)))
            {
                ImGui::OpenPopup("FilterPopup");
            }

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(bFilterActive ? "Filters active - Click to configure" : "Configure filters");
            }

            if (ImGui::BeginPopup("FilterPopup", ImGuiWindowFlags_NoMove))
            {
                ImGui::SeparatorText("Component Filters");
                DrawFilterOptions();
                ImGui::EndPopup();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        {
            ImGui::Text(LE_ICON_FORMAT_LIST_NUMBERED " Total Entities: %s", eastl::to_string(CountOutlinerEntities()).c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24 - ImGui::GetStyle().FramePadding.x);
            if (ImGui::Button(LE_ICON_REFRESH))
            {
                OutlinerListView.MarkTreeDirty();
            }

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
            if (ImGui::BeginChild("EntityList", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar))
            {
                FlushOutlinerPending();
                OutlinerListView.Draw(OutlinerContext);

                if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(), ImGui::GetCurrentWindow()->ID))
                {
                    HandleOutlinerEmptyAreaDrop();
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::EndChild();

            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
    }

    void FSceneEditorTool::DrawAddToEntityOrWorldPopup(entt::entity Entity)
    {
        using namespace entt::literals;

        ImGui::SetNextWindowSize(ImVec2(450.0f, 550.0f), ImGuiCond_Always);

        if (ImGui::BeginPopup("AddToEntityMenu", ImGuiWindowFlags_NoMove))
        {
            if (Entity == entt::null)
            {
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), LE_ICON_PLUS " Create New Entity");
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            }
            else if (SelectedEntities.size() > 1 && IsEntitySelected(Entity))
            {
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1.0f), LE_ICON_SELECT_GROUP " Add to %llu selected entities",
                    (unsigned long long)SelectedEntities.size());
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::SetNextItemWidth(-1);
            AddEntityComponentFilter.Draw("##Search");
            if (ImGui::IsWindowAppearing())
            {
                AddEntityComponentFilter.Clear();
                ImGui::SetKeyboardFocusHere(-1);
            }
            if (!AddEntityComponentFilter.IsActive())
            {
                ImGuiStyle& Style = ImGui::GetStyle();
                ImDrawList* DrawList = ImGui::GetWindowDrawList();
                ImVec2 TextPos = ImGui::GetItemRectMin();
                TextPos.x += Style.FramePadding.x + 2.0f;
                TextPos.y += Style.FramePadding.y;
                DrawList->AddText(TextPos, IM_COL32(110, 110, 110, 255), LE_ICON_FOLDER_SEARCH " Search components...");
            }
            ImGui::PopStyleVar();

            ImGui::Spacing();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

            if (ImGui::BeginChild("TemplateList", ImVec2(0, -35.0f), true))
            {
                bool bDrewComponentsHeader = false;
                auto DrawComponentsHeader = [&]()
                {
                    if (bDrewComponentsHeader)
                    {
                        return;
                    }
                    bDrewComponentsHeader = true;

                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
                    ImGui::TextUnformatted(LE_ICON_CUBE " Components");
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                    ImGui::Spacing();
                };

                if (Entity == entt::null)
                {
                    static const FName PrefabClassName = FName("CPrefab");
                    TVector<FAssetData*> PrefabAssets = FAssetRegistry::Get().FindByPredicate([](const FAssetData& Data)
                    {
                        return Data.AssetClass == PrefabClassName;
                    });

                    if (!PrefabAssets.empty())
                    {
                        TVector<FAssetData*> FilteredPrefabs;
                        FilteredPrefabs.reserve(PrefabAssets.size());
                        for (FAssetData* Data : PrefabAssets)
                        {
                            if (AddEntityComponentFilter.PassFilter(Data->AssetName.c_str()))
                            {
                                FilteredPrefabs.push_back(Data);
                            }
                        }

                        eastl::sort(FilteredPrefabs.begin(), FilteredPrefabs.end(), [](FAssetData* LHS, FAssetData* RHS)
                        {
                            return LHS->AssetName.ToString() < RHS->AssetName.ToString();
                        });

                        if (!FilteredPrefabs.empty())
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
                            ImGui::TextUnformatted(LE_ICON_PACKAGE_VARIANT_CLOSED " Prefabs");
                            ImGui::PopStyleColor();
                            ImGui::Separator();
                            ImGui::Spacing();

                            for (FAssetData* Data : FilteredPrefabs)
                            {
                                ImGui::PushID(Data);

                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.22f, 0.28f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.35f, 0.5f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.3f, 0.45f, 1.0f));
                                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));

                                const float ButtonWidth = ImGui::GetContentRegionAvail().x;

                                FFixedString Label;
                                Label.append(LE_ICON_PACKAGE_VARIANT_CLOSED " ");
                                Label.append(Data->AssetName.c_str());
                                if (ImGui::Button(Label.c_str(), ImVec2(ButtonWidth, 0.0f)))
                                {
                                    HandlePrefabContentDrop(FStringView(Data->Path.c_str()), entt::null);
                                    ImGui::CloseCurrentPopup();
                                    AddEntityComponentFilter.Clear();
                                }

                                ImGui::PopStyleVar(2);
                                ImGui::PopStyleColor(3);

                                ImGui::PopID();
                                ImGui::Spacing();
                            }

                            DrawComponentsHeader();
                        }
                    }
                }

                {
                    struct FPrimitiveEntry
                    {
                        const char* Label;
                        const char* EntityName;
                        CStaticMesh* (*GetMesh)();
                    };

                    static const FPrimitiveEntry PrimitiveEntries[] =
                    {
                        { LE_ICON_CUBE     " Cube",     "Cube",     []() -> CStaticMesh* { return CPrimitiveManager::Get().CubeMesh; } },
                        { LE_ICON_CIRCLE   " Sphere",   "Sphere",   []() -> CStaticMesh* { return CPrimitiveManager::Get().SphereMesh; } },
                        { LE_ICON_SQUARE   " Plane",    "Plane",    []() -> CStaticMesh* { return CPrimitiveManager::Get().PlaneMesh; } },
                        { LE_ICON_CYLINDER " Cylinder", "Cylinder", []() -> CStaticMesh* { return CPrimitiveManager::Get().CylinderMesh; } },
                        { LE_ICON_CONE     " Cone",     "Cone",     []() -> CStaticMesh* { return CPrimitiveManager::Get().ConeMesh; } },
                        { LE_ICON_GAS_CYLINDER " Capsule", "Capsule", []() -> CStaticMesh* { return CPrimitiveManager::Get().CapsuleMesh; } },
                    };

                    TVector<const FPrimitiveEntry*> FilteredPrimitives;
                    FilteredPrimitives.reserve(IM_ARRAYSIZE(PrimitiveEntries));
                    for (const FPrimitiveEntry& Entry : PrimitiveEntries)
                    {
                        if (AddEntityComponentFilter.PassFilter(Entry.EntityName))
                        {
                            FilteredPrimitives.push_back(&Entry);
                        }
                    }

                    if (!FilteredPrimitives.empty())
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
                        ImGui::TextUnformatted(LE_ICON_SHAPE " Primitives");
                        ImGui::PopStyleColor();
                        ImGui::Separator();
                        ImGui::Spacing();

                        for (const FPrimitiveEntry* Entry : FilteredPrimitives)
                        {
                            ImGui::PushID(Entry);

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.28f, 0.22f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.45f, 0.35f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.4f, 0.3f, 1.0f));
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));

                            const float ButtonWidth = ImGui::GetContentRegionAvail().x;

                            if (ImGui::Button(Entry->Label, ImVec2(ButtonWidth, 0.0f)))
                            {
                                CStaticMesh* PrimitiveMesh = Entry->GetMesh();
                                if (GetSceneRegistry().valid(Entity))
                                {
                                    if (PrimitiveMesh != nullptr)
                                    {
                                        BeginTransaction();
                                        SStaticMeshComponent& MeshComp = GetSceneRegistry().emplace_or_replace<SStaticMeshComponent>(Entity);
                                        MeshComp.StaticMesh = PrimitiveMesh;
                                        EndTransaction("Set Primitive Mesh");

                                        OutlinerListView.MarkTreeDirty();
                                        if (Entity == DetailsEntity)
                                        {
                                            bDetailsDirty = true;
                                        }
                                    }
                                }
                                else
                                {
                                    CreatePrimitiveEntity(PrimitiveMesh, Entry->EntityName);
                                }

                                ImGui::CloseCurrentPopup();
                                AddEntityComponentFilter.Clear();
                            }

                            ImGui::PopStyleVar(2);
                            ImGui::PopStyleColor(3);

                            ImGui::PopID();
                            ImGui::Spacing();
                        }

                        DrawComponentsHeader();
                    }
                }

                entt::meta_type       PickedMetaType;
                CStruct*              PickedStruct = nullptr;
                CEntityComponentType* PickedRuntime = nullptr;
                if (DrawAddableComponentList(AddEntityComponentFilter, PickedMetaType, PickedStruct, PickedRuntime))
                {
                    TVector<entt::entity> Targets = GetComponentEditTargets(Entity);

                    if (!Targets.empty())
                    {
                        ApplyAddComponentToTargets(Targets, PickedMetaType, PickedRuntime);
                    }
                    else if (PickedRuntime != nullptr)
                    {
                        BeginTransaction();
                        entt::entity Target = World->ConstructEntity("Entity", GetNewEntitySpawnTransform());
                        ECS::Utils::AddRuntimeComponent(GetSceneRegistry(), Target, PickedRuntime);
                        OnEntityCreatedInScene(Target);
                        EndTransaction("Add Runtime Component");

                        MarkSceneDirty();
                        OutlinerListView.MarkTreeDirty();
                        SetSingleSelectedEntity(Target);
                    }
                    else
                    {
                        CreateEntityWithComponent(PickedStruct);
                    }

                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndChild();

            ImGui::PopStyleVar(2);

            ImGui::Separator();

            ImGui::BeginGroup();
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.25f, 1.0f));
                if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
                {
                    ImGui::CloseCurrentPopup();
                    AddEntityComponentFilter.Clear();
                }
                ImGui::PopStyleColor();

                if (Entity == entt::null)
                {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
                    if (ImGui::Button(LE_ICON_CUBE " Empty Entity", ImVec2(-1, 0.0f)))
                    {
                        CreateEntity();
                        ImGui::CloseCurrentPopup();
                        AddEntityComponentFilter.Clear();
                    }
                    ImGui::PopStyleColor();

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Create entity without any components");
                    }
                }
            }
            ImGui::EndGroup();

            ImGui::EndPopup();
        }
    }
}
