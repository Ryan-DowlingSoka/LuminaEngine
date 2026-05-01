#include "WorldEditorTool.h"
#include <glm/gtx/string_cast.hpp>
#include "EditorToolContext.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/Prefabs/Prefab.h"
#include "Assets/AssetTypes/Prefabs/PrefabComponents.h"
#include "Components/EditorEntityTags.h"
#include "ContentBrowserEditorTool.h"
#include "Config/Config.h"
#include "Core/Application/Application.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Object/Package/Package.h"
#include "Core/Serialization/JsonArchiver.h"
#include "Core/Serialization/ObjectArchiver.h"
#include "EASTL/sort.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "Input/InputContext.h"
#include "Input/InputProcessor.h"
#include "Input/InputViewport.h"
#include "Memory/SmartPtr.h"
#include "Thumbnails/ThumbnailManager.h"
#include "Tools/ComponentVisualizers/ComponentVisualizer.h"
#include "Tools/Dialogs/Dialogs.h"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "World/WorldManager.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "world/entity/components/entitytags.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/ScriptComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/TagComponent.h"
#include "World/Scene/RenderScene/RenderScene.h"
#include "World/Scene/RenderScene/SceneRenderTypes.h"
#include "World/Subsystems/WorldSettings.h"


namespace Lumina
{
    static constexpr const char* WorldSettingsName = "World Settings";
    static constexpr const char* SceneGraphName = "Scene Graph";

    // Non-root prefab-instance members are locked against hierarchy edits so
    // the instance stays faithful to its source. Only the root moves/deletes.
    static bool IsLockedPrefabChild(const entt::registry& Registry, entt::entity Entity)
    {
        if (Entity == entt::null || !Registry.valid(Entity))
        {
            return false;
        }
        const SPrefabInstanceComponent* Instance = Registry.try_get<SPrefabInstanceComponent>(Entity);
        return Instance != nullptr && !Instance->bIsRoot;
    }

    // Viewport picks should always resolve to the prefab root so the user
    // selects the prefab as a unit. The outliner still allows sub-entity picks.
    static entt::entity ResolvePrefabRootForViewportPick(entt::registry& Registry, entt::entity Entity)
    {
        if (Entity == entt::null || !Registry.valid(Entity))
        {
            return Entity;
        }

        const SPrefabInstanceComponent* Instance = Registry.try_get<SPrefabInstanceComponent>(Entity);
        if (Instance == nullptr || Instance->bIsRoot)
        {
            return Entity;
        }

        const FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        while (Relationship != nullptr && Relationship->Parent != entt::null)
        {
            entt::entity Parent = Relationship->Parent;
            if (const SPrefabInstanceComponent* ParentInstance = Registry.try_get<SPrefabInstanceComponent>(Parent))
            {
                if (ParentInstance->bIsRoot)
                {
                    return Parent;
                }
            }
            Relationship = Registry.try_get<FRelationshipComponent>(Parent);
        }

        return Entity;
    }
    static constexpr const char* DragDropID = "EntityDropID";


    FWorldEditorTool::FWorldEditorTool(IEditorToolContext* Context, CWorld* InWorld)
        : FEditorTool(Context, "World Editor", InWorld)
    {
        GuizmoOp = ImGuizmo::TRANSLATE;
        GuizmoMode = ImGuizmo::WORLD;
    }

    void FWorldEditorTool::OnInitialize()
    {
        CreateToolWindow(SceneGraphName, [&] (bool bFocused)
        {
            DrawOutliner(bFocused);
        });
        
        CreateToolWindow(WorldSettingsName, [&](bool bFocused)
        {
            DrawWorldSettings(bFocused);
        });
        
        CreateToolWindow("Details", [&] (bool bFocused)
        {
            DrawEntityEditor(bFocused, LastSelectedEntity);
        });
        
        bGuizmoSnapEnabled  = GConfig->Get("Editor.WorldEditorTool.GuizmoSnapEnabled", true);
        GuizmoSnapTranslate = GConfig->Get("Editor.WorldEditorTool.GuizmoSnapTranslate", 0.1f);
        GuizmoSnapRotate    = GConfig->Get("Editor.WorldEditorTool.GuizmoSnapRotate", 5.0f);
        GuizmoSnapScale     = GConfig->Get("Editor.WorldEditorTool.GuizmoSnapScale", 0.1f);

        //------------------------------------------------------------------------------------------------------
        
        WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());
        
        OutlinerContext.SetDragDropFunction = [this] (FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);

