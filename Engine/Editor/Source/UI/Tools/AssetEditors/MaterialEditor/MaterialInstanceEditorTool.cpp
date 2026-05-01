#include "MaterialInstanceEditorTool.h"

#include "Assets/AssetRegistry/AssetData.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "Assets/AssetTypes/Material/MaterialInstance.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "Core/Engine/Engine.h"
#include "Core/Object/Cast.h"
#include "Core/Object/ObjectArray.h"
#include "glm/gtc/type_ptr.inl"
#include "Core/Math/Math.h"
#include "Paths/Paths.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RHIGlobals.h"
#include "Thumbnails/ThumbnailManager.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "UI/Tools/ContentBrowserEditorTool.h"
#include "World/Entity/Components/EnvironmentComponent.h"
#include "World/Entity/Components/LightComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Entity/Components/CameraComponent.h"

namespace Lumina
{
    static const char* MaterialInstanceParametersName = "Material Parameters";

    FMaterialInstanceEditorTool::FMaterialInstanceEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset, NewObject<CWorld>())
        , MeshEntity()
        , DirectionalLightEntity()
    {
    }

    void FMaterialInstanceEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        CreateToolWindow(MaterialInstanceParametersName, [this](bool bFocused)
        {
            DrawParameterEditor(bFocused);
        });
    }

    void FMaterialInstanceEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FMaterialInstanceEditorTool::SetupWorldForTool()
    {
        FAssetEditorTool::SetupWorldForTool();

        DirectionalLightEntity = World->ConstructEntity("Directional Light");
        World->GetEntityRegistry().emplace<SDirectionalLightComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SEnvironmentComponent>(DirectionalLightEntity);

        MeshEntity = World->ConstructEntity("MeshEntity");
        SStaticMeshComponent& StaticMeshComponent = World->GetEntityRegistry().emplace<SStaticMeshComponent>(MeshEntity);
        StaticMeshComponent.StaticMesh = CThumbnailManager::Get().SphereMesh;

        STransformComponent& MeshTransform = World->GetEntityRegistry().get<STransformComponent>(MeshEntity);

        STransformComponent& EditorTransform = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);
        glm::quat Rotation = Math::FindLookAtRotation(MeshTransform.GetLocation(), EditorTransform.GetLocation());
        EditorTransform.SetRotation(Rotation);

        StaticMeshComponent.MaterialOverrides.push_back(CastAsserted<CMaterialInterface>(Asset.Get()));
    }

    void FMaterialInstanceEditorTool::OnAssetLoadFinished()
    {
    }

    void FMaterialInstanceEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
    }

    void FMaterialInstanceEditorTool::DrawParameterEditor(bool bFocused)
    {
        CMaterialInstance* Instance = Cast<CMaterialInstance>(Asset.Get());
        if (Instance == nullptr)
        {
            ImGui::TextUnformatted("Asset is not a material instance.");
            return;
        }

        // The asset's own properties (parent material, etc.).
        PropertyTable.DrawTree();
        ImGui::Spacing();
        ImGui::Separator();

        if (!Instance->Material.IsValid())
        {
            ImGui::TextUnformatted("Assign a parent Material to edit parameters.");
            return;
        }

        if (Instance->Parameters.empty())
        {
            ImGui::TextUnformatted("Parent material exposes no parameters.");
            return;
        }

        ImGui::PushStyleColor(ImGuiCol_Header, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);

        if (ImGui::CollapsingHeader("Parameter Overrides", ImGuiTreeNodeFlags_DefaultOpen))
        {
            constexpr ImGuiTableFlags TableFlags =
                ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_NoBordersInBodyUntilResize |
                ImGuiTableFlags_SizingFixedFit;

            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 8));

            if (ImGui::BeginTable("MaterialInstanceParamsTable", 3, TableFlags))
            {
                ImGui::TableSetupColumn("##Override", ImGuiTableColumnFlags_WidthFixed, 24);
                ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthFixed, 175);
                ImGui::TableSetupColumn("##Editor", ImGuiTableColumnFlags_WidthStretch);

                for (FMaterialParameter& Param : Instance->Parameters)
                {
                    ImGui::PushID(&Param);
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    bool bHasOverride = Instance->HasOverride(Param.ParameterName);
                    if (ImGui::Checkbox("##Override", &bHasOverride))
                    {
                        if (!bHasOverride)
                        {
                            Instance->RemoveOverride(Param.ParameterName);
                            Asset->GetPackage()->MarkDirty();
                        }
                        else
                        {
                            // Re-assert the current uniform value as an override so the user can immediately tweak it.
                            switch (Param.Type)
                            {
                            case EMaterialParameterType::Scalar:
                                if (Param.Index < MAX_SCALARS)
                                {
                                    Instance->SetScalarValue(Param.ParameterName, Instance->MaterialUniforms.Scalars[Param.Index]);
                                }
                                break;
                            case EMaterialParameterType::Vector:
                                if (Param.Index < MAX_VECTORS)
                                {
                                    Instance->SetVectorValue(Param.ParameterName, Instance->MaterialUniforms.Vectors[Param.Index]);
                                }
                                break;
                            case EMaterialParameterType::Texture:
                                // Re-assign the parent's default so an override entry exists; user can replace it via drag/drop.
                                if (Param.Index < Instance->Material->Textures.size())
                                {
                                    Instance->SetTextureValue(Param.ParameterName, Instance->Material->Textures[Param.Index].Get());
                                }
                                break;
                            }
                            Asset->GetPackage()->MarkDirty();
                        }
                    }

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    if (bHasOverride)
                    {
                        ImGui::TextUnformatted(Param.ParameterName.c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("%s", Param.ParameterName.c_str());
                    }

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();

                    // Disable editor when no override is active so the parent value shows but cannot be edited.
                    ImGui::BeginDisabled(!bHasOverride);

                    switch (Param.Type)
                    {
                    case EMaterialParameterType::Scalar:
                    {
                        if (Param.Index >= MAX_SCALARS)
                        {
                            ImGui::TextDisabled("Invalid index");
                            break;
                        }
                        float Value = Instance->MaterialUniforms.Scalars[Param.Index];
                        if (ImGui::DragFloat("##Scalar", &Value, 0.01f))
                        {
                            Instance->SetScalarValue(Param.ParameterName, Value);
                            Asset->GetPackage()->MarkDirty();
                        }
                        break;
                    }

                    case EMaterialParameterType::Vector:
                    {
                        if (Param.Index >= MAX_VECTORS)
                        {
                            ImGui::TextDisabled("Invalid index");
                            break;
                        }
                        glm::vec4 Value = Instance->MaterialUniforms.Vectors[Param.Index];
                        if (ImGui::ColorEdit4("##Vector", glm::value_ptr(Value), ImGuiColorEditFlags_Float))
                        {
                            Instance->SetVectorValue(Param.ParameterName, Value);
                            Asset->GetPackage()->MarkDirty();
                        }
                        break;
                    }

                    case EMaterialParameterType::Texture:
                    {
                        DrawTextureParameterColumn(Instance, Param, bHasOverride);
                        break;
                    }
                    }

                    ImGui::EndDisabled();
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
        }

        ImGui::PopStyleColor(3);
    }

    void FMaterialInstanceEditorTool::DrawTextureParameterColumn(CMaterialInstance* Instance, const FMaterialParameter& Param, bool bHasOverride)
    {
        // Resolve the texture currently in effect for this slot:
        //  1) explicit override on this instance, else
        //  2) parent material's default texture for the slot.
        CTexture* DisplayTexture = nullptr;
        for (FMaterialParameterOverride& O : Instance->Overrides)
        {
            if (O.ParameterName == Param.ParameterName && O.Type == EMaterialParameterType::Texture)
            {
                DisplayTexture = O.Texture.Get();
                break;
            }
        }
        if (DisplayTexture == nullptr && Param.Index < Instance->Material->Textures.size())
        {
            DisplayTexture = Instance->Material->Textures[Param.Index].Get();
        }

        const ImVec2 ThumbSize(64, 64);
        const ImVec2 ButtonSize(64 + ImGui::GetStyle().FramePadding.x * 2.0f,
                                64 + ImGui::GetStyle().FramePadding.y * 2.0f);

        // ImageButton gives us a real interactive item: click opens the picker, drag-drop targets work,
        // and double-click jumps to the texture asset editor.
        bool bClicked = false;
        if (DisplayTexture && DisplayTexture->TextureResource && DisplayTexture->TextureResource->RHIImage.IsValid())
        {
            bClicked = ImGui::ImageButton("##TexThumb",
                ImGuiX::ToImTextureRef(DisplayTexture->TextureResource->RHIImage),
                ThumbSize);
        }
        else
        {
            bClicked = ImGui::Button("(none)##TexThumb", ButtonSize);
        }

        // Drag-drop target — works on the ImageButton's hit rect even while the row is BeginDisabled.
        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(
                FContentBrowserEditorTool::FContentBrowserTileViewItem::DragDropID,
                ImGuiDragDropFlags_AcceptBeforeDelivery);

            if (Payload && Payload->IsDelivery())
            {
                const uintptr_t ValuePtr = *static_cast<uintptr_t*>(Payload->Data);
                const auto* PayloadItem = reinterpret_cast<FContentBrowserEditorTool::FContentBrowserTileViewItem*>(ValuePtr);
                if (PayloadItem && PayloadItem->IsAsset())
                {
                    if (FAssetData* AssetData = FAssetRegistry::Get().GetAssetByPath(PayloadItem->GetVirtualPath()))
                    {
                        if (CObject* Loaded = LoadObject<CObject>(AssetData->AssetGUID))
                        {
                            if (CTexture* DroppedTexture = Cast<CTexture>(Loaded))
                            {
                                Instance->SetTextureValue(Param.ParameterName, DroppedTexture);
                                Asset->GetPackage()->MarkDirty();
                            }
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Click → open searchable picker; respects the disabled state so click-to-pick is gated by override toggle.
        if (bClicked && bHasOverride)
        {
            TexturePickerFilter.Clear();
            ImGui::OpenPopup("##TexturePickerPopup");
        }

        ImGui::SameLine();
        ImGui::BeginGroup();

        // Name + clear button next to the thumbnail.
        if (DisplayTexture)
        {
            ImGui::TextUnformatted(DisplayTexture->GetName().c_str());
        }
        else
        {
            ImGui::TextDisabled("None");
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
        if (ImGui::SmallButton(LE_ICON_CLOSE_CIRCLE " Clear"))
        {
            Instance->SetTextureValue(Param.ParameterName, nullptr);
            Asset->GetPackage()->MarkDirty();
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::SmallButton(LE_ICON_FILE_SEARCH " Browse"))
        {
            TexturePickerFilter.Clear();
            ImGui::OpenPopup("##TexturePickerPopup");
        }

        ImGui::EndGroup();

        // Picker popup — searchable list of all CTexture assets in the registry.
        // Uses BeginPopup (not BeginCombo) because the row is wrapped in BeginDisabled
        // and we want the picker itself to remain interactive once opened.
        ImGui::SetNextWindowSize(ImVec2(360, 400));
        if (ImGui::BeginPopup("##TexturePickerPopup"))
        {
            // Re-enable input inside the popup; the parent table sits inside BeginDisabled.
            ImGui::EndDisabled();

            const ImGuiStyle& Style = ImGui::GetStyle();
            TexturePickerFilter.Draw("##Search", ImGui::GetContentRegionAvail().x - Style.FramePadding.x);

            if (ImGui::BeginChild("##TextureList", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None))
            {
                static const FName TextureClassName = "CTexture";

                TVector<FAssetData*> TextureAssets = FAssetRegistry::Get().FindByPredicate([](const FAssetData& Data)
                {
                    return Data.AssetClass == TextureClassName;
                });

                if (ImGui::BeginTable("##TexTable", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInner))
                {
                    ImGui::TableSetupColumn("##Thumb", ImGuiTableColumnFlags_WidthFixed, 42.0f);
                    ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch);

                    for (FAssetData* AssetData : TextureAssets)
                    {
                        if (!TexturePickerFilter.PassFilter(AssetData->AssetName.c_str()))
                        {
                            continue;
                        }

                        ImGui::PushID(AssetData);
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, 42.0f);
                        ImGui::TableSetColumnIndex(0);

                        const bool bSelected = ImGui::Selectable("##sel", false,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 42.0f));

                        ImGuiX::TextTooltip("{}", AssetData->Path);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted(AssetData->AssetName.c_str());

                        if (bSelected)
                        {
                            if (CObject* Loaded = LoadObject<CObject>(AssetData->AssetGUID))
                            {
                                if (CTexture* PickedTexture = Cast<CTexture>(Loaded))
                                {
                                    Instance->SetTextureValue(Param.ParameterName, PickedTexture);
                                    Asset->GetPackage()->MarkDirty();
                                }
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();

            // Restore disabled state expected by the surrounding row.
            ImGui::BeginDisabled(!bHasOverride);
            ImGui::EndPopup();
        }
    }

    void FMaterialInstanceEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);

        ImGuiID leftDockID = 0, rightDockID = 0;
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.4f, &rightDockID, &leftDockID);

        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(), leftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(MaterialInstanceParametersName).c_str(), rightDockID);
    }
}
