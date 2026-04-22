#include "PrefabEditorTool.h"

#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/Prefabs/Prefab.h"
#include "Assets/AssetTypes/Prefabs/PrefabComponents.h"
#include "Components/EditorEntityTags.h"
#include "UI/Tools/ContentBrowserEditorTool.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Package/Package.h"
#include "EASTL/sort.h"
#include "GUID/GUID.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "UI/Properties/PropertyTable.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/EntityTags.h"
#include "World/Entity/Components/EnvironmentComponent.h"
#include "World/Entity/Components/LightComponent.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
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

        OutlinerContext.RebuildTreeFunction = [this](FTreeListView& Tree)
        {
            RebuildOutlinerTree(Tree);
        };

        OutlinerContext.ItemSelectedFunction = [this](FTreeListView& Tree, entt::entity Item, bool bShouldClear)
        {
            if (bShouldClear)
            {
                ClearSelectedEntities();
            }

            if (Item == entt::null)
            {
                return;
            }

            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            if (Data.Entity == entt::null || !World->GetEntityRegistry().valid(Data.Entity))
            {
                return;
            }

            AddSelectedEntity(Data.Entity, false);
        };

        OutlinerContext.ItemContextMenuFunction = [this](FTreeListView& Tree, entt::entity Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            entt::registry& Registry = World->GetEntityRegistry();

            if (!Registry.valid(Data.Entity))
            {
                return;
            }

            if (ECS::Utils::IsChild(Registry, Data.Entity))
            {
                if (ImGui::MenuItem("Unparent"))
                {
                    ECS::Utils::RemoveFromParent(Registry, Data.Entity);
                    OutlinerListView.MarkTreeDirty();
                    Asset->GetPackage()->MarkDirty();
                }
            }

            if (ImGui::MenuItem(LE_ICON_DELETE " Delete"))
            {
                RequestDestroyEntity(Data.Entity);
            }
        };

        OutlinerContext.SetDragDropFunction = [this](FTreeListView& Tree, entt::entity Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            entt::entity Source = Data.Entity;
            ImGui::SetDragDropPayload(PrefabDragDropID, &Source, sizeof(entt::entity));
        };

        OutlinerContext.DragDropFunction = [this](FTreeListView& Tree, entt::entity Item)
        {
            FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
            HandleOutlinerDragDrop(Tree, Data.Entity);
        };

        OutlinerContext.KeyPressedFunction = [this](FTreeListView& Tree, entt::entity Item, ImGuiKey Key) -> bool
        {
            if (Key == ImGuiKey_Delete)
            {
                FEntityListViewItemData& Data = Tree.Get<FEntityListViewItemData>(Item);
                RequestDestroyEntity(Data.Entity);
                return true;
            }
            return false;
        };
    }

    void FPrefabEditorTool::SetupWorldForTool()
    {
        FEditorTool::SetupWorldForTool();

        DirectionalLightEntity = World->ConstructEntity("PreviewLight");
        World->GetEntityRegistry().emplace<FHideInSceneOutliner>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SDirectionalLightComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SEnvironmentComponent>(DirectionalLightEntity);

        CreateFloorPlane(0.0f);
    }

    void FPrefabEditorTool::OnAssetLoadFinished()
    {
        LoadPrefabIntoPreviewWorld();
        OutlinerListView.MarkTreeDirty();
    }

    void FPrefabEditorTool::LoadPrefabIntoPreviewWorld()
    {
        CPrefab* Prefab = GetPrefab();
        if (Prefab == nullptr || World == nullptr)
        {
            return;
        }

        // Remove any previously-loaded prefab entities from the preview world (leave lights / floor / camera).
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
                WorldRegistry.destroy(E);
            }
        }

        // If the prefab is empty, seed it with a single root entity the user can start editing.
        if (Prefab->Registry.view<entt::entity>().empty())
        {
            entt::entity Root = WorldRegistry.create();
            WorldRegistry.emplace<SNameComponent>(Root).Name = FName("Root");
            WorldRegistry.emplace<STransformComponent>(Root);
            WorldRegistry.emplace<SPrefabComponent>(Root).StableID = FName(FGuid::New().ToShortString());
            AddSelectedEntity(Root, false);
            return;
        }

        THashMap<entt::entity, entt::entity> Map;
        CPrefab::CopyRegistry(Prefab->Registry, WorldRegistry, Map);

        // Pick the first prefab entity as a reasonable default selection.
        WorldRegistry.view<SPrefabComponent>().each([&](entt::entity E, const SPrefabComponent&)
        {
            if (GetLastSelectedEntity() == entt::null)
            {
                AddSelectedEntity(E, false);
            }
        });
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
            if (ID == entt::type_hash<FRelationshipComponent>::value()) continue;
            if (ID == entt::type_hash<FSelectedInEditorComponent>::value()) continue;
            if (ID == entt::type_hash<FHideInSceneOutliner>::value()) continue;
            if (ID == entt::type_hash<FEditorComponent>::value()) continue;
            if (ID == entt::type_hash<FLastSelectedTag>::value()) continue;
            if (ID == entt::type_hash<FCopiedTag>::value()) continue;
            if (ID == entt::type_hash<FNeedsTransformUpdate>::value()) continue;

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

        // Remap relationships into the prefab's registry. Parent/Children references are only kept if they point to
        // another captured prefab entity; outside references become null (severing ties to preview-only entities).
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
        FAssetEditorTool::OnSave();
    }

    void FPrefabEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::Update(UpdateContext);

        ProcessDestroyRequests();

        // Mark selection's transform dirty so the gizmo's edits propagate to children this frame.
        auto SelectedView = World->GetEntityRegistry().view<FSelectedInEditorComponent>();
        SelectedView.each([&](entt::entity Entity)
        {
            World->GetEntityRegistry().emplace_or_replace<FNeedsTransformUpdate>(Entity);
        });


        entt::entity LastSelected = GetLastSelectedEntity();
        if (World && World->GetEntityRegistry().valid(LastSelected))
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

        // Ctrl+S save, Delete key with viewport focused.
        if (bViewportHovered)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            {
                SelectedView.each([&](entt::entity Entity)
                {
                    RequestDestroyEntity(Entity);
                });
            }

            if (ImGui::IsKeyPressed(ImGuiKey_F))
            {
                FocusViewportToEntity(GetLastSelectedEntity());
            }
        }
    }

    void FPrefabEditorTool::ProcessDestroyRequests()
    {
        while (!EntityDestroyRequests.empty())
        {
            entt::entity Entity = EntityDestroyRequests.back();
            EntityDestroyRequests.pop();

            if (Entity == entt::null || !World->GetEntityRegistry().valid(Entity))
            {
                continue;
            }

            // Don't allow destroying the prefab root — a prefab must always have one.
            const entt::entity Root = FindPrefabRoot();
            if (Entity == Root)
            {
                ImGuiX::Notifications::NotifyError("Cannot delete the prefab root entity.");
                continue;
            }

            ECS::Utils::DestroyEntityHierarchy(World->GetEntityRegistry(), Entity);
            OutlinerListView.MarkTreeDirty();
            Asset->GetPackage()->MarkDirty();
        }
    }

    void FPrefabEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
        ComponentPropertyTables.clear();
        ComponentStructs.clear();
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

    entt::entity FPrefabEditorTool::CreateEntityAtRoot()
    {
        entt::registry& WorldRegistry = World->GetEntityRegistry();

        // Resolve the parent root BEFORE creating the new entity — otherwise the freshly created
        // entity (which already has SPrefabComponent and no parent) would be picked as its own root,
        // producing a self-parented node that infinite-loops ForEachChild during transform resolve.
        const entt::entity Root = FindPrefabRoot();

        entt::entity NewEntity = WorldRegistry.create();
        WorldRegistry.emplace<SNameComponent>(NewEntity).Name = FName("NewEntity");
        WorldRegistry.emplace<STransformComponent>(NewEntity);
        WorldRegistry.emplace<SPrefabComponent>(NewEntity).StableID = FName(FGuid::New().ToShortString());

        if (Root != entt::null && Root != NewEntity)
        {
            ECS::Utils::ReparentEntity(WorldRegistry, NewEntity, Root);
        }

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

    void FPrefabEditorTool::AddSelectedEntity(entt::entity Entity, bool bRebuild)
    {
        entt::registry& Registry = World->GetEntityRegistry();
        if (!Registry.valid(Entity))
        {
            return;
        }

        Registry.clear<FLastSelectedTag>();
        Registry.emplace_or_replace<FLastSelectedTag>(Entity);
        Registry.emplace_or_replace<FSelectedInEditorComponent>(Entity);

        if (bRebuild)
        {
            OutlinerListView.MarkTreeDirty();
        }
    }

    void FPrefabEditorTool::ClearSelectedEntities()
    {
        if (World == nullptr)
        {
            return;
        }
        World->GetEntityRegistry().clear<FSelectedInEditorComponent>();
        World->GetEntityRegistry().clear<FLastSelectedTag>();
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

    void FPrefabEditorTool::DrawAddEntityButton()
    {
        if (ImGui::Button(LE_ICON_PLUS " Add Entity"))
        {
            entt::entity New = CreateEntityAtRoot();
            ClearSelectedEntities();
            AddSelectedEntity(New, true);
        }
    }

    void FPrefabEditorTool::RebuildOutlinerTree(FTreeListView& Tree)
    {
        entt::registry& Registry = World->GetEntityRegistry();

        TFunction<void(entt::entity, entt::entity)> AddRecursive;
        AddRecursive = [&](entt::entity WorldEntity, entt::entity ParentItem)
        {
            SNameComponent* NameComp = Registry.try_get<SNameComponent>(WorldEntity);
            FFixedString DisplayName;
            DisplayName.append(LE_ICON_CUBE).append(" ")
                .append(NameComp ? NameComp->Name.c_str() : "<unnamed>")
                .append_convert(FString(" - (" + eastl::to_string(entt::to_integral(WorldEntity)) + ")"));

            entt::entity ItemEntity = Tree.CreateNode(ParentItem, DisplayName);
            FTreeNodeDisplay& Display = Tree.Get<FTreeNodeDisplay>(ItemEntity);
            Display.TooltipText = FString("Entity: " + eastl::to_string(entt::to_integral(WorldEntity))).c_str();
            Display.bAllowRenaming = false;

            Tree.EmplaceUserData<FEntityListViewItemData>(ItemEntity).Entity = WorldEntity;

            if (Registry.any_of<FSelectedInEditorComponent>(WorldEntity))
            {
                Tree.Get<FTreeNodeState>(ItemEntity).bSelected = true;
            }

            ECS::Utils::ForEachChild(Registry, WorldEntity, [&](entt::entity Child)
            {
                AddRecursive(Child, ItemEntity);
            });
        };

        // Only show prefab-tagged entities in the outliner. The root is the one with no prefab parent.
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
            AddRecursive(Root, entt::null);
        }
    }

    void FPrefabEditorTool::HandleOutlinerDragDrop(FTreeListView& Tree, entt::entity DropItem)
    {
        if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(PrefabDragDropID, ImGuiDragDropFlags_AcceptBeforeDelivery))
        {
            if (Payload->IsDelivery())
            {
                entt::entity Source = *static_cast<entt::entity*>(Payload->Data);
                if (Source != entt::null && Source != DropItem)
                {
                    entt::registry& Registry = World->GetEntityRegistry();
                    if (Registry.valid(Source) && (DropItem == entt::null || Registry.valid(DropItem)))
                    {
                        // Don't let the user reparent the prefab root — prefab hierarchy stays single-rooted.
                        if (Source != FindPrefabRoot() && DropItem != entt::null)
                        {
                            ECS::Utils::ReparentEntity(Registry, Source, DropItem);
                            OutlinerListView.MarkTreeDirty();
                            Asset->GetPackage()->MarkDirty();
                        }
                    }
                }
            }
            return;
        }
    }

    void FPrefabEditorTool::HandlePrefabContentDrop(FStringView VirtualPath, entt::entity DropTarget)
    {
        // Prefab-in-prefab drops aren't a common workflow; left as a stub so the outliner still provides
        // a drag-drop target site consistent with the world editor.
        (void)VirtualPath;
        (void)DropTarget;
    }

    void FPrefabEditorTool::DrawOutliner(bool bFocused)
    {
        if (World == nullptr)
        {
            return;
        }

        DrawAddEntityButton();
        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        if (ImGui::BeginChild("##PrefabEntityList", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar))
        {
            OutlinerListView.Draw(OutlinerContext);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void FPrefabEditorTool::DrawAddComponentButton(entt::entity Entity)
    {
        using namespace entt::literals;

        if (!ImGui::Button(LE_ICON_PLUS " Add Component"))
        {
            return;
        }
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

            auto Table = MakeUnique<FPropertyTable>(CompPtr, Struct);
            Table->SetPostEditCallback([this](const FPropertyChangedEvent&)
            {
                if (Asset && Asset->GetPackage())
                {
                    Asset->GetPackage()->MarkDirty();
                }
            });
            ComponentPropertyTables.push_back(eastl::move(Table));
            ComponentStructs.push_back(Struct);
        });
    }

    void FPrefabEditorTool::DrawEntityProperties(bool bFocused)
    {
        if (World == nullptr)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        entt::entity SelectedEntity = GetLastSelectedEntity();
        if (SelectedEntity == entt::null || !Registry.valid(SelectedEntity))
        {
            ImGui::TextDisabled("Select an entity to edit its properties.");
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
                Name->Name = FName(Buffer);
                OutlinerListView.MarkTreeDirty();
                Asset->GetPackage()->MarkDirty();
            }
        }
        ImGui::Separator();

        DrawAddComponentButton(SelectedEntity);
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DELETE " Delete Entity"))
        {
            RequestDestroyEntity(SelectedEntity);
        }

        if (ImGui::BeginPopup("##AddPrefabComponent"))
        {
            using namespace entt::literals;

            static ImGuiTextFilter Filter;
            Filter.Draw("##Search", 200.0f);

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

                const FString DisplayName = Struct->GetName().ToString();
                if (!Filter.PassFilter(DisplayName.c_str()))
                {
                    continue;
                }

                Sorted.emplace_back(MetaType, Struct);
            }

            eastl::sort(Sorted.begin(), Sorted.end(), [](const auto& LHS, const auto& RHS)
            {
                return LHS.second->GetName().ToString() < RHS.second->GetName().ToString();
            });

            for (auto&& [MetaType, Struct] : Sorted)
            {
                const FString DisplayName = Struct->GetName().ToString();
                if (ImGui::Selectable(DisplayName.c_str()))
                {
                    ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs,
                        entt::forward_as_meta(Registry), SelectedEntity, entt::forward_as_meta(entt::meta_any{}));
                    bPropertyTablesDirty = true;
                    Asset->GetPackage()->MarkDirty();
                }
            }

            ImGui::EndPopup();
        }

        ImGui::Separator();

        for (size_t i = 0; i < ComponentPropertyTables.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::CollapsingHeader(ComponentStructs[i]->GetName().c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ComponentPropertyTables[i]->DrawTree();
            }
            ImGui::PopID();
        }
    }

    void FPrefabEditorTool::DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize)
    {
        // Cycle gizmo op on Space, like the world editor.
        if (bViewportHovered && ImGui::IsKeyPressed(ImGuiKey_Space))
        {
            switch (GuizmoOp)
            {
                case ImGuizmo::TRANSLATE: GuizmoOp = ImGuizmo::ROTATE;    break;
                case ImGuizmo::ROTATE:    GuizmoOp = ImGuizmo::SCALE;     break;
                case ImGuizmo::SCALE:     GuizmoOp = ImGuizmo::TRANSLATE; break;
                default:                  GuizmoOp = ImGuizmo::TRANSLATE; break;
            }
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

        entt::entity PivotEntity = GetLastSelectedEntity();
        if (PivotEntity != entt::null && World->GetEntityRegistry().valid(PivotEntity))
        {
            STransformComponent* PivotTransform = World->GetEntityRegistry().try_get<STransformComponent>(PivotEntity);
            if (PivotTransform != nullptr)
            {
                glm::mat4 EntityMatrix = PivotTransform->GetWorldMatrix();
                glm::mat4 PreManipulate = EntityMatrix;

                ImGuizmo::Manipulate(glm::value_ptr(ViewMatrix), glm::value_ptr(ProjectionMatrix),
                    GuizmoOp, GuizmoMode, glm::value_ptr(EntityMatrix), nullptr, nullptr);

                if (ImGuizmo::IsUsing())
                {
                    if (!bImGuizmoUsedOnce)
                    {
                        bImGuizmoUsedOnce = true;
                    }

                    glm::mat4 LocalMatrix = EntityMatrix;
                    FRelationshipComponent* Rel = World->GetEntityRegistry().try_get<FRelationshipComponent>(PivotEntity);
                    if (Rel && Rel->Parent != entt::null)
                    {
                        STransformComponent& ParentTransform = World->GetEntityRegistry().get<STransformComponent>(Rel->Parent);
                        LocalMatrix = glm::inverse(ParentTransform.GetWorldMatrix()) * EntityMatrix;
                    }

                    glm::vec3 T, S, Skew;
                    glm::quat R;
                    glm::vec4 Persp;
                    glm::decompose(LocalMatrix, S, R, T, Skew, Persp);

                    PivotTransform->SetLocalLocation(T);
                    PivotTransform->SetLocalRotation(R);
                    PivotTransform->SetLocalScale(S);

                    Asset->GetPackage()->MarkDirty();
                }
                else if (bImGuizmoUsedOnce)
                {
                    bImGuizmoUsedOnce = false;
                }
            }
        }
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
            ImGui::EndMenu();
        }
    }

    void FPrefabEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGuiID LeftDockID = 0, RightDockID = 0;

        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.3f, &RightDockID, &LeftDockID);

        ImGuiID LeftOutlinerID = 0, LeftViewportID = 0;
        ImGui::DockBuilderSplitNode(LeftDockID, ImGuiDir_Left, 0.25f, &LeftOutlinerID, &LeftViewportID);

        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(), LeftViewportID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(OutlinerWindowName).c_str(), LeftOutlinerID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(PropertiesWindowName).c_str(), RightDockID);
    }
}