            entt::entity Payload = Data.Entity;
            ImGui::SetDragDropPayload(DragDropID, &Payload, sizeof(Payload));
        };

        OutlinerContext.ItemContextMenuFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            FEntityRegistry& Registry = World->GetEntityRegistry();
            const bool bLocked = IsLockedPrefabChild(Registry, Data.Entity);

            if (bLocked)
            {
                ImGui::TextDisabled(LE_ICON_LOCK " Locked (Prefab Instance)");
                ImGuiX::TextTooltip("{}", "This entity belongs to a prefab instance. Edit the source prefab to change its hierarchy.");
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Add Component"))
            {
                PushAddComponentModal(Data.Entity);
            }
            ImGuiX::TextTooltip("{}", "Add a new component to the entity");


            if (ImGui::MenuItem("Copy Entity ID"))
            {
                ImGui::SetClipboardText(eastl::to_string(entt::to_integral(Data.Entity)).c_str());
            }

            ImGuiX::TextTooltip("{}", "Copy entity identifier to platform clipboard");

            if (!bLocked && ECS::Utils::IsChild(Registry, Data.Entity))
            {
                if (ImGui::MenuItem("Unparent"))
                {
                    BeginTransaction();
                    ECS::Utils::RemoveFromParent(Registry, Data.Entity);
                    EndTransaction("Unparent");
                    ReparentEntityInOutliner(Data.Entity);
                }
            }

            if (!bLocked && ECS::Utils::IsParent(Registry, Data.Entity))
            {
                if (ImGui::MenuItem("Detach Children"))
                {
                    // Snapshot child IDs before mutating relationships, then move each in the tree.
                    TVector<entt::entity> Children;
                    ECS::Utils::ForEachChild(Registry, Data.Entity, [&](entt::entity Child) { Children.push_back(Child); });
                    BeginTransaction();
                    ECS::Utils::DetachImmediateChildren(Registry, Data.Entity);
                    EndTransaction("Detach Children");
                    for (entt::entity Child : Children)
                    {
                        ReparentEntityInOutliner(Child);
                    }
                }
            }

            if (ImGui::MenuItem("Rename"))
            {
                PushRenameEntityModal(Data.Entity);
            }

            if (!bLocked && ImGui::MenuItem("Duplicate"))
            {
                BeginTransaction();
                entt::entity New = entt::null;
                CopyEntity(New, Data.Entity);
                if (New != entt::null)
                {
                    EndTransaction("Duplicate");
                }
                else
                {
                    PendingBeforeState.clear();
                }
            }

            if (!bLocked && ImGui::MenuItem("Delete"))
            {
                EntityDestroyRequests.push(Data.Entity);
            }
        };
        
        OutlinerContext.VisibilityToggleFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            FTreeNodeState& State = Tree.Get<FTreeNodeState>(Item);
            
            if (State.bDisabled)
            {
                World->GetEntityRegistry().emplace<SDisabledTag>(Data.Entity);
            }
            else
            {
                World->GetEntityRegistry().remove<SDisabledTag>(Data.Entity);
            }
        };

        OutlinerContext.RebuildTreeFunction = [this](FTreeListView& Tree)
        {
            RebuildSceneOutliner(Tree);
        };

        OutlinerContext.BuildChildrenFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            BuildEntityChildren(Tree, Item);
        };

        OutlinerContext.RenameFunction = [this](FTreeListView& Tree, FTreeNodeID Item, FStringView NewName)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);

            FFixedString Name;
            Name.append(LE_ICON_CUBE).append(" ")
                .append_convert(NewName.begin(), NewName.length()).append_convert(FString(" - (" + eastl::to_string(entt::to_integral(Data.Entity)) + ")"));

			Tree.Get<FTreeNodeDisplay>(Item).DisplayName = Name;

            SNameComponent& NameComponent = World->GetEntityRegistry().get<SNameComponent>(Data.Entity);
            NameComponent.Name = NewName;
		};
        
        OutlinerContext.ItemSelectedFunction = [this](FTreeListView& Tree, FTreeNodeID Item, bool bShouldClear)
        {
            // bShouldClear == true means a plain click (no Ctrl): replace the whole selection.
            // bShouldClear == false is the Ctrl-click path: toggle this entity in/out without
            // disturbing the others. The tree widget never writes bSelected for these rows
            // itself; SetSingleSelectedEntity / ToggleSelectedEntity below handle it so the
            // canonical set, registry tags, and outliner stay consistent in one place.
            if (!Item.IsValid())
            {
                if (bShouldClear)
                {
                    ClearSelectedEntities();
                }
                return;
            }

            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);

            if (bShouldClear)
            {
                SetSingleSelectedEntity(Data.Entity);
            }
            else
            {
                ToggleSelectedEntity(Data.Entity);
            }
        };

        OutlinerContext.DragDropFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);

            HandleEntityEditorDragDrop(Tree, Data.Entity);
        };

        OutlinerContext.FilterFunction = [&](FTreeListView& Tree, FTreeNodeID Item)
        {
            using namespace entt::literals;
            
            const FTreeNodeDisplay& Display = Tree.Get<FTreeNodeDisplay>(Item);
            
            bool bPasses = EntityFilterState.FilterName.PassFilter(Display.DisplayName.c_str());

            for (const FName& ComponentFilter : EntityFilterState.ComponentFilters)
            {
                FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);

                entt::entity Entity = Data.Entity;
                
                if (entt::meta_type Meta = entt::resolve(entt::hashed_string(ComponentFilter.c_str())))
                {
                    entt::meta_any Return = ECS::Utils::InvokeMetaFunc(Meta, "has"_hs, entt::forward_as_meta(World->GetEntityRegistry()), Entity);
                    if (!Return.cast<bool>())
                    {
                        bPasses = false;
                    }
                }
            }
            
            return bPasses;
        };


        //------------------------------------------------------------------------------------------------------

        RebindRegistryObservers();

        WorldTravelledHandle = FCoreDelegates::OnWorldTravelled.AddMember(this, &FWorldEditorTool::OnWorldTravelled);
    }

    void FWorldEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
        FCoreDelegates::OnWorldTravelled.Remove(WorldTravelledHandle);
        WorldTravelledHandle = FDelegateHandle{};

        if (bSimulatingWorld)
        {
            SetWorldNewSimulate(false);
        }

        if (bGamePreviewRunning)
        {
            OnGamePreviewStopRequested.Broadcast();
        }
    }

    void FWorldEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FEditorTool::Update(UpdateContext);

        DrawWorldGrid();

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

        if (!EntityDestroyRequests.empty())
        {
            // Snapshot the registry once for the whole batch so a Delete keypress that
            // queues several entities collapses to a single undo step.
            BeginTransaction();
            bool bDestroyed = false;
            while (!EntityDestroyRequests.empty())
            {
                entt::entity Entity = EntityDestroyRequests.front();
                EntityDestroyRequests.pop();

                if (!World->GetEntityRegistry().valid(Entity))
                {
                    LOG_WARN("Attempted to delete an invalid entity! {}", entt::to_integral(Entity));
                    continue;
                }

                World->DestroyEntity(Entity);
                bDestroyed = true;
                // OutlinerListView is updated via OnOutlinerEntityDestroyed.
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

        auto View = World->GetEntityRegistry().view<FSelectedInEditorComponent>();

        if (bViewportHovered)
        {
            bool bCopyPressed = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_C);
            bool bDuplicatePressed = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_D);
            bool bDeletePressed = ImGui::IsKeyPressed(ImGuiKey_Delete);

            if (bCopyPressed)
            {
                ClearCopies();
            }

            // Snapshot the selection before mutating: duplicate and delete both walk the same
            // set, so iterating the view directly while emitting new entities (or destroying
            // current ones) would invalidate iterators or trip the "iterator overtook end"
            // assertion. Capture once, then act.
            TFixedVector<entt::entity, 64> CurrentSelection;
            CurrentSelection.reserve(SelectedEntities.size());
            for (entt::entity Selected : SelectedEntities)
            {
                if (World->GetEntityRegistry().valid(Selected))
                {
                    CurrentSelection.push_back(Selected);
                }
            }

            TFixedVector<entt::entity, 64> NewlyDuplicated;

            // Snapshot once if duplicate is happening so the Ctrl+D batch is a single undo.
            const bool bWantDuplicateTransaction = bDuplicatePressed;
            if (bWantDuplicateTransaction)
            {
                BeginTransaction();
            }

            for (entt::entity SelectedEntity : CurrentSelection)
            {
                World->GetEntityRegistry().emplace_or_replace<FNeedsTransformUpdate>(SelectedEntity);

                const bool bLocked = IsLockedPrefabChild(World->GetEntityRegistry(), SelectedEntity);

                if (bCopyPressed)
                {
                    AddEntityToCopies(SelectedEntity);
                }

                if (bDuplicatePressed && !bLocked)
                {
                    entt::entity New = entt::null;
                    CopyEntity(New, SelectedEntity);
                    if (New != entt::null)
                    {
                        NewlyDuplicated.push_back(New);
                    }
                }

                if (bDeletePressed && !bLocked)
                {
                    EntityDestroyRequests.push(SelectedEntity);
                    // Selection is cleaned up via OnEntityDestroyed when the destroy lands.
                }
            }

            // Replace the selection with the duplicates so the user can immediately keep
            // moving them (Ctrl+D → Ctrl+D feels right when the new copies are selected).
            if (bDuplicatePressed && !NewlyDuplicated.empty())
            {
                ClearSelectedEntities();
                for (entt::entity New : NewlyDuplicated)
                {
                    AddSelectedEntity(New, false);
                }
            }

            if (bWantDuplicateTransaction)
            {
                if (!NewlyDuplicated.empty())
                {
                    EndTransaction("Duplicate");
                }
                else
                {
                    PendingBeforeState.clear();
                }
            }
        }
        else
        {
            for (entt::entity Selected : SelectedEntities)
            {
                if (World->GetEntityRegistry().valid(Selected))
                {
                    World->GetEntityRegistry().emplace_or_replace<FNeedsTransformUpdate>(Selected);
                }
            }
        }

        for (entt::entity Entity : SelectedEntities)
        {
            if (!World->GetEntityRegistry().valid(Entity) || bGameViewMode)
            {
                continue;
            }
            
            if (SStaticMeshComponent* MeshComponent = World->GetEntityRegistry().try_get<SStaticMeshComponent>(Entity))
            {
                const STransformComponent& Transform = World->GetEntityRegistry().get<STransformComponent>(Entity);
                World->DrawBox(Transform.GetWorldLocation(), MeshComponent->GetAABB().GetSize() * 0.5f * Transform.GetWorldScale(), Transform.GetWorldRotation(), FColor::Red, 5.0f);
            }
        }

        const bool bPastePressed = bViewportHovered
            && ImGui::IsKeyDown(ImGuiKey_LeftCtrl)
            && ImGui::IsKeyPressed(ImGuiKey_V, false);

        if (bPastePressed)
        {
            // Pasting selects the new entities, mirroring duplicate. Snapshot the source
            // entities first because CopyEntity adds new rows that the view would otherwise
            // pick up and re-paste in the same iteration.
            TFixedVector<entt::entity, 64> CopySources;
            World->GetEntityRegistry().view<FCopiedTag>().each([&](entt::entity Entity)
            {
                if (!IsLockedPrefabChild(World->GetEntityRegistry(), Entity))
                {
                    CopySources.push_back(Entity);
                }
            });

            if (!CopySources.empty())
            {
                BeginTransaction();

                TFixedVector<entt::entity, 64> NewlyPasted;
                for (entt::entity Source : CopySources)
                {
                    entt::entity New = entt::null;
                    CopyEntity(New, Source);
                    if (New != entt::null)
                    {
                        NewlyPasted.push_back(New);
                    }
                }

                if (!NewlyPasted.empty())
                {
                    ClearSelectedEntities();
                    for (entt::entity New : NewlyPasted)
                    {
                        AddSelectedEntity(New, false);
                    }
                    EndTransaction("Paste");
                }
                else
                {
                    PendingBeforeState.clear();
                }
            }
        }
        
        if (ImGui::IsKeyPressed(ImGuiKey_F))
        {
            FocusViewportToEntity(GetLastSelectedEntity());
        }

        if (bViewportHovered && ImGui::IsKeyPressed(ImGuiKey_G, false) && !ImGui::GetIO().WantTextInput)
        {
            FSceneRenderSettings* Settings = nullptr;
            if (IRenderScene* RenderScene = World ? World->GetRenderer() : nullptr)
            {
                Settings = &RenderScene->GetSceneRenderSettings();
            }

            if (!bGameViewMode)
            {
                bSavedWorldGridEnabled = bWorldGridEnabled;
                bSavedShowComponentVisualizers = bShowComponentVisualizers;
                if (Settings)
                {
                    bSavedDrawBillboards = Settings->bDrawBillboards;
                    bSavedDrawAABB = Settings->bDrawAABB;
                }

                bWorldGridEnabled = false;
                bShowComponentVisualizers = false;
                if (Settings)
                {
                    Settings->bDrawBillboards = false;
                    Settings->bDrawAABB = false;
                }

                bGameViewMode = true;
            }
            else
            {
                bWorldGridEnabled = bSavedWorldGridEnabled;
                bShowComponentVisualizers = bSavedShowComponentVisualizers;
                if (Settings)
                {
                    Settings->bDrawBillboards = bSavedDrawBillboards;
                    Settings->bDrawAABB = bSavedDrawAABB;
                }

                bGameViewMode = false;
            }
        }


        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            if (World->GetWorldType() == EWorldType::Editor)
            {
                Undo();
            }
        }

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        {
            if (World->GetWorldType() == EWorldType::Editor)
            {
                Redo();
            }
        }

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            OnSave();
        }
    }

    void FWorldEditorTool::EndFrame()
    {
        using namespace entt::literals;
        
        if (bShowComponentVisualizers)
        {
            CComponentVisualizerRegistry& ComponentVisualizerRegistry = CComponentVisualizerRegistry::Get();

            // Iterate the registry view rather than SelectedEntities directly so we can use
            // entt::exclude<SDisabledTag>. The set and the tag stay synchronized via
            // ApplySelectionMutation, so this is consistent with the canonical selection.
            auto View = World->GetEntityRegistry().view<FSelectedInEditorComponent>(entt::exclude<SDisabledTag>);
            View.each([&] (entt::entity SelectedEntity)
            {
                ECS::Utils::ForEachComponent(World->GetEntityRegistry(), SelectedEntity, [&](void*, entt::basic_sparse_set<>& Set, const entt::meta_type& Type)
                {
                    if (entt::meta_any ReturnValue = ECS::Utils::InvokeMetaFunc(Type, "static_struct"_hs))
                    {
                        CStruct* StructType = ReturnValue.cast<CStruct*>();

                        if (CComponentVisualizer* Visualizer = ComponentVisualizerRegistry.GetComponentVisualizer(StructType))
                        {
                            Visualizer->Draw(World, World->GetEntityRegistry(), SelectedEntity);
                        }
                    }
                });
                
                ECS::Utils::ForEachChild(World->GetEntityRegistry(), SelectedEntity, [&](entt::entity Child)
                {
                    ECS::Utils::ForEachComponent(World->GetEntityRegistry(), Child, [&](void*, entt::basic_sparse_set<>& Set, const entt::meta_type& Type)
                    {
                        if (entt::meta_any ReturnValue = ECS::Utils::InvokeMetaFunc(Type, "static_struct"_hs))
                        {
                            CStruct* StructType = ReturnValue.cast<CStruct*>();
                    
                            if (CComponentVisualizer* Visualizer = ComponentVisualizerRegistry.GetComponentVisualizer(StructType))
                            {
                                Visualizer->Draw(World, World->GetEntityRegistry(), Child);
                            }
                        }
                    });
                });
            });
        }
    }

    void FWorldEditorTool::OnEntityCreated(entt::registry& Registry, entt::entity Entity)
    {
        // OutlinerListView.MarkTreeDirty(); @TODO Too expensive to enable.
    }

    const char* FWorldEditorTool::GetTitlebarIcon() const
    {
        return LE_ICON_EARTH;
    }

    void FWorldEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
        FEditorTool::DrawToolMenu(UpdateContext);
        
        if (ImGui::BeginMenu(LE_ICON_LANGUAGE_LUA " Lua"))
        {
            auto View = World->GetEntityRegistry().view<SScriptComponent>();
            
            ImGui::Text("Number Of Scripts: %i", View.size_hint<>());
            
            ImGui::Separator();
            
            if (ImGui::MenuItem("Reload All"))
            {
                View.each([&](entt::entity Entity, SScriptComponent& ScriptComponent)
                {
                   World->OnScriptComponentCreated(Entity, ScriptComponent, true); 
                });
            }
            
            ImGui::EndMenu();
        }
    }

    void FWorldEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);

        // Outer split: 75% viewport on the left, 25% inspector column on the right.
        ImGuiID dockLeft = 0, dockRight = 0;
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.25f, &dockRight, &dockLeft);

        // Right column: top scene graph, bottom details/settings strip.
        // SplitNode's third arg is the size ratio for the node "at_dir", and the fourth/fifth args
        // are out-pointers for at-dir and at-opposite. The previous code passed Down with a 0.25
        // ratio but mis-named the outputs (Top/Bottom were swapped), so SceneGraph landed at the
        // bottom and the details strip grew far too tall.
        ImGuiID dockRightBottom = 0, dockRightTop = 0;
        ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.35f, &dockRightBottom, &dockRightTop);

        // Bottom strip split horizontally for Details / World Settings, side by side.
        ImGuiID dockRightBottomLeft = 0, dockRightBottomRight = 0;
        ImGui::DockBuilderSplitNode(dockRightBottom, ImGuiDir_Right, 0.5f, &dockRightBottomRight, &dockRightBottomLeft);

        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(),    dockLeft);
        ImGui::DockBuilderDockWindow(GetToolWindowName(SceneGraphName).c_str(),        dockRightTop);
        ImGui::DockBuilderDockWindow(GetToolWindowName("Details").c_str(),             dockRightBottomLeft);
        ImGui::DockBuilderDockWindow(GetToolWindowName(WorldSettingsName).c_str(),     dockRightBottomRight);
    }

    void FWorldEditorTool::DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize)
    {
        if (bViewportHovered)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Space))
            {
                CycleGuizmoOp();
            }
        }
        
        if (World->IsGameWorld() || bGameViewMode)
        {
            return;
        }
        
        SCameraComponent& CameraComponent = World->GetEntityRegistry().get<SCameraComponent>(EditorEntity);

        glm::mat4 ViewMatrix = CameraComponent.GetViewMatrix();
        glm::mat4 ProjectionMatrix = CameraComponent.GetProjectionMatrix();
        // Camera projection bakes Vulkan +Y-down NDC; ImGuizmo expects the
        // GL math convention.
        ProjectionMatrix[1][1] *= -1.0f;

        const ImVec2 ViewportOrigin = ImGui::GetCursorScreenPos();

        ImGuizmo::SetDrawlist(ImGui::GetCurrentWindow()->DrawList);
        ImGuizmo::SetRect(ViewportOrigin.x, ViewportOrigin.y, ViewportSize.x, ViewportSize.y);

        {
            const ImRect ViewportRect(ViewportOrigin, ImVec2(ViewportOrigin.x + ViewportSize.x, ViewportOrigin.y + ViewportSize.y));
            if (ImGui::BeginDragDropTargetCustom(ViewportRect, ImGui::GetCurrentWindow()->ID))
            {
                AcceptContentBrowserPrefabPayload(entt::null);
                ImGui::EndDragDropTarget();
            }
        }

        TerrainEditMode.Tick(World, (float)World->GetWorldDeltaTime(), CameraComponent, bViewportHovered, ViewportOrigin, ViewportSize);
        TerrainEditMode.DrawOverlay(World, ViewportOrigin, ViewportSize, CameraComponent);

        auto SelectionView = World->GetEntityRegistry().view<FSelectedInEditorComponent, STransformComponent>();
        
        if (SelectionView.size_hint())
        {
            entt::entity PivotEntity = GetLastSelectedEntity();
            if (World->GetEntityRegistry().valid(PivotEntity))
            {
                STransformComponent& PivotTransformComponent = World->GetEntityRegistry().get<STransformComponent>(PivotEntity);
                if (CameraComponent.GetViewVolume().GetFrustum().IsInside(PivotTransformComponent.GetWorldLocation()))
                {
                    glm::mat4 EntityMatrix = PivotTransformComponent.GetWorldMatrix();

                    float* SnapValues = nullptr;
                    float SnapArray[3] = {};

                    if (bGuizmoSnapEnabled)
                    {
                        switch (GuizmoOp)
                        {
                        case ImGuizmo::TRANSLATE:
                            SnapArray[0] = GuizmoSnapTranslate;
                            SnapArray[1] = GuizmoSnapTranslate;
                            SnapArray[2] = GuizmoSnapTranslate;
                            SnapValues = SnapArray;
                            break;

                        case ImGuizmo::ROTATE:
                            SnapArray[0] = GuizmoSnapRotate;
                            SnapArray[1] = GuizmoSnapRotate;
                            SnapArray[2] = GuizmoSnapRotate;
                            SnapValues = SnapArray;
                            break;

                        case ImGuizmo::SCALE:
                            SnapArray[0] = GuizmoSnapScale;
                            SnapArray[1] = GuizmoSnapScale;
                            SnapArray[2] = GuizmoSnapScale;
                            SnapValues = SnapArray;
                            break;
                        }
                    }

                    glm::mat4 PreManipulateMatrix = EntityMatrix;

                    ImGuizmo::Manipulate(glm::value_ptr(ViewMatrix), glm::value_ptr(ProjectionMatrix),
                        GuizmoOp, GuizmoMode, glm::value_ptr(EntityMatrix), nullptr, SnapValues);
                
                    if (ImGuizmo::IsUsing())
                    {
                        if (!bImGuizmoUsedOnce)
                        {
                            BeginTransaction();
                            bImGuizmoUsedOnce = true;
                        }
                        
                        glm::mat4 DeltaMatrix = EntityMatrix * glm::inverse(PreManipulateMatrix);
                
                        glm::vec3 DeltaTranslation, DeltaScale, DeltaSkew;
                        glm::quat DeltaRotation;
                        glm::vec4 DeltaPerspective;
                        glm::decompose(DeltaMatrix, DeltaScale, DeltaRotation, DeltaTranslation, DeltaSkew, DeltaPerspective);

                        glm::vec3 PivotPosition = PivotTransformComponent.WorldTransform.Location;
                        
                        SelectionView.each([&](entt::entity Entity, STransformComponent& Transform)
                        {
                            // Compute the desired world-space matrix based on the operation
                            glm::mat4 DesiredWorldMatrix;
                        
                            switch (GuizmoOp)
                            {
                                case ImGuizmo::TRANSLATE:
                                {
                                    // Apply delta to current world matrix
                                    glm::mat4 TranslationDelta = glm::translate(glm::mat4(1.f), DeltaTranslation);
                                    DesiredWorldMatrix = TranslationDelta * Transform.GetWorldMatrix();
                                    break;
                                }
                        
                                case ImGuizmo::ROTATE:
                                {
                                    glm::vec3 OffsetFromPivot = Transform.WorldTransform.Location - PivotPosition;
                                    glm::vec3 RotatedOffset   = DeltaRotation * OffsetFromPivot;
                                    glm::vec3 NewWorldPos     = PivotPosition + RotatedOffset;
                                    glm::quat NewWorldRot     = DeltaRotation * Transform.GetWorldRotation();
                                    glm::vec3 WorldScale      = Transform.GetWorldScale();
                        
                                    DesiredWorldMatrix = glm::translate(glm::mat4(1.f), NewWorldPos)
                                                       * glm::mat4_cast(NewWorldRot)
                                                       * glm::scale(glm::mat4(1.f), WorldScale);
                                    break;
                                }
                        
                                case ImGuizmo::SCALE:
                                {
                                    glm::vec3 OffsetFromPivot = Transform.WorldTransform.Location - PivotPosition;
                                    glm::vec3 ScaledOffset    = OffsetFromPivot * DeltaScale;
                                    glm::vec3 NewWorldPos     = PivotPosition + ScaledOffset;
                                    glm::quat WorldRot        = Transform.GetWorldRotation();
                                    glm::vec3 NewWorldScale   = Transform.GetWorldScale() * DeltaScale;
                        
                                    DesiredWorldMatrix = glm::translate(glm::mat4(1.f), NewWorldPos)
                                                       * glm::mat4_cast(WorldRot)
                                                       * glm::scale(glm::mat4(1.f), NewWorldScale);
                                    break;
                                }
                            }
                        
                            // Convert to local if parented, otherwise set directly
                            FRelationshipComponent* Rel = World->GetEntityRegistry().try_get<FRelationshipComponent>(Entity);
                            if (Rel && Rel->Parent != entt::null)
                            {
                                STransformComponent& ParentTransform = World->GetEntityRegistry().get<STransformComponent>(Rel->Parent);
                                glm::mat4 LocalMatrix = glm::inverse(ParentTransform.GetWorldMatrix()) * DesiredWorldMatrix;
                        
                                glm::vec3 LocalTranslation, LocalScale, LocalSkew;
                                glm::quat LocalRotation;
                                glm::vec4 LocalPerspective;
                                glm::decompose(LocalMatrix, LocalScale, LocalRotation, LocalTranslation, LocalSkew, LocalPerspective);
                        
                                Transform.SetLocalLocation(LocalTranslation);
                                Transform.SetLocalRotation(LocalRotation);
                                Transform.SetLocalScale(LocalScale);
                            }
                            else
                            {
                                glm::vec3 WorldTranslation, WorldScale, WorldSkew;
                                glm::quat WorldRotation;
                                glm::vec4 WorldPerspective;
                                glm::decompose(DesiredWorldMatrix, WorldScale, WorldRotation, WorldTranslation, WorldSkew, WorldPerspective);
                        
                                Transform.SetLocalLocation(WorldTranslation);
                                Transform.SetLocalRotation(WorldRotation);
                                Transform.SetLocalScale(WorldScale);
                            }
                        });
                    }
                    else if (bImGuizmoUsedOnce)
                    {
                        EndTransaction("Transform");
                        bImGuizmoUsedOnce = false;
                    }
                }
            }
        }

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
        {
            uint32 PickerWidth = World->GetRenderer()->GetRenderTarget()->GetExtent().x;
            uint32 PickerHeight = World->GetRenderer()->GetRenderTarget()->GetExtent().y;
            
            ImVec2 viewportScreenPos = ImGui::GetWindowPos();
            ImVec2 mousePos = ImGui::GetMousePos();

            ImVec2 MousePosInViewport;
            MousePosInViewport.x = mousePos.x - viewportScreenPos.x;
            MousePosInViewport.y = mousePos.y - viewportScreenPos.y;

            MousePosInViewport.x = glm::clamp(MousePosInViewport.x, 0.0f, ViewportSize.x - 1.0f);
            MousePosInViewport.y = glm::clamp(MousePosInViewport.y, 0.0f, ViewportSize.y - 1.0f);

            float ScaleX = static_cast<float>(PickerWidth) / ViewportSize.x;
            float ScaleY = static_cast<float>(PickerHeight) / ViewportSize.y;

            uint32 TexX = static_cast<uint32>(MousePosInViewport.x * ScaleX);
            uint32 TexY = static_cast<uint32>(MousePosInViewport.y * ScaleY);
            
            bool bOverImGuizmo = bImGuizmoUsedOnce ? ImGuizmo::IsOver() : false;
            
            if (!bOverImGuizmo)
            {
                ImVec2 LeftDragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                float LeftDragDistance = sqrtf(LeftDragDelta.x * LeftDragDelta.x + LeftDragDelta.y * LeftDragDelta.y);
                bool bLeftDragging = LeftDragDistance >= 15.0f;
    
                ImVec2 RightDragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                float RightDragDistance = sqrtf(RightDragDelta.x * RightDragDelta.x + RightDragDelta.y * RightDragDelta.y);
                bool bRightDragging = RightDragDistance < 15.0f;
                
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    SelectionBox.bActive = true;
                    SelectionBox.Start = MousePosInViewport;
                    SelectionBox.Current = SelectionBox.Start;
                }

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                {
                    if (bRightDragging)
                    {
                        entt::entity EntityHandle = World->GetRenderer()->GetEntityAtPixel(TexX, TexY);
                        EntityHandle = ResolvePrefabRootForViewportPick(World->GetEntityRegistry(), EntityHandle);

                        SetSingleSelectedEntity(EntityHandle);

                        if (EntityHandle != entt::null)
                        {
                            ImGui::OpenPopup("EntityContextMenu");
                        }
                    }
                }
            
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && SelectionBox.bActive)
                {
                    SelectionBox.Current = MousePosInViewport;
                }
                
