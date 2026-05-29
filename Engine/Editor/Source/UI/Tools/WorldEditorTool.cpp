#include "WorldEditorTool.h"
#include "Core/Math/Math.h"
#include "EditorToolContext.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
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
#include "Core/Object/ObjectArray.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Object/Package/Package.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "Core/Serialization/JsonArchiver.h"
#include "Core/Serialization/ObjectArchiver.h"
#include "EASTL/sort.h"
#include "Input/InputContext.h"
#include "Input/InputProcessor.h"
#include "UI/RmlUiBridge.h"
#include "Input/InputViewport.h"
#include "Memory/SmartPtr.h"
#include "Core/Math/Math.h"
#include "Thumbnails/ThumbnailManager.h"
#include "Tools/ComponentVisualizers/ComponentVisualizer.h"
#include "Tools/PrimitiveManager/PrimitiveManager.h"
#include "Tools/Dialogs/Dialogs.h"
#include "Tools/UI/ImGui/ImGuiDragDrop.h"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "TerrainEditMode.h"
#include "World/Entity/Components/TerrainComponent.h"
#include "UI/Tools/EditorEntityUtils.h"
#include "UI/Properties/EntityPropertyContext.h"
#include "World/WorldManager.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "world/entity/components/entitytags.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/ScriptComponent.h"
#include "world/entity/components/skeletalmeshcomponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/TagComponent.h"
#include "World/Scene/RenderScene/RenderScene.h"
#include "World/Scene/RenderScene/SceneRenderTypes.h"
#include "World/Subsystems/WorldSettings.h"


namespace Lumina
{
    static constexpr const char* WorldSettingsName = "World Settings";
    static constexpr const char* SceneGraphName = "Scene Graph";
    
    static FVector3 SanitizeManipulationScale(FVector3 Scale)
    {
        constexpr float MinScale = 0.001f;
        for (int i = 0; i < 3; ++i)
        {
            if (!std::isfinite(Scale[i]) || std::abs(Scale[i]) < MinScale)
            {
                Scale[i] = Scale[i] < 0.0f ? -MinScale : MinScale;
            }
        }
        return Scale;
    }

    static bool IsLockedPrefabChild(const entt::registry& Registry, entt::entity Entity)
    {
        if (Entity == entt::null || !Registry.valid(Entity))
        {
            return false;
        }
        const SPrefabInstanceComponent* Instance = Registry.try_get<SPrefabInstanceComponent>(Entity);
        return Instance != nullptr && !Instance->bIsRoot;
    }
    
    static entt::entity ResolveSelectionRootForViewportPick(entt::registry& Registry, entt::entity Entity)
    {
        if (Entity == entt::null || !Registry.valid(Entity))
        {
            return Entity;
        }

        // The picked entity is itself a root — keep it.
        if (Registry.all_of<FSelectionRoot>(Entity))
        {
            return Entity;
        }
        if (const SPrefabInstanceComponent* Instance = Registry.try_get<SPrefabInstanceComponent>(Entity))
        {
            if (Instance->bIsRoot)
            {
                return Entity;
            }
        }

        const FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        while (Relationship != nullptr && Relationship->Parent != entt::null)
        {
            entt::entity Parent = Relationship->Parent;
            if (Registry.all_of<FSelectionRoot>(Parent))
            {
                return Parent;
            }
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

    // CPU marquee-pick + drop-to-floor: editor world has no physics scene, so we project mesh AABBs in software.

    // Project world AABB to screen rect (y-down, viewport pixels). Returns false if the box is behind near plane.
    static bool ProjectAABBToScreenRect(const FAABB& WorldAABB, const FMatrix4& ViewProj,
                                        const ImVec2& ViewportSize,
                                        ImVec2& OutMin, ImVec2& OutMax)
    {
        const FVector3 Corners[8] = {
            { WorldAABB.Min.x, WorldAABB.Min.y, WorldAABB.Min.z },
            { WorldAABB.Max.x, WorldAABB.Min.y, WorldAABB.Min.z },
            { WorldAABB.Min.x, WorldAABB.Max.y, WorldAABB.Min.z },
            { WorldAABB.Max.x, WorldAABB.Max.y, WorldAABB.Min.z },
            { WorldAABB.Min.x, WorldAABB.Min.y, WorldAABB.Max.z },
            { WorldAABB.Max.x, WorldAABB.Min.y, WorldAABB.Max.z },
            { WorldAABB.Min.x, WorldAABB.Max.y, WorldAABB.Max.z },
            { WorldAABB.Max.x, WorldAABB.Max.y, WorldAABB.Max.z },
        };

        OutMin = ImVec2( FLT_MAX,  FLT_MAX);
        OutMax = ImVec2(-FLT_MAX, -FLT_MAX);
        bool bAnyInFront = false;

        for (const FVector3& Corner : Corners)
        {
            FVector4 Clip = ViewProj * FVector4(Corner, 1.0f);
            if (Clip.w <= 1e-4f)
            {
                continue;
            }
            const float NdcX = Clip.x / Clip.w;
            const float NdcY = Clip.y / Clip.w;
            // Caller flipped [1][1] to GL-Y-up; convert NDC +Y up to y-down pixels.
            const float Px = (NdcX * 0.5f + 0.5f) * ViewportSize.x;
            const float Py = (1.0f - (NdcY * 0.5f + 0.5f)) * ViewportSize.y;
            OutMin.x = Math::Min(OutMin.x, Px);
            OutMin.y = Math::Min(OutMin.y, Py);
            OutMax.x = Math::Max(OutMax.x, Px);
            OutMax.y = Math::Max(OutMax.y, Py);
            bAnyInFront = true;
        }

        return bAnyInFront;
    }

    // Slab ray-vs-AABB; OutT is along (possibly unnormalized) Dir. Returns true on hit in front of origin.
    static bool RayVsAABB(const FVector3& Origin, const FVector3& Dir,
                          const FAABB& Box, float& OutT)
    {
        float TMin = 0.0f;
        float TMax = FLT_MAX;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            if (Math::Abs(Dir[Axis]) < 1e-6f)
            {
                if (Origin[Axis] < Box.Min[Axis] || Origin[Axis] > Box.Max[Axis])
                {
                    return false;
                }
                continue;
            }
            float T1 = (Box.Min[Axis] - Origin[Axis]) / Dir[Axis];
            float T2 = (Box.Max[Axis] - Origin[Axis]) / Dir[Axis];
            if (T1 > T2) { eastl::swap(T1, T2); }
            TMin = Math::Max(TMin, T1);
            TMax = Math::Min(TMax, T2);
            if (TMin > TMax)
            {
                return false;
            }
        }
        OutT = TMin;
        return true;
    }

    // Project world point to viewport pixels (y-down); ViewProj uses GL-Y-up like ProjectAABBToScreenRect.
    static bool ProjectPointToScreen(const FVector3& WorldPos, const FMatrix4& ViewProj,
                                     const ImVec2& ViewportSize, ImVec2& OutScreen)
    {
        FVector4 Clip = ViewProj * FVector4(WorldPos, 1.0f);
        if (Clip.w <= 1e-4f)
        {
            return false;
        }
        const float NdcX = Clip.x / Clip.w;
        const float NdcY = Clip.y / Clip.w;
        OutScreen.x = (NdcX * 0.5f + 0.5f) * ViewportSize.x;
        OutScreen.y = (1.0f - (NdcY * 0.5f + 0.5f)) * ViewportSize.y;
        return true;
    }

    // Walk LOD-0 verts. Mesh data is per-meshlet 10-10-10 quantized: dequant = MeshOrigin + (LoInt + q) * GridStep.
    template <typename TVisitor>
    static void ForEachMeshVertexLocal(const CStaticMesh& Mesh, TVisitor&& Visit)
    {
        const FMeshResource& Resource = Mesh.GetMeshResource();
        if (Resource.bSkinnedMesh)
        {
            return;
        }
        const FMeshletData& MeshletData = Resource.MeshletData;
        if (MeshletData.IsEmpty())
        {
            return;
        }
        const TVector<FMeshletVertex>& Verts = MeshletData.MeshletVertices;
        const TVector<FMeshlet>& Meshlets    = MeshletData.Meshlets;

        Mesh.ForEachSurface([&](const FGeometrySurface& Surface, uint32)
        {
            if (Surface.NumLODs == 0)
            {
                return;
            }
            const uint32 Offset = Surface.LODMeshletOffset[0];
            const uint32 Count  = Surface.LODMeshletCount[0];
            for (uint32 i = 0; i < Count; ++i)
            {
                const FMeshlet& M = Meshlets[Offset + i];
                for (uint32 v = 0; v < M.VertexCount; ++v)
                {
                    const uint32 P = Verts[M.VertexOffset + v].Position;
                    FIntVector3 Q;
                    Q.x = int32( P        & 0x3FFu);
                    Q.y = int32((P >> 10) & 0x3FFu);
                    Q.z = int32((P >> 20) & 0x3FFu);
                    const FVector3 LocalPos = MeshletData.MeshOrigin[M.LODIndex] + FVector3(M.LoInt + Q) * MeshletData.MeshGridStep[M.LODIndex];
                    Visit(LocalPos);
                }
            }
        });
    }

    // LOD-0 vertex closest to TargetScreenPos; returns true if any projected within MaxScreenDistPx.
    static bool FindClosestVertexToScreenPoint(const CStaticMesh& Mesh,
                                               const FMatrix4& MeshWorldMatrix,
                                               const FMatrix4& ViewProj,
                                               const ImVec2& ViewportSize,
                                               const ImVec2& TargetScreenPos,
                                               float MaxScreenDistPx,
                                               FVector3& OutLocalPos,
                                               FVector3& OutWorldPos)
    {
        const FMeshResource& Resource = Mesh.GetMeshResource();
        if (Resource.bSkinnedMesh || Resource.MeshletData.IsEmpty())
        {
            return false;
        }

        // Cheap whole-mesh cull against an inflated screen rect.
        ImVec2 BoxMin, BoxMax;
        const FAABB WorldAABB = Mesh.GetAABB().ToWorld(MeshWorldMatrix);
        if (!ProjectAABBToScreenRect(WorldAABB, ViewProj, ViewportSize, BoxMin, BoxMax))
        {
            return false;
        }
        if (TargetScreenPos.x < BoxMin.x - MaxScreenDistPx || TargetScreenPos.x > BoxMax.x + MaxScreenDistPx ||
            TargetScreenPos.y < BoxMin.y - MaxScreenDistPx || TargetScreenPos.y > BoxMax.y + MaxScreenDistPx)
        {
            return false;
        }

        const FMatrix4 MVP = ViewProj * MeshWorldMatrix;
        float BestDistSq = MaxScreenDistPx * MaxScreenDistPx;
        bool  bFound = false;

        ForEachMeshVertexLocal(Mesh, [&](const FVector3& LocalPos)
        {
            FVector4 Clip = MVP * FVector4(LocalPos, 1.0f);
            if (Clip.w <= 1e-4f)
            {
                return;
            }
            const float Px = (Clip.x / Clip.w * 0.5f + 0.5f) * ViewportSize.x;
            const float Py = (1.0f - (Clip.y / Clip.w * 0.5f + 0.5f)) * ViewportSize.y;
            const float dx = Px - TargetScreenPos.x;
            const float dy = Py - TargetScreenPos.y;
            const float DistSq = dx * dx + dy * dy;
            if (DistSq < BestDistSq)
            {
                BestDistSq  = DistSq;
                OutLocalPos = LocalPos;
                OutWorldPos = FVector3(MeshWorldMatrix * FVector4(LocalPos, 1.0f));
                bFound = true;
            }
        });

        return bFound;
    }


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

        RegisterEditorActions();
        RegisterEditorModes();

        WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());
        
        OutlinerContext.SetDragDropFunction = [this] (FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            DragDrop::SetEntityPayload(World, Data.Entity);
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
                    TFixedVector<entt::entity, 20> Children;
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

            if (!bLocked)
            {
                const bool bIsSelectionRoot = Registry.all_of<FSelectionRoot>(Data.Entity);
                if (ImGui::MenuItem(bIsSelectionRoot ? "Unmark Selection Root" : "Mark as Selection Root"))
                {
                    if (bIsSelectionRoot)
                    {
                        Registry.remove<FSelectionRoot>(Data.Entity);
                    }
                    else
                    {
                        Registry.emplace<FSelectionRoot>(Data.Entity);
                    }
                }
                ImGuiX::TextTooltip("{}", "Viewport clicks on any descendant will resolve up to this entity. Outliner clicks still select directly.");
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

            if (!bLocked && ImGui::MenuItem(LE_ICON_PACKAGE_VARIANT " Create Prefab from Entity..."))
            {
                PushCreatePrefabModalForEntity(Data.Entity);
            }
            ImGuiX::TextTooltip("{}", "Save this entity (and its descendants) as a reusable prefab asset.");

            // Detach: only on a prefab instance root. After detach the entities become plain
            // and stop syncing to the source asset.
            if (const SPrefabInstanceComponent* Instance = Registry.try_get<SPrefabInstanceComponent>(Data.Entity);
                Instance != nullptr && Instance->bIsRoot)
            {
                if (ImGui::MenuItem(LE_ICON_LINK_VARIANT_OFF " Detach from Prefab"))
                {
                    BeginTransaction();
                    if (CPrefab::DetachInstance(World, Data.Entity))
                    {
                        EndTransaction("Detach from Prefab");
                        OutlinerListView.MarkTreeDirty();
                    }
                    else
                    {
                        PendingBeforeState.clear();
                    }
                }
                ImGuiX::TextTooltip("{}", "Unlink this instance from its source prefab; the entities become plain and stop syncing.");
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
        
        OutlinerContext.HoveredFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            FEntityRegistry& Registry = World->GetEntityRegistry();
            if (!Registry.valid(Data.Entity))
            {
                return;
            }
            
            EditorEntityUtils::DrawEntityBounds(World, Data.Entity, FColor::White, 3.0f);
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
            // bShouldClear: plain click replaces selection; false is Ctrl-toggle. Selection mutators below
            // own writing bSelected so the canonical set, registry tags, and outliner rows stay in sync.
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

        OutlinerContext.ItemDoubleClickedFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            FocusViewportToEntity(Data.Entity);
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

    void FWorldEditorTool::UpdateCameraPreview()
    {
        bCameraPreviewActive = false;

        IRenderScene* RenderScene = World ? World->GetRenderer() : nullptr;
        if (RenderScene == nullptr)
        {
            return;
        }

        // The render scene can be torn down + rebuilt (idle reclaim), invalidating our handle.
        // Detect the swap and force re-registration against the new scene.
        if (RenderScene != CameraPreviewScene)
        {
            CameraPreviewScene  = RenderScene;
            CameraPreviewHandle = -1;
        }

        // Only preview in the editor (not during PIE / game-view), and only for a selected
        // camera entity. Otherwise leave the capture registered but disabled (no render cost).
        entt::registry& Registry = World->GetEntityRegistry();
        const entt::entity Selected = GetLastSelectedEntity();
        const bool bWantPreview =
            !World->IsGameWorld() && !bGameViewMode &&
            Registry.valid(Selected) &&
            Registry.all_of<SCameraComponent, STransformComponent>(Selected);

        if (!bWantPreview)
        {
            if (CameraPreviewHandle >= 0)
            {
                RenderScene->SetCaptureView(CameraPreviewHandle, FViewVolume{}, false);
            }
            return;
        }

        if (CameraPreviewHandle < 0)
        {
            CameraPreviewHandle = RenderScene->RegisterCaptureView(FUIntVector2(CameraPreviewWidth, CameraPreviewHeight));
            if (CameraPreviewHandle < 0)
            {
                return;
            }
        }

        // Build the camera's view from its world transform + FOV (its own ViewVolume isn't
        // resolved while it's a non-active camera). Forward/up convention matches SCameraSystem.
        const SCameraComponent& Camera = Registry.get<SCameraComponent>(Selected);
        STransformComponent& Transform = Registry.get<STransformComponent>(Selected);
        (void)Transform.GetWorldMatrix();   // ensure the world transform is current

        const FVector3 Position = Transform.GetWorldLocation();
        const FQuat Rotation = Transform.GetWorldRotation();
        const FVector3 Forward  = Rotation * FVector3(0.0f, 0.0f, 1.0f);
        const FVector3 Up       = Rotation * FVector3(0.0f, 1.0f, 0.0f);

        // Use the authored FOV property, not GetFOV(): the latter reads the camera's internal
        // ViewVolume, which only tracks the property for the *active* camera (via SCameraSystem).
        // A non-active selected camera's ViewVolume is stale, so editing FOV wouldn't show.
        FViewVolume View(Camera.FOV, (float)CameraPreviewWidth / (float)CameraPreviewHeight);
        View.SetView(Position, Forward, Up);

        RenderScene->SetCaptureView(CameraPreviewHandle, View, true);
        bCameraPreviewActive = true;
    }

    void FWorldEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FEditorTool::Update(UpdateContext);

        // Drive the selected-camera preview before the world extracts this frame.
        UpdateCameraPreview();

        // Reassert Game focus each frame: a game script's SetMouseMode("Normal")
        // clears NoMouse, which would otherwise re-enable ImGui mid-play.
        if (bGamePreviewRunning && InputFocus == EInputFocus::Game)
        {
            ApplyInputFocus();
        }

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
            // Snapshot once so a Delete that queues several entities is one undo step.
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

            // Snapshot before mutating; iterating the view while emitting/destroying invalidates iterators.
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

            // Snapshot once so a Ctrl+D batch is a single undo.
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
                }
            }

            // Select the duplicates so Ctrl+D twice keeps moving the new copies.
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

            // Every selectable entity type gets the same selection box (static mesh, skeletal mesh,
            // or a unit-box fallback for lights/empties/etc.), resolved by the shared helper.
            EditorEntityUtils::DrawEntitySelectionBox(World, Entity, FColor::Green, 0.2f, 5.0f);
        }

