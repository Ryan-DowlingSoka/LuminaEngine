#include "PrefabEditorTool.h"

#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/Prefabs/Prefab.h"
#include "Assets/AssetTypes/Prefabs/PrefabComponents.h"
#include "Components/EditorEntityTags.h"
#include "Config/Config.h"
#include "Core/Math/Math.h"
#include "UI/Tools/ContentBrowserEditorTool.h"
#include "UI/Tools/EditorEntityUtils.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Package/Package.h"
#include "EASTL/sort.h"
#include "GUID/GUID.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "Tools/ComponentVisualizers/ComponentVisualizer.h"
#include "Tools/PrimitiveManager/PrimitiveManager.h"
#include "Tools/UI/ImGui/ImGuiDragDrop.h"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "UI/Properties/PropertyTable.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/EntityTags.h"
#include "World/Entity/Components/EnvironmentComponent.h"
#include "World/Entity/Components/SkyLightComponent.h"
#include "World/Entity/Components/LightComponent.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Traits.h"
#include "World/World.h"


namespace Lumina
{
    FPrefabEditorTool::FPrefabEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset, NewObject<CWorld>())
    {
    }

    CPrefab* FPrefabEditorTool::GetPrefab() const
    {
        return Cast<CPrefab>(Asset.Get());
    }

    void FPrefabEditorTool::OnInitialize()
    {
        CreateToolWindow(OutlinerWindowName, [this](bool bFocused)
        {
            DrawOutliner(bFocused);
        });

        CreateToolWindow(PropertiesWindowName, [this](bool bFocused)
        {
            DrawEntityProperties(bFocused);
        });

        bGuizmoSnapEnabled  = GConfig->Get("Editor.PrefabEditorTool.GuizmoSnapEnabled", true);
        GuizmoSnapTranslate = GConfig->Get("Editor.PrefabEditorTool.GuizmoSnapTranslate", 0.1f);
        GuizmoSnapRotate    = GConfig->Get("Editor.PrefabEditorTool.GuizmoSnapRotate", 5.0f);
        GuizmoSnapScale     = GConfig->Get("Editor.PrefabEditorTool.GuizmoSnapScale", 0.1f);

        OutlinerContext.RebuildTreeFunction = [this](FTreeListView& Tree)
        {
            RebuildOutlinerTree(Tree);
        };

        OutlinerContext.FilterFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            const FTreeNodeDisplay& Display = Tree.Get<FTreeNodeDisplay>(Item);
            return OutlinerNameFilter.PassFilter(Display.DisplayName.c_str());
        };

        OutlinerContext.ItemSelectedFunction = [this](FTreeListView& Tree, FTreeNodeID Item, bool bShouldClear)
        {
            // Plain click replaces; Ctrl-click toggles. Mirrors WorldEditor.
            if (!Item.IsValid())
            {
                if (bShouldClear)
                {
                    ClearSelectedEntities();
                }
                return;
            }

            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            if (Data.Entity == entt::null || !World->GetEntityRegistry().valid(Data.Entity))
            {
                return;
            }

            if (bShouldClear)
            {
                SetSingleSelectedEntity(Data.Entity);
            }
            else
            {
                ToggleSelectedEntity(Data.Entity);
            }
        };

        OutlinerContext.ItemDoubleClickedFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            FocusViewportToEntity(Data.Entity);
        };

        OutlinerContext.ItemContextMenuFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            entt::registry& Registry = World->GetEntityRegistry();

            if (!Registry.valid(Data.Entity))
            {
                return;
            }

            const entt::entity Root = FindPrefabRoot();
            const bool bIsRoot = (Data.Entity == Root);

            if (ImGui::MenuItem("Copy Entity ID"))
            {
                ImGui::SetClipboardText(eastl::to_string(entt::to_integral(Data.Entity)).c_str());
            }

            if (!bIsRoot && ECS::Utils::IsChild(Registry, Data.Entity))
            {
                if (ImGui::MenuItem("Unparent"))
                {
                    BeginTransaction();
                    ECS::Utils::RemoveFromParent(Registry, Data.Entity);
                    EndTransaction("Unparent");
                    OutlinerListView.MarkTreeDirty();
                }
            }

            if (ECS::Utils::IsParent(Registry, Data.Entity))
            {
                if (ImGui::MenuItem("Detach Children"))
                {
                    TVector<entt::entity> Children;
                    ECS::Utils::ForEachChild(Registry, Data.Entity, [&](entt::entity Child) { Children.push_back(Child); });
                    BeginTransaction();
                    ECS::Utils::DetachImmediateChildren(Registry, Data.Entity);
                    EndTransaction("Detach Children");
                    OutlinerListView.MarkTreeDirty();
                }
            }

            if (!bIsRoot && ImGui::MenuItem(LE_ICON_CONTENT_DUPLICATE " Duplicate"))
            {
                BeginTransaction();
                entt::entity New = DuplicatePrefabEntity(Data.Entity);
                if (New != entt::null)
                {
                    EndTransaction("Duplicate");
                    OutlinerListView.MarkTreeDirty();
                }
                else
                {
                    PendingBeforeState.clear();
                }
            }

            if (!bIsRoot && ImGui::MenuItem(LE_ICON_DELETE " Delete"))
            {
                RequestDestroyEntity(Data.Entity);
            }
        };

        OutlinerContext.SetDragDropFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            DragDrop::SetEntityPayload(World, Data.Entity);
        };

        OutlinerContext.DragDropFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            HandleOutlinerDragDrop(Tree, Data.Entity);
        };

        OutlinerContext.RenameFunction = [this](FTreeListView& Tree, FTreeNodeID Item, FStringView NewName)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            entt::registry& Registry = World->GetEntityRegistry();
            if (!Registry.valid(Data.Entity))
            {
                return;
            }

            BeginTransaction();
            if (SNameComponent* NameComp = Registry.try_get<SNameComponent>(Data.Entity))
            {
                NameComp->Name = NewName;
            }
            EndTransaction("Rename");

            const SNameComponent* NameComp = Registry.try_get<SNameComponent>(Data.Entity);
            Tree.Get<FTreeNodeDisplay>(Item).DisplayName = EditorEntityUtils::MakeOutlinerDisplayName(NameComp, Data.Entity).c_str();
            Asset->GetPackage()->MarkDirty();
        };

        OutlinerContext.VisibilityToggleFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            FTreeNodeState& State = Tree.Get<FTreeNodeState>(Item);
            if (!World->GetEntityRegistry().valid(Data.Entity))
            {
                return;
            }
            if (State.bDisabled)
            {
                World->GetEntityRegistry().emplace<SDisabledTag>(Data.Entity);
            }
            else
            {
                World->GetEntityRegistry().remove<SDisabledTag>(Data.Entity);
            }
        };

        OutlinerContext.HoveredFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            entt::registry& Registry = World->GetEntityRegistry();
            if (!Registry.valid(Data.Entity))
            {
                return;
            }
            if (const STransformComponent* Transform = Registry.try_get<STransformComponent>(Data.Entity))
            {
                if (const SStaticMeshComponent* MeshComp = Registry.try_get<SStaticMeshComponent>(Data.Entity))
                {
                    World->DrawBox(Transform->GetWorldLocation(), MeshComp->GetAABB().GetSize() * 0.5f * Transform->GetWorldScale() * 1.2f, Transform->GetWorldRotation(), FColor::White, 3.0f);
                }
                else
                {
                    World->DrawBox(Transform->GetWorldLocation(), 1.0f * Transform->GetWorldScale(), Transform->GetWorldRotation(), FColor::White, 3.0f);
                }
            }
        };

        OutlinerContext.KeyPressedFunction = [this](FTreeListView& Tree, FTreeNodeID Item, ImGuiKey Key) -> bool
        {
            if (Key == ImGuiKey_Delete)
            {
                FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
                RequestDestroyEntity(Data.Entity);
                return true;
            }
            return false;
        };

        RegisterEditorActions();
    }

    void FPrefabEditorTool::RegisterEditorActions()
    {
        auto Hovered = [this]() { return bViewportHovered; };
        auto AlwaysOn = []() { return true; };

        RegisterAction({"Translate Mode", "Gizmo", "Switch the gizmo to translate (move) mode",
            FInputChord{ImGuiKey_W}, [this]{ GuizmoOp = ImGuizmo::TRANSLATE; }, Hovered});

        RegisterAction({"Rotate Mode", "Gizmo", "Switch the gizmo to rotate mode",
            FInputChord{ImGuiKey_E}, [this]{ GuizmoOp = ImGuizmo::ROTATE; }, Hovered});

        RegisterAction({"Scale Mode", "Gizmo", "Switch the gizmo to scale mode",
            FInputChord{ImGuiKey_R}, [this]{ GuizmoOp = ImGuizmo::SCALE; }, Hovered});

        RegisterAction({"Toggle Local/World", "Gizmo", "Switch the gizmo between world-space and entity-local space",
            FInputChord{ImGuiKey_X}, [this]{ EditorEntityUtils::ToggleGizmoMode(GuizmoMode); }, Hovered});

        RegisterAction({"Focus Selection", "View", "Frame the camera on the last-selected entity",
            FInputChord{ImGuiKey_F}, [this]{ FocusViewportToEntity(GetLastSelectedEntity()); }});

        RegisterAction({"Frame All", "View", "Frame the camera on every prefab entity",
            FInputChord{ImGuiKey_Home}, [this]{ FrameAllEntities(); }, Hovered});

        RegisterAction({"Reset Transform", "Selection", "Reset the selected entities' local transform to identity",
            FInputChord{ImGuiKey_R, true, true}, [this]{ ResetSelectionTransform(); }, Hovered});

        RegisterAction({"Undo", "History", "Revert the last transacted edit",
            FInputChord{ImGuiKey_Z, true}, [this]{ Undo(); }, AlwaysOn});

        RegisterAction({"Redo", "History", "Re-apply the last undone edit",
            FInputChord{ImGuiKey_Y, true}, [this]{ Redo(); }, AlwaysOn});

        // Advisory entries: inline-handled shortcuts surfaced in Help > Keybinds.
        RegisterAction({"Duplicate", "Selection", "Duplicate the selection in place",
            FInputChord{ImGuiKey_D, true}, nullptr});
        RegisterAction({"Copy", "Selection", "Copy the selection to the entity clipboard",
            FInputChord{ImGuiKey_C, true}, nullptr});
        RegisterAction({"Paste", "Selection", "Paste previously-copied entities under the prefab root",
            FInputChord{ImGuiKey_V, true}, nullptr});
        RegisterAction({"Delete", "Selection", "Delete the selected entities",
            FInputChord{ImGuiKey_Delete}, nullptr});
        RegisterAction({"Cycle Gizmo", "Gizmo", "Cycle Translate→Rotate→Scale",
            FInputChord{ImGuiKey_Space}, nullptr});
    }

    void FPrefabEditorTool::SetupWorldForTool()
    {
        FEditorTool::SetupWorldForTool();

        DirectionalLightEntity = World->ConstructEntity("PreviewLight");
        World->GetEntityRegistry().emplace<FHideInSceneOutliner>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SDirectionalLightComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SEnvironmentComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SSkyLightComponent>(DirectionalLightEntity);

        // Pitch the preview light so meshes don't render with a flat-top look.
        if (STransformComponent* LightTransform = World->GetEntityRegistry().try_get<STransformComponent>(DirectionalLightEntity))
        {
            LightTransform->SetRotationFromEuler(glm::vec3(-50.0f, 35.0f, 0.0f));
        }

        CreateFloorPlane(0.0f);
    }

    void FPrefabEditorTool::OnAssetLoadFinished()
    {
        LoadPrefabIntoPreviewWorld();
        OutlinerListView.MarkTreeDirty();

        // Loading the prefab repopulates the registry; nothing before this point is meaningful
        // to undo back into.
        ClearTransactionHistory();

        // Frame the freshly loaded prefab so the preview camera isn't dropped on origin
        // when the prefab is offset away from world zero.
        FrameAllEntities();
    }

    void FPrefabEditorTool::OnPostUndoRedo()
    {
        // Selection lives in registry tags; undo restores them. Mirror them into the cached set.
        ResyncSelectionFromRegistry();

        // Component pointers in PropertyTables are stale after the registry serialize.
        OutlinerListView.MarkTreeDirty();
        bPropertyTablesDirty = true;
        CachedPropertyEntity = entt::null;
    }

    void FPrefabEditorTool::LoadPrefabIntoPreviewWorld()
    {
        CPrefab* Prefab = GetPrefab();
        if (Prefab == nullptr || World == nullptr)
        {
            return;
        }

        // Wipe any previously-loaded prefab entities (leave preview-only lights / floor / camera).
        entt::registry& WorldRegistry = World->GetEntityRegistry();
        TVector<entt::entity> ToDestroy;
        WorldRegistry.view<SPrefabComponent>().each([&](entt::entity E, const SPrefabComponent&)
        {
            ToDestroy.push_back(E);
        });
        for (entt::entity E : ToDestroy)
        {
            if (WorldRegistry.valid(E))
            {
                ECS::Utils::DestroyEntityHierarchy(WorldRegistry, E);
            }
        }

        ClearSelectedEntities();

        // Empty prefab: seed a single root the user can edit.
        if (Prefab->Registry.view<entt::entity>().empty())
        {
            entt::entity Root = WorldRegistry.create();
            WorldRegistry.emplace<SNameComponent>(Root).Name = FName("Root");
            WorldRegistry.emplace<STransformComponent>(Root);
            WorldRegistry.emplace<SPrefabComponent>(Root).StableID = FName(FGuid::New().ToShortString());
            SetSingleSelectedEntity(Root);
            return;
        }

        THashMap<entt::entity, entt::entity> Map;
        CPrefab::CopyRegistry(Prefab->Registry, WorldRegistry, Map);

        // Auto-select the root so the property panel isn't empty on first load.
        const entt::entity Root = FindPrefabRoot();
        if (Root != entt::null)
        {
            SetSingleSelectedEntity(Root);
        }
    }

    void FPrefabEditorTool::CommitPreviewWorldToPrefab()
    {
        using namespace entt::literals;

        CPrefab* Prefab = GetPrefab();
        if (Prefab == nullptr || World == nullptr)
        {
            return;
        }

        entt::registry& WorldRegistry = World->GetEntityRegistry();

        // Gather every entity that belongs to the prefab (tagged with SPrefabComponent).
        TVector<entt::entity> PrefabEntities;
        WorldRegistry.view<SPrefabComponent>().each([&](entt::entity E, const SPrefabComponent&)
        {
            PrefabEntities.push_back(E);
        });

        // Rebuild the prefab's registry from scratch to avoid accumulating dead entities.
        Prefab->Registry = entt::registry{};

        THashMap<entt::entity, entt::entity> SrcToDst;
        for (entt::entity SrcE : PrefabEntities)
        {
            SrcToDst[SrcE] = Prefab->Registry.create();
        }

        for (auto&& [ID, SrcSet] : WorldRegistry.storage())
        {
            // Skip every editor-only state plus FRelationshipComponent (we remap below).
            if (ID == entt::type_hash<FRelationshipComponent>::value()) continue;
            if (EditorEntityUtils::IsEditorOnlyComponent(ID)) continue;

            entt::meta_type MetaType = entt::resolve(SrcSet.info());
            if (!MetaType) continue;

            for (entt::entity SrcE : PrefabEntities)
            {
                if (!SrcSet.contains(SrcE)) continue;

                entt::entity DstE = SrcToDst[SrcE];
                void* Ptr = SrcSet.value(SrcE);
                entt::meta_any Any = MetaType.from_void(Ptr);
                ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs,
                    entt::forward_as_meta(Prefab->Registry), DstE, entt::forward_as_meta(Any));
            }
        }

        // Remap relationships: parent/sibling pointers that escape the captured set become null.
        auto Remap = [&](entt::entity& E)
        {
            if (E == entt::null) return;
            auto It = SrcToDst.find(E);
            E = (It != SrcToDst.end()) ? It->second : entt::null;
        };

        for (entt::entity SrcE : PrefabEntities)
        {
            if (const FRelationshipComponent* Rel = WorldRegistry.try_get<FRelationshipComponent>(SrcE))
            {
                FRelationshipComponent DstRel = *Rel;
                Remap(DstRel.First);
                Remap(DstRel.Prev);
                Remap(DstRel.Next);
                Remap(DstRel.Parent);
                Prefab->Registry.emplace_or_replace<FRelationshipComponent>(SrcToDst[SrcE], DstRel);
            }
        }
    }

    void FPrefabEditorTool::OnSave()
    {
        CommitPreviewWorldToPrefab();

        // Persist gizmo prefs alongside the asset save so they survive across editor sessions.
        GConfig->Set("Editor.PrefabEditorTool.GuizmoSnapEnabled",   bGuizmoSnapEnabled);
        GConfig->Set("Editor.PrefabEditorTool.GuizmoSnapTranslate", GuizmoSnapTranslate);
        GConfig->Set("Editor.PrefabEditorTool.GuizmoSnapRotate",    GuizmoSnapRotate);
        GConfig->Set("Editor.PrefabEditorTool.GuizmoSnapScale",     GuizmoSnapScale);

        FAssetEditorTool::OnSave();
    }

    void FPrefabEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::Update(UpdateContext);

        ProcessDestroyRequests();

        // Mark selection's transform dirty so the gizmo's edits propagate to children this frame.
        entt::registry& Registry = World->GetEntityRegistry();
        Registry.view<FSelectedInEditorComponent>().each([&](entt::entity Entity)
        {
            Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
        });

        const entt::entity LastSelected = GetLastSelectedEntity();
        if (Registry.valid(LastSelected))
        {
            if (LastSelected != CachedPropertyEntity || bPropertyTablesDirty)
            {
                RebuildPropertyTables(LastSelected);
                CachedPropertyEntity = LastSelected;
                bPropertyTablesDirty = false;
            }
        }
        else if (!ComponentPropertyTables.empty() || CachedPropertyEntity != entt::null)
        {
            ComponentPropertyTables.clear();
            ComponentStructs.clear();
            CachedPropertyEntity = entt::null;
            bPropertyTablesDirty = false;
        }

        if (bViewportHovered)
        {
            // Delete every selected entity (root is filtered out inside the destroy queue).
            if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            {
                Registry.view<FSelectedInEditorComponent>().each([&](entt::entity Entity)
                {
                    RequestDestroyEntity(Entity);
                });
            }
        }

        ProcessClipboardShortcuts();

        // Selection-highlight bounds; mirror world editor's red-AABB.
        Registry.view<FSelectedInEditorComponent>().each([&](entt::entity Entity)
        {
            if (!Registry.valid(Entity))
            {
                return;
            }
            if (SStaticMeshComponent* MeshComp = Registry.try_get<SStaticMeshComponent>(Entity))
            {
                const STransformComponent& Transform = Registry.get<STransformComponent>(Entity);
                World->DrawBox(Transform.GetWorldLocation(), MeshComp->GetAABB().GetSize() * 0.5f * Transform.GetWorldScale() * 1.2f, Transform.GetWorldRotation(), FColor::Red, 5.0f);
            }
        });
    }

    void FPrefabEditorTool::EndFrame()
    {
        using namespace entt::literals;

        if (!bShowComponentVisualizers || World == nullptr)
        {
            return;
        }

        CComponentVisualizerRegistry& VisualizerRegistry = CComponentVisualizerRegistry::Get();

        entt::registry& Registry = World->GetEntityRegistry();
        Registry.view<FSelectedInEditorComponent>(entt::exclude<SDisabledTag>).each([&](entt::entity SelectedEntity)
        {
            ECS::Utils::ForEachComponent(Registry, SelectedEntity,
                [&](void*, entt::basic_sparse_set<>&, const entt::meta_type& Type)
            {
                if (entt::meta_any ReturnValue = ECS::Utils::InvokeMetaFunc(Type, "static_struct"_hs))
                {
                    CStruct* StructType = ReturnValue.cast<CStruct*>();
                    if (CComponentVisualizer* Visualizer = VisualizerRegistry.GetComponentVisualizer(StructType))
                    {
                        Visualizer->Draw(World, Registry, SelectedEntity);
                    }
                }
            });
        });
    }

    void FPrefabEditorTool::ProcessDestroyRequests()
    {
        if (EntityDestroyRequests.empty())
        {
            return;
        }

        BeginTransaction();
        bool bDestroyed = false;
        const entt::entity Root = FindPrefabRoot();

        while (!EntityDestroyRequests.empty())
        {
            entt::entity Entity = EntityDestroyRequests.back();
            EntityDestroyRequests.pop();

            if (Entity == entt::null || !World->GetEntityRegistry().valid(Entity))
            {
                continue;
            }

            // Don't allow destroying the prefab root; a prefab must have one.
            if (Entity == Root)
            {
                ImGuiX::Notifications::NotifyError("Cannot delete the prefab root entity.");
                continue;
            }

            // Drop selection links so the selection set doesn't carry stale entities into next frame.
            RemoveSelectedEntity(Entity);
            ECS::Utils::ForEachDescendant(World->GetEntityRegistry(), Entity, [&](entt::entity Desc)
            {
                RemoveSelectedEntity(Desc);
            });

            ECS::Utils::DestroyEntityHierarchy(World->GetEntityRegistry(), Entity);
            bDestroyed = true;
            OutlinerListView.MarkTreeDirty();
            Asset->GetPackage()->MarkDirty();
        }

        if (bDestroyed)
        {
            EndTransaction("Delete Entity");
        }
        else
        {
            PendingBeforeState.clear();
        }
    }

    void FPrefabEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
        ComponentPropertyTables.clear();
        ComponentStructs.clear();
        SelectedEntities.clear();
    }

    entt::entity FPrefabEditorTool::FindPrefabRoot() const
    {
        if (World == nullptr)
        {
            return entt::null;
        }

        entt::registry& WorldRegistry = World->GetEntityRegistry();
        entt::entity Root = entt::null;
        WorldRegistry.view<SPrefabComponent>().each([&](entt::entity E, const SPrefabComponent&)
        {
            if (Root != entt::null) return;

            const FRelationshipComponent* Rel = WorldRegistry.try_get<FRelationshipComponent>(E);
            const bool bHasPrefabParent = Rel && Rel->Parent != entt::null &&
                WorldRegistry.any_of<SPrefabComponent>(Rel->Parent);
            if (!bHasPrefabParent)
            {
                Root = E;
            }
        });
        return Root;
    }

    entt::entity FPrefabEditorTool::CreateEntityAtRoot(const char* DisplayName)
    {
        entt::registry& WorldRegistry = World->GetEntityRegistry();

        // Resolve parent root BEFORE creating the new entity. Otherwise the
        // fresh entity (has SPrefabComponent, no parent) becomes its own root,
        // self-parenting into an infinite ForEachChild loop.
        const entt::entity Root = FindPrefabRoot();

        BeginTransaction();

        entt::entity NewEntity = WorldRegistry.create();
        WorldRegistry.emplace<SNameComponent>(NewEntity).Name = FName(DisplayName);
        WorldRegistry.emplace<STransformComponent>(NewEntity);
        WorldRegistry.emplace<SPrefabComponent>(NewEntity).StableID = FName(FGuid::New().ToShortString());

        if (Root != entt::null && Root != NewEntity)
        {
            ECS::Utils::ReparentEntity(WorldRegistry, NewEntity, Root);
        }

        EndTransaction("New Entity");

        Asset->GetPackage()->MarkDirty();
        OutlinerListView.MarkTreeDirty();
        return NewEntity;
    }

    entt::entity FPrefabEditorTool::CreatePrimitiveEntityAtRoot(CStaticMesh* PrimitiveMesh, const char* DisplayName)
    {
        if (PrimitiveMesh == nullptr)
        {
            return entt::null;
        }

        entt::registry& WorldRegistry = World->GetEntityRegistry();
        const entt::entity Root = FindPrefabRoot();

        BeginTransaction();

        entt::entity NewEntity = WorldRegistry.create();
        WorldRegistry.emplace<SNameComponent>(NewEntity).Name = FName(DisplayName);
        WorldRegistry.emplace<STransformComponent>(NewEntity);
        WorldRegistry.emplace<SPrefabComponent>(NewEntity).StableID = FName(FGuid::New().ToShortString());
        SStaticMeshComponent& MeshComp = WorldRegistry.emplace<SStaticMeshComponent>(NewEntity);
        MeshComp.StaticMesh = PrimitiveMesh;

        if (Root != entt::null && Root != NewEntity)
        {
            ECS::Utils::ReparentEntity(WorldRegistry, NewEntity, Root);
        }

        EndTransaction("New Primitive");

        Asset->GetPackage()->MarkDirty();
        OutlinerListView.MarkTreeDirty();
        return NewEntity;
    }

    void FPrefabEditorTool::RequestDestroyEntity(entt::entity Entity)
    {
        if (Entity == entt::null)
        {
            return;
        }
        EntityDestroyRequests.push(Entity);
    }

    void FPrefabEditorTool::RemoveComponent(entt::entity Entity, CStruct* StructType)
    {
        if (StructType == nullptr || World == nullptr)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Entity))
        {
            return;
        }

        bool bRemoved = false;
        ECS::Utils::ForEachComponent(Registry, Entity,
            [&](void*, entt::basic_sparse_set<>& Set, const entt::meta_type& Type)
        {
            using namespace entt::literals;
            if (entt::meta_any Any = ECS::Utils::InvokeMetaFunc(Type, "static_struct"_hs))
            {
                if (Any.cast<CStruct*>() == StructType)
                {
                    Set.remove(Entity);
                    bRemoved = true;
                }
            }
        });

        if (bRemoved)
        {
            bPropertyTablesDirty = true;
            Asset->GetPackage()->MarkDirty();
        }
        else
        {
            ImGuiX::Notifications::NotifyError("Failed to remove component: {0}", StructType->GetName().c_str());
        }
    }

    void FPrefabEditorTool::ResetSelectionTransform()
    {
        if (World == nullptr)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();

        TFixedVector<entt::entity, 64> Targets;
        Registry.view<FSelectedInEditorComponent>().each([&](entt::entity Selected)
        {
            if (Registry.valid(Selected))
            {
                Targets.push_back(Selected);
            }
        });

        if (Targets.empty())
        {
            return;
        }

        BeginTransaction();
        for (entt::entity Entity : Targets)
        {
            if (STransformComponent* Transform = Registry.try_get<STransformComponent>(Entity))
            {
                Transform->SetLocalLocation(glm::vec3(0.0f));
                Transform->SetLocalRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                Transform->SetLocalScale(glm::vec3(1.0f));
                Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
            }
        }
        EndTransaction("Reset Transform");
        Asset->GetPackage()->MarkDirty();
    }

    void FPrefabEditorTool::SetSingleSelectedEntity(entt::entity Entity)
    {
        if (World == nullptr)
        {
            return;
        }
        entt::registry& Registry = World->GetEntityRegistry();
        if (Entity != entt::null && !Registry.valid(Entity))
        {
            Entity = entt::null;
        }

        Registry.clear<FSelectedInEditorComponent>();
        Registry.clear<FLastSelectedTag>();
        SelectedEntities.clear();

        if (Entity != entt::null)
        {
            SelectedEntities.insert(Entity);
            Registry.emplace_or_replace<FSelectedInEditorComponent>(Entity);
            Registry.emplace_or_replace<FLastSelectedTag>(Entity);
        }
    }

    void FPrefabEditorTool::AddSelectedEntity(entt::entity Entity, bool bRebuild)
    {
        if (World == nullptr)
        {
            return;
        }
        entt::registry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Entity))
        {
            return;
        }

        SelectedEntities.insert(Entity);
        Registry.emplace_or_replace<FSelectedInEditorComponent>(Entity);
        Registry.clear<FLastSelectedTag>();
        Registry.emplace_or_replace<FLastSelectedTag>(Entity);

        if (bRebuild)
        {
            OutlinerListView.MarkTreeDirty();
        }
    }

    void FPrefabEditorTool::RemoveSelectedEntity(entt::entity Entity)
    {
        if (World == nullptr || Entity == entt::null)
        {
            return;
        }

        auto It = SelectedEntities.find(Entity);
        if (It == SelectedEntities.end())
        {
            return;
        }
        SelectedEntities.erase(It);

        entt::registry& Registry = World->GetEntityRegistry();
        if (Registry.valid(Entity))
        {
            Registry.remove<FSelectedInEditorComponent>(Entity);
        }

        // If the deselected entity was the focal one, pick a fresh last-selected from the
        // remaining set so the property panel doesn't blank.
        if (GetLastSelectedEntity() == Entity)
        {
            Registry.clear<FLastSelectedTag>();
            for (entt::entity Candidate : SelectedEntities)
            {
                if (Registry.valid(Candidate))
                {
                    Registry.emplace_or_replace<FLastSelectedTag>(Candidate);
                    break;
                }
            }
        }
    }

    void FPrefabEditorTool::ToggleSelectedEntity(entt::entity Entity)
    {
        if (IsEntitySelected(Entity))
        {
            RemoveSelectedEntity(Entity);
        }
        else
        {
            AddSelectedEntity(Entity, false);
        }
    }

    void FPrefabEditorTool::ClearSelectedEntities()
    {
        if (World == nullptr)
        {
            SelectedEntities.clear();
            return;
        }
        World->GetEntityRegistry().clear<FSelectedInEditorComponent>();
        World->GetEntityRegistry().clear<FLastSelectedTag>();
        SelectedEntities.clear();
    }

    void FPrefabEditorTool::ResyncSelectionFromRegistry()
    {
        SelectedEntities.clear();
        if (World == nullptr)
        {
            return;
        }
        World->GetEntityRegistry().view<FSelectedInEditorComponent>().each([&](entt::entity Entity)
        {
            SelectedEntities.insert(Entity);
        });
    }

    entt::entity FPrefabEditorTool::GetLastSelectedEntity() const
    {
        if (World == nullptr)
        {
            return entt::null;
        }
        entt::entity Last = entt::null;
        World->GetEntityRegistry().view<FLastSelectedTag>().each([&](entt::entity E)
        {
            Last = E;
        });
        return Last;
    }

    void FPrefabEditorTool::FrameAllEntities()
    {
        if (World == nullptr)
        {
            return;
        }

        const entt::entity Root = FindPrefabRoot();
        if (Root == entt::null)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        glm::vec3 Center;
        float Radius;
        if (!EditorEntityUtils::ComputeFocusBoundsForEntity(Registry, Root, Center, Radius))
        {
            return;
        }

        if (!Registry.valid(EditorEntity))
        {
            return;
        }

        const SCameraComponent& Camera = Registry.get<SCameraComponent>(EditorEntity);
        const float HalfFov  = glm::radians(Camera.GetFOV() * 0.5f);
        const float Distance = (Radius / glm::tan(glm::max(HalfFov, glm::radians(1.0f)))) * 1.5f;

        STransformComponent& EditorTransform = Registry.get<STransformComponent>(EditorEntity);
        const glm::vec3 Forward = EditorTransform.GetForward();
        EditorTransform.SetLocation(Center - Forward * Distance);
        EditorTransform.SetRotation(Math::FindLookAtRotation(Center, Center - Forward * Distance));
    }

    void FPrefabEditorTool::DrawAddEntityButton()
    {
        if (ImGui::Button(LE_ICON_PLUS " Add Entity"))
        {
            ImGui::OpenPopup("##AddEntityRoot");
        }

        if (ImGui::BeginPopup("##AddEntityRoot"))
        {
            if (ImGui::MenuItem(LE_ICON_CUBE " Empty Entity"))
            {
                entt::entity New = CreateEntityAtRoot();
                if (New != entt::null)
                {
                    SetSingleSelectedEntity(New);
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::Separator();
            DrawAddPrimitiveMenu(entt::null);

            ImGui::EndPopup();
        }
    }

    void FPrefabEditorTool::DrawAddPrimitiveMenu(entt::entity Entity)
    {
        struct FPrimitiveEntry
        {
            const char* Label;
            const char* EntityName;
            CStaticMesh* (*GetMesh)();
        };
        static const FPrimitiveEntry PrimitiveEntries[] =
        {
            { LE_ICON_CUBE         " Cube",     "Cube",     []() -> CStaticMesh* { return CPrimitiveManager::Get().CubeMesh; } },
            { LE_ICON_CIRCLE       " Sphere",   "Sphere",   []() -> CStaticMesh* { return CPrimitiveManager::Get().SphereMesh; } },
            { LE_ICON_SQUARE       " Plane",    "Plane",    []() -> CStaticMesh* { return CPrimitiveManager::Get().PlaneMesh; } },
            { LE_ICON_CYLINDER     " Cylinder", "Cylinder", []() -> CStaticMesh* { return CPrimitiveManager::Get().CylinderMesh; } },
            { LE_ICON_CONE         " Cone",     "Cone",     []() -> CStaticMesh* { return CPrimitiveManager::Get().ConeMesh; } },
            { LE_ICON_GAS_CYLINDER " Capsule",  "Capsule",  []() -> CStaticMesh* { return CPrimitiveManager::Get().CapsuleMesh; } },
        };

        for (const FPrimitiveEntry& Entry : PrimitiveEntries)
        {
            if (ImGui::MenuItem(Entry.Label))
            {
                if (Entity != entt::null && World->GetEntityRegistry().valid(Entity))
                {
                    BeginTransaction();
                    SStaticMeshComponent& MeshComp = World->GetEntityRegistry().emplace_or_replace<SStaticMeshComponent>(Entity);
                    MeshComp.StaticMesh = Entry.GetMesh();
                    EndTransaction("Set Primitive Mesh");
                    bPropertyTablesDirty = true;
                    Asset->GetPackage()->MarkDirty();
                }
                else
                {
                    entt::entity New = CreatePrimitiveEntityAtRoot(Entry.GetMesh(), Entry.EntityName);
                    if (New != entt::null)
                    {
                        SetSingleSelectedEntity(New);
                    }
                }
            }
        }
    }

    void FPrefabEditorTool::RebuildOutlinerTree(FTreeListView& Tree)
    {
        entt::registry& Registry = World->GetEntityRegistry();

        TFunction<void(entt::entity, FTreeNodeID)> AddRecursive;
        AddRecursive = [&](entt::entity WorldEntity, FTreeNodeID ParentItem)
        {
            const SNameComponent* NameComp = Registry.try_get<SNameComponent>(WorldEntity);
            const FFixedString DisplayName = EditorEntityUtils::MakeOutlinerDisplayName(NameComp, WorldEntity);

            FTreeNodeID ItemEntity = Tree.CreateNode(ParentItem, FStringView(DisplayName.data(), DisplayName.length()));
            FTreeNodeDisplay& Display = Tree.Get<FTreeNodeDisplay>(ItemEntity);
            Display.TooltipText = FString("Entity: " + eastl::to_string(entt::to_integral(WorldEntity))).c_str();
            Display.bAllowRenaming = true;

            FTreeNodeState& State = Tree.Get<FTreeNodeState>(ItemEntity);
            State.bDisabled = Registry.any_of<SDisabledTag>(WorldEntity);

            Tree.EmplaceUserData<FEntityListViewItemData>(ItemEntity).Entity = WorldEntity;

            if (Registry.any_of<FSelectedInEditorComponent>(WorldEntity))
            {
                State.bSelected = true;
            }

            ECS::Utils::ForEachChild(Registry, WorldEntity, [&](entt::entity Child)
            {
                AddRecursive(Child, ItemEntity);
            });
        };

        // Only show prefab-tagged entities in the outliner. The root has no prefab parent.
        TVector<entt::entity> Roots;
        Registry.view<SPrefabComponent>().each([&](entt::entity E, const SPrefabComponent&)
        {
            const FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(E);
            const bool bHasPrefabParent = Rel && Rel->Parent != entt::null &&
                Registry.any_of<SPrefabComponent>(Rel->Parent);
            if (!bHasPrefabParent)
            {
                Roots.push_back(E);
            }
        });

        for (entt::entity Root : Roots)
        {
            AddRecursive(Root, InvalidTreeNode);
        }
    }

    void FPrefabEditorTool::HandleOutlinerDragDrop(FTreeListView& Tree, entt::entity DropItem)
    {
        const DragDrop::FPayload* Peek = DragDrop::PeekPayload();
        if (Peek == nullptr)
        {
            return;
        }

        if (Peek->Kind == DragDrop::EPayloadKind::Entity)
        {
            CWorld* SourceWorld = nullptr;
            entt::entity Source = entt::null;
            if (DragDrop::AcceptEntity(&SourceWorld, &Source) && SourceWorld == World)
            {
                if (Source == entt::null || Source == DropItem)
                {
                    return;
                }

                entt::registry& Registry = World->GetEntityRegistry();
                if (!Registry.valid(Source) || (DropItem != entt::null && !Registry.valid(DropItem)))
                {
                    return;
                }

                // Don't reparent the prefab root; hierarchy stays single-rooted.
                if (Source == FindPrefabRoot() || DropItem == entt::null)
                {
                    return;
                }

                // Cycle guard: dropping a parent into one of its descendants would form a loop.
                if (ECS::Utils::IsDescendantOf(Registry, DropItem, Source))
                {
                    ImGuiX::Notifications::NotifyError("Cannot reparent: target is a descendant of the dragged entity.");
                    return;
                }

                BeginTransaction();
                ECS::Utils::ReparentEntity(Registry, Source, DropItem);
                EndTransaction("Reparent");
                OutlinerListView.MarkTreeDirty();
                Asset->GetPackage()->MarkDirty();
            }
            return;
        }

        // Asset drop (static mesh, material, etc.) onto an outliner row.
        if (Peek->Kind == DragDrop::EPayloadKind::Asset && DragDrop::IsDelivered())
        {
            HandlePrefabContentDrop(FStringView(Peek->AssetPath.c_str(), Peek->AssetPath.size()), DropItem);
        }
    }

    void FPrefabEditorTool::HandlePrefabContentDrop(FStringView VirtualPath, entt::entity DropTarget)
    {
        // Default drop target is the prefab root so dropped meshes become prefab-owned children.
        if (DropTarget == entt::null)
        {
            DropTarget = FindPrefabRoot();
        }

        BeginTransaction();
        entt::entity Spawned = HandleContentBrowserAssetDrop(VirtualPath, DropTarget);
        if (Spawned != entt::null && Spawned != DropTarget)
        {
            // Mark the freshly created entity as part of the prefab so it round-trips on save.
            entt::registry& Registry = World->GetEntityRegistry();
            if (!Registry.any_of<SPrefabComponent>(Spawned))
            {
                Registry.emplace<SPrefabComponent>(Spawned).StableID = FName(FGuid::New().ToShortString());
            }
            EndTransaction("Drop Asset");
            OutlinerListView.MarkTreeDirty();
            Asset->GetPackage()->MarkDirty();
        }
        else if (Spawned == DropTarget && Spawned != entt::null)
        {
            // Existing entity was modified in-place (e.g. material override applied to mesh slot).
            EndTransaction("Drop Asset");
            Asset->GetPackage()->MarkDirty();
            bPropertyTablesDirty = true;
        }
        else
        {
            PendingBeforeState.clear();
        }
    }

    entt::entity FPrefabEditorTool::DuplicatePrefabEntity(entt::entity Source)
    {
        if (Source == entt::null || World == nullptr)
        {
            return entt::null;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Source))
        {
            return entt::null;
        }

        entt::entity NewEntity = entt::null;
        World->DuplicateEntity(NewEntity, Source, &EditorEntityUtils::DefaultDuplicateFilter);

        if (NewEntity == entt::null)
        {
            return entt::null;
        }

        // Force unique stable IDs across the whole subtree. The duplicate copies the source's
        // SPrefabComponent (and all descendant copies) verbatim, which would collide with the
        // source's entity-pairing on save.
        auto FreshenStableID = [&](entt::entity E)
        {
            if (SPrefabComponent* Prefab = Registry.try_get<SPrefabComponent>(E))
            {
                Prefab->StableID = FName(FGuid::New().ToShortString());
            }
        };
        FreshenStableID(NewEntity);
        ECS::Utils::ForEachDescendant(Registry, NewEntity, FreshenStableID);

        // Re-parent under the source's parent (or the prefab root if it had none).
        const FRelationshipComponent* SourceRel = Registry.try_get<FRelationshipComponent>(Source);
        entt::entity Parent = (SourceRel && SourceRel->Parent != entt::null) ? SourceRel->Parent : FindPrefabRoot();
        if (Parent != entt::null && Parent != NewEntity)
        {
            ECS::Utils::ReparentEntity(Registry, NewEntity, Parent);
        }

        return NewEntity;
    }

    void FPrefabEditorTool::ProcessClipboardShortcuts()
    {
        if (!bViewportHovered || World == nullptr)
        {
            return;
        }

        const ImGuiIO& IO = ImGui::GetIO();
        const bool bCopyPressed      = IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C);
        const bool bDuplicatePressed = IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D);
        const bool bPastePressed     = IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false);
        const bool bSavePressed      = IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false);

        entt::registry& Registry = World->GetEntityRegistry();
        const entt::entity Root = FindPrefabRoot();

        if (bSavePressed)
        {
            OnSave();
        }

        if (bCopyPressed)
        {
            Registry.clear<FCopiedTag>();
            Registry.view<FSelectedInEditorComponent>().each([&](entt::entity Selected)
            {
                if (Selected != Root)
                {
                    Registry.emplace_or_replace<FCopiedTag>(Selected);
                }
            });
        }

        auto DuplicateBatch = [&](TFixedVector<entt::entity, 64>& Sources, FName Label)
        {
            if (Sources.empty())
            {
                return;
            }

            BeginTransaction();
            TFixedVector<entt::entity, 64> NewlyCreated;
            for (entt::entity Source : Sources)
            {
                entt::entity New = DuplicatePrefabEntity(Source);
                if (New != entt::null)
                {
                    NewlyCreated.push_back(New);
                }
            }

            if (!NewlyCreated.empty())
            {
                ClearSelectedEntities();
                for (entt::entity New : NewlyCreated)
                {
                    AddSelectedEntity(New, false);
                }
                EndTransaction(Label);
                OutlinerListView.MarkTreeDirty();
                Asset->GetPackage()->MarkDirty();
            }
            else
            {
                PendingBeforeState.clear();
            }
        };

        if (bDuplicatePressed)
        {
            TFixedVector<entt::entity, 64> Sources;
            Registry.view<FSelectedInEditorComponent>().each([&](entt::entity Selected)
            {
                if (Selected != Root)
                {
                    Sources.push_back(Selected);
                }
            });
            DuplicateBatch(Sources, "Duplicate");
        }

        if (bPastePressed)
        {
            TFixedVector<entt::entity, 64> Sources;
            Registry.view<FCopiedTag>().each([&](entt::entity Tagged)
            {
                if (Tagged != Root)
                {
                    Sources.push_back(Tagged);
                }
            });
            DuplicateBatch(Sources, "Paste");
        }
    }

    void FPrefabEditorTool::DrawOutliner(bool bFocused)
    {
        if (World == nullptr)
        {
            return;
        }

        DrawAddEntityButton();
        ImGui::SameLine();

        const float SearchWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(SearchWidth);
        if (OutlinerNameFilter.Draw("##PrefabSearch"))
        {
            OutlinerListView.InvalidateVisibleList();
        }

        if (!OutlinerNameFilter.IsActive())
        {
            const ImGuiStyle& Style = ImGui::GetStyle();
            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImVec2 TextPos = ImGui::GetItemRectMin();
            TextPos.x += Style.FramePadding.x + 2.0f;
            TextPos.y += Style.FramePadding.y;
            DrawList->AddText(TextPos, IM_COL32(110, 110, 110, 255), LE_ICON_FILE_SEARCH " Search...");
        }

        ImGui::Separator();

        // Stat row: total prefab entity count + a manual rebuild button.
        size_t EntityCount = 0;
        World->GetEntityRegistry().view<SPrefabComponent>().each([&](entt::entity, const SPrefabComponent&)
        {
            ++EntityCount;
        });
        ImGui::Text(LE_ICON_FORMAT_LIST_NUMBERED " Entities: %zu", EntityCount);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24 - ImGui::GetStyle().FramePadding.x);
        if (ImGui::Button(LE_ICON_REFRESH))
        {
            OutlinerListView.MarkTreeDirty();
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        if (ImGui::BeginChild("##PrefabEntityList", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar))
        {
            OutlinerListView.Draw(OutlinerContext);

            // Drop into empty area drops onto the prefab root.
            if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(), ImGui::GetCurrentWindow()->ID))
            {
                const DragDrop::FPayload* Peek = DragDrop::PeekPayload();
                if (Peek && Peek->Kind == DragDrop::EPayloadKind::Asset && DragDrop::IsDelivered())
                {
                    HandlePrefabContentDrop(FStringView(Peek->AssetPath.c_str(), Peek->AssetPath.size()), entt::null);
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void FPrefabEditorTool::DrawAddComponentButton(entt::entity Entity)
    {
        if (!ImGui::Button(LE_ICON_PLUS " Add Component"))
        {
            return;
        }
        AddComponentFilter.Clear();
        ImGui::OpenPopup("##AddPrefabComponent");
    }

    void FPrefabEditorTool::RebuildPropertyTables(entt::entity Entity)
    {
        ComponentPropertyTables.clear();
        ComponentStructs.clear();

        if (World == nullptr || !World->GetEntityRegistry().valid(Entity))
        {
            return;
        }

        using namespace entt::literals;

        entt::registry& Registry = World->GetEntityRegistry();

        struct FPair { void* Ptr; CStruct* Struct; };
        TVector<FPair> Sorted;

        ECS::Utils::ForEachComponent(Registry, Entity, [&](void* CompPtr, entt::basic_sparse_set<>& /*Storage*/, entt::meta_type MetaType)
        {
            entt::meta_any StructAny = ECS::Utils::InvokeMetaFunc(MetaType, "static_struct"_hs);
            if (!StructAny)
            {
                return;
            }

            CStruct* Struct = StructAny.cast<CStruct*>();
            if (Struct == nullptr)
            {
                return;
            }

            // Hide tool-internal bookkeeping from the property panel; it's never user-editable.
            const FName StructName = Struct->GetName();
            if (StructName == FName("SPrefabComponent"))
            {
                return;
            }

            Sorted.push_back({ CompPtr, Struct });
        });

        // Same priority ordering the world editor uses: name → transform → everything else alphabetically.
        eastl::sort(Sorted.begin(), Sorted.end(), [&](const FPair& LHS, const FPair& RHS)
        {
            auto Priority = [](const CStruct* Type)
            {
                if (Type == SNameComponent::StaticStruct())      return 0;
                if (Type == STransformComponent::StaticStruct()) return 1;
                return 2;
            };

            const uint32 APriority = Priority(LHS.Struct);
            const uint32 BPriority = Priority(RHS.Struct);
            if (APriority != BPriority)
            {
                return APriority < BPriority;
            }
            return LHS.Struct->MakeDisplayName() < RHS.Struct->MakeDisplayName();
        });

        for (const FPair& Pair : Sorted)
        {
            auto Table = MakeUnique<FPropertyTable>(Pair.Ptr, Pair.Struct);
            Table->SetPreEditCallback([this](const FPropertyChangedEvent&)
            {
                BeginTransaction();
            });
            Table->SetFinishEditCallback([this](const FPropertyChangedEvent& Event)
            {
                EndTransaction(Event.PropertyName);
            });
            Table->SetPostEditCallback([this](const FPropertyChangedEvent&)
            {
                if (Asset && Asset->GetPackage())
                {
                    Asset->GetPackage()->MarkDirty();
                }
            });
            ComponentPropertyTables.push_back(eastl::move(Table));
            ComponentStructs.push_back(Pair.Struct);
        }
    }

    void FPrefabEditorTool::DrawComponentList(entt::entity Entity)
    {
        for (size_t i = 0; i < ComponentPropertyTables.size(); ++i)
        {
            DrawComponentHeader(ComponentPropertyTables[i], Entity, ComponentStructs[i]);
            ImGui::Spacing();
        }
    }

    void FPrefabEditorTool::DrawComponentHeader(const TUniquePtr<FPropertyTable>& Table, entt::entity Entity, CStruct* StructType)
    {
        const bool bIsRequired = (StructType == STransformComponent::StaticStruct() || StructType == SNameComponent::StaticStruct());

        ImGui::PushID(Table.get());

        constexpr ImGuiTableFlags Flags =
            ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_NoBordersInBodyUntilResize |
            ImGuiTableFlags_SizingFixedFit;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
        bool bIsOpen = false;
        if (ImGui::BeginTable("##GridTable", 1, Flags))
        {
            ImGui::TableSetupColumn("##Header", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();

            ImGui::PushStyleColor(ImGuiCol_Header, 0xFF3A3A3A);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0xFF484848);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0xFF404040);
            ImGui::SetNextItemAllowOverlap();
            bIsOpen = ImGui::CollapsingHeader(StructType->MakeDisplayName().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, 0xFF1C1C1C);
            ImGui::PopStyleColor(3);

            if (!bIsRequired)
            {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 28.0f);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.25f, 0.25f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.2f, 0.2f, 0.9f));
                if (ImGui::SmallButton(LE_ICON_TRASH_CAN "##Remove"))
                {
                    BeginTransaction();
                    RemoveComponent(Entity, StructType);
                    EndTransaction("Remove Component");
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Remove component");
                }
            }

            ImGui::EndTable();
        }
        ImGui::PopStyleVar(2);

        if (bIsOpen)
        {
            Table->DrawTree();
        }

        ImGui::PopID();
    }

    void FPrefabEditorTool::DrawEmptyState()
    {
        ImVec2 WindowSize = ImGui::GetWindowSize();
        ImVec2 CenterPos = ImVec2(WindowSize.x * 0.5f - 100.0f, WindowSize.y * 0.5f - 40.0f);
        ImGui::SetCursorPos(CenterPos);

        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));
        ImGui::TextUnformatted(LE_ICON_INBOX "  Nothing selected");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
        ImGui::TextUnformatted("Select an entity to edit its properties.");
        ImGui::PopStyleColor();
        ImGui::EndGroup();
    }

    void FPrefabEditorTool::DrawEntityProperties(bool bFocused)
    {
        if (World == nullptr)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        const entt::entity SelectedEntity = GetLastSelectedEntity();
        if (SelectedEntity == entt::null || !Registry.valid(SelectedEntity))
        {
            DrawEmptyState();
            return;
        }

        if (SNameComponent* Name = Registry.try_get<SNameComponent>(SelectedEntity))
        {
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::LargeBold);
            ImGui::TextUnformatted(Name->Name.c_str());
            ImGui::PopFont();

            char Buffer[128];
            const FString NameStr = Name->Name.ToString();
            std::snprintf(Buffer, sizeof(Buffer), "%s", NameStr.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##Rename", Buffer, sizeof(Buffer), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                BeginTransaction();
                Name->Name = FName(Buffer);
                EndTransaction("Rename");
                OutlinerListView.MarkTreeDirty();
                Asset->GetPackage()->MarkDirty();
            }
        }

        // Multi-select badge so it's obvious when typing affects more than one entity downstream.
        if (SelectedEntities.size() > 1)
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            ImGui::Text("(+%zu more selected)", SelectedEntities.size() - 1);
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        DrawAddComponentButton(SelectedEntity);
        ImGui::SameLine();

        const entt::entity Root = FindPrefabRoot();
        ImGui::BeginDisabled(SelectedEntity == Root);
        if (ImGui::Button(LE_ICON_DELETE " Delete Entity"))
        {
            RequestDestroyEntity(SelectedEntity);
        }
        ImGui::EndDisabled();
        if (SelectedEntity == Root && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("The prefab root cannot be deleted.");
        }

        if (ImGui::BeginPopup("##AddPrefabComponent"))
        {
            using namespace entt::literals;

            ImGui::SetNextItemWidth(250.0f);
            AddComponentFilter.Draw("##Search", 250.0f);

            TVector<TPair<entt::meta_type, CStruct*>> Sorted;
            for (auto&& [ID, MetaType] : entt::resolve())
            {
                ECS::ETraits Traits = MetaType.traits<ECS::ETraits>();
                if (!EnumHasAllFlags(Traits, ECS::ETraits::Component))
                {
                    continue;
                }

                entt::meta_any StructAny = ECS::Utils::InvokeMetaFunc(MetaType, "static_struct"_hs);
                if (!StructAny)
                {
                    continue;
                }

                CStruct* Struct = StructAny.cast<CStruct*>();
                if (Struct == nullptr || Struct->HasMeta("HideInComponentList"))
                {
                    continue;
                }

                const FString DisplayName = Struct->MakeDisplayName().c_str();
                if (!AddComponentFilter.PassFilter(DisplayName.c_str()))
                {
                    continue;
                }

                Sorted.emplace_back(MetaType, Struct);
            }

            eastl::sort(Sorted.begin(), Sorted.end(), [](const auto& LHS, const auto& RHS)
            {
                return LHS.second->MakeDisplayName() < RHS.second->MakeDisplayName();
            });

            const float ChildHeight = ImGui::GetTextLineHeightWithSpacing() * 12.0f;
            if (ImGui::BeginChild("##PrefabComponentList", ImVec2(0, ChildHeight), true))
            {
                for (auto&& [MetaType, Struct] : Sorted)
                {
                    if (ImGui::Selectable(Struct->MakeDisplayName().c_str()))
                    {
                        BeginTransaction();
                        ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs,
                            entt::forward_as_meta(Registry), SelectedEntity, entt::forward_as_meta(entt::meta_any{}));
                        EndTransaction("Add Component");
                        bPropertyTablesDirty = true;
                        Asset->GetPackage()->MarkDirty();
                    }
                }
            }
            ImGui::EndChild();

            ImGui::EndPopup();
        }

        ImGui::Separator();

        DrawComponentList(SelectedEntity);
    }

    void FPrefabEditorTool::DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize)
    {
        // Cycle gizmo op on Space, like the world editor.
        if (bViewportHovered && ImGui::IsKeyPressed(ImGuiKey_Space))
        {
            EditorEntityUtils::CycleGizmoOp(GuizmoOp);
        }

        SCameraComponent* CameraComponent = World->GetEntityRegistry().try_get<SCameraComponent>(EditorEntity);
        if (CameraComponent == nullptr)
        {
            return;
        }

        glm::mat4 ViewMatrix = CameraComponent->GetViewMatrix();
        glm::mat4 ProjectionMatrix = CameraComponent->GetProjectionMatrix();
        ProjectionMatrix[1][1] *= -1.0f;

        const ImVec2 ViewportOrigin = ImGui::GetCursorScreenPos();

        ImGuizmo::SetDrawlist(ImGui::GetCurrentWindow()->DrawList);
        ImGuizmo::SetRect(ViewportOrigin.x, ViewportOrigin.y, ViewportSize.x, ViewportSize.y);

        // Drop-target on the viewport: dropping a content-browser asset onto empty space spawns it
        // under the prefab root. Mirrors the world editor's behavior.
        {
            const ImRect ViewportRect(ViewportOrigin, ImVec2(ViewportOrigin.x + ViewportSize.x, ViewportOrigin.y + ViewportSize.y));
            if (ImGui::BeginDragDropTargetCustom(ViewportRect, ImGui::GetCurrentWindow()->ID))
            {
                const DragDrop::FPayload* Peek = DragDrop::PeekPayload();
                if (Peek && Peek->Kind == DragDrop::EPayloadKind::Asset && DragDrop::IsDelivered())
                {
                    HandlePrefabContentDrop(FStringView(Peek->AssetPath.c_str(), Peek->AssetPath.size()), entt::null);
                }
                ImGui::EndDragDropTarget();
            }
        }

        const entt::entity PivotEntity = GetLastSelectedEntity();
        const bool bGizmoTargetValid = PivotEntity != entt::null && World->GetEntityRegistry().valid(PivotEntity);

        // Mid-drag selection vanish: end the transaction so future clicks aren't blocked by IsOver().
        if (!bGizmoTargetValid && bImGuizmoUsedOnce)
        {
            EndTransaction("Transform");
            bImGuizmoUsedOnce = false;
        }

        if (!bGizmoTargetValid)
        {
            return;
        }

        STransformComponent* PivotTransform = World->GetEntityRegistry().try_get<STransformComponent>(PivotEntity);
        if (PivotTransform == nullptr)
        {
            return;
        }

        glm::mat4 EntityMatrix = PivotTransform->GetWorldMatrix();
        glm::mat4 PreManipulate = EntityMatrix;

        float* SnapValues = nullptr;
        float SnapArray[3] = {};
        if (bGuizmoSnapEnabled)
        {
            switch (GuizmoOp)
            {
            case ImGuizmo::TRANSLATE:
                SnapArray[0] = SnapArray[1] = SnapArray[2] = GuizmoSnapTranslate;
                SnapValues = SnapArray;
                break;
            case ImGuizmo::ROTATE:
                SnapArray[0] = SnapArray[1] = SnapArray[2] = GuizmoSnapRotate;
                SnapValues = SnapArray;
                break;
            case ImGuizmo::SCALE:
                SnapArray[0] = SnapArray[1] = SnapArray[2] = GuizmoSnapScale;
                SnapValues = SnapArray;
                break;
            }
        }

        ImGuizmo::Manipulate(glm::value_ptr(ViewMatrix), glm::value_ptr(ProjectionMatrix),
            GuizmoOp, GuizmoMode, glm::value_ptr(EntityMatrix), nullptr, SnapValues);

        if (ImGuizmo::IsUsing())
        {
            if (!bImGuizmoUsedOnce)
            {
                BeginTransaction();
                bImGuizmoUsedOnce = true;
            }

            entt::registry& Registry = World->GetEntityRegistry();

            // Pivot delta in world space. Apply the same translation/rotation to every selected
            // entity so multi-select drags move as a rigid group; scale is per-entity to avoid
            // skew artefacts when selection has mixed parent transforms.
            glm::mat4 DeltaWorld = EntityMatrix * glm::inverse(PreManipulate);
            glm::vec3 DeltaTranslation, DeltaScale, DeltaSkew;
            glm::quat DeltaRotation;
            glm::vec4 DeltaPersp;
            glm::decompose(DeltaWorld, DeltaScale, DeltaRotation, DeltaTranslation, DeltaSkew, DeltaPersp);

            // Pivot itself: drive it directly with the manipulator's full output.
            EditorEntityUtils::ApplyWorldMatrixToTransform(Registry, PivotEntity, EntityMatrix);

            // Co-move every other selected entity by the pivot's delta. Skip locked-prefab-style
            // children — prefab editor has no locked instances, so the only filter is "valid + not pivot".
            const glm::vec3 PivotPreLocation = glm::vec3(PreManipulate[3]);
            for (entt::entity Other : SelectedEntities)
            {
                if (Other == PivotEntity || !Registry.valid(Other))
                {
                    continue;
                }
                STransformComponent* OtherTransform = Registry.try_get<STransformComponent>(Other);
                if (OtherTransform == nullptr)
                {
                    continue;
                }

                const glm::mat4 OtherWorld = OtherTransform->GetWorldMatrix();
                glm::mat4 NewWorld = OtherWorld;

                switch (GuizmoOp)
                {
                case ImGuizmo::TRANSLATE:
                    NewWorld[3] = glm::vec4(glm::vec3(OtherWorld[3]) + DeltaTranslation, 1.0f);
                    break;
                case ImGuizmo::ROTATE:
                {
                    const glm::vec3 OffsetFromPivot = glm::vec3(OtherWorld[3]) - PivotPreLocation;
                    const glm::vec3 RotatedOffset   = DeltaRotation * OffsetFromPivot;
                    NewWorld = glm::translate(glm::mat4(1.f), PivotPreLocation + RotatedOffset)
                             * glm::mat4_cast(DeltaRotation)
                             * glm::mat4(glm::mat3(OtherWorld));
                    break;
                }
                case ImGuizmo::SCALE:
                {
                    const glm::vec3 OffsetFromPivot = glm::vec3(OtherWorld[3]) - PivotPreLocation;
                    const glm::vec3 ScaledOffset    = OffsetFromPivot * DeltaScale;
                    glm::quat OtherRot;
                    glm::vec3 OtherTr, OtherSc, OtherSk;
                    glm::vec4 OtherPe;
                    glm::decompose(OtherWorld, OtherSc, OtherRot, OtherTr, OtherSk, OtherPe);
                    NewWorld = glm::translate(glm::mat4(1.f), PivotPreLocation + ScaledOffset)
                             * glm::mat4_cast(OtherRot)
                             * glm::scale(glm::mat4(1.f), OtherSc * DeltaScale);
                    break;
                }
                default: break;
                }

                EditorEntityUtils::ApplyWorldMatrixToTransform(Registry, Other, NewWorld);
            }

            Asset->GetPackage()->MarkDirty();
        }
        else if (bImGuizmoUsedOnce)
        {
            EndTransaction("Transform");
            bImGuizmoUsedOnce = false;
        }
    }

    void FPrefabEditorTool::DrawViewportToolbar(const FUpdateContext& UpdateContext)
    {
        // Compact in-viewport toolbar mirroring the world editor's gizmo strip.
        const float ButtonSize = 28.0f;

        if (BeginViewportToolbarGroup("PrefabGizmoBar", ImVec2(0, 0), ImVec2(4, 4)))
        {
            auto GizmoButton = [&](const char* Icon, const char* Tooltip, ImGuizmo::OPERATION Op)
            {
                const bool bActive = (GuizmoOp == Op);
                if (bActive)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.85f, 1.0f));
                }
                if (ImGui::Button(Icon, ImVec2(ButtonSize, ButtonSize)))
                {
                    GuizmoOp = Op;
                }
                if (bActive)
                {
                    ImGui::PopStyleColor();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::SetTooltip("%s", Tooltip);
                }
                ImGui::SameLine();
            };

            GizmoButton(LE_ICON_AXIS_ARROW,         "Translate (W)", ImGuizmo::TRANSLATE);
            GizmoButton(LE_ICON_ROTATE_ORBIT,       "Rotate (E)",    ImGuizmo::ROTATE);
            GizmoButton(LE_ICON_RESIZE,             "Scale (R)",     ImGuizmo::SCALE);

            // Mode toggle (X).
            const char* ModeLabel = (GuizmoMode == ImGuizmo::WORLD) ? LE_ICON_EARTH : LE_ICON_AXIS_ARROW_LOCK;
            if (ImGui::Button(ModeLabel, ImVec2(ButtonSize, ButtonSize)))
            {
                EditorEntityUtils::ToggleGizmoMode(GuizmoMode);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip(GuizmoMode == ImGuizmo::WORLD ? "World space (X)" : "Local space (X)");
            }
            ImGui::SameLine();

            // Snap toggle + popup.
            if (bGuizmoSnapEnabled)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.85f, 1.0f));
            }
            if (ImGui::Button(LE_ICON_GRID, ImVec2(ButtonSize, ButtonSize)))
            {
                bGuizmoSnapEnabled = !bGuizmoSnapEnabled;
            }
            if (bGuizmoSnapEnabled)
            {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Toggle gizmo snap");
            }
            ImGui::SameLine();
            if (ImGui::Button(LE_ICON_COG "##SnapSettings", ImVec2(ButtonSize, ButtonSize)))
            {
                ImGui::OpenPopup("##PrefabSnapSettings");
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Snap step settings");
            }

            EndViewportToolbarGroup();
        }

        if (ImGui::BeginPopup("##PrefabSnapSettings"))
        {
            ImGui::TextUnformatted("Snap Steps");
            ImGui::Separator();
            ImGui::DragFloat("Translate", &GuizmoSnapTranslate, 0.01f, 0.001f, 100.0f);
            ImGui::DragFloat("Rotate (deg)", &GuizmoSnapRotate, 0.5f, 0.1f, 90.0f);
            ImGui::DragFloat("Scale", &GuizmoSnapScale, 0.01f, 0.001f, 10.0f);
            ImGui::EndPopup();
        }
    }

    void FPrefabEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Authoring",
            "Build the prefab in this isolated preview world like you would a normal scene. "
            "Saving commits the entity hierarchy back to the prefab asset.");
        DrawHelpTextRow("Selection / Gizmo",
            "Same controls as the world editor: W/E/R for translate/rotate/scale, X for World/Local, "
            "Ctrl-click multi-select, F frames the selection.");
        DrawHelpTextRow("Components",
            "Add Component on a selected entity to attach. Drag an asset directly onto the entity row "
            "in the outliner for shortcut adds (e.g. drop a static mesh to add a SStaticMeshComponent).");
        DrawHelpTextRow("Nested Prefabs",
            "Drag another prefab asset into the outliner to instance it as a child. Property overrides "
            "are per-instance; structural changes happen on the source prefab.");
        DrawHelpTextRow("Save",
            "Ctrl+S commits all entities, components and overrides to the prefab. Existing instances "
            "in worlds reload on next open or with Reload Asset.");
    }

    void FPrefabEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::DrawToolMenu(UpdateContext);

        if (ImGui::BeginMenu(LE_ICON_MOVE_RESIZE " Gizmo"))
        {
            const char* Ops[] = { "Translate", "Rotate", "Scale" };
            int Current = (GuizmoOp == ImGuizmo::TRANSLATE) ? 0 : (GuizmoOp == ImGuizmo::ROTATE ? 1 : 2);
            if (ImGui::Combo("##GizmoOp", &Current, Ops, IM_ARRAYSIZE(Ops)))
            {
                switch (Current)
                {
                case 0: GuizmoOp = ImGuizmo::TRANSLATE; break;
                case 1: GuizmoOp = ImGuizmo::ROTATE;    break;
                case 2: GuizmoOp = ImGuizmo::SCALE;     break;
                }
            }

            const char* Modes[] = { "World", "Local" };
            int ModeIdx = (GuizmoMode == ImGuizmo::WORLD) ? 0 : 1;
            if (ImGui::Combo("##GizmoMode", &ModeIdx, Modes, IM_ARRAYSIZE(Modes)))
            {
                GuizmoMode = (ModeIdx == 0) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
            }

            ImGui::Checkbox("Snap", &bGuizmoSnapEnabled);
            ImGui::DragFloat("Translate Step", &GuizmoSnapTranslate, 0.01f, 0.001f, 100.0f);
            ImGui::DragFloat("Rotate Step (deg)", &GuizmoSnapRotate, 0.5f, 0.1f, 90.0f);
            ImGui::DragFloat("Scale Step", &GuizmoSnapScale, 0.01f, 0.001f, 10.0f);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(LE_ICON_EYE " View"))
        {
            ImGui::Checkbox("World Grid", &bWorldGridEnabled);
            ImGui::Checkbox("Component Visualizers", &bShowComponentVisualizers);
            if (ImGui::MenuItem(LE_ICON_HOME " Frame All", "Home"))
            {
                FrameAllEntities();
            }
            if (ImGui::MenuItem(LE_ICON_TARGET " Focus Selection", "F"))
            {
                FocusViewportToEntity(GetLastSelectedEntity());
            }
            ImGui::EndMenu();
        }
    }

    void FPrefabEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);

        ImGuiID LeftDockID = 0, RightDockID = 0;
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.3f, &RightDockID, &LeftDockID);

        ImGuiID LeftOutlinerID = 0, LeftViewportID = 0;
        ImGui::DockBuilderSplitNode(LeftDockID, ImGuiDir_Left, 0.25f, &LeftOutlinerID, &LeftViewportID);

        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(), LeftViewportID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(OutlinerWindowName).c_str(), LeftOutlinerID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(PropertiesWindowName).c_str(), RightDockID);
    }
}