#if 0
                if (SelectionBox.bActive)
                {
                    ImDrawList* DrawList = ImGui::GetWindowDrawList();
                    ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
                
                    ImVec2 ScreenStart = ImVec2(ViewportPos.x + SelectionBox.Start.x, ViewportPos.y + SelectionBox.Start.y);
                    ImVec2 ScreenEnd = ImVec2(ViewportPos.x + SelectionBox.Current.x, ViewportPos.y + SelectionBox.Current.y);

                    DrawList->AddRectFilled(ScreenStart, ScreenEnd, IM_COL32(100, 150, 255, 50));
                    DrawList->AddRect(ScreenStart, ScreenEnd, IM_COL32(100, 150, 255, 255), 0.0f, 0, 2.0f);
                }
#endif
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && SelectionBox.bActive)
                {
                    ImVec2 Start = SelectionBox.Start;
                    ImVec2 End = SelectionBox.Current;
                    
                    if (!bLeftDragging)
                    {
                        entt::entity EntityHandle = World->GetRenderer()->GetEntityAtPixel(TexX, TexY);
                        EntityHandle = ResolvePrefabRootForViewportPick(World->GetEntityRegistry(), EntityHandle);

                        // Ctrl+click in the viewport mirrors the outliner: toggle the picked entity
                        // in the existing selection. Plain click replaces.
                        if (ImGui::GetIO().KeyCtrl)
                        {
                            if (EntityHandle != entt::null)
                            {
                                ToggleSelectedEntity(EntityHandle);
                            }
                        }
                        else
                        {
                            SetSingleSelectedEntity(EntityHandle);
                        }
                    }
                    else
                    {
#if 0
                        uint32 MinTexX = static_cast<uint32>(glm::min(Start.x, End.x) * ScaleX);
                        uint32 MinTexY = static_cast<uint32>(glm::min(Start.y, End.y) * ScaleY);
                        uint32 MaxTexX = static_cast<uint32>(glm::max(Start.x, End.x) * ScaleX);
                        uint32 MaxTexY = static_cast<uint32>(glm::max(Start.y, End.y) * ScaleY);
                    
                        for (entt::entity Entity : World->GetRenderer()->GetEntitiesInPixelRange(MinTexX, MinTexY, MaxTexX, MaxTexY))
                        {
                            AddSelectedEntity(Entity, true);
                        }
						ImGuiX::Notifications::NotifyInfo("{}", "This functionality is temporarily disabled as the current implementation is too slow. We are working on a more efficient solution that should be available in a future update.");
#endif
                    } 
    
                    SelectionBox.bActive = false;
                }
            }
        }
        
        if (ImGui::BeginPopup("EntityContextMenu"))
        {
            const entt::entity LastSelectedEntity = GetLastSelectedEntity();

            if (World->GetEntityRegistry().valid(LastSelectedEntity))
            {
                entt::registry& Registry = World->GetEntityRegistry();
                const bool bLastSelectedLocked = IsLockedPrefabChild(Registry, LastSelectedEntity);

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::TextUnformatted("ENTITY");
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                ImGui::Text("%u", (uint32)LastSelectedEntity);
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (!bLastSelectedLocked)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    if (ImGui::MenuItem(LE_ICON_TRASH_CAN" Delete Entity", "Del"))
                    {
                        if (Dialogs::Confirmation("Confirm Deletion", "Are you sure you want to delete entity \"{0}\"?\n\nThis action cannot be undone.", entt::to_integral(LastSelectedEntity)))
                        {
                            EntityDestroyRequests.push(LastSelectedEntity);
                        }

                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor();
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                if (ImGui::MenuItem("Add Component"))
                {
                    PushAddComponentModal(LastSelectedEntity);
                    ImGui::CloseCurrentPopup();
                }
                
                if (ImGui::BeginMenu("Remove Component"))
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.4f, 1.0f));
                    
                    ECS::Utils::ForEachComponent(Registry, LastSelectedEntity, [&](void*, const entt::basic_sparse_set<>& Set, entt::meta_type Meta)
                    {
                        using namespace entt::literals;
                        
                        if (entt::meta_any ReturnValue = ECS::Utils::InvokeMetaFunc(Meta, "static_struct"_hs))
                        {
                            CStruct* StructType = ReturnValue.cast<CStruct*>();
                            if (StructType == SNameComponent::StaticStruct() || StructType == STransformComponent::StaticStruct())
                            {
                                return;
                            }
                            
                            if (ImGui::MenuItem(ReturnValue.cast<CStruct*>()->MakeDisplayName().c_str()))
                            {
                                ComponentDestroyRequests.push(FComponentDestroyRequest{StructType, LastSelectedEntity});
                            }
                        }
                    });
                    
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar();
                    ImGui::EndMenu();
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                if (!bLastSelectedLocked)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
                    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                    {
                        BeginTransaction();
                        entt::entity To = entt::null;
                        CopyEntity(To, LastSelectedEntity);
                        if (To != entt::null)
                        {
                            EndTransaction("Duplicate");
                        }
                        else
                        {
                            PendingBeforeState.clear();
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor();
                }
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.6f, 1.0f));
                if (ImGui::MenuItem("Copy", "Ctrl+C"))
                {
                    ClearCopies();
                    AddEntityToCopies(LastSelectedEntity);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.6f, 1.0f));
                if (ImGui::MenuItem("Copy Entity ID"))
                {
                    ImGui::SetClipboardText(std::to_string(entt::to_integral(LastSelectedEntity)).c_str());
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();

                if (!bLastSelectedLocked && ECS::Utils::IsChild(Registry, LastSelectedEntity))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.3f, 0.6f, 1.0f));
                    if (ImGui::MenuItem("Unparent"))
                    {
                        BeginTransaction();
                        ECS::Utils::RemoveFromParent(Registry, LastSelectedEntity);
                        EndTransaction("Unparent");
                        ReparentEntityInOutliner(LastSelectedEntity);
                    }
                    ImGui::PopStyleColor();
                }

                if (!bLastSelectedLocked && ECS::Utils::IsParent(Registry, LastSelectedEntity))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.2f, 1.0f));
                    if (ImGui::MenuItem("Detach Children"))
                    {
                        TVector<entt::entity> Children;
                        ECS::Utils::ForEachChild(Registry, LastSelectedEntity, [&](entt::entity Child) { Children.push_back(Child); });
                        BeginTransaction();
                        ECS::Utils::DetachImmediateChildren(Registry, LastSelectedEntity);
                        EndTransaction("Detach Children");
                        for (entt::entity Child : Children)
                        {
                            ReparentEntityInOutliner(Child);
                        }
                    }
                    ImGui::PopStyleColor();
                }
                
                ImGui::Spacing();
                
                ImGui::PopStyleVar(3);
            }
            
            ImGui::EndPopup();
        }
    }

    void FWorldEditorTool::DrawViewportToolbar(const FUpdateContext& UpdateContext)
    {
        constexpr float Padding = 8.0f;
        constexpr float ItemSpacing = 6.0f;
        constexpr float ButtonSize = 32.0f;
        constexpr float CornerRounding = 8.0f;
        
        ImVec2 Pos = ImGui::GetWindowPos();
        ImGui::SetNextWindowPos(Pos + ImVec2(Padding, Padding));
        ImGui::SetNextWindowBgAlpha(0.85f);
    
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
            
            if (IsAssetEditorTool() || bSimulatingWorld || bGamePreviewRunning)
            {
                DrawSimulationControls(ButtonSize);
            }
            
            if (!bGamePreviewRunning)
            {
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();  
                
                DrawCameraControls(ButtonSize);
        
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();
        
                DrawViewportOptions(ButtonSize);

                TerrainEditMode.DrawToolbar(World, ButtonSize);
            }

            ImGui::EndGroup();
        }
        ImGui::End();

        ImGui::PopStyleVar(4);
    }

    void FWorldEditorTool::PushAddTagModal(entt::entity Entity)
    {
        struct FTagModalState
        {
            char TagBuffer[256] = {0};
            bool bTagExists = false;
        };
        
        TUniquePtr<FTagModalState> State = MakeUnique<FTagModalState>();
        
        ToolContext->PushModal("Add Tag", ImVec2(400.0f, 180.0f), [this, Entity, State = Move(State)] () -> bool
        {
            bool bTagAdded = false;
    
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            ImGui::TextUnformatted("Enter a tag name for this entity");
            ImGui::PopStyleColor();
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
    
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.19f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.2f, 0.2f, 0.21f, 1.0f));
            
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            
            bool bInputEnter = ImGui::InputTextWithHint(
                "##TagInput",
                LE_ICON_TAG " Tag name...",
                State->TagBuffer,
                sizeof(State->TagBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue
            );
            
            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere(-1);
            }
            
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            
            FString TagName(State->TagBuffer);
            State->bTagExists = !TagName.empty() && ECS::Utils::EntityHasTag(TagName, World->GetEntityRegistry(), Entity);
            
            if (State->bTagExists)
            {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
                ImGui::TextUnformatted(LE_ICON_ALERT_CIRCLE " Tag already exists on this entity");
                ImGui::PopStyleColor();
            }
    
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
    
            constexpr float buttonWidth = 100.0f;
            float const buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
            float const totalWidth = buttonWidth * 2 + buttonSpacing;
            float const availWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((availWidth - totalWidth) * 0.5f);
            
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            
            bool const bCanAdd = !TagName.empty() && !State->bTagExists;
            
            if (!bCanAdd)
            {
                ImGui::BeginDisabled();
            }

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.65f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.25f, 1.0f));
            
            if (ImGui::Button("Add", ImVec2(buttonWidth, 0)) || (bInputEnter && bCanAdd))
            {
                entt::hashed_string IDType = entt::hashed_string(TagName.c_str());
                auto& Storage = World->GetEntityRegistry().storage<STagComponent>(IDType);
                Storage.emplace(Entity).Tag = TagName;
                bTagAdded = true;
            }
            
            ImGui::PopStyleColor(3);
            
            if (!bCanAdd)
            {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.32f, 1.0f));
            
            bool bShouldClose = false;
            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
            {
                bShouldClose = true;
            }
            
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            
            return bTagAdded || bShouldClose;
        });
    }

    void FWorldEditorTool::PushAddComponentModal(entt::entity Entity)
    {
        TUniquePtr<ImGuiTextFilter> Filter = MakeUnique<ImGuiTextFilter>();
        ToolContext->PushModal("Add Component", ImVec2(650.0f, 500.0f), [this, Entity, Filter = Move(Filter)] () -> bool
        {
            bool bComponentAdded = false;
    
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            ImGui::TextUnformatted("Select a component to add to the entity");
            ImGui::PopStyleColor();
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
    
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.19f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.2f, 0.2f, 0.21f, 1.0f));
            
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            Filter->Draw(LE_ICON_BRIEFCASE_SEARCH " Search Components...", ImGui::GetContentRegionAvail().x);
            
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            
            ImGui::Spacing();
    
            float const tableHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2;
            
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12, 8));
            ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.12f, 0.12f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.14f, 0.14f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.17f, 1.0f));
            
            if (ImGui::BeginTable("##ComponentsList", 2, 
                ImGuiTableFlags_NoSavedSettings | 
                ImGuiTableFlags_Borders | 
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY, 
                ImVec2(0, tableHeight)))
            {
                ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();
    
                ImGui::PushID((int)Entity);
    
                struct ComponentInfo
                {
                    FFixedString Name;
                    FFixedString Category;
                    entt::meta_type MetaType;
                };
                
                TVector<ComponentInfo> Components;
                
                for(auto&& [_, MetaType]: entt::resolve())
                {
                    ECS::ETraits Traits = MetaType.traits<ECS::ETraits>();
                    if (!EnumHasAllFlags(Traits, ECS::ETraits::Component))
                    {
                        continue;
                    }
                    
                    using namespace entt::literals;
                    entt::meta_any Any = ECS::Utils::InvokeMetaFunc(MetaType, "static_struct"_hs);
                    CStruct* Type = Any.cast<CStruct*>();
                    ASSERT(Type);
                    
                    if (Type->HasMeta("HideInComponentList"))
                    {
                        continue;
                    }
                    
                    FFixedString ComponentName = Type->MakeDisplayName();
                    
                    if (!Filter->PassFilter(ComponentName.c_str()))
                    {
                        continue;
                    }
                    
                    const char* Category = "General";
                    Components.push_back({ComponentName, Category, MetaType});
                }
                
                eastl::sort(Components.begin(), Components.end(), [](const ComponentInfo& a, const ComponentInfo& b)
                {
                    if (a.Category != b.Category)
                    {
                        return a.Category < b.Category;
                    }

                    return a.Name < b.Name;
                });
                
                for (const ComponentInfo& CompInfo : Components)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    
                    ImVec4 IconColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                    const char* Icon = LE_ICON_CUBE;
                    
                    ImGui::PushStyleColor(ImGuiCol_Text, IconColor);
                    ImGui::TextUnformatted(Icon);
                    ImGui::PopStyleColor();
                    
                    ImGui::SameLine();
                    
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.5f, 0.8f, 0.4f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.6f, 0.9f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.35f, 0.65f, 0.95f, 0.6f));
                    
                    if (ImGui::Selectable(CompInfo.Name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        using namespace entt::literals;
                        BeginTransaction();

                        ECS::Utils::InvokeMetaFunc(CompInfo.MetaType, "emplace"_hs, entt::forward_as_meta(World->GetEntityRegistry()), Entity, entt::forward_as_meta(entt::meta_any{}));
                        
                        EndTransaction("Emplace Component");
                        
                        bComponentAdded = true;
                    }
                    
                    ImGui::PopStyleColor(3);
                    
                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::TextUnformatted(CompInfo.Category.c_str());
                    ImGui::PopStyleColor();
                }
                
                ImGui::PopID();
                ImGui::EndTable();
            }
            
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
    
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
    
            float buttonWidth = 120.0f;
            float availWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((availWidth - buttonWidth) * 0.5f);
            
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.32f, 1.0f));
            
            bool shouldClose = false;
            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
            {
                shouldClose = true;
            }
            
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            
            if (bComponentAdded && Entity == DetailsEntity)
            {
                bDetailsDirty = true;
            }

            return bComponentAdded || shouldClose;
        });
    }

    void FWorldEditorTool::PushRenameEntityModal(entt::entity Entity)
    {
        ToolContext->PushModal("Rename Entity", ImVec2(450.0f, 250.0f), [this, Entity]() -> bool
        {
            auto& NameComponent = World->GetEntityRegistry().get<SNameComponent>(Entity);
            static FFixedString InputBuffer;
    
            if (ImGui::IsWindowAppearing())
            {
                InputBuffer = NameComponent.Name.c_str();
            }
    
            ImGui::Text("Enter new name:");
            ImGui::Spacing();
    
            ImGui::SetNextItemWidth(-1.0f);
            bool bShouldClose = ImGui::InputText("##Name", InputBuffer.data(), 
                                                  InputBuffer.max_size(), 
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            constexpr float ButtonWidth = 100.0f;
            const float AvailWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((AvailWidth - ButtonWidth * 2 - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    
            if (ImGui::Button("OK", ImVec2(ButtonWidth, 0.0f)) || bShouldClose)
            {
                NameComponent.Name = FName(InputBuffer.c_str());

                // Update just this entity's row label rather than rebuilding the whole tree.
                auto It = EntityToTreeNode.find(Entity);
                if (It != EntityToTreeNode.end())
                {
                    FFixedString Label;
                    Label.append(LE_ICON_CUBE).append(" ")
                        .append(NameComponent.Name.c_str())
                        .append_convert(FString(" - (" + eastl::to_string(entt::to_integral(Entity)) + ")"));
                    OutlinerListView.Get<FTreeNodeDisplay>(It->second).DisplayName.assign(Label.data(), Label.length());
                }
                return true;
            }
    
            ImGui::SameLine();
    
            if (ImGui::Button("Cancel", ImVec2(ButtonWidth, 0.0f)))
            {
                return true;
            }
    
            return false;
        });
    }

    void FWorldEditorTool::OnSave()
    {
		if (!IsAssetEditorTool())
        {
            ImGuiX::Notifications::NotifyWarning("Cannot save world: No associated package.");
            return;
        }
        
        if (ShouldGenerateThumbnailOnSave() && World->GetPackage())
        {
            GenerateThumbnail(World->GetPackage());
        }
        
        if (CPackage::SavePackage(World->GetPackage(), World->GetPackage()->GetPackagePath()))
        {
            FAssetRegistry::Get().AssetSaved(World);
            ImGuiX::Notifications::NotifySuccess("Successfully saved world: \"{0}\"", World->GetName().c_str());
        }
        else
        {
            ImGuiX::Notifications::NotifyError("Failed to save world: \"{0}\"", World->GetName().c_str());
        }
    }

    bool FWorldEditorTool::IsAssetEditorTool() const
    {
        return World->GetPackage() != nullptr;
    }

    void FWorldEditorTool::NotifyPlayInEditorStart()
    {
        bGamePreviewRunning = true;
    }

    void FWorldEditorTool::NotifyPlayInEditorStop()
    {
         bGamePreviewRunning = false;
    }

    void FWorldEditorTool::SetWorld(CWorld* InWorld)
    {
        if (World)
        {
            FEntityRegistry& OldRegistry = World->GetEntityRegistry();
            OldRegistry.on_construct<entt::entity>().disconnect<&FWorldEditorTool::OnEntityCreated>(this);
            OldRegistry.on_destroy<entt::entity>().disconnect<&FWorldEditorTool::OnEntityDestroyed>(this);
            OldRegistry.on_construct<SNameComponent>().disconnect<&FWorldEditorTool::OnOutlinerEntityConstructed>(this);
            OldRegistry.on_destroy<SNameComponent>().disconnect<&FWorldEditorTool::OnOutlinerEntityDestroyed>(this);
            OldRegistry.clear<FSelectedInEditorComponent>();
            OldRegistry.clear<FLastSelectedTag>();
        }

        // Tear down anything that points at the old registry: property tables hold raw
        // component pointers, the selection cache holds entt handles into the old domain.
        PropertyTables.clear();
        SelectedEntities.clear();
        LastSelectedEntity = entt::null;
        DetailsEntity = entt::null;
        bDetailsDirty = true;

        FEditorTool::SetWorld(InWorld);

        WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());

        RebindRegistryObservers();
        OutlinerListView.MarkTreeDirty();
    }

    void FWorldEditorTool::OnEntityDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        // The entity is about to leave the registry. Drop it from the canonical selection
        // set, fix up LastSelectedEntity if it was the focus, and invalidate any cached
        // property tables that pointed at its components — those become dangling otherwise.
        if (SelectedEntities.find(Entity) != SelectedEntities.end())
        {
            SelectedEntities.erase(Entity);
        }

        if (LastSelectedEntity == Entity)
        {
            entt::entity NewLast = entt::null;
            for (entt::entity Candidate : SelectedEntities)
            {
                if (Registry.valid(Candidate))
                {
                    NewLast = Candidate;
                    break;
                }
            }
            LastSelectedEntity = NewLast;
            bDetailsDirty = true;
        }

        if (DetailsEntity == Entity)
        {
            PropertyTables.clear();
            DetailsEntity = entt::null;
            bDetailsDirty = true;
        }
        // Outliner row removal happens in OnOutlinerEntityDestroyed.
    }

    void FWorldEditorTool::DrawSimulationControls(float ButtonSize)
    {
        const ImVec2 BtnSize = ImVec2(ButtonSize, ButtonSize);
        
        if (!bGamePreviewRunning)
        {
            if (!bSimulatingWorld)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
                if (ImGuiX::IconButton(LE_ICON_PLAY, "##PlayBtn", 0xFFFFFFFF, BtnSize))
                {
                    SetWorldPlayInEditor(true);
                }
                ImGui::PopStyleColor(2);
                
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::SetTooltip("Play (Start game preview)");
                }
                
                ImGui::SameLine();
                
                if (ImGuiX::IconButton(LE_ICON_COG_BOX, "##SimulateBtn", 0xFFFFFFFF, BtnSize))
                {
                    SetWorldNewSimulate(true);
                }
                
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::SetTooltip("Simulate (Run physics without gameplay)");
                }
            }
            else
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    SetWorldNewSimulate(false);
                }
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.5f, 0.1f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
                if (ImGuiX::IconButton(LE_ICON_COG_BOX, "##SimulateActiveBtn", 0xFFFFFFFF, BtnSize))
                {
                    SetWorldNewSimulate(false);
                }
                ImGui::PopStyleColor(2);
                
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::SetTooltip("Stop Simulation (ESC)");
                }
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGuiX::IconButton(LE_ICON_STOP, "##StopBtn", 0xFFFFFFFF, BtnSize))
            {
                SetWorldPlayInEditor(false);
                //OnGamePreviewStopRequested.Broadcast();
            }
            ImGui::PopStyleColor(2);
            
            
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Stop Game Preview");
            }
            
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                SetWorldPlayInEditor(false);
            }
        }
    }

    void FWorldEditorTool::DrawCameraControls(float ButtonSize)
    {
        if (bGamePreviewRunning)
        {
            return;
        }
        
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
            STransformComponent& CameraTransform = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);

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
        
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Translation (Location)");
            }
                
            ImGui::DragFloat3("T", glm::value_ptr(CameraTransform.WorldTransform.Location), 0.01f);
        
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.7f, 1.0f));
            ImGui::TextUnformatted(LE_ICON_ROTATE_360);
            ImGui::PopStyleColor();
            
            ImGuiX::TextTooltip("Rotation (Euler Angles)");
        
            ImGui::SameLine();
        
            glm::vec3 EulerRotation = CameraTransform.GetRotationAsEuler();
            if (ImGui::DragFloat3("R", glm::value_ptr(EulerRotation), 0.01f))
            {
                CameraTransform.SetRotationFromEuler(EulerRotation);
            }
            
            ImGui::Separator();
            
            if (ImGui::Button("Reset Position", ImVec2(-1, 0)))
            {
                World->GetEntityRegistry().get<STransformComponent>(EditorEntity).SetLocation(glm::vec3(0.0f));
            }
            
            if (ImGui::Button("Reset Rotation", ImVec2(-1, 0)))
            {
                World->GetEntityRegistry().get<STransformComponent>(EditorEntity).SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
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

    void FWorldEditorTool::DrawViewportOptions(float ButtonSize)
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
        case ImGuizmo::OPERATION::TRANSLATE:
            {
                Icon = LE_ICON_AXIS_ARROW;
            }
            break;
        case ImGuizmo::OPERATION::ROTATE:
            {
                Icon = LE_ICON_ROTATE_360;
            }
            break;
        case ImGuizmo::OPERATION::SCALE:
            {
                Icon = LE_ICON_ARROW_TOP_RIGHT_BOTTOM_LEFT;
            }
            break;
        }
        
        if (ImGuiX::IconButton(Icon, "##GizmoMode", 0xFFFFFFFF, BtnSize))
        {
            CycleGuizmoOp();
        }
        
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Gizmo: %s (R)", ImGuiX::ImGuizmoOpToString(GuizmoOp).data());
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
            GConfig->Set("Editor.WorldEditorTool.GuizmoSnapEnabled", bGuizmoSnapEnabled);
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
            ImGui::SetTooltip("Add something to the world.");
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
            
            if (ImGui::BeginMenu("Rendering"))
            {
                FSceneRenderSettings& Settings = RenderScene->GetSceneRenderSettings();

                if (ImGui::BeginMenu("View Mode"))
                {
                    // View-mode groups. Keep the grouping aligned with the
                    // ERenderSceneDebugFlags enum so adding a new visualization
                    // is a one-liner here plus the enum/shader entry.
                    struct FViewModeEntry
                    {
                        ERenderSceneDebugFlags Mode;
                        const char* Label;
                    };

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
                        { ERenderSceneDebugFlags::Meshlets,        "Meshlets"         },
                        { ERenderSceneDebugFlags::LightComplexity, "Light Complexity" },
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
                    DrawGroup("Buffers", Buffers, sizeof(Buffers) / sizeof(Buffers[0]));
                    ImGui::Spacing();
                    DrawGroup("Geometry", Geometry, sizeof(Geometry) / sizeof(Geometry[0]));

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
                
                if (ImGui::MenuItem("Game View", "G", &bGameViewMode))
                {
                    bGameViewMode = !bGameViewMode;
                }

                ImGui::EndMenu();
            }
            
            ImGui::EndPopup();
        }
    }
    
    void FWorldEditorTool::DrawSnapSettingsPopup()
    {
        ImGui::Text("Snap Settings");
        ImGui::Separator();
        
        if (ImGui::Checkbox("Enable Snap", &bGuizmoSnapEnabled))
        {
            GConfig->Set("Editor.WorldEditorTool.GuizmoSnapEnabled", bGuizmoSnapEnabled);
        }
        
        ImGui::Spacing();
        
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.4f, 0.6f, 0.3f));
        bool bAnySettingDirty = false;
        
        if (ImGui::CollapsingHeader("Translation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("Translate");
            ImGui::Indent();
            
            ImGui::BeginDisabled(!bGuizmoSnapEnabled);
            
            ImGui::Text("Presets:");
            ImGui::SameLine();
            
            if (ImGui::Button("0.1"))
            {
                GuizmoSnapTranslate = 0.1f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("1.0"))
            {
                GuizmoSnapTranslate = 1.0f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("5.0"))
            {
                GuizmoSnapTranslate = 5.0f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("10"))
            {
                GuizmoSnapTranslate = 10.0f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("50"))
            {
                GuizmoSnapTranslate = 50.0f;
                bAnySettingDirty = true;
            }
            
            if (ImGui::DragFloat("Value##Translation", &GuizmoSnapTranslate, 0.1f, 0.01f, 1000.0f, "%.2f units"))
            {
                bAnySettingDirty = true;
            }
            
            ImGui::EndDisabled();
            ImGui::Unindent();
            ImGui::PopID();
        }
        
        if (ImGui::CollapsingHeader("Rotation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("Rotate");
            ImGui::Indent();
            
            ImGui::BeginDisabled(!bGuizmoSnapEnabled);
            
            ImGui::Text("Presets:");
            ImGui::SameLine();
            
            if (ImGui::Button("1 " LE_ICON_ANGLE_ACUTE))
            {
                GuizmoSnapRotate = 1.0f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("5 " LE_ICON_ANGLE_ACUTE))
            {
                GuizmoSnapRotate = 5.0f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("15 " LE_ICON_ANGLE_ACUTE))
            {
                GuizmoSnapRotate = 15.0f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("45 " LE_ICON_ANGLE_ACUTE))
            {
                GuizmoSnapRotate = 45.0f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("90 " LE_ICON_ANGLE_ACUTE))
            {
                GuizmoSnapRotate = 90.0f;
                bAnySettingDirty = true;
            }
            
            if (ImGui::DragFloat("Value##Rotation", &GuizmoSnapRotate, 0.5f, 0.1f, 180.0f, "%.1f " LE_ICON_ANGLE_ACUTE))
            {
                bAnySettingDirty = true;
            }
            
            ImGui::EndDisabled();
            ImGui::Unindent();
            ImGui::PopID();
        }
        
        if (ImGui::CollapsingHeader("Scale", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("Scale");
            ImGui::Indent();
            
            ImGui::BeginDisabled(!bGuizmoSnapEnabled);
            
            ImGui::Text("Presets:");
            ImGui::SameLine();
            
            if (ImGui::Button("0.1"))
            {
                GuizmoSnapScale = 0.1f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("0.25"))
            {
                GuizmoSnapScale = 0.25f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("0.5"))
            {
                GuizmoSnapScale = 0.5f;
                bAnySettingDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("1.0"))
            {
                GuizmoSnapScale = 1.0f;
                bAnySettingDirty = true;
            }
            
            if (ImGui::DragFloat("Value##Scale", &GuizmoSnapScale, 0.01f, 0.01f, 10.0f, "%.2f"))
            {
                bAnySettingDirty = true;
            }
            
            ImGui::EndDisabled();
            ImGui::Unindent();
            ImGui::PopID();
        }
        
        if (bAnySettingDirty)
        {
            GConfig->Set("Editor.WorldEditorTool.GuizmoSnapTranslate", GuizmoSnapTranslate);
            GConfig->Set("Editor.WorldEditorTool.GuizmoSnapRotate", GuizmoSnapRotate);
            GConfig->Set("Editor.WorldEditorTool.GuizmoSnapScale", GuizmoSnapScale);
        }

        ImGui::PopStyleColor();
    }

    void FWorldEditorTool::StopAllSimulations()
    {
        SetWorldNewSimulate(false);
        SetWorldPlayInEditor(false);
    }

    void FWorldEditorTool::OnPostUndoRedo()
    {
        // The serialized registry is the authority on what's selected post-undo. Rebuild
        // the cached set from FSelectedInEditorComponent / FLastSelectedTag so all three
        // views (set, tags, outliner rows) line up.
        ResyncSelectionFromRegistry();

        // Outliner topology may have changed (entities created/destroyed by the undo);
        // forcing a rebuild keeps tree rows in sync.
        OutlinerListView.MarkTreeDirty();
    }

    // -- Selection plumbing -------------------------------------------------------------------
    //
    // The tool keeps three views of selection:
    //   1. SelectedEntities (the authoritative set, on the tool)
    //   2. FSelectedInEditorComponent on the registry (a projection — read by render highlight,
    //      visualizers, prefab editor, etc.)
    //   3. FTreeNodeState::bSelected on the outliner (a projection — driven by the tool, never
    //      written directly by the tree widget for entries we own)
    //
    // ApplySelectionMutation is the single funnel that keeps all three in sync. Public mutation
    // methods (AddSelectedEntity, RemoveSelectedEntity, etc.) update the in-memory set, then
    // call this to propagate. Whenever LastSelectedEntity changes, the details panel is marked
    // dirty so its property tables rebuild on the next draw.
    namespace
    {
        // Pulled out so we don't have to write the same out-of-line helper for each path.
        FORCEINLINE void SetTreeNodeSelected(FTreeListView& Tree, FTreeNodeID Node, bool bSelected)
        {
            if (Node.IsValid() && Tree.IsValid(Node))
            {
                Tree.Get<FTreeNodeState>(Node).bSelected = bSelected;
            }
        }
    }

    void FWorldEditorTool::SetSingleSelectedEntity(entt::entity Entity)
    {
        if (Entity != entt::null && !World->GetEntityRegistry().valid(Entity))
        {
            Entity = entt::null;
        }

        // Fast-path: clicking the already-singularly-selected entity is a no-op.
        if (Entity == LastSelectedEntity && SelectedEntities.size() == (Entity == entt::null ? 0 : 1)
            && (Entity == entt::null || SelectedEntities.find(Entity) != SelectedEntities.end()))
        {
            return;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();

        // Drop tags from previously-selected entities not in the new set, so render
        // highlighting stays in lockstep with the canonical set.
        for (entt::entity Old : SelectedEntities)
        {
            if (Old != Entity && Registry.valid(Old))
            {
                Registry.remove<FSelectedInEditorComponent>(Old);
                auto It = EntityToTreeNode.find(Old);
                if (It != EntityToTreeNode.end())
                {
                    SetTreeNodeSelected(OutlinerListView, It->second, false);
                }
            }
        }
        SelectedEntities.clear();

        // Clear last-selected tag unconditionally — we'll re-emplace below if the new
        // selection isn't empty. Keeps the registry in a consistent state if the caller
        // passes entt::null (meaning "select nothing").
        Registry.clear<FLastSelectedTag>();

        if (Entity != entt::null)
        {
            SelectedEntities.insert(Entity);
            Registry.emplace_or_replace<FSelectedInEditorComponent>(Entity);
            Registry.emplace_or_replace<FLastSelectedTag>(Entity);

            auto It = EntityToTreeNode.find(Entity);
            if (It != EntityToTreeNode.end())
            {
                SetTreeNodeSelected(OutlinerListView, It->second, true);
            }
        }

        if (LastSelectedEntity != Entity)
        {
            LastSelectedEntity = Entity;
            bDetailsDirty = true;
        }
    }

    void FWorldEditorTool::AddSelectedEntity(entt::entity Entity, bool /*bRebuild*/)
    {
        if (Entity == entt::null || !World->GetEntityRegistry().valid(Entity))
        {
            return;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();

        const bool bWasAlreadySelected = SelectedEntities.find(Entity) != SelectedEntities.end();
        if (!bWasAlreadySelected)
        {
            SelectedEntities.insert(Entity);
            Registry.emplace_or_replace<FSelectedInEditorComponent>(Entity);

            auto It = EntityToTreeNode.find(Entity);
            if (It != EntityToTreeNode.end())
            {
                SetTreeNodeSelected(OutlinerListView, It->second, true);
            }
        }

        // Always promote to last-selected: clicking an already-selected row in a multi-select
        // should still focus the details panel on it.
        if (LastSelectedEntity != Entity)
        {
            Registry.clear<FLastSelectedTag>();
            Registry.emplace_or_replace<FLastSelectedTag>(Entity);
            LastSelectedEntity = Entity;
            bDetailsDirty = true;
        }
    }

    void FWorldEditorTool::RemoveSelectedEntity(entt::entity Entity, bool /*bRebuild*/)
    {
        if (World == nullptr || Entity == entt::null)
        {
            return;
        }

        auto SetIt = SelectedEntities.find(Entity);
        if (SetIt == SelectedEntities.end())
        {
            return;
        }

        SelectedEntities.erase(SetIt);

        FEntityRegistry& Registry = World->GetEntityRegistry();
        if (Registry.valid(Entity))
        {
            Registry.remove<FSelectedInEditorComponent>(Entity);
        }

        auto TreeIt = EntityToTreeNode.find(Entity);
        if (TreeIt != EntityToTreeNode.end())
        {
            SetTreeNodeSelected(OutlinerListView, TreeIt->second, false);
        }

        // If the entity we just deselected was the focus target, pick a new one from
        // whatever remains so multi-select doesn't end up with a stale "last".
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
            bDetailsDirty = true;
        }
    }

    void FWorldEditorTool::ToggleSelectedEntity(entt::entity Entity)
    {
        if (Entity == entt::null || !World->GetEntityRegistry().valid(Entity))
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

    void FWorldEditorTool::ResyncSelectionFromRegistry()
    {
        // Drop outliner row state for the old set first; we'll re-mark from the
        // post-resync set below. Anything that's no longer selected ends up cleared
        // because we don't visit it.
        for (entt::entity Old : SelectedEntities)
        {
            auto It = EntityToTreeNode.find(Old);
            if (It != EntityToTreeNode.end())
            {
                SetTreeNodeSelected(OutlinerListView, It->second, false);
            }
        }
        SelectedEntities.clear();
        LastSelectedEntity = entt::null;

        if (World == nullptr)
        {
            bDetailsDirty = true;
            return;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();

        Registry.view<FSelectedInEditorComponent>().each([&](entt::entity Entity)
        {
            SelectedEntities.insert(Entity);

            auto It = EntityToTreeNode.find(Entity);
            if (It != EntityToTreeNode.end())
            {
                SetTreeNodeSelected(OutlinerListView, It->second, true);
            }
        });

        // FLastSelectedTag should ride along with the serialized state, but be defensive
        // — if it's missing for any reason, fall back to picking the first selected.
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

        bDetailsDirty = true;
    }

    void FWorldEditorTool::ClearSelectedEntities()
    {
        if (World == nullptr)
        {
            SelectedEntities.clear();
            LastSelectedEntity = entt::null;
            bDetailsDirty = true;
            return;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();

        for (entt::entity Entity : SelectedEntities)
        {
            auto It = EntityToTreeNode.find(Entity);
            if (It != EntityToTreeNode.end())
            {
                SetTreeNodeSelected(OutlinerListView, It->second, false);
            }
        }

        SelectedEntities.clear();

        // clear<>() on the registry is the bulk-erase path and matches what a multi-deselect
        // wants — cheaper than walking SelectedEntities and removing one by one (which we
        // already did above for the outliner, where we need the entity ids anyway).
        Registry.clear<FSelectedInEditorComponent>();
        Registry.clear<FLastSelectedTag>();

        if (LastSelectedEntity != entt::null)
        {
            LastSelectedEntity = entt::null;
            bDetailsDirty = true;
        }
    }

    void FWorldEditorTool::AddEntityToCopies(entt::entity Entity)
    {
        World->GetEntityRegistry().emplace_or_replace<FCopiedTag>(Entity);
    }

    void FWorldEditorTool::RemoveEntityFromCopies(entt::entity Entity)
    {
        World->GetEntityRegistry().remove<FCopiedTag>(Entity);
    }

    void FWorldEditorTool::ClearCopies() const
    {
        World->GetEntityRegistry().clear<FCopiedTag>();
    }

    void FWorldEditorTool::RebindRegistryObservers()
    {
        FEntityRegistry& Registry = World->GetEntityRegistry();
        Registry.on_construct<entt::entity>().disconnect<&FWorldEditorTool::OnEntityCreated>(this);
        Registry.on_destroy<entt::entity>().disconnect<&FWorldEditorTool::OnEntityDestroyed>(this);
        Registry.on_construct<SNameComponent>().disconnect<&FWorldEditorTool::OnOutlinerEntityConstructed>(this);
        Registry.on_destroy<SNameComponent>().disconnect<&FWorldEditorTool::OnOutlinerEntityDestroyed>(this);

        Registry.on_construct<entt::entity>().connect<&FWorldEditorTool::OnEntityCreated>(this);
        Registry.on_destroy<entt::entity>().connect<&FWorldEditorTool::OnEntityDestroyed>(this);
        // SNameComponent is the canonical "this entity should appear in the outliner" marker;
        // ConstructEntity always emplaces it, and we also exclude FHideInSceneOutliner inside
        // the handler. Hooking it (rather than entt::entity) means we don't add a row before
        // the entity has a name to display.
        Registry.on_construct<SNameComponent>().connect<&FWorldEditorTool::OnOutlinerEntityConstructed>(this);
        Registry.on_destroy<SNameComponent>().connect<&FWorldEditorTool::OnOutlinerEntityDestroyed>(this);
    }

    void FWorldEditorTool::OnWorldTravelled(CWorld* OldWorld, CWorld* NewWorld)
    {
        // Only react if Travel swapped the world this tool is displaying.
        if (OldWorld != World.Get() || NewWorld == nullptr)
        {
            return;
        }

        // Drop pointers into the torn-down world before rebinding: property
        // tables hold raw registry pointers, observers are connected to the
        // old registry, the outliner caches handles from it.
        PropertyTables.clear();
        WorldSettingsPropertyTable.reset();

        // Selection caches are full of entt handles from the old registry's domain;
        // they're meaningless against the new world.
        SelectedEntities.clear();
        LastSelectedEntity = entt::null;
        DetailsEntity = entt::null;
        bDetailsDirty = true;

        EditorEntity = entt::null;

        // RebindToWorld updates both the World pointer and InputViewport so the
        // tool's viewport stops dereferencing the torn-down world.
        // ProxyWorld / ProxyEditorEntity are intentionally untouched: Travel
        // only replaces the running game world, never the editor's source map,
        // so SetWorldPlayInEditor(false) can still restore them on stop.
        RebindToWorld(NewWorld);

        WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());

        OutlinerListView.ClearTree();
        OutlinerListView.MarkTreeDirty();
        EntityToTreeNode.clear();
        PendingOutlinerAdds.clear();

        RebindRegistryObservers();

        // Simulate mode owns the editor entity inside the active world (PIE
        // mode does not), so the entity must be rebuilt against NewWorld; the
        // simulate-exit path reads transform/camera from EditorEntity and
        // would otherwise dereference entt::null.
        if (bSimulatingWorld)
        {
            SetupWorldForTool();
        }
    }

    void FWorldEditorTool::SetWorldPlayInEditor(bool bShouldPlay)
    {
        if (bShouldPlay == bGamePreviewRunning)
        {
            return;
        }

        if (bShouldPlay)
        {
            bGamePreviewRunning = true;
            PropertyTables.clear();
            SelectedEntities.clear();
            LastSelectedEntity = entt::null;
            DetailsEntity = entt::null;
            bDetailsDirty = true;

            World->SetActive(false);
            ProxyWorld = World;
            ProxyEditorEntity = EditorEntity;

            // PIE world is owned by FWorldManager; RebindToWorld is a pointer-only swap.
            RebindToWorld(GWorldManager->StartPIE(ProxyWorld, EWorldType::Game, ENetMode::Standalone));
            EditorEntity = entt::null;

            WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());

            OutlinerListView.ClearTree();
            OutlinerListView.MarkTreeDirty();

            RebindRegistryObservers();
        }
        else
        {
            PropertyTables.clear();
            SelectedEntities.clear();
            LastSelectedEntity = entt::null;
            DetailsEntity = entt::null;
            bDetailsDirty = true;
            World->SetPaused(true);
            bGamePreviewRunning = false;

            // ProxyEditorEntity is the editor-world entity captured at PIE entry;
            // EditorEntity may be entt::null here if Travel swapped the active world
            // mid-PIE, so we cannot rely on it to address the editor world.
            if (ProxyEditorEntity != entt::null && ProxyWorld->GetEntityRegistry().valid(ProxyEditorEntity))
            {
                ProxyWorld->DestroyEntity(ProxyEditorEntity);
            }
            ProxyEditorEntity = entt::null;
            EditorEntity = entt::null;

            SetWorld(ProxyWorld);
            ProxyWorld->SetActive(true);

            WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());

            ProxyWorld = nullptr;

            OutlinerListView.ClearTree();
            OutlinerListView.MarkTreeDirty();

            RebindRegistryObservers();

            if (InputViewport)
            {
                // Activate the editor viewport before adjusting mouse mode so
                // FInputProcessor routes the change (and clears ImGui's
                // NoMouse flag) against the right context.
                FInputViewportRegistry::Get().SetActiveViewport(InputViewport.get());

                // Drop Lua-registered action callbacks left over from the PIE
                // session; without this they keep firing against the editor's
                // input state every frame.
                InputViewport->GetContext().ClearActionCallbacks();

                InputViewport->GetContext().SetInputMode(EInputMode::Game);

                // Go through FInputProcessor so ImGuiConfigFlags_NoMouse is
                // cleared — setting the context field directly would leave
                // ImGui ignoring the mouse, breaking editor clicks/hover.
                FInputProcessor::Get().SetMouseMode(EMouseMode::Normal);
            }
        }
    }

    void FWorldEditorTool::SetWorldNewSimulate(bool bShouldSimulate)
    {
        if (bShouldSimulate == bSimulatingWorld)
        {
            return;
        }

        if (bShouldSimulate)
        {
            PropertyTables.clear();
            SelectedEntities.clear();
            LastSelectedEntity = entt::null;
            DetailsEntity = entt::null;
            bDetailsDirty = true;
            bSimulatingWorld = true;

            FTransform TransformCopy = World->GetEntityRegistry().get<STransformComponent>(EditorEntity).GetWorldTransform();
            SCameraComponent CameraCopy = World->GetEntityRegistry().get<SCameraComponent>(EditorEntity);

            World->SetActive(false);
            ProxyWorld = World;
            ProxyEditorEntity = EditorEntity;
            RebindToWorld(GWorldManager->StartPIE(ProxyWorld, EWorldType::Simulation, ENetMode::Standalone));

            if (ProxyEditorEntity != entt::null && ProxyWorld->GetEntityRegistry().valid(ProxyEditorEntity))
            {
                ProxyWorld->DestroyEntity(ProxyEditorEntity);
            }
            ProxyEditorEntity = entt::null;
            EditorEntity = entt::null;

            SetupWorldForTool();
            ASSERT(World->GetEntityRegistry().valid(EditorEntity));

            WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());

            World->GetEntityRegistry().get<STransformComponent>(EditorEntity).SetLocalTransform(TransformCopy);

            World->GetEntityRegistry().patch<SCameraComponent>(EditorEntity, [CameraCopy](SCameraComponent& Patch)
            {
                Patch = CameraCopy;
            });

            OutlinerListView.ClearTree();
            OutlinerListView.MarkTreeDirty();

            RebindRegistryObservers();
        }
        else
        {
            PropertyTables.clear();
            SelectedEntities.clear();
            LastSelectedEntity = entt::null;
            DetailsEntity = entt::null;
            bDetailsDirty = true;
            bSimulatingWorld = false;

            FTransform TransformCopy = World->GetEntityRegistry().get<STransformComponent>(EditorEntity).GetWorldTransform();
            SCameraComponent CameraCopy = World->GetEntityRegistry().get<SCameraComponent>(EditorEntity);

            if (EditorEntity != entt::null && World->GetEntityRegistry().valid(EditorEntity))
            {
                World->DestroyEntity(EditorEntity);
            }
            EditorEntity = entt::null;
            ProxyEditorEntity = entt::null;

            SetWorld(ProxyWorld);
            ProxyWorld->SetActive(true);
            ASSERT(World->GetEntityRegistry().valid(EditorEntity));

            WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());

            World->GetEntityRegistry().get<STransformComponent>(EditorEntity).SetLocalTransform(TransformCopy);

            World->GetEntityRegistry().patch<SCameraComponent>(EditorEntity, [CameraCopy](SCameraComponent& Patch)
            {
                Patch = CameraCopy;
            });

            ProxyWorld = nullptr;

            OutlinerListView.ClearTree();
            OutlinerListView.MarkTreeDirty();

            if (InputViewport)
            {
                FInputViewportRegistry::Get().SetActiveViewport(InputViewport.get());
                InputViewport->GetContext().ClearActionCallbacks();
                InputViewport->GetContext().SetInputMode(EInputMode::Game);
                FInputProcessor::Get().SetMouseMode(EMouseMode::Normal);
            }
        }
    }

    void FWorldEditorTool::DrawAddToEntityOrWorldPopup(entt::entity Entity)
    {
        ImGui::SetNextWindowSize(ImVec2(450.0f, 550.0f), ImGuiCond_Always);
    
        if (ImGui::BeginPopup("AddToEntityMenu", ImGuiWindowFlags_NoMove))
        {
            if (Entity == entt::null)
            {
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), LE_ICON_PLUS " Create New Entity");
        
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
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
                using namespace entt::literals;

                bool bDrewComponentsHeader = false;
                auto DrawComponentsHeader = [&]()
                {
                    if (bDrewComponentsHeader)
                    {
                        return;
                    }
                    bDrewComponentsHeader = true;

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

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

                    struct FPrimitiveEntry
                    {
                        const char* Label;
                        const char* EntityName;
                        CStaticMesh* (*GetMesh)();
                    };

                    static const FPrimitiveEntry PrimitiveEntries[] =
                    {
                        { LE_ICON_CUBE     " Cube",     "Cube",     []() -> CStaticMesh* { return CThumbnailManager::Get().CubeMesh; } },
                        { LE_ICON_CIRCLE   " Sphere",   "Sphere",   []() -> CStaticMesh* { return CThumbnailManager::Get().SphereMesh; } },
                        { LE_ICON_SQUARE   " Plane",    "Plane",    []() -> CStaticMesh* { return CThumbnailManager::Get().PlaneMesh; } },
                        { LE_ICON_CYLINDER " Cylinder", "Cylinder", []() -> CStaticMesh* { return CThumbnailManager::Get().CylinderMesh; } },
                        { LE_ICON_CONE     " Cone",     "Cone",     []() -> CStaticMesh* { return CThumbnailManager::Get().ConeMesh; } },
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
                                BeginTransaction();
                                CreatePrimitiveEntity(Entry->GetMesh(), Entry->EntityName);
                                EndTransaction("New Primitive");

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

                TVector<TPair<entt::meta_type, CStruct*>> SortedComponents;
                
                for(auto &&[ID, MetaType]: entt::resolve())
                {
                    ECS::ETraits Traits = MetaType.traits<ECS::ETraits>();
                    if (!EnumHasAllFlags(Traits, ECS::ETraits::Component))
                    {
                        continue;
                    }
                    
                    entt::meta_any Any = ECS::Utils::InvokeMetaFunc(MetaType, "static_struct"_hs);
                    CStruct* Struct = Any.cast<CStruct*>();
                    ASSERT(Struct);
                    
                    if (Struct->HasMeta("HideInComponentList"))
                    {
                        continue;
                    }
                    
                    FFixedString DisplayName = Struct->MakeDisplayName();
                    if (!AddEntityComponentFilter.PassFilter(DisplayName.c_str()))
                    {
                        continue;
                    }

                    SortedComponents.emplace_back(MetaType, Struct);
                }
                
                
                eastl::sort(SortedComponents.begin(), SortedComponents.end(), [](const auto& LHS, const auto& RHS)
                {
                   return LHS.second->GetName().ToString() < RHS.second->GetName().ToString(); 
                });

                for (auto&& [MetaType, Struct] : SortedComponents)
                {
                    ImGui::PushID(Struct);
                    
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.21f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.45f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.3f, 0.4f, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));
                    
                    const float ButtonWidth = ImGui::GetContentRegionAvail().x;
                    
                    FFixedString DisplayName = Struct->MakeDisplayName();
                    if (ImGui::Button(DisplayName.c_str(), ImVec2(ButtonWidth, 0.0f)))
                    {
                        if (World->GetEntityRegistry().valid(Entity))
                        {
                            ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs, entt::forward_as_meta(World->GetEntityRegistry()), Entity, entt::forward_as_meta(entt::meta_any{}));
                            OutlinerListView.MarkTreeDirty();
                            if (Entity == DetailsEntity)
                            {
                                bDetailsDirty = true;
                            }
                        }
                        else
                        {
                            BeginTransaction();
                            CreateEntityWithComponent(Struct);
                            EndTransaction("New Component");
                        }
                        
                        ImGui::CloseCurrentPopup();
                    }
                    
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(3);
                    
                    ImGui::PopID();
                    ImGui::Spacing();
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
                        BeginTransaction();
                        CreateEntity();
                        EndTransaction("New Entity");

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

    void FWorldEditorTool::DrawFilterOptions()
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
        
            int ColumnIndex = 0;
        
            for (auto&& [ID, Storage] : World->GetEntityRegistry().storage())
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
                    
                        ColumnIndex++;
                    }
                }
            }
        
            ImGui::EndTable();
        }
    }

    void FWorldEditorTool::RebuildSceneOutliner(FTreeListView& Tree)
    {
        LUMINA_PROFILE_SCOPE();

        // The outliner is now incremental. A "rebuild" just resets the local map and re-adds the
        // root entities; component lists and child entity rows are produced lazily when each row
        // is first expanded (BuildEntityChildren).
        EntityToTreeNode.clear();
        PendingOutlinerAdds.clear();

        TFixedVector<entt::entity, 1000> Roots;
        auto View = World->GetEntityRegistry().view<SNameComponent>(entt::exclude<FHideInSceneOutliner>);
        for (entt::entity Entity : View)
        {
            if (FRelationshipComponent* Rel = World->GetEntityRegistry().try_get<FRelationshipComponent>(Entity))
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

            return std::tie(A, LHS) < std::tie(B, RHS);
        });

        for (entt::entity Root : Roots)
        {
            AddEntityToOutliner(Root);
        }
    }

    FTreeNodeID FWorldEditorTool::AddEntityToOutliner(entt::entity Entity)
    {
        FEntityRegistry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Entity) || Registry.any_of<FHideInSceneOutliner>(Entity))
        {
            return InvalidTreeNode;
        }
        if (!Registry.all_of<SNameComponent>(Entity))
        {
            return InvalidTreeNode;
        }

        // Don't double-insert.
        auto Existing = EntityToTreeNode.find(Entity);
        if (Existing != EntityToTreeNode.end())
        {
            return Existing->second;
        }

        // If this entity has a parent and that parent is in the tree, attach there.
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
                    // Parent isn't in the tree yet; defer until it is. Avoid attaching as a
                    // root only to relocate a moment later when the parent's row is built.
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
        if (bIsLockedPrefabChild)
        {
            Display.TooltipText = "Prefab instance child, hierarchy is locked. Edit the source prefab to change.";
        }
        else
        {
            Display.TooltipText = FString("Entity: " + eastl::to_string(entt::to_integral(Entity))).c_str();
        }
        Display.bShowDisabledIcon = true;
        Display.bAllowRenaming = !bIsLockedPrefabChild;

        OutlinerListView.EmplaceUserData<FEntityListViewItemData>(ItemEntity).Entity = Entity;

        if (Registry.any_of<FSelectedInEditorComponent>(Entity))
        {
            OutlinerListView.Get<FTreeNodeState>(ItemEntity).bSelected = true;
        }

        if (Registry.any_of<SDisabledTag>(Entity))
        {
            OutlinerListView.Get<FTreeNodeState>(ItemEntity).bDisabled = true;
        }

        // Components and child entities are populated on demand via BuildChildrenFunction.
        OutlinerListView.MarkHasLazyChildren(ItemEntity);

        return ItemEntity;
    }

    void FWorldEditorTool::RemoveEntityFromOutliner(entt::entity Entity)
    {
        auto It = EntityToTreeNode.find(Entity);
        if (It == EntityToTreeNode.end())
        {
            return;
        }

        // RemoveNode tears down the subtree, but EntityToTreeNode also points at descendants;
        // walk the world hierarchy first and erase those map entries so we don't leak stale ids.
        FEntityRegistry& Registry = World->GetEntityRegistry();
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

    void FWorldEditorTool::ReparentEntityInOutliner(entt::entity Entity)
    {
        // Cheapest correct option: drop the row and re-add it. The lazy children of the new
        // parent will rebuild on the next expand. This avoids a generic "MoveSubtree" API
        // on the tree widget for a path that only fires on user-initiated drags.
        RemoveEntityFromOutliner(Entity);
        AddEntityToOutliner(Entity);
    }

    void FWorldEditorTool::BuildEntityChildren(FTreeListView& Tree, FTreeNodeID Item)
    {
        FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
        FEntityRegistry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Data.Entity))
        {
            return;
        }

        // 1. Component rows. These don't get incremental hooks; they're fully rebuilt each
        //    time the parent's lazy children fire. Component add/remove during a session is rare
        //    enough that re-expanding the parent is acceptable.
        ECS::Utils::ForEachComponent(Registry, Data.Entity, [&](void* Component, entt::basic_sparse_set<>& Set, entt::meta_type Meta)
        {
            FFixedString NameString;
            NameString.assign(LE_ICON_PUZZLE).append(" ").append(Meta.name());
            FTreeNodeID ComponentEntity = Tree.CreateNode(Item, FStringView(NameString.data(), NameString.length()));

            Tree.Get<FTreeNodeDisplay>(ComponentEntity).TooltipText = Meta.name();
            Tree.EmplaceUserData<FEntityListViewItemData>(ComponentEntity).Entity = Data.Entity;
        });

        // 2. Child entity rows. Skip any that are already present (e.g. spawned-while-parent-
        //    -expanded races where the on_construct hook beat us to it).
        ECS::Utils::ForEachChild(Registry, Data.Entity, [&](entt::entity Child)
        {
            if (Registry.any_of<FHideInSceneOutliner>(Child))
            {
                return;
            }
            if (EntityToTreeNode.find(Child) != EntityToTreeNode.end())
            {
                return;
            }

            // Reuse the same path as on_construct so display setup stays consistent.
            AddEntityToOutliner(Child);
        });
    }

    void FWorldEditorTool::OnOutlinerEntityConstructed(entt::registry& Registry, entt::entity Entity)
    {
        if (Registry.any_of<FHideInSceneOutliner>(Entity))
        {
            return;
        }
        // Defer to next flush — FRelationshipComponent may not be set yet at this point.
        PendingOutlinerAdds.push_back(Entity);
    }

    void FWorldEditorTool::OnOutlinerEntityDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        (void)Registry;
        RemoveEntityFromOutliner(Entity);
        PendingOutlinerAdds.erase(eastl::remove(PendingOutlinerAdds.begin(), PendingOutlinerAdds.end(), Entity), PendingOutlinerAdds.end());
    }

    void FWorldEditorTool::FlushOutlinerPending()
    {
        if (PendingOutlinerAdds.empty())
        {
            return;
        }

        // Iterate by index because AddEntityToOutliner may indirectly grow the queue in pathological
        // cases (it doesn't today, but be defensive).
        for (int32 i = 0; i < static_cast<int32>(PendingOutlinerAdds.size()); ++i)
        {
            AddEntityToOutliner(PendingOutlinerAdds[i]);
        }
        PendingOutlinerAdds.clear();
    }

    void FWorldEditorTool::HandleEntityEditorDragDrop(FTreeListView& Tree, entt::entity DropItem)
    {
        if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(DragDropID, ImGuiDragDropFlags_AcceptBeforeDelivery))
        {
            if (Payload->IsDelivery())
            {
                entt::entity SourceEntity = *static_cast<entt::entity*>(Payload->Data);
                entt::registry& Registry = World->GetEntityRegistry();

                if (IsLockedPrefabChild(Registry, SourceEntity) || IsLockedPrefabChild(Registry, DropItem))
                {
                    ImGuiX::Notifications::NotifyError("Cannot reparent prefab-instance children. Edit the source prefab instead.");
                    return;
                }

                BeginTransaction();
                ECS::Utils::ReparentEntity(Registry, SourceEntity, DropItem);
                EndTransaction("Reparent");

                ReparentEntityInOutliner(SourceEntity);
            }
            return;
        }

        AcceptContentBrowserPrefabPayload(DropItem);
    }

    void FWorldEditorTool::AcceptContentBrowserPrefabPayload(entt::entity DropTarget)
    {
        const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(
            FContentBrowserEditorTool::FContentBrowserTileViewItem::DragDropID,
            ImGuiDragDropFlags_AcceptBeforeDelivery);

        if (Payload == nullptr || !Payload->IsDelivery())
        {
            return;
        }

        const uintptr_t ValuePtr = *static_cast<uintptr_t*>(Payload->Data);
        const auto* PayloadItem = reinterpret_cast<FContentBrowserEditorTool::FContentBrowserTileViewItem*>(ValuePtr);
        if (PayloadItem && PayloadItem->IsAsset())
        {
            HandlePrefabContentDrop(PayloadItem->GetVirtualPath(), DropTarget);
        }
    }

    void FWorldEditorTool::HandlePrefabContentDrop(FStringView VirtualPath, entt::entity DropTarget)
    {
        // Despite the legacy name, this dispatches every asset class via the editor drop
        // registry (static mesh, material, prefab, ...). Spawn transform comes from the
        // camera so dropped assets land where the user is looking, not at world origin.
        BeginTransaction();
        entt::entity Spawned = HandleContentBrowserAssetDrop(VirtualPath, DropTarget);
        if (Spawned != entt::null)
        {
            EndTransaction("Drop Asset");
            SetSingleSelectedEntity(Spawned);
            OutlinerListView.MarkTreeDirty();
        }
        else
        {
            PendingBeforeState.clear();
        }
    }

    void FWorldEditorTool::DrawWorldSettings(bool bFocused)
    {
        WorldSettingsPropertyTable->DrawTree();
    }

    void FWorldEditorTool::DrawOutliner(bool bFocused)
    {
        const ImGuiStyle& Style = ImGui::GetStyle();
        
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            
            constexpr float ButtonWidth = 30.0f;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
            if (ImGui::Button(LE_ICON_PLUS, ImVec2(ButtonWidth, 0.0f)))
            {
                ImGui::OpenPopup("AddToEntityMenu");
            }
            ImGui::PopStyleColor();
            
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Add something new to the world.");
            }

            DrawAddToEntityOrWorldPopup();
            
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (ButtonWidth) - ImGui::GetStyle().FramePadding.x);
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
            
            if (ImGui::Button(LE_ICON_FILTER_SETTINGS "##ComponentFilter", ImVec2(ButtonWidth, 0.0f)))
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
            size_t EntityCount = World->GetEntityRegistry().view<entt::entity>().size<>();
            ImGui::Text(LE_ICON_FORMAT_LIST_NUMBERED " Total Entities: %s", eastl::to_string(EntityCount).c_str());
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
                    AcceptContentBrowserPrefabPayload(entt::null);
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::EndChild();
            
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        
    }

    void FWorldEditorTool::DrawEntityProperties(entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();

        if (World->IsSimulating())
        {
            ImGui::BeginDisabled();
        }
        
        SNameComponent* NameComponent = World->GetEntityRegistry().try_get<SNameComponent>(Entity);
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
            ImGui::BeginHorizontal(EntityName.c_str());
        
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::LargeBold);
            ImGui::AlignTextToFramePadding();
            ImGuiX::Text("Entity: {} (ID: ({})", EntityName, entt::to_integral(Entity));
            ImGui::PopFont();

            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(35, 35, 35, 255));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.65f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.25f, 1.0f));
        
            if (ImGui::Button(LE_ICON_PLUS))
            {
                ImGui::OpenPopup("AddToEntityMenu");
            }
            
            DrawAddToEntityOrWorldPopup(Entity);
        
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Add Component");
            }
        
            ImGui::PopStyleColor(3);
        
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.65f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.25f, 1.0f));
        
            if (ImGui::Button(LE_ICON_TAG))
            {
                PushAddTagModal(Entity);
            }
        
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Add Tag");
            }
        
            ImGui::PopStyleColor(3);
        
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
        
            if (ImGui::Button(LE_ICON_TRASH_CAN))
            {
                if (Dialogs::Confirmation("Confirm Deletion", 
                    "Are you sure you want to delete entity \"{0}\"?\n\nThis action cannot be undone.", 
                    (uint32)Entity))
                {
                    EntityDestroyRequests.push(Entity);
                }
            }
        
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Delete Entity");
            }
        
            ImGui::PopStyleColor(3);
        
            ImGui::EndHorizontal();
            ImGui::PopStyleVar(3);
            
            ImGui::EndTable();
        }
        
        if (World->IsSimulating())
        {
            ImGui::EndDisabled();
        }
        
        ImGui::SeparatorText("Details");

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(LE_ICON_PUZZLE " Tags");
        ImGui::PopStyleColor();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        DrawTagList(Entity);
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(LE_ICON_CUBE " Components");
        ImGui::PopStyleColor();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        DrawComponentList(Entity);
    }

    void FWorldEditorTool::DrawEntityActionButtons(entt::entity Entity)
    {
        constexpr float ButtonHeight = 32.0f;
        const float AvailWidth = ImGui::GetContentRegionAvail().x;
        const float ButtonWidth = (AvailWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.65f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.25f, 1.0f));

        if (ImGui::Button(LE_ICON_PLUS " Add Component", ImVec2(ButtonWidth, ButtonHeight)))
        {
            PushAddComponentModal(Entity);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Add a new component to this entity");
        }

        ImGui::SameLine();

        if (ImGui::Button(LE_ICON_TAG " Add Tag", ImVec2(ButtonWidth, ButtonHeight)))
        {
            PushAddTagModal(Entity);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Add a runtime tag to this entity to use with runtime views.");
        }

        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));

        if (ImGui::Button(LE_ICON_TRASH_CAN " Destroy", ImVec2(AvailWidth, ButtonHeight)))
        {
            if (Dialogs::Confirmation("Confirm Deletion", "Are you sure you want to delete entity \"{0}\"?\n""\nThis action cannot be undone.", (uint32)Entity))
            {
                EntityDestroyRequests.push(Entity);
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Permanently delete this entity");
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

    void FWorldEditorTool::DrawComponentList(entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();
        
        for (TUniquePtr<FPropertyTable>& Table : PropertyTables)
        {
            DrawComponentHeader(Table, Entity);
        
            ImGui::Spacing();
        }
    }

    void FWorldEditorTool::DrawTagList(entt::entity Entity)
    {

        TFixedVector<FName, 4> Tags;
        for (auto [Name, Storage] : World->GetEntityRegistry().storage())
        {
            if (Storage.info() == entt::type_id<STagComponent>())
            {
                if (Storage.contains(Entity))
                {
                    STagComponent* ComponentPtr = static_cast<STagComponent*>(Storage.value(Entity));
                    Tags.push_back(ComponentPtr->Tag);
                }
            }
        }
        
        if (Tags.empty())
        {
            return;
        }
        
        if (World->IsSimulating())
        {
            ImGui::BeginDisabled();
        }
        
        ImGui::PushID("TagList");
        
        // Section header
        ImVec2 CursorPos = ImGui::GetCursorScreenPos();
        ImVec2 HeaderSize = ImVec2(ImGui::GetContentRegionAvail().x, 32.0f);
        
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        DrawList->AddRectFilled(CursorPos, ImVec2(CursorPos.x + HeaderSize.x, CursorPos.y + HeaderSize.y), IM_COL32(25, 25, 30, 255), 6.0f);
        
        DrawList->AddRect(CursorPos, ImVec2(CursorPos.x + HeaderSize.x, CursorPos.y + HeaderSize.y), IM_COL32(45, 45, 52, 255), 6.0f, 0, 1.0f);
        
        ImVec2 IconPos = CursorPos;
        IconPos.x += 12.0f;
        IconPos.y += (HeaderSize.y - ImGui::GetTextLineHeight()) * 0.5f;
        DrawList->AddText(IconPos, IM_COL32(150, 170, 200, 255), LE_ICON_TAG);
        
        ImVec2 TitlePos = IconPos;
        TitlePos.x += 24.0f;
        DrawList->AddText(TitlePos, IM_COL32(220, 220, 230, 255), "Tags");
        
        // Tag count badge
        char CountBuf[16];
        snprintf(CountBuf, sizeof(CountBuf), "%zu", Tags.size());
        ImVec2 CountPos = TitlePos;
        CountPos.x += ImGui::CalcTextSize("Tags").x + 8.0f;
        CountPos.y -= 1.0f;
        
        ImVec2 CountBadgeSize = ImGui::CalcTextSize(CountBuf);
        CountBadgeSize.x += 10.0f;
        CountBadgeSize.y += 2.0f;
        
        DrawList->AddRectFilled(CountPos, 
            ImVec2(CountPos.x + CountBadgeSize.x, CountPos.y + CountBadgeSize.y),
            IM_COL32(60, 80, 120, 180), 3.0f);
        DrawList->AddText(ImVec2(CountPos.x + 5.0f, CountPos.y + 1.0f), 
            IM_COL32(180, 200, 240, 255), CountBuf);
        
        ImGui::SetCursorScreenPos(ImVec2(CursorPos.x, CursorPos.y + HeaderSize.y + 4.0f));
        
        // Tag chips
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));
        
        float AvailWidth = ImGui::GetContentRegionAvail().x;
        float CurrentX = 0.0f;
        
        FName TagToRemove;
        
        for (const FName& Tag : Tags)
        {
            ImGui::PushID(Tag.c_str());
            
            const char* TagStr = Tag.c_str();
            ImVec2 TagSize = ImGui::CalcTextSize(TagStr);
            float ChipWidth = TagSize.x + 52.0f;
            
            if (CurrentX + ChipWidth > AvailWidth && CurrentX > 0.0f)
            {
                CurrentX = 0.0f;
            }
            else if (CurrentX > 0.0f)
            {
                ImGui::SameLine();
            }
            
            ImVec2 ChipPos = ImGui::GetCursorScreenPos();
            ImVec2 ChipSize = ImVec2(ChipWidth, TagSize.y + 10.0f);
            
            bool bHovered = ImGui::IsMouseHoveringRect(ChipPos, 
                ImVec2(ChipPos.x + ChipSize.x, ChipPos.y + ChipSize.y));
            
            ImU32 ChipBg = bHovered ? IM_COL32(55, 65, 85, 255) : IM_COL32(45, 55, 75, 255);
            ImU32 ChipBorder = bHovered ? IM_COL32(80, 100, 140, 255) : IM_COL32(65, 80, 115, 255);
            
            DrawList->AddRectFilled(ChipPos, ImVec2(ChipPos.x + ChipSize.x, ChipPos.y + ChipSize.y), ChipBg, 12.0f);
            DrawList->AddRect(ChipPos, ImVec2(ChipPos.x + ChipSize.x, ChipPos.y + ChipSize.y), ChipBorder, 12.0f, 0, 1.0f);
            
            DrawList->AddText(ImVec2(ChipPos.x + 10.0f, ChipPos.y + 5.0f), IM_COL32(130, 160, 210, 255), LE_ICON_TAG);
            
            DrawList->AddText(ImVec2(ChipPos.x + 28.0f, ChipPos.y + 5.0f), IM_COL32(200, 210, 230, 255), TagStr);
            
            ImVec2 ClosePos = ImVec2(ChipPos.x + ChipSize.x - 20.0f, ChipPos.y + 5.0f);
            bool bCloseHovered = ImGui::IsMouseHoveringRect(ImVec2(ClosePos.x - 4.0f, ClosePos.y - 4.0f), ImVec2(ClosePos.x + 12.0f, ClosePos.y + 12.0f));
            
            ImU32 CloseColor = bCloseHovered ? IM_COL32(240, 100, 100, 255) : IM_COL32(150, 150, 160, 255);
            DrawList->AddText(ClosePos, CloseColor, LE_ICON_CLOSE);
            
            ImGui::InvisibleButton("##chip", ChipSize);
            
            if (bCloseHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                TagToRemove = Tag;
            }
            
            CurrentX += ChipWidth + ImGui::GetStyle().ItemSpacing.x;
            
            ImGui::PopID();
        }
        
        ImGui::PopStyleVar(3);
        
        if (!TagToRemove.IsNone())
        {
            World->GetEntityRegistry().storage<STagComponent>(entt::hashed_string(TagToRemove.c_str())).remove(Entity);
        }
        
        ImGui::Spacing();
        ImGui::PopID();
        
        if (World->IsSimulating())
        {
            ImGui::EndDisabled();
        }
    }

    void FWorldEditorTool::DrawComponentHeader(const TUniquePtr<FPropertyTable>& Table, entt::entity Entity)
    {
        using namespace entt::literals;
        
        const bool bIsRequired = (Table->GetType() == STransformComponent::StaticStruct() || Table->GetType() == SNameComponent::StaticStruct());
    
        if (Table->GetType() == STagComponent::StaticStruct())
        {
            return;
        }
        
        entt::meta_type MetaType = entt::resolve(entt::hashed_string(Table->GetType()->GetName().c_str()));
        if (!ECS::Utils::HasComponent(World->GetEntityRegistry(), Entity, MetaType))
        {
            return;
        }
        
        ImGui::PushID(Table.get());
            
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
            bIsOpen = ImGui::CollapsingHeader(Table->GetType()->MakeDisplayName().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
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
                    ComponentDestroyRequests.push(FComponentDestroyRequest{Table->GetType(), Entity});
                }
            
                ImGuiX::TextTooltip("{}", "Remove Component");
            
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
            }
                        
            ImGui::EndTable();
        }
        
        ImGui::PopStyleVar(2);
        
        
        if (bIsOpen)
        {
            ImGui::Spacing();
            
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.015f, 0.015f, 0.015f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
            
            ImGui::Indent(8.0f);
            
            Table->DrawTree(World->IsSimulating());
            
            ImGui::Unindent(8.0f);
            
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            
            ImGui::Spacing();
        }
            
        ImGui::PopID();
    }

    void FWorldEditorTool::RemoveComponent(entt::entity Entity, const CStruct* ComponentType)
    {
        bool bWasRemoved = false;

        if (ComponentType == nullptr)
        {
            return;
        }
        
        ECS::Utils::ForEachComponent(World->GetEntityRegistry(), Entity, [&](void* Component, entt::basic_sparse_set<>& Set, const entt::meta_type& Type)
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
            // The next DrawEntityEditor pass will rebuild PropertyTables from the post-removal
            // component set. Marking dirty (instead of an inline rebuild) keeps a single
            // rebuild pathway and avoids tearing down handles mid-frame while the panel is
            // already drawing.
            if (Entity == DetailsEntity)
            {
                bDetailsDirty = true;
            }
        }
        else
        {
            ImGuiX::Notifications::NotifyError("Failed to remove component: {0}", ComponentType->GetName().c_str());
        }
    }

    void FWorldEditorTool::DrawEmptyState()
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

    void FWorldEditorTool::OnPrePropertyChangeEvent(const FPropertyChangedEvent& Event)
    {

    }

    void FWorldEditorTool::OnPostPropertyChangeEvent(const FPropertyChangedEvent& Event)
    {
        using namespace entt::literals;
        
        entt::id_type TypeID = ECS::Utils::GetTypeID(Event.OuterType->GetName().c_str());

        auto View = World->GetEntityRegistry().view<FSelectedInEditorComponent>();
        View.each([&](entt::entity Entity)
        {
            entt::meta_any Has = ECS::Utils::InvokeMetaFunc(TypeID, "has"_hs, entt::forward_as_meta(World->GetEntityRegistry()), Entity);
            if (Has.cast<bool>())
            {
                entt::meta_any Component = ECS::Utils::InvokeMetaFunc(TypeID, "get"_hs, entt::forward_as_meta(World->GetEntityRegistry()), Entity);
                ECS::Utils::InvokeMetaFunc(TypeID, "patch"_hs, entt::forward_as_meta(World->GetEntityRegistry()), Entity, entt::forward_as_meta(Component));
            }
        });
    }

    bool FWorldEditorTool::IsUnsavedDocument()
    {
        return World && World->GetPackage() && World->GetPackage()->IsDirty();
    }

    void FWorldEditorTool::DrawEntityEditor(bool bFocused, entt::entity Entity)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);

        ImGui::BeginChild("Property Editor", ImVec2(0, 0), true);

        // The details panel reads from PropertyTables, which hold raw pointers into
        // component storage for DetailsEntity. Whenever the focused entity changes, or
        // the entity went invalid (destroyed), or some structural change explicitly
        // marked us dirty (component add/remove, undo/redo), rebuild before drawing.
        const bool bEntityValid = (Entity != entt::null) && World->GetEntityRegistry().valid(Entity);

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

    void FWorldEditorTool::DrawPropertyEditor(bool bFocused)
    {
        
    }

    void FWorldEditorTool::RebuildPropertyTables(entt::entity Entity)
    {
        using namespace entt::literals;

        PropertyTables.clear();

        // Tracking which entity these tables belong to lets DrawEntityEditor detect
        // staleness without re-running this work every frame. Reset to null on invalid
        // input so the next valid selection forces a full rebuild.
        DetailsEntity = (Entity != entt::null && World->GetEntityRegistry().valid(Entity)) ? Entity : entt::null;
        bDetailsDirty = false;

        if (World->GetEntityRegistry().valid(Entity))
        {
            using PairType = TPair<void*, CStruct*>;
            TVector<PairType> Sorted;
            ECS::Utils::ForEachComponent(World->GetEntityRegistry(), Entity, [&](void* Component, entt::basic_sparse_set<>& Set, const entt::meta_type& Type)
            {
                entt::meta_any Any = ECS::Utils::InvokeMetaFunc(Type, "static_struct"_hs);
                if (!Any)
                {
                    return;
                }
                    
                CStruct* Struct = Any.cast<CStruct*>();

                Sorted.emplace_back(Component, Struct);
            });
            
            eastl::sort(Sorted.begin(), Sorted.end(), [&](const PairType& LHS, const PairType& RHS)
            {
                const FFixedString A = LHS.second->MakeDisplayName();
                const FFixedString B = RHS.second->MakeDisplayName();
                
                auto Comparator = [] (const CStruct* Type)
                {
                    if (Type == SNameComponent::StaticStruct())
                    {
                        return 0;
                    }
                    
                    if (Type == STransformComponent::StaticStruct())
                    {
                        return 1;
                    }
                    
                    return 2;
                };
                
                uint32 APriority = Comparator(LHS.second);
                uint32 BPriority = Comparator(RHS.second);
                
                if (APriority != BPriority)
                {
                    return  APriority < BPriority;
                }
                
                return A < B;
            });


            for (const auto& [Component, Struct] : Sorted)
            {
                TUniquePtr<FPropertyTable> NewTable = MakeUnique<FPropertyTable>(Component, Struct);
                
                NewTable->SetPreEditCallback([&](const FPropertyChangedEvent& Event)
                {
                    OnPrePropertyChangeEvent(Event);
                });
                
                NewTable->SetPostEditCallback([&] (const FPropertyChangedEvent& Event)
                {
                    OnPostPropertyChangeEvent(Event);
                });
                
                NewTable->SetStartEditCallback([&](const FPropertyChangedEvent& Event)
                {
                    BeginTransaction();
                });
                
                NewTable->SetFinishEditCallback([&](const FPropertyChangedEvent& Event)
                {
                    EndTransaction(Event.PropertyName);
                });
                
                PropertyTables.emplace_back(Move(NewTable))->MarkDirty();
            }
        }
    }

    void FWorldEditorTool::CreateEntityWithComponent(const CStruct* Component)
    {
        using namespace entt::literals;

        entt::hashed_string Hash = entt::hashed_string(Component->GetName().c_str());
        entt::meta_type MetaType = entt::resolve(Hash);

        entt::entity CreatedEntity = World->ConstructEntity(Component->MakeDisplayName(), GetCameraSpawnTransform());
        ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs, entt::forward_as_meta(World->GetEntityRegistry()), CreatedEntity, entt::forward_as_meta(entt::meta_any{}));

        // Newly-created entities always become the selection so the user immediately sees them
        // in the details panel and the outliner highlight. Old code only auto-selected if
        // something was already selected, which left fresh worlds with no feedback.
        if (CreatedEntity != entt::null)
        {
            SetSingleSelectedEntity(CreatedEntity);
        }
        // Outliner row appears via OnOutlinerEntityConstructed → FlushOutlinerPending.
    }

    void FWorldEditorTool::CreateEntity()
    {
        entt::entity NewEntity = World->ConstructEntity("Entity", GetCameraSpawnTransform());
        if (NewEntity != entt::null)
        {
            SetSingleSelectedEntity(NewEntity);
        }
        // Outliner row appears via OnOutlinerEntityConstructed → FlushOutlinerPending.
    }

    void FWorldEditorTool::CreatePrimitiveEntity(CStaticMesh* PrimitiveMesh, const char* DisplayName)
    {
        if (PrimitiveMesh == nullptr)
        {
            return;
        }

        entt::entity CreatedEntity = World->ConstructEntity(DisplayName, GetCameraSpawnTransform());
        if (CreatedEntity == entt::null)
        {
            return;
        }

        SStaticMeshComponent& MeshComp = World->GetEntityRegistry().emplace<SStaticMeshComponent>(CreatedEntity);
        MeshComp.StaticMesh = PrimitiveMesh;

        SetSingleSelectedEntity(CreatedEntity);
    }

    void FWorldEditorTool::CopyEntity(entt::entity& To, entt::entity From)
    {
        World->DuplicateEntity(To, From, [&](const entt::type_info& Type)
        {
            if    (Type == entt::type_id<FRelationshipComponent>()
                || Type == entt::type_id<FSelectedInEditorComponent>()
                || Type == entt::type_id<FCopiedTag>()
                || Type == entt::type_id<FLastSelectedTag>())
            {
                return false;
            }

            return true;
        });
    }

    void FWorldEditorTool::CycleGuizmoOp()
    {
        switch (GuizmoOp)
        {
        case ImGuizmo::TRANSLATE:
            {
                GuizmoOp = ImGuizmo::ROTATE;
            }
            break;
        case ImGuizmo::ROTATE:
            {
                GuizmoOp = ImGuizmo::SCALE;
            }
            break;
        case ImGuizmo::SCALE:
            {
                GuizmoOp = ImGuizmo::TRANSLATE;
            }
            break;
        }
    }
}