        const bool bPastePressed = bViewportHovered
            && ImGui::IsKeyDown(ImGuiKey_LeftCtrl)
            && ImGui::IsKeyPressed(ImGuiKey_V, false);

        if (bPastePressed)
        {
            // Snapshot sources first; CopyEntity adds rows the view would otherwise re-paste.
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
        
        // Camera bookmarks: 1..9 recall, Ctrl+1..9 save. Loop-driven, so handled inline rather than as N actions.
        if (bViewportHovered && !ImGui::GetIO().WantTextInput)
        {
            const ImGuiIO& IO = ImGui::GetIO();
            const bool bPlain = !IO.KeyCtrl && !IO.KeyShift && !IO.KeyAlt;

            for (int32 Slot = 0; Slot < NumCameraBookmarks; ++Slot)
            {
                const ImGuiKey TopKey = (ImGuiKey)((int)ImGuiKey_1 + Slot);
                const ImGuiKey PadKey = (ImGuiKey)((int)ImGuiKey_Keypad1 + Slot);
                const bool bPressed = ImGui::IsKeyPressed(TopKey, false) || ImGui::IsKeyPressed(PadKey, false);
                if (!bPressed)
                {
                    continue;
                }

                if (IO.KeyCtrl && !IO.KeyShift && !IO.KeyAlt)
                {
                    SaveCameraBookmark(Slot);
                }
                else if (bPlain)
                {
                    RecallCameraBookmark(Slot);
                }
            }
        }
    }

    void FWorldEditorTool::ToggleGameViewMode()
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

    void FWorldEditorTool::RegisterEditorActions()
    {
        auto Hovered      = [this]() { return bViewportHovered; };
        auto EditorWorld  = [this]() { return World && World->GetWorldType() == EWorldType::Editor; };

        RegisterAction({"Translate Mode", "Gizmo", "Switch the gizmo to translate (move) mode",
            FInputChord{ImGuiKey_W}, [this]{ GuizmoOp = ImGuizmo::TRANSLATE; }, Hovered});

        RegisterAction({"Rotate Mode", "Gizmo", "Switch the gizmo to rotate mode",
            FInputChord{ImGuiKey_E}, [this]{ GuizmoOp = ImGuizmo::ROTATE; }, Hovered});

        RegisterAction({"Scale Mode", "Gizmo", "Switch the gizmo to scale mode",
            FInputChord{ImGuiKey_R}, [this]{ GuizmoOp = ImGuizmo::SCALE; }, Hovered});

        RegisterAction({"Toggle Local/World", "Gizmo", "Switch the gizmo between world-space and entity-local space",
            FInputChord{ImGuiKey_X}, [this]{ ToggleGuizmoMode(); }, Hovered});

        RegisterAction({"Focus Selection", "View", "Frame the camera on the last-selected entity",
            FInputChord{ImGuiKey_F}, [this]{ FocusViewportToEntity(GetLastSelectedEntity()); }});

        RegisterAction({"Toggle Game View", "View", "Hide editor overlays so the viewport shows what a runtime camera would",
            FInputChord{ImGuiKey_G}, [this]{ ToggleGameViewMode(); }, Hovered});

        RegisterAction({"Frame All", "View", "Frame the camera on every entity in the world",
            FInputChord{ImGuiKey_Home}, [this]{ FrameAllEntities(); }, Hovered});

        RegisterAction({"Group Selected", "Selection", "Wrap the selection under a new parent entity",
            FInputChord{ImGuiKey_G, /*Ctrl*/true}, [this]{ GroupSelectedEntities(); }, Hovered});

        RegisterAction({"Drop to Floor", "Selection", "Project the selection straight down onto the nearest mesh",
            FInputChord{ImGuiKey_End}, [this]{ DropSelectionToFloor(); }, Hovered});

        RegisterAction({"Copy Transform", "Selection", "Copy the last-selected entity's transform to the clipboard",
            FInputChord{ImGuiKey_C, true, true}, [this]{ CopyTransformFromLastSelected(); }, Hovered});

        RegisterAction({"Paste Transform", "Selection", "Apply the previously-copied transform to every selected entity",
            FInputChord{ImGuiKey_V, true, true}, [this]{ PasteTransformToSelection(); }, Hovered});

        RegisterAction({"Undo", "History", "Revert the last transacted edit",
            FInputChord{ImGuiKey_Z, true}, [this]{ Undo(); }, EditorWorld});

        RegisterAction({"Redo", "History", "Re-apply the last undone edit",
            FInputChord{ImGuiKey_Y, true}, [this]{ Redo(); }, EditorWorld});

        RegisterAction({"Save World", "File", "Save the current world",
            FInputChord{ImGuiKey_S, true}, [this]{ OnSave(); }});

        // Advisory entries: inline-handled shortcuts registered so the shortcuts window surfaces them.
        RegisterAction({"Copy Entities", "Selection", "Copy the selection to the entity clipboard",
            FInputChord{ImGuiKey_C, true}, nullptr});
        RegisterAction({"Duplicate Entities", "Selection", "Duplicate the selection in place",
            FInputChord{ImGuiKey_D, true}, nullptr});
        RegisterAction({"Paste Entities", "Selection", "Paste previously-copied entities",
            FInputChord{ImGuiKey_V, true}, nullptr});
        RegisterAction({"Delete Selection", "Selection", "Delete every selected entity",
            FInputChord{ImGuiKey_Delete}, nullptr});
        RegisterAction({"Recall Camera Bookmark", "Camera", "Press 1-9 to recall a saved camera position",
            FInputChord{}, nullptr});
        RegisterAction({"Save Camera Bookmark", "Camera", "Ctrl+1..9 saves the camera into the matching slot",
            FInputChord{}, nullptr});
    }

    void FWorldEditorTool::RegisterEditorModes()
    {
        // Selection must be first so ActiveModeIndex=0 matches the pre-existing default.
        EditorModes.clear();
        EditorModes.push_back(MakeUnique<FSelectionEditorMode>());
        EditorModes.push_back(MakeUnique<FTerrainEditMode>());

        // Modes call back into the host for editor services (e.g. undo transactions).
        for (TUniquePtr<IWorldEditorMode>& Mode : EditorModes)
        {
            Mode->SetContext(this);
        }

        ActiveModeIndex = 0;
        if (IWorldEditorMode* Active = GetActiveMode())
        {
            Active->OnEnter(World);
        }
    }

    IWorldEditorMode* FWorldEditorTool::GetActiveMode() const
    {
        if (EditorModes.empty()) return nullptr;
        const int32 Idx = Math::Clamp(ActiveModeIndex, 0, (int32)EditorModes.size() - 1);
        return EditorModes[Idx].get();
    }

    void FWorldEditorTool::SetActiveMode(int32 NewIndex)
    {
        if (EditorModes.empty()) return;
        NewIndex = Math::Clamp(NewIndex, 0, (int32)EditorModes.size() - 1);
        if (NewIndex == ActiveModeIndex) return;

        // Drop half-drag gizmo state before yielding: bImGuizmoUsedOnce sticks true otherwise and blocks clicks after switching back.
        if (bImGuizmoUsedOnce)
        {
            EndTransaction("Transform");
            bImGuizmoUsedOnce = false;
        }
        bVertexSnapAnchorValid = false;
        bVertexSnapApplied     = false;
        SelectionBox.bActive   = false;

        if (IWorldEditorMode* Old = EditorModes[ActiveModeIndex].get())
        {
            Old->OnExit(World);
        }
        ActiveModeIndex = NewIndex;
        if (IWorldEditorMode* New = EditorModes[ActiveModeIndex].get())
        {
            New->OnEnter(World);
        }
    }

    void FWorldEditorTool::EndFrame()
    {
        using namespace entt::literals;
        
        if (bShowComponentVisualizers)
        {
            CComponentVisualizerRegistry& ComponentVisualizerRegistry = CComponentVisualizerRegistry::Get();

            // Iterate the view (not SelectedEntities) so entt::exclude<SDisabledTag> applies.
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
        // @TODO MarkTreeDirty here is too expensive; outliner is updated incrementally.
    }

    const char* FWorldEditorTool::GetTitlebarIcon() const
    {
        return LE_ICON_EARTH;
    }

    void FWorldEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
        FEditorTool::DrawToolMenu(UpdateContext);
    }

    void FWorldEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Selection",
            "Click an entity in the viewport or outliner to select. Ctrl-click adds, Shift-click range-selects. "
            "Marquee-drag in the viewport for area selection. Esc clears.");
        DrawHelpTextRow("Gizmo",
            "W/E/R = Translate/Rotate/Scale. Spacebar cycles. X toggles World/Local space. "
            "Hold Ctrl during a translate drag for vertex-snap to nearest unselected mesh vertex.");
        DrawHelpTextRow("Snap",
            "Snap settings (translate/rotate/scale step) live under the snap popup in the viewport toolbar.");
        DrawHelpTextRow("Camera",
            "Right-click + WASD to fly. Mouse wheel adjusts speed. F frames the selection. "
            "Ctrl+1..9 saves a bookmark to that slot; 1..9 recalls it.");
        DrawHelpTextRow("Game View (G)",
            "Hides grid, billboards, AABBs, and gizmos so the viewport shows only what a runtime camera would. "
            "Restores your prior toggles on exit.");
        DrawHelpTextRow("Simulate / Play",
            "Simulate runs physics + scripts in-place; Play (PIE) duplicates the world and switches to it. "
            "Stop returns to the original editor world.");
        DrawHelpTextRow("Undo / Redo",
            "Ctrl+Z / Ctrl+Y. Each action that mutates the registry (transform edit, component add/remove, "
            "entity creation) is captured as a transaction.");
        DrawHelpTextRow("Drop to Floor",
            "Casts a ray downward from each selected entity's pivot using their CPU AABBs. "
            "No physics scene is required.");
        DrawHelpTextRow("Prefabs",
            "Right-click a selection > Create Prefab to author one; drag the asset back into the viewport "
            "or outliner to instantiate.");
        DrawHelpTextRow("Lua / Scripts",
            "Attach an ScriptComponent and point it at a .luau asset. Open Tools > Debug > Scripts Info "
            "for a live API reference.");
    }

    void FWorldEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);

        // 75% viewport / 25% inspector column.
        ImGuiID dockLeft = 0, dockRight = 0;
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.25f, &dockRight, &dockLeft);

        // Right column: scene graph on top, details/settings strip below.
        // Note: SplitNode args are (parent, dir, ratio, out-at-dir, out-opposite); easy to swap by accident.
        ImGuiID dockRightBottom = 0, dockRightTop = 0;
        ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.35f, &dockRightBottom, &dockRightTop);

        // Bottom strip: Details / World Settings side by side.
        ImGuiID dockRightBottomLeft = 0, dockRightBottomRight = 0;
        ImGui::DockBuilderSplitNode(dockRightBottom, ImGuiDir_Right, 0.5f, &dockRightBottomRight, &dockRightBottomLeft);

        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(),    dockLeft);
        ImGui::DockBuilderDockWindow(GetToolWindowName(SceneGraphName).c_str(),        dockRightTop);
        ImGui::DockBuilderDockWindow(GetToolWindowName("Details").c_str(),             dockRightBottomLeft);
        ImGui::DockBuilderDockWindow(GetToolWindowName(WorldSettingsName).c_str(),     dockRightBottomRight);
    }

    void FWorldEditorTool::DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize)
    {
        // Game-focus indicator: a subtle outline + hint so it's obvious input is
        // routed to the game (not the editor), and how to hand it back.
        if (bGamePreviewRunning && InputFocus == EInputFocus::Game)
        {
            ImDrawList* DL = ImGui::GetWindowDrawList();

            // Back out ItemSpacing to land the outline exactly on the image rect.
            const ImVec2 Spacing = ImGui::GetStyle().ItemSpacing;
            const ImVec2 Cursor  = ImGui::GetCursorScreenPos();
            const ImVec2 Min(Cursor.x - Spacing.x, Cursor.y - Spacing.y);
            const ImVec2 Max(Min.x + ViewportSize.x, Min.y + ViewportSize.y);
            const ImU32  Accent = IM_COL32(255, 176, 64, 200);

            // Drawn 1px inside the edge so the full 2px stroke stays within the image.
            DL->AddRect(ImVec2(Min.x + 1.0f, Min.y + 1.0f), ImVec2(Max.x - 1.0f, Max.y - 1.0f),
                Accent, 0.0f, 0, 2.0f);

            // Faint, translucent hint in the top-right (clear of the toolbar at top-left).
            const char*  Hint     = "Shift+F1: Editor focus";
            const ImVec2 TextSize = ImGui::CalcTextSize(Hint);
            const float  Pad = 6.0f, Margin = 8.0f;
            const ImVec2 BgMin(Max.x - TextSize.x - Pad * 2.0f - Margin, Min.y + Margin);
            const ImVec2 BgMax(BgMin.x + TextSize.x + Pad * 2.0f, BgMin.y + TextSize.y + Pad * 1.5f);
            DL->AddRectFilled(BgMin, BgMax, IM_COL32(0, 0, 0, 70), 4.0f);
            DL->AddText(ImVec2(BgMin.x + Pad, BgMin.y + Pad * 0.75f), IM_COL32(235, 235, 235, 120), Hint);
        }

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

        FMatrix4 ViewMatrix = CameraComponent.GetViewMatrix();
        FMatrix4 ProjectionMatrix = CameraComponent.GetProjectionMatrix();
        // Camera projection bakes Vulkan +Y-down NDC; ImGuizmo expects GL convention.
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

        if (IWorldEditorMode* ActiveMode = GetActiveMode())
        {
            ActiveMode->Tick(World, CameraComponent, bViewportHovered, ViewportOrigin, ViewportSize);
            ActiveMode->DrawOverlay(World, ViewportOrigin, ViewportSize, CameraComponent);
        }
        NavMeshEditMode.DrawOverlay(World);

        // Modes that own the viewport suppress selection, marquee, and gizmo input.
        const bool bModeOwnsInput = GetActiveMode() && GetActiveMode()->ConsumesViewportInput();

        auto SelectionView = World->GetEntityRegistry().view<FSelectedInEditorComponent, STransformComponent>();

        const entt::entity PivotEntityForGizmo = GetLastSelectedEntity();
        const bool bGizmoTargetValid = SelectionView.size_hint() && World->GetEntityRegistry().valid(PivotEntityForGizmo);

        // If selection/pivot vanished mid-drag (entity destroyed, selection cleared by undo, etc.),
        // ImGuizmo never sees the release. End the transaction and reset so clicks are not blocked.
        if (!bGizmoTargetValid && bImGuizmoUsedOnce)
        {
            EndTransaction("Transform");
            bImGuizmoUsedOnce = false;
            bVertexSnapAnchorValid = false;
            bVertexSnapApplied = false;
        }

        if (bGizmoTargetValid && !bModeOwnsInput)
        {
            {
                entt::entity PivotEntity = PivotEntityForGizmo;
                STransformComponent& PivotTransformComponent = World->GetEntityRegistry().get<STransformComponent>(PivotEntity);

                // Padded AABB so the gizmo stays visible when the pivot is just outside the frustum but handles aren't.
                const FVector3 PivotWorld = PivotTransformComponent.GetWorldLocation();
                const FAABB PivotBounds(PivotWorld - FVector3(1.0f), PivotWorld + FVector3(1.0f));
                const bool bPivotVisible = CameraComponent.GetViewVolume().GetFrustum().IsInside(PivotBounds);

                // Mid-drag stays drawn so ImGuizmo's release fires; otherwise bImGuizmoUsedOnce sticks and IsOver() blocks clicks.
                if (bPivotVisible || bImGuizmoUsedOnce)
                {
                    FMatrix4 EntityMatrix = PivotTransformComponent.GetWorldMatrix();

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

                    FMatrix4 PreManipulateMatrix = EntityMatrix;
                    
                    const bool bCtrlHeld = ImGui::GetIO().KeyCtrl;
                    const bool bVertexSnapArmed = bCtrlHeld
                                               && GuizmoOp == ImGuizmo::TRANSLATE
                                               && GuizmoMode == ImGuizmo::WORLD
                                               && !World->IsGameWorld();
                    const FMatrix4 SnapViewProj = ProjectionMatrix * ViewMatrix;
                    FVector3 PreviewAnchorLocal(0.0f);
                    FVector3 PreviewAnchorWorld(0.0f);
                    bool bPreviewAnchorValid = false;
                    if (bVertexSnapArmed)
                    {
                        const SStaticMeshComponent* PivotMC = World->GetEntityRegistry().try_get<SStaticMeshComponent>(PivotEntity);
                        if (PivotMC && PivotMC->StaticMesh)
                        {
                            const ImVec2 MP = ImGui::GetMousePos();
                            const ImVec2 MouseInViewport(MP.x - ViewportOrigin.x, MP.y - ViewportOrigin.y);
                            bPreviewAnchorValid = FindClosestVertexToScreenPoint(
                                *PivotMC->StaticMesh.Get(), PreManipulateMatrix, SnapViewProj,
                                ViewportSize, MouseInViewport, FLT_MAX,
                                PreviewAnchorLocal, PreviewAnchorWorld);
                        }
                    }

                    FMatrix4 GizmoDeltaMatrix(1.0f);
                    ImGuizmo::Manipulate(Math::ValuePtr(ViewMatrix), Math::ValuePtr(ProjectionMatrix),
                        GuizmoOp, GuizmoMode, Math::ValuePtr(EntityMatrix), Math::ValuePtr(GizmoDeltaMatrix), SnapValues);

                    if (ImGuizmo::IsUsing())
                    {
                        if (!bImGuizmoUsedOnce)
                        {
                            BeginTransaction();
                            bImGuizmoUsedOnce = true;
                            // Click landed on the gizmo, not empty space — kill the marquee armed by IsMouseClicked.
                            SelectionBox.bActive = false;
                        }
                        
                        const FMatrix4& DeltaMatrix = GizmoDeltaMatrix;

                        FVector3 DeltaTranslation, DeltaScale, DeltaSkew;
                        FQuat DeltaRotation;
                        FVector4 DeltaPerspective;
                        Math::Decompose(DeltaMatrix, DeltaScale, DeltaRotation, DeltaTranslation, DeltaSkew, DeltaPerspective);

                        // Override DeltaTranslation to align the anchor vertex to the closest non-selected vertex.
                        bVertexSnapApplied = false;
                        if (bVertexSnapArmed)
                        {
                            // Lock in the preview anchor on first armed frame of the drag.
                            if (!bVertexSnapAnchorValid && bPreviewAnchorValid)
                            {
                                VertexSnapAnchorLocal  = PreviewAnchorLocal;
                                bVertexSnapAnchorValid = true;
                            }

                            if (bVertexSnapAnchorValid)
                            {
                                FEntityRegistry& Registry = World->GetEntityRegistry();
                                const FVector3 AnchorPreWorld = FVector3(PreManipulateMatrix * FVector4(VertexSnapAnchorLocal, 1.0f));
                                const FVector3 AnchorCandidateWorld = AnchorPreWorld + DeltaTranslation;

                                ImVec2 AnchorScreen;
                                if (ProjectPointToScreen(AnchorCandidateWorld, SnapViewProj, ViewportSize, AnchorScreen))
                                {
                                    float BestDistSq = VertexSnapPixelRadius * VertexSnapPixelRadius;
                                    FVector3 BestTargetWorld(0.0f);
                                    bool bFoundTarget = false;

                                    Registry.view<SStaticMeshComponent, STransformComponent>().each(
                                        [&](entt::entity Entity, SStaticMeshComponent& MeshComp, STransformComponent& Xform)
                                    {
                                        if (Entity == EditorEntity || !MeshComp.StaticMesh)
                                        {
                                            return;
                                        }
                                        if (Registry.all_of<FSelectedInEditorComponent>(Entity))
                                        {
                                            return;
                                        }

                                        FVector3 LP, WP;
                                        if (!FindClosestVertexToScreenPoint(*MeshComp.StaticMesh.Get(),
                                                                            Xform.GetWorldMatrix(), SnapViewProj,
                                                                            ViewportSize, AnchorScreen,
                                                                            VertexSnapPixelRadius, LP, WP))
                                        {
                                            return;
                                        }
                                        ImVec2 HitScreen;
                                        if (!ProjectPointToScreen(WP, SnapViewProj, ViewportSize, HitScreen)) return;
                                        const float dx = HitScreen.x - AnchorScreen.x;
                                        const float dy = HitScreen.y - AnchorScreen.y;
                                        const float DistSq = dx * dx + dy * dy;
                                        if (DistSq < BestDistSq)
                                        {
                                            BestDistSq      = DistSq;
                                            BestTargetWorld = WP;
                                            bFoundTarget    = true;
                                        }
                                    });

                                    if (bFoundTarget)
                                    {
                                        DeltaTranslation       = BestTargetWorld - AnchorPreWorld;
                                        bVertexSnapApplied     = true;
                                        VertexSnapTargetWorld  = BestTargetWorld;
                                        VertexSnapAnchorWorld  = BestTargetWorld;
                                    }
                                    else
                                    {
                                        VertexSnapAnchorWorld = AnchorCandidateWorld;
                                    }
                                }
                            }
                        }
                        else
                        {
                            bVertexSnapAnchorValid = false;
                        }

                        if (GuizmoMode == ImGuizmo::LOCAL)
                        {
                            FMatrix4 LocalDeltaMatrix = Math::Inverse(PreManipulateMatrix) * EntityMatrix;

                            FVector3 LocalDeltaTrans, LocalDeltaScaleVec, LocalDeltaSkew;
                            FQuat LocalDeltaRot;
                            FVector4 LocalDeltaPersp;
                            const bool bLocalDeltaValid = Math::Decompose(
                                LocalDeltaMatrix, LocalDeltaScaleVec, LocalDeltaRot, LocalDeltaTrans, LocalDeltaSkew, LocalDeltaPersp);

                            SelectionView.each([&](entt::entity, STransformComponent& Transform)
                            {
                                if (!bLocalDeltaValid)
                                {
                                    return;
                                }

                                switch (GuizmoOp)
                                {
                                    case ImGuizmo::TRANSLATE:
                                    {
                                        // Delta is in entity-local axes; rotate into parent space before adding.
                                        FVector3 ParentSpaceDelta = Transform.GetLocalRotation() * LocalDeltaTrans;
                                        Transform.SetLocalLocation(Transform.GetLocalLocation() + ParentSpaceDelta);
                                        break;
                                    }

                                    case ImGuizmo::ROTATE:
                                    {
                                        Transform.SetLocalRotation(Math::Normalize(Transform.GetLocalRotation() * LocalDeltaRot));
                                        break;
                                    }

                                    case ImGuizmo::SCALE:
                                    {
                                        Transform.SetLocalScale(SanitizeManipulationScale(Transform.GetLocalScale() * LocalDeltaScaleVec));
                                        break;
                                    }
                                }
                            });
                        }
                        else
                        {
                            FVector3 PivotPosition = PivotTransformComponent.WorldTransform.Location;

                            SelectionView.each([&](entt::entity Entity, STransformComponent& Transform)
                            {
                                FMatrix4 DesiredWorldMatrix;

                                switch (GuizmoOp)
                                {
                                    case ImGuizmo::TRANSLATE:
                                    {
                                        FMatrix4 TranslationDelta = Math::Translate(FMatrix4(1.f), DeltaTranslation);
                                        DesiredWorldMatrix = TranslationDelta * Transform.GetWorldMatrix();
                                        break;
                                    }

                                    case ImGuizmo::ROTATE:
                                    {
                                        FVector3 OffsetFromPivot = Transform.WorldTransform.Location - PivotPosition;
                                        FVector3 RotatedOffset   = DeltaRotation * OffsetFromPivot;
                                        FVector3 NewWorldPos     = PivotPosition + RotatedOffset;
                                        FQuat NewWorldRot     = DeltaRotation * Transform.GetWorldRotation();
                                        FVector3 WorldScale      = Transform.GetWorldScale();

                                        DesiredWorldMatrix = Math::Translate(FMatrix4(1.f), NewWorldPos)
                                                           * Math::ToMatrix4(NewWorldRot)
                                                           * Math::Scale(FMatrix4(1.f), WorldScale);
                                        break;
                                    }

                                    case ImGuizmo::SCALE:
                                    {
                                        const FVector3 CurrentWorldScale = Transform.GetWorldScale();
                                        FVector3 ClampedDeltaScale       = DeltaScale;
                                        constexpr float MinScale          = 0.001f;
                                        for (int Axis = 0; Axis < 3; ++Axis)
                                        {
                                            const float Target = CurrentWorldScale[Axis] * DeltaScale[Axis];
                                            if (!std::isfinite(Target) || Math::Abs(Target) < MinScale)
                                            {
                                                const float SignedMin = (Target < 0.0f) ? -MinScale : MinScale;
                                                ClampedDeltaScale[Axis] = (Math::Abs(CurrentWorldScale[Axis]) > 1e-8f)
                                                                        ? SignedMin / CurrentWorldScale[Axis]
                                                                        : 1.0f;
                                            }
                                        }

                                        FVector3 OffsetFromPivot = Transform.WorldTransform.Location - PivotPosition;
                                        FVector3 ScaledOffset    = OffsetFromPivot * ClampedDeltaScale;
                                        FVector3 NewWorldPos     = PivotPosition + ScaledOffset;
                                        FQuat WorldRot        = Transform.GetWorldRotation();
                                        FVector3 NewWorldScale   = CurrentWorldScale * ClampedDeltaScale;

                                        DesiredWorldMatrix = Math::Translate(FMatrix4(1.f), NewWorldPos)
                                                           * Math::ToMatrix4(WorldRot)
                                                           * Math::Scale(FMatrix4(1.f), NewWorldScale);
                                        break;
                                    }
                                }

                                FRelationshipComponent* Rel = World->GetEntityRegistry().try_get<FRelationshipComponent>(Entity);
                                if (Rel && Rel->Parent != entt::null)
                                {
                                    STransformComponent& ParentTransform = World->GetEntityRegistry().get<STransformComponent>(Rel->Parent);
                                    FMatrix4 LocalMatrix = Math::Inverse(ParentTransform.GetWorldMatrix()) * DesiredWorldMatrix;

                                    FVector3 LocalTranslation, LocalScale, LocalSkew;
                                    FQuat LocalRotation;
                                    FVector4 LocalPerspective;

                                    if (!Math::Decompose(LocalMatrix, LocalScale, LocalRotation, LocalTranslation, LocalSkew, LocalPerspective))
                                    {
                                        return;
                                    }

                                    Transform.SetLocalLocation(LocalTranslation);
                                    Transform.SetLocalRotation(LocalRotation);
                                    Transform.SetLocalScale(SanitizeManipulationScale(LocalScale));
                                }
                                else
                                {
                                    FVector3 WorldTranslation, WorldScale, WorldSkew;
                                    FQuat WorldRotation;
                                    FVector4 WorldPerspective;
                                    if (!Math::Decompose(DesiredWorldMatrix, WorldScale, WorldRotation, WorldTranslation, WorldSkew, WorldPerspective))
                                    {
                                        return;
                                    }

                                    Transform.SetLocalLocation(WorldTranslation);
                                    Transform.SetLocalRotation(WorldRotation);
                                    Transform.SetLocalScale(SanitizeManipulationScale(WorldScale));
                                }
                            });
                        }
                    }
                    else if (bImGuizmoUsedOnce)
                    {
                        EndTransaction("Transform");
                        bImGuizmoUsedOnce = false;
                        bVertexSnapAnchorValid = false;
                        bVertexSnapApplied     = false;
                    }

                    // Vertex-snap viz: hint banner + anchor marker; locked target is drawn while snapping.
                    if (bVertexSnapArmed)
                    {
                        ImDrawList* DL = ImGui::GetCurrentWindow()->DrawList;

                        const ImU32 ArmedCol = IM_COL32(120, 200, 255, 255);
                        const ImU32 SnapCol  = IM_COL32(255, 220,   0, 255);

                        const ImVec2 BannerPos(ViewportOrigin.x + 8.0f, ViewportOrigin.y + 8.0f);
                        const char* Label = bVertexSnapApplied ? "VERTEX SNAP" : "VERTEX SNAP (armed)";
                        const ImVec2 TextSize = ImGui::CalcTextSize(Label);
                        DL->AddRectFilled(BannerPos,
                            ImVec2(BannerPos.x + TextSize.x + 12.0f, BannerPos.y + TextSize.y + 6.0f),
                            IM_COL32(0, 0, 0, 160), 3.0f);
                        DL->AddText(ImVec2(BannerPos.x + 6.0f, BannerPos.y + 3.0f),
                            bVertexSnapApplied ? SnapCol : ArmedCol, Label);

                        // Live anchor marker.
                        FVector3 AnchorWorld(0.0f);
                        bool bHaveAnchor = false;
                        if (bVertexSnapAnchorValid)
                        {
                            AnchorWorld = bVertexSnapApplied
                                ? VertexSnapAnchorWorld
                                : FVector3(PivotTransformComponent.GetWorldMatrix() * FVector4(VertexSnapAnchorLocal, 1.0f));
                            bHaveAnchor = true;
                        }
                        else if (bPreviewAnchorValid)
                        {
                            AnchorWorld = PreviewAnchorWorld;
                            bHaveAnchor = true;
                        }

                        if (bHaveAnchor)
                        {
                            ImVec2 S;
                            if (ProjectPointToScreen(AnchorWorld, SnapViewProj, ViewportSize, S))
                            {
                                const ImVec2 P(S.x + ViewportOrigin.x, S.y + ViewportOrigin.y);
                                const ImU32 C = bVertexSnapApplied ? SnapCol : ArmedCol;
                                DL->AddRectFilled(ImVec2(P.x - 3, P.y - 3), ImVec2(P.x + 3, P.y + 3), C);
                                DL->AddRect(ImVec2(P.x - 7, P.y - 7), ImVec2(P.x + 7, P.y + 7), C, 0.0f, 0, 2.0f);
                            }
                        }

                        // Snap-target marker.
                        if (bVertexSnapApplied)
                        {
                            ImVec2 T;
                            if (ProjectPointToScreen(VertexSnapTargetWorld, SnapViewProj, ViewportSize, T))
                            {
                                const ImVec2 P(T.x + ViewportOrigin.x, T.y + ViewportOrigin.y);
                                DL->AddCircleFilled(P, 5.0f, SnapCol);
                                DL->AddCircle(P, 10.0f, SnapCol, 0, 2.0f);
                            }
                        }
                    }
                }
            }
        }

        // Yield to the world's UI: a click over an interactive Rml element must not
        // also fall through to entity picking / marquee behind it.
        if (!bModeOwnsInput && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && !RmlUi::WorldUIWantsMouse(World))
        {
            uint32 PickerWidth = World->GetRenderer()->GetRenderTarget()->GetExtent().x;
            uint32 PickerHeight = World->GetRenderer()->GetRenderTarget()->GetExtent().y;
            
            ImVec2 viewportScreenPos = ImGui::GetWindowPos();
            ImVec2 mousePos = ImGui::GetMousePos();

            ImVec2 MousePosInViewport;
            MousePosInViewport.x = mousePos.x - viewportScreenPos.x;
            MousePosInViewport.y = mousePos.y - viewportScreenPos.y;

            MousePosInViewport.x = Math::Clamp(MousePosInViewport.x, 0.0f, ViewportSize.x - 1.0f);
            MousePosInViewport.y = Math::Clamp(MousePosInViewport.y, 0.0f, ViewportSize.y - 1.0f);

            float ScaleX = static_cast<float>(PickerWidth) / ViewportSize.x;
            float ScaleY = static_cast<float>(PickerHeight) / ViewportSize.y;

            uint32 TexX = static_cast<uint32>(MousePosInViewport.x * ScaleX);
            uint32 TexY = static_cast<uint32>(MousePosInViewport.y * ScaleY);

            // Publish the cursor so the renderer copies just the window around it.
            World->GetRenderer()->SetPickerCursor(TexX, TexY, true);

            bool bOverImGuizmo = bImGuizmoUsedOnce ? ImGuizmo::IsOver() : false;

            // Eyedropper: a details-panel entity-reference picker is waiting for a click.
            // Intercept it here so the click assigns the reference instead of selecting.
            if (IsEntityPickRequested())
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip(LE_ICON_EYEDROPPER " Click an entity to assign (Esc to cancel)");

                if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    CancelEntityPick();
                }
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    entt::entity Hit = World->GetRenderer()->GetEntityAtPixel(TexX, TexY);
                    Hit = ResolveSelectionRootForViewportPick(World->GetEntityRegistry(), Hit);
                    if (Hit != entt::null)
                    {
                        FulfillEntityPick(static_cast<uint32>(entt::to_integral(Hit)));
                    }
                }
            }
            else if (!bOverImGuizmo)
            {
                ImVec2 LeftDragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                float LeftDragDistance = sqrtf(LeftDragDelta.x * LeftDragDelta.x + LeftDragDelta.y * LeftDragDelta.y);
                bool bLeftDragging = LeftDragDistance >= 15.0f;
    
                ImVec2 RightDragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                float RightDragDistance = sqrtf(RightDragDelta.x * RightDragDelta.x + RightDragDelta.y * RightDragDelta.y);
                // Right release was a tap, not a camera-look gesture: open context menu.
                bool bRightWasShortClick = RightDragDistance < 15.0f;

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    SelectionBox.bActive = true;
                    SelectionBox.Start = MousePosInViewport;
                    SelectionBox.Current = SelectionBox.Start;
                }

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                {
                    if (bRightWasShortClick)
                    {
                        entt::entity EntityHandle = World->GetRenderer()->GetEntityAtPixel(TexX, TexY);
                        EntityHandle = ResolveSelectionRootForViewportPick(World->GetEntityRegistry(), EntityHandle);

                        // On a picker miss keep existing selection so the menu still has something to act on.
                        if (EntityHandle != entt::null && !IsEntitySelected(EntityHandle))
                        {
                            SetSingleSelectedEntity(EntityHandle);
                        }

                        const entt::entity MenuTarget = (EntityHandle != entt::null)
                            ? EntityHandle
                            : GetLastSelectedEntity();

                        if (World->GetEntityRegistry().valid(MenuTarget))
                        {
                            ImGui::OpenPopup("EntityContextMenu");
                        }
                    }
                }
            
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && SelectionBox.bActive)
                {
                    SelectionBox.Current = MousePosInViewport;
                }
                
                if (SelectionBox.bActive && bLeftDragging)
                {
                    ImDrawList* DrawList = ImGui::GetWindowDrawList();
                    const ImVec2 Origin = ViewportOrigin;
                    const ImVec2 ScreenStart = ImVec2(Origin.x + SelectionBox.Start.x,   Origin.y + SelectionBox.Start.y);
                    const ImVec2 ScreenEnd   = ImVec2(Origin.x + SelectionBox.Current.x, Origin.y + SelectionBox.Current.y);
                    DrawList->AddRectFilled(ScreenStart, ScreenEnd, IM_COL32(100, 150, 255, 50));
                    DrawList->AddRect(ScreenStart, ScreenEnd, IM_COL32(100, 150, 255, 255), 0.0f, 0, 2.0f);
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && SelectionBox.bActive)
                {
                    ImVec2 Start = SelectionBox.Start;
                    ImVec2 End = SelectionBox.Current;
                    
                    if (!bLeftDragging)
                    {
                        entt::entity EntityHandle = World->GetRenderer()->GetEntityAtPixel(TexX, TexY);
                        EntityHandle = ResolveSelectionRootForViewportPick(World->GetEntityRegistry(), EntityHandle);

                        // Ctrl+click toggles picked entity in selection; plain click replaces.
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
                        const ImVec2 RectMin(Math::Min(Start.x, End.x), Math::Min(Start.y, End.y));
                        const ImVec2 RectMax(Math::Max(Start.x, End.x), Math::Max(Start.y, End.y));
                        const FMatrix4 ViewProj = ProjectionMatrix * ViewMatrix;

                        entt::registry& Registry = World->GetEntityRegistry();

                        THashSet<entt::entity> Hits;
                        Registry.view<STransformComponent>().each([&](entt::entity Entity, STransformComponent& Transform)
                        {
                            if (Entity == EditorEntity)
                            {
                                return;
                            }

                            ImVec2 ProjMin, ProjMax;

                            if (const SStaticMeshComponent* Mesh = Registry.try_get<SStaticMeshComponent>(Entity))
                            {
                                FAABB WorldAABB = Mesh->GetAABB().ToWorld(Transform.GetWorldMatrix());
                                if (!ProjectAABBToScreenRect(WorldAABB, ViewProj, ViewportSize, ProjMin, ProjMax))
                                {
                                    return;
                                }
                            }
                            else
                            {
                                const FVector3 P = Transform.GetWorldLocation();
                                FVector4 Clip = ViewProj * FVector4(P, 1.0f);
                                if (Clip.w <= 1e-4f)
                                {
                                    return;
                                }
                                const float Px = (Clip.x / Clip.w * 0.5f + 0.5f) * ViewportSize.x;
                                const float Py = (1.0f - (Clip.y / Clip.w * 0.5f + 0.5f)) * ViewportSize.y;
                                ProjMin = ImVec2(Px - 4.0f, Py - 4.0f);
                                ProjMax = ImVec2(Px + 4.0f, Py + 4.0f);
                            }

                            const bool bOverlap = ProjMax.x >= RectMin.x && ProjMin.x <= RectMax.x
                                               && ProjMax.y >= RectMin.y && ProjMin.y <= RectMax.y;
                            if (bOverlap)
                            {
                                Hits.insert(ResolveSelectionRootForViewportPick(Registry, Entity));
                            }
                        });

                        const bool bShift = ImGui::GetIO().KeyShift;
                        const bool bCtrl  = ImGui::GetIO().KeyCtrl;
                        if (!bShift && !bCtrl)
                        {
                            ClearSelectedEntities();
                        }

                        for (entt::entity Hit : Hits)
                        {
                            if (bCtrl)
                            {
                                ToggleSelectedEntity(Hit);
                            }
                            else
                            {
                                AddSelectedEntity(Hit, false);
                            }
                        }
                    } 
    
                    SelectionBox.bActive = false;
                }
            }
        }
        else
        {
            // Not a pick target this frame (cursor off the viewport or a mode owns input):
            // tell the renderer to skip the picker readback.
            World->GetRenderer()->SetPickerCursor(0, 0, false);
        }

        if (ImGui::BeginPopup("EntityContextMenu"))
        {
            const entt::entity LastSelectedEntity = GetLastSelectedEntity();

            if (World->GetEntityRegistry().valid(LastSelectedEntity))
            {
                entt::registry& Registry = World->GetEntityRegistry();
                const bool bLastSelectedLocked = IsLockedPrefabChild(Registry, LastSelectedEntity);
                const size_t NumSelected = SelectedEntities.size();
                const bool bMultiSelected = NumSelected > 1;

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));

                // Header: name + ID for the focal entity, with a "+N more" badge when there's a wider selection.
                {
                    const SNameComponent* HeaderName = Registry.try_get<SNameComponent>(LastSelectedEntity);
                    FStringView HeaderText = HeaderName ? FStringView(HeaderName->Name.c_str()) : FStringView("<unnamed>");

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    ImGui::TextUnformatted(HeaderText.data(), HeaderText.data() + HeaderText.size());
                    ImGui::PopStyleColor();

                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    ImGui::Text("#%u", (uint32)LastSelectedEntity);
                    ImGui::PopStyleColor();

                    if (bMultiSelected)
                    {
                        ImGui::SameLine();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                        ImGui::Text("+%zu more", NumSelected - 1);
                        ImGui::PopStyleColor();
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Edit: clipboard + duplicate.
                if (!bLastSelectedLocked)
                {
                    if (ImGui::MenuItem(LE_ICON_CONTENT_DUPLICATE " Duplicate", "Ctrl+D"))
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
                }

                if (ImGui::MenuItem(LE_ICON_CONTENT_COPY " Copy", "Ctrl+C"))
                {
                    ClearCopies();
                    AddEntityToCopies(LastSelectedEntity);
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::MenuItem("Copy Entity ID"))
                {
                    ImGui::SetClipboardText(std::to_string(entt::to_integral(LastSelectedEntity)).c_str());
                    ImGui::CloseCurrentPopup();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Components.
                if (ImGui::MenuItem("Add Component..."))
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

                // Hierarchy.
                if (!bLastSelectedLocked && bMultiSelected)
                {
                    if (ImGui::MenuItem(LE_ICON_FOLDER " Group Selection", "Ctrl+G"))
                    {
                        GroupSelectedEntities();
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!bLastSelectedLocked && ECS::Utils::IsChild(Registry, LastSelectedEntity))
                {
                    if (ImGui::MenuItem("Unparent"))
                    {
                        BeginTransaction();
                        ECS::Utils::RemoveFromParent(Registry, LastSelectedEntity);
                        EndTransaction("Unparent");
                        ReparentEntityInOutliner(LastSelectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!bLastSelectedLocked && ECS::Utils::IsParent(Registry, LastSelectedEntity))
                {
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
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!bLastSelectedLocked)
                {
                    const bool bIsSelectionRoot = Registry.all_of<FSelectionRoot>(LastSelectedEntity);
                    if (ImGui::MenuItem(bIsSelectionRoot ? "Unmark Selection Root" : "Mark as Selection Root"))
                    {
                        if (bIsSelectionRoot)
                        {
                            Registry.remove<FSelectionRoot>(LastSelectedEntity);
                        }
                        else
                        {
                            Registry.emplace<FSelectionRoot>(LastSelectedEntity);
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGuiX::TextTooltip("{}", "Viewport clicks on any descendant will resolve up to this entity.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Prefab.
                if (ImGui::MenuItem(LE_ICON_PACKAGE_VARIANT " Create Prefab from Selected..."))
                {
                    PushCreatePrefabFromSelectionModal();
                    ImGui::CloseCurrentPopup();
                }
                ImGuiX::TextTooltip("{}", "Save the selection as a reusable prefab asset. Children of selected entities are included automatically.");

                if (const SPrefabInstanceComponent* Instance = Registry.try_get<SPrefabInstanceComponent>(LastSelectedEntity);
                    Instance != nullptr && Instance->bIsRoot)
                {
                    if (ImGui::MenuItem(LE_ICON_LINK_VARIANT_OFF " Detach from Prefab"))
                    {
                        BeginTransaction();
                        if (CPrefab::DetachInstance(World, LastSelectedEntity))
                        {
                            EndTransaction("Detach from Prefab");
                            OutlinerListView.MarkTreeDirty();
                        }
                        else
                        {
                            PendingBeforeState.clear();
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGuiX::TextTooltip("{}", "Unlink this instance from its source prefab; the entities become plain and stop syncing.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Destructive at bottom.
                if (!bLastSelectedLocked)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    if (ImGui::MenuItem(LE_ICON_TRASH_CAN " Delete Entity", "Del"))
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

                ImGui::PopStyleVar(3);
            }

            ImGui::EndPopup();
        }

        DrawCursorWorldPositionOverlay(ViewportOrigin, ViewportSize, CameraComponent);
        DrawEntityDebugOverlay(ViewportOrigin, ViewportSize, CameraComponent);
        DrawOffscreenSelectionIndicators(ViewportOrigin, ViewportSize, CameraComponent);

        // Selected-camera preview: a small picture-in-picture of what that camera sees,
        // pinned to the viewport's bottom-right. The render scene shades it into a capture RT
        // (driven in UpdateCameraPreview); here we just composite it.
        if (bCameraPreviewActive && CameraPreviewHandle >= 0)
        {
            if (FRHIImage* PreviewRT = World->GetRenderer()->GetCaptureRenderTarget(CameraPreviewHandle))
            {
                const float Scale  = 0.6f;
                const float Margin = 14.0f;
                const ImVec2 Size((float)CameraPreviewWidth * Scale, (float)CameraPreviewHeight * Scale);
                const ImVec2 Max(ViewportOrigin.x + ViewportSize.x - Margin,
                                 ViewportOrigin.y + ViewportSize.y - Margin);
                const ImVec2 Min(Max.x - Size.x, Max.y - Size.y);

                ImDrawList* DL = ImGui::GetWindowDrawList();
                DL->AddRectFilled(ImVec2(Min.x - 3.0f, Min.y - 18.0f), ImVec2(Max.x + 3.0f, Max.y + 3.0f),
                    IM_COL32(0, 0, 0, 190), 4.0f);
                DL->AddText(ImVec2(Min.x + 2.0f, Min.y - 16.0f), IM_COL32(235, 235, 235, 220), "Camera Preview");
                DL->AddImage(ImGuiX::ToImTextureRef(PreviewRT), Min, Max);
                DL->AddRect(Min, Max, IM_COL32(255, 255, 255, 110), 2.0f);
            }
        }
    }

    void FWorldEditorTool::DrawViewportToolbar(const FUpdateContext& UpdateContext)
    {
        constexpr float Padding = 8.0f;
        constexpr float ItemSpacing = 6.0f;
        // While playing, the only control is Stop: shrink it and make the bar solid
        // so it reads as a small, deliberate widget instead of a distracting overlay.
        const float ButtonSize = bGamePreviewRunning ? 24.0f : 32.0f;
        constexpr float CornerRounding = 8.0f;

        ImVec2 Pos = ImGui::GetWindowPos();
        ImGui::SetNextWindowPos(Pos + ImVec2(Padding, Padding));
        ImGui::SetNextWindowBgAlpha(bGamePreviewRunning ? 1.0f : 0.85f);
    
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
            
            DrawSimulationControls(ButtonSize);
            
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

                // Mode-selector bar: mutually exclusive; switching drives OnEnter/OnExit.
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();

                for (int32 Idx = 0; Idx < (int32)EditorModes.size(); ++Idx)
                {
                    IWorldEditorMode* Mode = EditorModes[Idx].get();
                    if (!Mode) continue;
                    const bool bSelected = (Idx == ActiveModeIndex);

                    ImGui::PushID(Idx);
                    if (bSelected)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.55f, 0.25f, 1.0f));
                    }
                    if (ImGui::Button(Mode->GetDisplayName(), ImVec2(0, ButtonSize)))
                    {
                        SetActiveMode(Idx);
                    }
                    if (bSelected)
                    {
                        ImGui::PopStyleColor();
                    }
                    if (const char* Tip = Mode->GetTooltip())
                    {
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s", Tip);
                        }
                    }
                    ImGui::PopID();
                    if (Idx + 1 < (int32)EditorModes.size())
                    {
                        ImGui::SameLine();
                    }
                }

                if (IWorldEditorMode* ActiveMode = GetActiveMode())
                {
                    ActiveMode->DrawToolbar(World, ButtonSize);
                }
                NavMeshEditMode.DrawToolbar(World, ButtonSize);
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
        ToolContext->PushModal("Add Component", ImVec2(500.0f, 600.0f), [this, Entity, Filter = Move(Filter)] () -> bool
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

            const float ListHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

            if (ImGui::BeginChild("##ComponentsList", ImVec2(0, ListHeight), true))
            {
                entt::meta_type       PickedMetaType;
                CStruct*              PickedStruct = nullptr;
                CEntityComponentType* PickedRuntime = nullptr;
                if (DrawAddableComponentList(*Filter, PickedMetaType, PickedStruct, PickedRuntime))
                {
                    using namespace entt::literals;
                    if (PickedRuntime != nullptr)
                    {
                        BeginTransaction();
                        ECS::Utils::AddRuntimeComponent(World->GetEntityRegistry(), Entity, PickedRuntime);
                        EndTransaction("Add Runtime Component");
                        if (World->GetPackage() != nullptr)
                        {
                            World->GetPackage()->MarkDirty();
                        }
                        bDetailsDirty = true;
                    }
                    else
                    {
                        BeginTransaction();
                        ECS::Utils::InvokeMetaFunc(PickedMetaType, "emplace"_hs, entt::forward_as_meta(World->GetEntityRegistry()), Entity, entt::forward_as_meta(entt::meta_any{}));
                        EndTransaction("Emplace Component");
                    }

                    bComponentAdded = true;
                }
            }
            ImGui::EndChild();

            ImGui::PopStyleVar(2);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const float ButtonWidth = 120.0f;
            const float AvailWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((AvailWidth - ButtonWidth) * 0.5f);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.32f, 1.0f));

            bool bShouldClose = false;
            if (ImGui::Button("Cancel", ImVec2(ButtonWidth, 0)))
            {
                bShouldClose = true;
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);

            if (bComponentAdded && Entity == DetailsEntity)
            {
                bDetailsDirty = true;
            }

            return bComponentAdded || bShouldClose;
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
		if (!World->GetPackage())
        {
            PushSaveAsAssetModal();
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

    void FWorldEditorTool::PushSaveAsAssetModal()
    {
        ToolContext->PushModal("Save World As Asset", ImVec2(550.0f, 240.0f), [this]() -> bool
        {
            static FFixedString NameBuffer;
            static FFixedString DirBuffer;
            static FFixedString ErrorMessage;

            if (ImGui::IsWindowAppearing())
            {
                NameBuffer = "NewWorld";
                DirBuffer = "/Game";
                ErrorMessage.clear();
            }

            ImGui::TextUnformatted("This world is not saved as an asset yet.");
            ImGui::TextUnformatted("Pick a name and content folder to save it.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Folder");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##Folder", DirBuffer.data(), DirBuffer.max_size());

            ImGui::Spacing();
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(-1.0f);
            const bool bEnter = ImGui::InputText("##Name", NameBuffer.data(), NameBuffer.max_size(), ImGuiInputTextFlags_EnterReturnsTrue);

            if (!ErrorMessage.empty())
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", ErrorMessage.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            constexpr float ButtonWidth = 110.0f;
            const float AvailWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((AvailWidth - ButtonWidth * 2 - ImGui::GetStyle().ItemSpacing.x) * 0.5f);

            const bool bConfirm = ImGui::Button("Save", ImVec2(ButtonWidth, 0.0f)) || bEnter;
            ImGui::SameLine();
            const bool bCancel = ImGui::Button("Cancel", ImVec2(ButtonWidth, 0.0f));

            if (bCancel)
            {
                return true;
            }

            if (!bConfirm)
            {
                return false;
            }

            if (NameBuffer.data()[0] == '\0')
            {
                ErrorMessage = "Name cannot be empty.";
                return false;
            }

            if (DirBuffer.data()[0] == '\0')
            {
                ErrorMessage = "Folder cannot be empty.";
                return false;
            }

            FFixedString Path = Paths::Combine(FStringView(DirBuffer.c_str()), FStringView(NameBuffer.c_str()));
            Path = VFS::ResolveToVirtualPath(Path);
            if (Path.empty() || Path.front() != '/')
            {
                ErrorMessage = "Folder must resolve to a virtual path under a mounted alias (e.g. /Game).";
                return false;
            }
            CPackage::AddPackageExt(Path);
            Path = VFS::MakeUniqueFilePath(Path);

            FFixedString SafePath = SanitizeObjectName(Path);
            if (FindObject<CPackage>(SafePath) != nullptr)
            {
                ErrorMessage = "A package with that name is already loaded.";
                return false;
            }

            CPackage* NewPackage = CPackage::CreatePackage(SafePath);
            if (NewPackage == nullptr)
            {
                ErrorMessage = "Failed to create package.";
                return false;
            }

            FStringView FileName = VFS::FileName(Path, true);
            World->Rename(FName(FileName), NewPackage);
            World->SetFlag(OF_Public);

            FObjectExport& Export = NewPackage->ExportTable.emplace_back();
            Export.ObjectGUID = World->GetGUID();
            Export.ObjectName = World->GetName();
            Export.ClassName = World->GetClass()->GetName();
            Export.Offset = 0;
            Export.Size = 0;
            Export.Object = World.Get();

            if (ShouldGenerateThumbnailOnSave())
            {
                GenerateThumbnail(NewPackage);
            }

            if (CPackage::SavePackage(NewPackage, Path))
            {
                FAssetRegistry::Get().AssetCreated(World);
                ImGuiX::Notifications::NotifySuccess("Saved world: \"{0}\"", Path);
                return true;
            }

            ErrorMessage = "Failed to save package to disk.";
            return false;
        });
    }

    namespace
    {
        // Capture-time snapshot the modal needs: which entities to capture, the pivot, the suggested name,
        // and a precomputed total entity count so the modal can show the user exactly what's being captured.
        struct FCreatePrefabRequest
        {
            TVector<entt::entity> Roots;
            FVector3 Pivot;
            FFixedString DefaultName;
            uint32 TotalEntityCount;
        };

        FCreatePrefabRequest BuildCreatePrefabRequest(entt::registry& Registry, TVector<entt::entity> InitialRoots)
        {
            FCreatePrefabRequest Out;
            Out.Roots = eastl::move(InitialRoots);
            Out.Pivot = FVector3(0.0f);
            Out.TotalEntityCount = 0;

            for (entt::entity Entity : Out.Roots)
            {
                Out.Pivot += Registry.get<STransformComponent>(Entity).GetWorldLocation();
                Out.TotalEntityCount += 1;
                ECS::Utils::ForEachDescendant(Registry, Entity, [&](entt::entity)
                {
                    Out.TotalEntityCount += 1;
                });
            }
            if (!Out.Roots.empty())
            {
                Out.Pivot /= static_cast<float>(Out.Roots.size());
            }

            Out.DefaultName = "NewPrefab";
            if (Out.Roots.size() == 1)
            {
                if (const SNameComponent* NameComp = Registry.try_get<SNameComponent>(Out.Roots[0]))
                {
                    Out.DefaultName = NameComp->Name.c_str();
                }
            }
            return Out;
        }
    }

    void FWorldEditorTool::PushCreatePrefabFromSelectionModal()
    {
        if (World == nullptr || World->IsSimulating())
        {
            ImGuiX::Notifications::NotifyWarning("Cannot create a prefab while simulating.");
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();

        // Filter the selection: drop invalid handles and prefab-instance children whose hierarchy is locked.
        THashSet<entt::entity> Filtered;
        for (entt::entity Entity : SelectedEntities)
        {
            if (Registry.valid(Entity) && !IsLockedPrefabChild(Registry, Entity))
            {
                Filtered.insert(Entity);
            }
        }

        if (Filtered.empty())
        {
            ImGuiX::Notifications::NotifyWarning("Select an entity in the world before creating a prefab.");
            return;
        }

        // Reduce to top-level entities: if any ancestor is also selected, we descend through the parent.
        TVector<entt::entity> Roots;
        Roots.reserve(Filtered.size());
        for (entt::entity Entity : Filtered)
        {
            const FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity);
            entt::entity Walk = Rel ? Rel->Parent : entt::null;
            bool bAncestorInSet = false;
            while (Walk != entt::null)
            {
                if (Filtered.find(Walk) != Filtered.end())
                {
                    bAncestorInSet = true;
                    break;
                }
                const FRelationshipComponent* WalkRel = Registry.try_get<FRelationshipComponent>(Walk);
                Walk = WalkRel ? WalkRel->Parent : entt::null;
            }
            if (!bAncestorInSet)
            {
                Roots.push_back(Entity);
            }
        }

        FCreatePrefabRequest Req = BuildCreatePrefabRequest(Registry, eastl::move(Roots));

        ToolContext->PushModal("Create Prefab From Selection", ImVec2(560.0f, 290.0f),
            [this, Req = eastl::move(Req)]() -> bool
        {
            static FFixedString NameBuffer;
            static FFixedString DirBuffer;
            static FFixedString ErrorMessage;

            if (ImGui::IsWindowAppearing())
            {
                NameBuffer = Req.DefaultName;
                DirBuffer = "/Game";
                ErrorMessage.clear();
            }

            ImGui::TextUnformatted("Save the selection as a reusable prefab asset.");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Capturing %u entities total (%zu top-level + descendants).",
                Req.TotalEntityCount, Req.Roots.size());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Folder");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##Folder", DirBuffer.data(), DirBuffer.max_size());

            ImGui::Spacing();
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(-1.0f);
            const bool bEnter = ImGui::InputText("##Name", NameBuffer.data(), NameBuffer.max_size(), ImGuiInputTextFlags_EnterReturnsTrue);

            if (!ErrorMessage.empty())
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", ErrorMessage.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            constexpr float ButtonWidth = 110.0f;
            const float AvailWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((AvailWidth - ButtonWidth * 2 - ImGui::GetStyle().ItemSpacing.x) * 0.5f);

            const bool bConfirm = ImGui::Button("Create", ImVec2(ButtonWidth, 0.0f)) || bEnter;
            ImGui::SameLine();
            const bool bCancel = ImGui::Button("Cancel", ImVec2(ButtonWidth, 0.0f));

            if (bCancel) return true;
            if (!bConfirm) return false;

            if (NameBuffer.data()[0] == '\0') { ErrorMessage = "Name cannot be empty."; return false; }
            if (DirBuffer.data()[0]  == '\0') { ErrorMessage = "Folder cannot be empty."; return false; }

            entt::registry& WorkingRegistry = World->GetEntityRegistry();
            for (entt::entity Entity : Req.Roots)
            {
                if (!WorkingRegistry.valid(Entity))
                {
                    ErrorMessage = "Selection changed; cannot capture missing entities.";
                    return false;
                }
            }

            FFixedString Path = Paths::Combine(FStringView(DirBuffer.c_str()), FStringView(NameBuffer.c_str()));
            Path = VFS::ResolveToVirtualPath(Path);
            if (Path.empty() || Path.front() != '/')
            {
                ErrorMessage = "Folder must resolve to a virtual path under a mounted alias (e.g. /Game).";
                return false;
            }
            CPackage::AddPackageExt(Path);
            Path = VFS::MakeUniqueFilePath(Path);

            FFixedString SafePath = SanitizeObjectName(Path);
            if (FindObject<CPackage>(SafePath) != nullptr)
            {
                ErrorMessage = "A package with that name is already loaded.";
                return false;
            }

            CPackage* NewPackage = CPackage::CreatePackage(SafePath);
            if (NewPackage == nullptr) { ErrorMessage = "Failed to create package."; return false; }

            const FStringView FileName = VFS::FileName(Path, true);
            CPrefab* Prefab = NewObject<CPrefab>(NewPackage, FName(FileName));
            if (Prefab == nullptr) { ErrorMessage = "Failed to create prefab object."; return false; }
            Prefab->SetFlag(OF_Public);

            // Multi-root: build scratch parent at pivot, reparent top-level entities under it (ReparentEntity is world-preserving), then capture and restore.
            entt::entity CaptureRoot = entt::null;
            entt::entity ScratchRoot = entt::null;
            TVector<entt::entity> OriginalParents;

            if (Req.Roots.size() == 1)
            {
                CaptureRoot = Req.Roots[0];
            }
            else
            {
                FTransform PivotTransform;
                PivotTransform.SetLocation(Req.Pivot);
                ScratchRoot = World->ConstructEntity(FName(FileName), PivotTransform);
                if (ScratchRoot == entt::null)
                {
                    ErrorMessage = "Failed to create scratch parent.";
                    return false;
                }

                OriginalParents.reserve(Req.Roots.size());
                for (entt::entity Entity : Req.Roots)
                {
                    const FRelationshipComponent* Rel = WorkingRegistry.try_get<FRelationshipComponent>(Entity);
                    OriginalParents.push_back(Rel ? Rel->Parent : entt::null);
                    ECS::Utils::ReparentEntity(WorkingRegistry, Entity, ScratchRoot);
                }
                CaptureRoot = ScratchRoot;
            }

            Prefab->CaptureFromWorld(World, CaptureRoot);

            // Anchor the captured root at origin so the prefab opens centered in its editor.
            Prefab->Registry.view<entt::entity>().each([&](entt::entity E)
            {
                const FRelationshipComponent* Rel = Prefab->Registry.try_get<FRelationshipComponent>(E);
                if (Rel != nullptr && Rel->Parent != entt::null)
                {
                    return;
                }
                if (STransformComponent* Tx = Prefab->Registry.try_get<STransformComponent>(E))
                {
                    Tx->SetLocalTransform(FTransform());
                }
            });

            // Restore the world: detach top-level entities back to their original parents, drop the scratch.
            if (ScratchRoot != entt::null)
            {
                for (size_t i = 0; i < Req.Roots.size(); ++i)
                {
                    if (OriginalParents[i] == entt::null)
                    {
                        ECS::Utils::RemoveFromParent(WorkingRegistry, Req.Roots[i]);
                    }
                    else
                    {
                        ECS::Utils::ReparentEntity(WorkingRegistry, Req.Roots[i], OriginalParents[i]);
                    }
                }
                World->DestroyEntity(ScratchRoot);
            }

            FObjectExport& Export = NewPackage->ExportTable.emplace_back();
            Export.ObjectGUID = Prefab->GetGUID();
            Export.ObjectName = Prefab->GetName();
            Export.ClassName = Prefab->GetClass()->GetName();
            Export.Offset = 0;
            Export.Size = 0;
            Export.Object = Prefab;

            if (CPackage::SavePackage(NewPackage, Path))
            {
                FAssetRegistry::Get().AssetCreated(Prefab);
                ImGuiX::Notifications::NotifySuccess("Created prefab: \"{0}\"", Path);
                return true;
            }

            ErrorMessage = "Failed to save prefab to disk.";
            return false;
        });
    }

    void FWorldEditorTool::PushCreatePrefabModalForEntity(entt::entity Entity)
    {
        if (World == nullptr || World->IsSimulating())
        {
            ImGuiX::Notifications::NotifyWarning("Cannot create a prefab while simulating.");
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Entity) || IsLockedPrefabChild(Registry, Entity))
        {
            ImGuiX::Notifications::NotifyWarning("Cannot create a prefab from this entity.");
            return;
        }

        // Fake single-entity selection to reuse PushCreatePrefabFromSelectionModal without a more invasive hook.
        const THashSet<entt::entity> SavedSelection = SelectedEntities;
        SelectedEntities.clear();
        SelectedEntities.insert(Entity);
        PushCreatePrefabFromSelectionModal();
        SelectedEntities = SavedSelection;
    }

    bool FWorldEditorTool::IsAssetEditorTool() const
    {
        return true;
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

        // Drop anything pointing at the old registry: property tables hold raw component pointers; selection cache holds old entt handles.
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
        // Entity is leaving the registry: drop from selection, fix up LastSelectedEntity,
        // invalidate cached property tables (their component pointers are about to dangle).
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
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.75f));
            // shouldCenterContents=true: center the icon glyph in the square (otherwise
            // it's left-aligned at FramePadding and looks off in the small button).
            if (ImGuiX::IconButton(LE_ICON_STOP, "##StopBtn", 0xFFFFFFFF, BtnSize, true))
            {
                SetWorldPlayInEditor(false);
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
                World->GetEntityRegistry().get<STransformComponent>(EditorEntity).SetLocation(FVector3(0.0f));
            }
            
            if (ImGui::Button("Reset Rotation", ImVec2(-1, 0)))
            {
                World->GetEntityRegistry().get<STransformComponent>(EditorEntity).SetRotation(FQuat(1.0f, 0.0f, 0.0f, 0.0f));
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
                    // Keep grouping aligned with ERenderSceneDebugFlags so adding a viz is a one-liner here plus the enum/shader entry.
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
                    };

                    static const FViewModeEntry Lighting[] =
                    {
                        { ERenderSceneDebugFlags::LightComplexity, "Light Complexity" },
                        { ERenderSceneDebugFlags::ClusterGrid,     "Light Clusters"   },
                        { ERenderSceneDebugFlags::ShadowCascades,  "Shadow Cascades"  },
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
                    ImGui::Spacing();
                    DrawGroup("Lighting", Lighting, sizeof(Lighting) / sizeof(Lighting[0]));

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

                ImGui::MenuItem("Draw Entity Debug Info", nullptr, &bDrawEntityDebugInfo);

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
        ImGuiX::HelpMarker(
            "Constrains gizmo drags to fixed steps. Translate = world units. Rotate = degrees. "
            "Scale = multiplicative factor. Toggle quickly with the Snap button on the toolbar.");
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
        // Serialized registry is authoritative post-undo; rebuild the cached set from FSelectedInEditorComponent / FLastSelectedTag.
        ResyncSelectionFromRegistry();

        // Outliner topology may have changed; force a rebuild.
        OutlinerListView.MarkTreeDirty();

        // Terrain GPU mirrors are transient and not serialized, so the restored
        // heightmap/weights won't show until we flag a full re-upload + chunk rebuild.
        if (World)
        {
            auto TerrainView = World->GetEntityRegistry().view<STerrainComponent>();
            for (entt::entity Entity : TerrainView)
            {
                FTerrainGPUState& State = World->GetEntityRegistry().get<STerrainComponent>(Entity).GPUState;
                State.bFullHeightmapDirty = true;
                State.bFullWeightsDirty   = true;
                State.bChunksDirty        = true;
            }
        }
    }
    
    namespace
    {
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

        // Drop tags from entities no longer selected so render highlighting matches the canonical set.
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

        // Clear last-selected tag unconditionally; re-emplace below if new selection isn't empty.
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

        // Always promote to last-selected so clicking a row in a multi-select focuses details.
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
        // Clear old outliner row state; re-mark below from the post-resync set.
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

        // Bulk-erase via registry clear<>(); cheaper than walking SelectedEntities.
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
        // Hook on SNameComponent (not entt::entity) so we don't add an outliner row before the entity has a name.
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

        // Drop pointers into the torn-down world before rebinding.
        PropertyTables.clear();
        WorldSettingsPropertyTable.reset();

        // Old entt handles are meaningless against the new registry.
        SelectedEntities.clear();
        LastSelectedEntity = entt::null;
        DetailsEntity = entt::null;
        bDetailsDirty = true;

        EditorEntity = entt::null;

        // RebindToWorld updates World + InputViewport. ProxyWorld / ProxyEditorEntity are untouched
        // so SetWorldPlayInEditor(false) can still restore the editor's source map on stop.
        RebindToWorld(NewWorld);

        WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());

        OutlinerListView.ClearTree();
        OutlinerListView.MarkTreeDirty();
        EntityToTreeNode.clear();
        PendingOutlinerAdds.clear();

        RebindRegistryObservers();

        // Simulate mode owns the editor entity inside the active world; rebuild it against NewWorld
        // or simulate-exit dereferences entt::null when reading transform/camera.
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

            // Clear selection tags before stashing: leftover tags on ProxyWorld confuse the outliner rebuild on PIE-exit.
            World->GetEntityRegistry().clear<FSelectedInEditorComponent>();
            World->GetEntityRegistry().clear<FLastSelectedTag>();

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

            // Play starts in Game focus: ImGui stands down, input goes to game + UI.
            SetInputFocus(EInputFocus::Game);
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

            // Hand input back to the editor on stop.
            SetInputFocus(EInputFocus::Editor);

            // SetWorld -> SetupWorldForTool builds a fresh editor entity at the default origin. Stash the
            // pre-Play camera pose so we can restore it; otherwise the viewport snaps back to world 0 on stop.
            bool bHasSavedCamera = false;
            FTransform SavedCameraTransform;
            SCameraComponent SavedCamera;
            // Use ProxyEditorEntity (captured at PIE entry); EditorEntity may be null if Travel swapped worlds mid-PIE.
            if (ProxyEditorEntity != entt::null && ProxyWorld->GetEntityRegistry().valid(ProxyEditorEntity))
            {
                SavedCameraTransform = ProxyWorld->GetEntityRegistry().get<STransformComponent>(ProxyEditorEntity).GetWorldTransform();
                SavedCamera = ProxyWorld->GetEntityRegistry().get<SCameraComponent>(ProxyEditorEntity);
                bHasSavedCamera = true;

                ProxyWorld->DestroyEntity(ProxyEditorEntity);
            }
            ProxyEditorEntity = entt::null;
            EditorEntity = entt::null;

            SetWorld(ProxyWorld);
            ProxyWorld->SetActive(true);

            if (bHasSavedCamera && World->GetEntityRegistry().valid(EditorEntity))
            {
                World->GetEntityRegistry().get<STransformComponent>(EditorEntity).SetLocalTransform(SavedCameraTransform);
                World->GetEntityRegistry().patch<SCameraComponent>(EditorEntity, [SavedCamera](SCameraComponent& Patch)
                {
                    Patch = SavedCamera;
                });
            }

            WorldSettingsPropertyTable = MakeUnique<FPropertyTable>(&World->GetDefaultWorldSettings(), SDefaultWorldSettings::StaticStruct());

            ProxyWorld = nullptr;

            OutlinerListView.ClearTree();
            OutlinerListView.MarkTreeDirty();

            RebindRegistryObservers();

            if (InputViewport)
            {
                // Activate editor viewport first so FInputProcessor routes the mode change against the right context.
                FInputViewportRegistry::Get().SetActiveViewport(InputViewport.get());

                // Drop PIE-leftover Lua action callbacks or they keep firing against editor input.
                InputViewport->GetContext().ClearActionCallbacks();

                InputViewport->GetContext().SetInputMode(EInputMode::Game);

                // Route through FInputProcessor so ImGuiConfigFlags_NoMouse clears; direct context-field set would leave ImGui ignoring the mouse.
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

            // Clear selection tags before stashing: leftover tags on ProxyWorld confuse the outliner rebuild on Simulate-exit.
            World->GetEntityRegistry().clear<FSelectedInEditorComponent>();
            World->GetEntityRegistry().clear<FLastSelectedTag>();

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

    void FWorldEditorTool::ApplyInputFocus()
    {
        ImGuiIO& IO = ImGui::GetIO();
        const ImGuiConfigFlags Mask = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
        if (InputFocus == EInputFocus::Game)
        {
            IO.ConfigFlags |= Mask;
        }
        else
        {
            IO.ConfigFlags &= ~Mask;
        }
    }

    void FWorldEditorTool::SetInputFocus(EInputFocus NewFocus)
    {
        InputFocus = NewFocus;
        ApplyInputFocus();

        // Returning to the editor hands the cursor back so panels are usable even
        // if the game had captured/hidden it.
        if (NewFocus == EInputFocus::Editor)
        {
            FInputProcessor::Get().SetMouseMode(EMouseMode::Normal);
        }
    }

    bool FWorldEditorTool::OnEvent(FEvent& Event)
    {
        // Shift+F1 toggles editor/game input focus while playing. Read off the raw
        // event, not the ImGui action registry: Game focus sets NoKeyboard, which
        // would hide the key from ImGui::IsKeyPressed.
        if (bGamePreviewRunning && Event.IsA<FKeyPressedEvent>())
        {
            FKeyPressedEvent& Key = Event.As<FKeyPressedEvent>();
            if (Key.GetKeyCode() == EKey::F1 && Key.IsShiftDown() && !Key.IsRepeat())
            {
                SetInputFocus(InputFocus == EInputFocus::Game ? EInputFocus::Editor : EInputFocus::Game);
                return true;
            }
        }
        return FEditorTool::OnEvent(Event);
    }

    bool FWorldEditorTool::DrawAddableComponentList(const ImGuiTextFilter& Filter, entt::meta_type& OutMetaType, CStruct*& OutStruct, CEntityComponentType*& OutRuntimeType)
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

        // Runtime (data-authored) component types appear in the same list, under "Data". They are
        // enumerated from the asset registry -- not GObjectArray -- so every one on disk shows up
        // regardless of whether it has been loaded yet; the pick loads it on demand.
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
                        { LE_ICON_GAS_CYLINDER " Capsule",     "Capsule",     []() -> CStaticMesh* { return CPrimitiveManager::Get().CapsuleMesh; } },
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
                                if (World->GetEntityRegistry().valid(Entity))
                                {
                                    if (PrimitiveMesh != nullptr)
                                    {
                                        BeginTransaction();
                                        SStaticMeshComponent& MeshComp = World->GetEntityRegistry().emplace_or_replace<SStaticMeshComponent>(Entity);
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
                                    BeginTransaction();
                                    CreatePrimitiveEntity(PrimitiveMesh, Entry->EntityName);
                                    EndTransaction("New Primitive");
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
                    using namespace entt::literals;

                    if (PickedRuntime != nullptr)
                    {
                        BeginTransaction();
                        entt::entity Target = World->GetEntityRegistry().valid(Entity)
                            ? Entity
                            : World->ConstructEntity("Entity", GetCameraSpawnTransform());

                        ECS::Utils::AddRuntimeComponent(World->GetEntityRegistry(), Target, PickedRuntime);
                        EndTransaction("Add Runtime Component");

                        if (World->GetPackage() != nullptr)
                        {
                            World->GetPackage()->MarkDirty();
                        }
                        OutlinerListView.MarkTreeDirty();
                        if (Target != Entity)
                        {
                            SetSingleSelectedEntity(Target);
                        }
                        else if (Target == DetailsEntity)
                        {
                            bDetailsDirty = true;
                        }
                    }
                    else if (World->GetEntityRegistry().valid(Entity))
                    {
                        ECS::Utils::InvokeMetaFunc(PickedMetaType, "emplace"_hs, entt::forward_as_meta(World->GetEntityRegistry()), Entity, entt::forward_as_meta(entt::meta_any{}));
                        OutlinerListView.MarkTreeDirty();
                        if (Entity == DetailsEntity)
                        {
                            bDetailsDirty = true;
                        }
                    }
                    else
                    {
                        BeginTransaction();
                        CreateEntityWithComponent(PickedStruct);
                        EndTransaction("New Component");
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

        // Outliner is incremental: rebuild just resets the map and re-adds roots. Children fill lazily on expand.
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

        // Tooltip header. Component list appended below.
        FString Tooltip;
        if (bIsLockedPrefabChild)
        {
            Tooltip = "Prefab instance child, hierarchy is locked. Edit the source prefab to change.\n";
        }
        else
        {
            Tooltip = FString("Entity: " + eastl::to_string(entt::to_integral(Entity)));
        }

        // Components shown on hover only — they no longer clutter the outliner tree.
        Tooltip += "\n\nComponents:";
        bool bAnyComponent = false;
        ECS::Utils::ForEachComponent(Registry, Entity, [&](void*, const entt::basic_sparse_set<>& /*Set*/, entt::meta_type Meta)
        {
            using namespace entt::literals;
            Tooltip += "\n  ";
            Tooltip += LE_ICON_PUZZLE " ";
            if (entt::meta_any Resolved = ECS::Utils::InvokeMetaFunc(Meta, "static_struct"_hs))
            {
                if (CStruct* StructType = Resolved.cast<CStruct*>())
                {
                    Tooltip += StructType->MakeDisplayName().c_str();
                    bAnyComponent = true;
                    return;
                }
            }
            Tooltip += Meta.name();
            bAnyComponent = true;
        });
        if (!bAnyComponent)
        {
            Tooltip += "\n  (none)";
        }
        Display.TooltipText = Tooltip;

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

        // Only show an expander if the entity actually has child entities; lazy expansion will populate them.
        const FRelationshipComponent* RelForChildren = Registry.try_get<FRelationshipComponent>(Entity);
        const bool bHasChildren = RelForChildren != nullptr && RelForChildren->Children > 0;
        OutlinerListView.MarkHasLazyChildren(ItemEntity, bHasChildren);

        return ItemEntity;
    }

    void FWorldEditorTool::RemoveEntityFromOutliner(entt::entity Entity)
    {
        auto It = EntityToTreeNode.find(Entity);
        if (It == EntityToTreeNode.end())
        {
            return;
        }

        // RemoveNode tears down the subtree; walk hierarchy first to clear EntityToTreeNode for descendants.
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

    void FWorldEditorTool::RefreshOutlinerExpander(entt::entity Entity)
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

        FEntityRegistry& Registry = World->GetEntityRegistry();
        const FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity);
        const bool bHasChildren = Rel != nullptr && Rel->Children > 0;
        OutlinerListView.MarkHasLazyChildren(It->second, bHasChildren);
    }

    void FWorldEditorTool::BuildEntityChildren(FTreeListView& Tree, FTreeNodeID Item)
    {
        FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
        FEntityRegistry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Data.Entity))
        {
            return;
        }

        // Child entity rows: skip ones already present (on_construct race).
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

            AddEntityToOutliner(Child);
        });
    }

    void FWorldEditorTool::OnOutlinerEntityConstructed(entt::registry& Registry, entt::entity Entity)
    {
        if (Registry.any_of<FHideInSceneOutliner>(Entity))
        {
            return;
        }
        // Defer to next flush; FRelationshipComponent may not be set yet.
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

        // Iterate by index; AddEntityToOutliner could grow the queue.
        for (int32 i = 0; i < static_cast<int32>(PendingOutlinerAdds.size()); ++i)
        {
            AddEntityToOutliner(PendingOutlinerAdds[i]);
        }
        PendingOutlinerAdds.clear();
    }

    void FWorldEditorTool::HandleEntityEditorDragDrop(FTreeListView& Tree, entt::entity DropItem)
    {
        // Distinguish entity reparent from asset drop by inspecting the typed
        // payload rather than racing two AcceptDragDropPayload calls.
        const DragDrop::FPayload* Peek = DragDrop::PeekPayload();
        if (Peek == nullptr)
        {
            return;
        }

        if (Peek->Kind == DragDrop::EPayloadKind::Entity)
        {
            CWorld* OutWorld = nullptr;
            entt::entity SourceEntity = entt::null;
            if (DragDrop::AcceptEntity(&OutWorld, &SourceEntity) && OutWorld == World)
            {
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
        const DragDrop::FPayload* Peek = DragDrop::PeekPayload();
        if (Peek == nullptr || Peek->Kind != DragDrop::EPayloadKind::Asset)
        {
            return;
        }
        if (!DragDrop::IsDelivered())
        {
            return;
        }
        HandlePrefabContentDrop(FStringView(Peek->AssetPath.c_str(), Peek->AssetPath.size()), DropTarget);
    }

    void FWorldEditorTool::HandlePrefabContentDrop(FStringView VirtualPath, entt::entity DropTarget)
    {
        // Dispatches every asset class via the editor drop registry. Spawn transform comes from the camera.
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

        for (FComponentTableEntry& Entry : PropertyTables)
        {
            DrawComponentHeader(Entry, Entity);

            ImGui::Spacing();
        }

        // Deferred so we never remove a storage element while iterating / drawing its table.
        if (PendingRuntimeRemove != nullptr)
        {
            ECS::Utils::RemoveRuntimeComponent(World->GetEntityRegistry(), Entity, PendingRuntimeRemove);
            PendingRuntimeRemove = nullptr;
            bDetailsDirty = true;
            if (World != nullptr && World->GetPackage() != nullptr)
            {
                World->GetPackage()->MarkDirty();
            }
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
        
        ImGui::PushID("TagList");
        
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
    }

    void FWorldEditorTool::DrawComponentHeader(FComponentTableEntry& Entry, entt::entity Entity)
    {
        using namespace entt::literals;

        const bool bRuntime = Entry.bRuntime;

        // The live runtime type is fetched from the storage (which strong-refs it) -- never from a
        // cached pointer, which could dangle if the type asset was deleted.
        CEntityComponentType* RuntimeType = nullptr;

        // Visibility / existence check + (runtime) re-point. Reflected components verify via meta;
        // runtime ones verify the storage still holds the entity and re-bind the table if the
        // contiguous storage moved this element (realloc) or migrated its layout.
        if (!bRuntime)
        {
            if (Entry.ReflectedType == STagComponent::StaticStruct())
            {
                return;
            }

            entt::meta_type MetaType = entt::resolve(entt::hashed_string(Entry.ReflectedType->GetName().c_str()));
            if (!ECS::Utils::HasComponent(World->GetEntityRegistry(), Entity, MetaType))
            {
                return;
            }
        }
        else
        {
            FRuntimeComponentStorage* Storage = ECS::Utils::FindRuntimeStorageById(World->GetEntityRegistry(), Entry.RuntimeStorageId);
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
            // Mark dirty; next DrawEntityEditor pass rebuilds PropertyTables. Avoids tearing down handles mid-draw.
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

        // PropertyTables hold raw component pointers; rebuild before drawing on focus change, invalidation, or dirty mark.
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

        // Track owning entity so DrawEntityEditor can detect staleness; null on invalid input forces a rebuild next time.
        DetailsEntity = (Entity != entt::null && World->GetEntityRegistry().valid(Entity)) ? Entity : entt::null;
        bDetailsDirty = false;

        if (World->GetEntityRegistry().valid(Entity))
        {
            // One intermediate row for both reflected and runtime components so they sort together.
            struct FPendingRow
            {
                void*                 Data = nullptr;
                CStruct*              Layout = nullptr;        // reflected CStruct, or runtime layout
                const CStruct*        ReflectedType = nullptr; // null for runtime
                CEntityComponentType* RuntimeType = nullptr;   // null for reflected
                FString               Title;
            };

            TVector<FPendingRow> Pending;

            ECS::Utils::ForEachComponent(World->GetEntityRegistry(), Entity, [&](void* Component, entt::basic_sparse_set<>& Set, const entt::meta_type& Type)
            {
                entt::meta_any Any = ECS::Utils::InvokeMetaFunc(Type, "static_struct"_hs);
                if (!Any)
                {
                    return;
                }

                CStruct* Struct = Any.cast<CStruct*>();
                Pending.push_back({ Component, Struct, Struct, nullptr, Struct->MakeDisplayName().c_str() });
            });

            ECS::Utils::ForEachRuntimeComponent(World->GetEntityRegistry(), Entity,
                [&](CEntityComponentType* Type, CStruct* Layout, void* Data)
            {
                if (Type == nullptr)
                {
                    return;
                }
                Pending.push_back({ Data, Layout, nullptr, Type, Type->GetName().ToString() });
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
                        Entry.Table->SetPostEditCallback([this](const FPropertyChangedEvent&)
                        {
                            // Values live in the storage and serialize with the world; just mark dirty.
                            if (World != nullptr && World->GetPackage() != nullptr)
                            {
                                World->GetPackage()->MarkDirty();
                            }
                        });
                        Entry.Table->MarkDirty();
                        Entry.BoundLayout = Row.Layout;
                        Entry.BoundData   = Row.Data;
                    }
                }
                else
                {
                    Entry.Table = MakeUnique<FPropertyTable>(Row.Data, Row.Layout);
                    Entry.Table->SetPreEditCallback([&](const FPropertyChangedEvent& Event)    { OnPrePropertyChangeEvent(Event); });
                    Entry.Table->SetPostEditCallback([&](const FPropertyChangedEvent& Event)   { OnPostPropertyChangeEvent(Event); });
                    Entry.Table->SetStartEditCallback([&](const FPropertyChangedEvent& Event)  { BeginTransaction(); });
                    Entry.Table->SetFinishEditCallback([&](const FPropertyChangedEvent& Event) { EndTransaction(Event.PropertyName); });
                    Entry.Table->MarkDirty();
                }

                PropertyTables.emplace_back(Move(Entry));
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

        // Always select the new entity so details + outliner highlight show it immediately.
        if (CreatedEntity != entt::null)
        {
            SetSingleSelectedEntity(CreatedEntity);
        }
    }

    void FWorldEditorTool::CreateEntity()
    {
        entt::entity NewEntity = World->ConstructEntity("Entity", GetCameraSpawnTransform());
        if (NewEntity != entt::null)
        {
            SetSingleSelectedEntity(NewEntity);
        }
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
        World->DuplicateEntity(To, From, &EditorEntityUtils::DefaultDuplicateFilter);
    }

    void FWorldEditorTool::CycleGuizmoOp()
    {
        EditorEntityUtils::CycleGizmoOp(GuizmoOp);
    }

    void FWorldEditorTool::ToggleGuizmoMode()
    {
        EditorEntityUtils::ToggleGizmoMode(GuizmoMode);
    }

    void FWorldEditorTool::GroupSelectedEntities()
    {
        if (World == nullptr || World->IsSimulating())
        {
            return;
        }

        TFixedVector<entt::entity, 64> Targets;
        Targets.reserve(SelectedEntities.size());
        entt::registry& Registry = World->GetEntityRegistry();
        for (entt::entity Entity : SelectedEntities)
        {
            if (!Registry.valid(Entity) || IsLockedPrefabChild(Registry, Entity))
            {
                continue;
            }
            Targets.push_back(Entity);
        }

        if (Targets.size() < 2)
        {
            return;
        }

        FVector3 Median(0.0f);
        for (entt::entity Entity : Targets)
        {
            Median += Registry.get<STransformComponent>(Entity).GetWorldLocation();
        }
        Median /= static_cast<float>(Targets.size());

        BeginTransaction();

        FTransform GroupTransform;
        GroupTransform.SetLocation(Median);
        entt::entity Group = World->ConstructEntity("Group", GroupTransform);
        if (Group == entt::null)
        {
            PendingBeforeState.clear();
            return;
        }

        for (entt::entity Entity : Targets)
        {
            ECS::Utils::ReparentEntity(Registry, Entity, Group);
            ReparentEntityInOutliner(Entity);
        }

        SetSingleSelectedEntity(Group);
        EndTransaction("Group Selected");
    }

    void FWorldEditorTool::DropSelectionToFloor()
    {
        if (World == nullptr || World->IsSimulating())
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();

        TFixedVector<entt::entity, 64> Targets;
        Targets.reserve(SelectedEntities.size());
        for (entt::entity Entity : SelectedEntities)
        {
            if (Registry.valid(Entity) && !IsLockedPrefabChild(Registry, Entity))
            {
                Targets.push_back(Entity);
            }
        }

        if (Targets.empty())
        {
            return;
        }

        // Build exclusion set (dropped entities + descendants) so a group doesn't land on its own child mesh.
        THashSet<entt::entity> Exclude;
        for (entt::entity Entity : Targets)
        {
            Exclude.insert(Entity);
            ECS::Utils::ForEachDescendant(Registry, Entity, [&](entt::entity Desc)
            {
                Exclude.insert(Desc);
            });
        }

        // Snapshot world-space AABBs once so per-entity raycast is a flat vector walk.
        struct FCandidate { FAABB Box; };
        TVector<FCandidate> Candidates;
        Registry.view<STransformComponent, SStaticMeshComponent>().each(
            [&](entt::entity Entity, STransformComponent& Transform, SStaticMeshComponent& Mesh)
        {
            if (Exclude.find(Entity) != Exclude.end())
            {
                return;
            }
            Candidates.push_back({ Mesh.GetAABB().ToWorld(Transform.GetWorldMatrix()) });
        });

        BeginTransaction();

        bool bAnyMoved = false;
        const FVector3 Down(0.0f, -1.0f, 0.0f);

        for (entt::entity Entity : Targets)
        {
            STransformComponent& Transform = Registry.get<STransformComponent>(Entity);
            const FVector3 WorldLocation = Transform.GetWorldLocation();
            const FVector3 Origin = WorldLocation + FVector3(0.0f, 0.5f, 0.0f);

            float BestT = FLT_MAX;
            for (const FCandidate& C : Candidates)
            {
                float T;
                if (RayVsAABB(Origin, Down, C.Box, T) && T < BestT)
                {
                    BestT = T;
                }
            }

            // Fallback to Y=0 plane so the action always does something predictable.
            float NewY;
            if (BestT < FLT_MAX)
            {
                NewY = (Origin + Down * BestT).y;
            }
            else
            {
                NewY = 0.0f;
            }

            FTransform NewWorld = Transform.GetWorldTransform();
            NewWorld.Location.y = NewY;

            FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity);
            if (Rel != nullptr && Rel->Parent != entt::null && Registry.valid(Rel->Parent))
            {
                FMatrix4 ParentWorld = Registry.get<STransformComponent>(Rel->Parent).GetWorldMatrix();
                FMatrix4 NewLocalMat = Math::Inverse(ParentWorld) * NewWorld.GetMatrix();

                FVector3 LT, LS, LSkew; FQuat LR; FVector4 LP;
                Math::Decompose(NewLocalMat, LS, LR, LT, LSkew, LP);

                Transform.SetLocalLocation(LT);
                Transform.SetLocalRotation(LR);
                Transform.SetLocalScale(LS);
            }
            else
            {
                Transform.SetLocalLocation(NewWorld.Location);
            }

            bAnyMoved = true;
        }

        if (bAnyMoved)
        {
            EndTransaction("Drop to Floor");
        }
        else
        {
            PendingBeforeState.clear();
        }
    }

    void FWorldEditorTool::FrameAllEntities()
    {
        if (World == nullptr)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(EditorEntity))
        {
            return;
        }

        FVector3 Min(FLT_MAX);
        FVector3 Max(-FLT_MAX);
        bool bAny = false;

        auto View = Registry.view<STransformComponent>();
        for (entt::entity Entity : View)
        {
            if (Entity == EditorEntity)
            {
                continue;
            }

            const FVector3 Loc = Registry.get<STransformComponent>(Entity).GetWorldLocation();
            Min = Math::Min(Min, Loc);
            Max = Math::Max(Max, Loc);
            bAny = true;
        }

        if (!bAny)
        {
            return;
        }

        const FVector3 Center = (Min + Max) * 0.5f;
        const float Radius = Math::Max(Math::Length(Max - Min) * 0.5f, 1.0f);

        const SCameraComponent& Camera = Registry.get<SCameraComponent>(EditorEntity);
        const float HalfFov = Math::Radians(Camera.GetFOV() * 0.5f);
        const float Distance = (Radius / Math::Tan(Math::Max(HalfFov, Math::Radians(1.0f)))) * 1.5f;

        STransformComponent& EditorTransform = Registry.get<STransformComponent>(EditorEntity);
        const FVector3 Forward = EditorTransform.GetForward();
        const FVector3 NewPos  = Center - Forward * Distance;
        EditorTransform.SetLocation(NewPos);
        EditorTransform.SetRotation(Math::FindLookAtRotation(Center, NewPos));
    }

    void FWorldEditorTool::CopyTransformFromLastSelected()
    {
        if (World == nullptr)
        {
            return;
        }

        const entt::entity Entity = GetLastSelectedEntity();
        entt::registry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Entity))
        {
            return;
        }

        CopiedTransform = Registry.get<STransformComponent>(Entity).GetWorldTransform();
        bHasCopiedTransform = true;
    }

    void FWorldEditorTool::PasteTransformToSelection()
    {
        if (World == nullptr || World->IsSimulating() || !bHasCopiedTransform)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();

        TFixedVector<entt::entity, 64> Targets;
        Targets.reserve(SelectedEntities.size());
        for (entt::entity Entity : SelectedEntities)
        {
            if (Registry.valid(Entity) && !IsLockedPrefabChild(Registry, Entity))
            {
                Targets.push_back(Entity);
            }
        }

        if (Targets.empty())
        {
            return;
        }

        BeginTransaction();

        for (entt::entity Entity : Targets)
        {
            STransformComponent& Transform = Registry.get<STransformComponent>(Entity);

            FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity);
            if (Rel != nullptr && Rel->Parent != entt::null && Registry.valid(Rel->Parent))
            {
                FMatrix4 ParentWorld = Registry.get<STransformComponent>(Rel->Parent).GetWorldMatrix();
                FMatrix4 NewLocalMat = Math::Inverse(ParentWorld) * CopiedTransform.GetMatrix();

                FVector3 LT, LS, LSkew; FQuat LR; FVector4 LP;
                Math::Decompose(NewLocalMat, LS, LR, LT, LSkew, LP);

                Transform.SetLocalLocation(LT);
                Transform.SetLocalRotation(LR);
                Transform.SetLocalScale(LS);
            }
            else
            {
                Transform.SetLocalTransform(CopiedTransform);
            }
        }

        EndTransaction("Paste Transform");
    }

    void FWorldEditorTool::SaveCameraBookmark(int32 Slot)
    {
        if (Slot < 0 || Slot >= NumCameraBookmarks || World == nullptr)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(EditorEntity))
        {
            return;
        }

        CameraBookmarks[Slot] = Registry.get<STransformComponent>(EditorEntity).GetWorldTransform();
        bCameraBookmarkSet[Slot] = true;
    }

    void FWorldEditorTool::RecallCameraBookmark(int32 Slot)
    {
        if (Slot < 0 || Slot >= NumCameraBookmarks || !bCameraBookmarkSet[Slot] || World == nullptr)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(EditorEntity))
        {
            return;
        }

        // Restore to free-cam; orbit re-derives from the saved pose on next input.
        if (CameraState.Mode != EEditorCameraMode::Free)
        {
            SetCameraMode(EEditorCameraMode::Free);
        }

        STransformComponent& Transform = Registry.get<STransformComponent>(EditorEntity);
        Transform.SetLocalLocation(CameraBookmarks[Slot].Location);
        Transform.SetLocalRotation(CameraBookmarks[Slot].Rotation);
        Transform.SetLocalScale(CameraBookmarks[Slot].Scale);
    }

    void FWorldEditorTool::DrawCursorWorldPositionOverlay(ImVec2 ViewportOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera)
    {
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
        {
            return;
        }

        const ImVec2 MousePos = ImGui::GetMousePos();
        const float LocalX = MousePos.x - ViewportOrigin.x;
        const float LocalY = MousePos.y - ViewportOrigin.y;
        if (LocalX < 0.0f || LocalY < 0.0f || LocalX >= ViewportSize.x || LocalY >= ViewportSize.y)
        {
            return;
        }

        // Unproject through camera ViewProj; flip Y because camera bakes Vulkan +Y-down NDC.
        FMatrix4 Proj = Camera.GetProjectionMatrix();
        Proj[1][1] *= -1.0f;
        const FMatrix4 InvVP = Math::Inverse(Proj * Camera.GetViewMatrix());

        const float NdcX = (LocalX / ViewportSize.x) * 2.0f - 1.0f;
        const float NdcY = 1.0f - (LocalY / ViewportSize.y) * 2.0f;
        FVector4 Far = InvVP * FVector4(NdcX, NdcY, 1.0f, 1.0f);
        if (Math::Abs(Far.w) < 1e-6f)
        {
            return;
        }
        const FVector3 FarWorld = FVector3(Far) / Far.w;
        const FVector3 Origin   = Camera.GetPosition();
        const FVector3 Dir      = Math::Normalize(FarWorld - Origin);

        // Intersect Y=0 plane; skip nearly-parallel rays or up-pointing rays from above.
        if (Math::Abs(Dir.y) < 1e-4f)
        {
            return;
        }
        const float T = -Origin.y / Dir.y;
        if (T <= 0.0f)
        {
            return;
        }
        const FVector3 Hit = Origin + Dir * T;

        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const ImVec2 TextPos(ViewportOrigin.x + 12.0f, ViewportOrigin.y + ViewportSize.y - 24.0f);
        char Buf[128];
        snprintf(Buf, sizeof(Buf), "Cursor: %.2f, %.2f, %.2f", Hit.x, Hit.y, Hit.z);
        DrawList->AddRectFilled(ImVec2(TextPos.x - 6.0f, TextPos.y - 4.0f),
                                ImVec2(TextPos.x + 220.0f, TextPos.y + 18.0f),
                                IM_COL32(0, 0, 0, 140), 4.0f);
        DrawList->AddText(TextPos, IM_COL32(220, 220, 220, 230), Buf);
    }

    void FWorldEditorTool::DrawEntityDebugOverlay(ImVec2 ViewportOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera)
    {
        if (!bDrawEntityDebugInfo || World == nullptr)
        {
            return;
        }

        FMatrix4 Proj = Camera.GetProjectionMatrix();
        Proj[1][1] *= -1.0f;
        const FMatrix4 ViewProj = Proj * Camera.GetViewMatrix();
        FFrustum Frustum = Camera.GetViewVolume().GetFrustum();
        const FVector3 CameraPos = Camera.GetPosition();

        FEntityRegistry& Registry = World->GetEntityRegistry();
        auto View = Registry.view<STransformComponent>(entt::exclude<FEditorComponent>);

        struct FCandidate
        {
            entt::entity Entity;
            ImVec2       Screen;
            float        DepthSq;
        };

        TVector<FCandidate> Candidates;
        Candidates.reserve(View.size_hint());

        for (entt::entity Entity : View)
        {
            const STransformComponent& Transform = View.get<STransformComponent>(Entity);
            const FVector3 WorldPos = Transform.GetWorldLocation();
            if (!Frustum.IsInside(WorldPos))
            {
                continue;
            }

            ImVec2 Screen;
            if (!ProjectPointToScreen(WorldPos, ViewProj, ViewportSize, Screen))
            {
                continue;
            }

            const FVector3 Delta = WorldPos - CameraPos;
            Candidates.push_back({ Entity, Screen, Math::Dot(Delta, Delta) });
        }

        // Front-to-back so closer labels claim space first.
        eastl::sort(Candidates.begin(), Candidates.end(), [](const FCandidate& A, const FCandidate& B)
        {
            return A.DepthSq < B.DepthSq;
        });

        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const float LineHeight = ImGui::GetTextLineHeight();
        const float Padding = 4.0f;
        const ImU32 BgColor = IM_COL32(0, 0, 0, 160);
        const ImU32 TextColor = IM_COL32(230, 230, 230, 235);

        TVector<ImRect> PlacedRects;
        PlacedRects.reserve(Candidates.size());

        for (const FCandidate& C : Candidates)
        {
            const SNameComponent* NameComp = Registry.try_get<SNameComponent>(C.Entity);
            const STransformComponent& Transform = Registry.get<STransformComponent>(C.Entity);
            const FVector3 P = Transform.GetWorldLocation();

            char Line0[96];
            char Line1[96];
            const char* Name = (NameComp && !NameComp->Name.IsNone()) ? NameComp->Name.c_str() : "Entity";
            snprintf(Line0, sizeof(Line0), "%s (id=%u)", Name, (uint32)entt::to_integral(C.Entity));
            snprintf(Line1, sizeof(Line1), "%.2f, %.2f, %.2f", P.x, P.y, P.z);

            const ImVec2 Size0 = ImGui::CalcTextSize(Line0);
            const ImVec2 Size1 = ImGui::CalcTextSize(Line1);
            const float BoxW = Math::Max(Size0.x, Size1.x) + Padding * 2.0f;
            const float BoxH = LineHeight * 2.0f + Padding * 2.0f;

            // Anchor above the entity, then nudge down on collision until clear or we give up.
            ImVec2 Anchor(ViewportOrigin.x + C.Screen.x - BoxW * 0.5f,
                          ViewportOrigin.y + C.Screen.y - BoxH - 6.0f);

            // Clamp to viewport horizontally so labels don't drift offscreen.
            const float MinX = ViewportOrigin.x + 2.0f;
            const float MaxX = ViewportOrigin.x + ViewportSize.x - BoxW - 2.0f;
            Anchor.x = Math::Clamp(Anchor.x, MinX, MaxX);

            ImRect Rect(Anchor, ImVec2(Anchor.x + BoxW, Anchor.y + BoxH));

            bool bPlaced = false;
            for (int32 Attempt = 0; Attempt < 16; ++Attempt)
            {
                bool bOverlap = false;
                for (const ImRect& Other : PlacedRects)
                {
                    if (Rect.Overlaps(Other))
                    {
                        bOverlap = true;
                        Rect.Min.y = Other.Max.y + 1.0f;
                        Rect.Max.y = Rect.Min.y + BoxH;
                        break;
                    }
                }
                if (!bOverlap)
                {
                    bPlaced = true;
                    break;
                }
            }

            if (!bPlaced)
            {
                continue;
            }

            // Drop labels that got pushed off the bottom of the viewport.
            if (Rect.Max.y > ViewportOrigin.y + ViewportSize.y - 2.0f)
            {
                continue;
            }

            PlacedRects.push_back(Rect);

            DrawList->AddRectFilled(Rect.Min, Rect.Max, BgColor, 3.0f);
            DrawList->AddText(ImVec2(Rect.Min.x + Padding, Rect.Min.y + Padding), TextColor, Line0);
            DrawList->AddText(ImVec2(Rect.Min.x + Padding, Rect.Min.y + Padding + LineHeight), TextColor, Line1);
        }
    }

    void FWorldEditorTool::DrawOffscreenSelectionIndicators(ImVec2 ViewportOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera)
    {
        if (World == nullptr)
        {
            return;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();
        auto SelView = Registry.view<FSelectedInEditorComponent, STransformComponent>();
        if (SelView.size_hint() == 0)
        {
            return;
        }

        FMatrix4 Proj = Camera.GetProjectionMatrix();
        Proj[1][1] *= -1.0f;
        const FMatrix4 ViewProj = Proj * Camera.GetViewMatrix();

        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const float EdgePadding = 32.0f;
        const ImVec2 Center(ViewportOrigin.x + ViewportSize.x * 0.5f,
                            ViewportOrigin.y + ViewportSize.y * 0.5f);
        const ImVec2 RectMin(ViewportOrigin.x + EdgePadding,
                             ViewportOrigin.y + EdgePadding);
        const ImVec2 RectMax(ViewportOrigin.x + ViewportSize.x - EdgePadding,
                             ViewportOrigin.y + ViewportSize.y - EdgePadding);

        const ImU32 FillColor    = IM_COL32(255, 195, 60, 235);
        const ImU32 OutlineColor = IM_COL32(20, 20, 20, 220);

        for (entt::entity Entity : SelView)
        {
            const STransformComponent& Transform = SelView.get<STransformComponent>(Entity);
            const FVector3 WorldPos = Transform.GetWorldLocation();

            FVector4 Clip = ViewProj * FVector4(WorldPos, 1.0f);

            // Reflect points behind the camera through origin so the indicator points back toward the entity.
            const bool bBehind = Clip.w <= 0.0f;
            if (bBehind)
            {
                Clip.x = -Clip.x;
                Clip.y = -Clip.y;
                Clip.w = -Clip.w;
            }
            const float SafeW = Math::Max(Clip.w, 1e-4f);
            float NdcX = Clip.x / SafeW;
            float NdcY = Clip.y / SafeW;

            // Force NDC outside [-1,1] for behind-camera entities so we still emit an indicator.
            if (bBehind)
            {
                const float Mag = Math::Max(Math::Abs(NdcX), Math::Abs(NdcY));
                if (Mag > 1e-4f)
                {
                    const float Scale = 1.5f / Mag;
                    NdcX *= Scale;
                    NdcY *= Scale;
                }
                else
                {
                    NdcX = 0.0f;
                    NdcY = -1.5f;
                }
            }

            const float ScreenX = (NdcX * 0.5f + 0.5f) * ViewportSize.x + ViewportOrigin.x;
            const float ScreenY = (1.0f - (NdcY * 0.5f + 0.5f)) * ViewportSize.y + ViewportOrigin.y;

            if (!bBehind &&
                ScreenX >= RectMin.x && ScreenX <= RectMax.x &&
                ScreenY >= RectMin.y && ScreenY <= RectMax.y)
            {
                continue;
            }

            ImVec2 Dir(ScreenX - Center.x, ScreenY - Center.y);
            const float Len = sqrtf(Dir.x * Dir.x + Dir.y * Dir.y);
            if (Len < 1e-3f)
            {
                continue;
            }
            Dir.x /= Len;
            Dir.y /= Len;

            // Clip ray from center to the inset rect.
            const float TX = (Dir.x > 0.0f) ? (RectMax.x - Center.x) / Dir.x
                            : (Dir.x < 0.0f) ? (RectMin.x - Center.x) / Dir.x
                            : FLT_MAX;
            const float TY = (Dir.y > 0.0f) ? (RectMax.y - Center.y) / Dir.y
                            : (Dir.y < 0.0f) ? (RectMin.y - Center.y) / Dir.y
                            : FLT_MAX;
            const float T = Math::Min(TX, TY);
            if (T <= 0.0f)
            {
                continue;
            }

            const ImVec2 Tip(Center.x + Dir.x * T, Center.y + Dir.y * T);

            const float ArrowLen  = 16.0f;
            const float ArrowHalf = 9.0f;
            const ImVec2 Perp(-Dir.y, Dir.x);
            const ImVec2 BaseCenter(Tip.x - Dir.x * ArrowLen, Tip.y - Dir.y * ArrowLen);
            const ImVec2 BaseL(BaseCenter.x + Perp.x * ArrowHalf, BaseCenter.y + Perp.y * ArrowHalf);
            const ImVec2 BaseR(BaseCenter.x - Perp.x * ArrowHalf, BaseCenter.y - Perp.y * ArrowHalf);

            DrawList->AddTriangleFilled(Tip, BaseL, BaseR, FillColor);
            DrawList->AddTriangle(Tip, BaseL, BaseR, OutlineColor, 1.5f);
            DrawList->AddCircleFilled(BaseCenter, 3.0f, FillColor);
            DrawList->AddCircle(BaseCenter, 3.0f, OutlineColor, 0, 1.5f);
        }
    }
}
