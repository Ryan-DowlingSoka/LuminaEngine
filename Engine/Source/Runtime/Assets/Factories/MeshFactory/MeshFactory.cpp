#include "pch.h"
#include "MeshFactory.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "assets/assettypes/mesh/skeleton/skeleton.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Assets/Factories/TextureFactory/TextureFactory.h"
#include "Core/Object/Package/Package.h"
#include "Core/Utils/Defer.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "TaskSystem/TaskSystem.h"
#include "Tools/Import/ImportHelpers.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"


namespace Lumina
{
    bool CMeshFactory::DrawImportDialogue(const FFixedString& RawPath, const FFixedString& DestinationPath, TUniquePtr<Import::FImportSettings>& ImportSettings, bool& bShouldClose)
    {
        using namespace Import::Mesh;
        
        static FMeshImportOptions Options;
        
        FMeshImportData* ImportedData = nullptr;
        
        if (ImportSettings.get())
        {
            ImportedData = static_cast<FMeshImportData*>(ImportSettings.get());
        }
        
        bool bShouldImport = false;
        auto Reimport = [&]()
        {
            ImportSettings  = MakeUnique<FMeshImportData>();
            ImportedData    = static_cast<FMeshImportData*>(ImportSettings.get());
            
            FName FileExtension = VFS::Extension(RawPath);
            TExpected<FMeshImportData, FString> Expected;
            if (FileExtension == ".obj")
            {
                Expected = OBJ::ImportOBJ(Options, RawPath);
            }
            else if (FileExtension == ".gltf" || FileExtension == ".glb")
            {
                Expected = GLTF::ImportGLTF(Options, RawPath);
            }
            else if (FileExtension == ".fbx")
            {
                Expected = FBX::ImportFBX(Options, RawPath);
            }
            
            if (!Expected)
            {
                LOG_ERROR("Encountered problem importing GLTF: {0}", Expected.Error());
                bShouldImport = false;
                bShouldClose = true;
            }
                
            *ImportedData = Move(Expected.Value());
        };
        
        if (ImGui::IsWindowAppearing())
        {
            Reimport();
        }
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 8));
    
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::TextWrapped("Importing: %s", VFS::FileName(RawPath).data());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.4f, 0.6f, 0.8f, 0.8f));
        ImGui::SeparatorText("Import Options");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12, 10));
        if (ImGui::BeginTable("MeshImportOptionsTable", 2, 
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX))
        {
            ImGui::TableSetupColumn("Option", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        
            auto AddSectionHeader = [&](const char* Label)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.9f, 1.0f, 1.0f));
                ImGui::TextUnformatted(Label);
                ImGui::PopStyleColor();
                ImGui::TableSetColumnIndex(1);
                ImGui::Separator();
            };
        
            auto AddCheckboxRow = [&](const char* Icon, const char* Label, const char* Description, bool& Option)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                
                ImGui::Text("%s  %s", Icon, Label);
                if (Description && ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
                    ImGui::TextUnformatted(Description);
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                
                ImGui::TableSetColumnIndex(1);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
                if (ImGui::Checkbox(("##" + FString(Label)).c_str(), &Option))
                {
                    Reimport();
                }
            };
        
            auto AddSliderRow = [&](const char* Icon, const char* Label, const char* Description, 
                                    float& Value, float Min, float Max, const char* Format = "%.2f")
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                
                ImGui::Text("%s  %s", Icon, Label);
                if (Description && ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
                    ImGui::TextUnformatted(Description);
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                
                ImGui::TableSetColumnIndex(1);
                ImGui::PushItemWidth(-1);
                if (ImGui::DragFloat(("##" + FString(Label)).c_str(), &Value, 0.001f, Min, Max, Format))
                {
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        Reimport();
                    }
                }
                ImGui::PopItemWidth();
            };
        
            auto AddComboRow = [&](const char* Icon, const char* Label, const char* Description,
                                  const char* Items[], int ItemCount, int& CurrentItem)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                
                ImGui::Text("%s  %s", Icon, Label);
                if (Description && ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
                    ImGui::TextUnformatted(Description);
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                
                ImGui::TableSetColumnIndex(1);
                ImGui::PushItemWidth(-1);
                if (ImGui::Combo(("##" + FString(Label)).c_str(), &CurrentItem, Items, ItemCount))
                {
                    Reimport();
                }
                ImGui::PopItemWidth();
            };
            
            AddSectionHeader("Geometry");
            
            AddCheckboxRow(LE_ICON_ANIMATION, "Import Meshes", 
                "Import skeletal and static meshes from the FBX file", 
                Options.bImportMeshes);
            
            AddCheckboxRow(LE_ICON_SKULL, "Import Skeletons", 
                "Import skeleton hierarchies and bone data", 
                Options.bImportSkeleton);
            
            AddCheckboxRow(LE_ICON_BADGE_ACCOUNT, "Optimize Mesh", 
                "Optimize vertex cache locality and reduce overdraw for better rendering performance", 
                Options.bOptimize);
            
            AddSectionHeader("Animation");
            
            AddCheckboxRow(LE_ICON_ANIMATION, "Import Animations", 
                "Import skeletal and morph target animations", 
                Options.bImportAnimations);
            
            
            AddSectionHeader("Materials & Textures");
            
            AddCheckboxRow(LE_ICON_MATERIAL_DESIGN, "Import Materials", 
                "Import material definitions and create material assets", 
                Options.bImportMaterials);
            
            if (!Options.bImportMaterials)
            {
                AddCheckboxRow(LE_ICON_TEXTURE, "Import Textures", 
                    "Import texture files referenced by the FBX", 
                    Options.bImportTextures);
            }
            
            AddSectionHeader("Transform");
            
            AddSliderRow(LE_ICON_RESIZE, "Scale", 
                "Uniform scale factor applied to all imported geometry", 
                Options.Scale, 0.001f, 100.0f, "%.3f");
            
            
            AddCheckboxRow(LE_ICON_FLIP_VERTICAL, "Flip UVs", 
                "Flip UV coordinates vertically (1 - V)", 
                Options.bFlipUVs);
            
            AddCheckboxRow(LE_ICON_FLIP_HORIZONTAL, "Flip Normals", 
                "Invert mesh normals (useful for inside-out geometry)", 
                Options.bFlipNormals);
            
            AddSectionHeader("Advanced");

            AddCheckboxRow(LE_ICON_ANIMATION, "Merge Meshes",
                "Combine every mesh in the source file into a single asset. "
                "Primitives that share a source material are folded onto the "
                "same material slot. Ideal for kit-bash scenes where thousands "
                "of parts should import as one unit.",
                Options.bMergeMeshes);

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    
        if (!ImportedData->Resources.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.6f, 0.8f, 0.4f, 0.8f));
            ImGui::SeparatorText(LE_ICON_ACCOUNT_BOX "Import Statistics");
            ImGui::PopStyleColor();
            ImGui::Spacing();
    
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10, 6));
            if (ImGui::BeginTable("ImportMeshStats", 6, 
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 150)))
            {
                ImGui::TableSetupColumn("Mesh Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Vertices", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Indices", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Surfaces", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Overdraw", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("V-Fetch", ImGuiTableColumnFlags_WidthFixed, 80);
                
                ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.3f, 0.4f, 0.5f, 1.0f));
                ImGui::TableHeadersRow();
                ImGui::PopStyleColor();
    
                auto DrawRow = [](const FMeshResource& Resource, const auto& Overdraw, const auto& VertexFetch)
                {
                    ImGui::TableNextRow();
                    
                    auto SetColoredColumn = [&]<typename ... T>(int idx, std::format_string<T...> fmt, T&&... value)
                    {
                        ImGui::TableSetColumnIndex(idx);
                        ImGuiX::Text(fmt, std::forward<T>(value)...);
                    };

                    auto SetColoredColumnWithColor = [&]<typename ... T>(int idx, ImVec4 color, std::format_string<T...> fmt, T&&... value)
                    {
                        ImGui::TableSetColumnIndex(idx);
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                        ImGuiX::Text(fmt, std::forward<T>(value)...);
                        ImGui::PopStyleColor();
                    };
    
                    SetColoredColumn(0, "{0}", Resource.Name.c_str());
                    
                    ImVec4 VertexColor = Resource.GetNumVertices() > 10000 ? ImVec4(1,0.7f,0.3f,1) : ImVec4(0.7f,1,0.7f,1);
                    SetColoredColumnWithColor(1, VertexColor,  "{0}", ImGuiX::FormatSize(Resource.GetNumVertices()));
                    SetColoredColumn(2, "{0}", ImGuiX::FormatSize(Resource.Indices.size()));
                    SetColoredColumn(3, "{0}", Resource.GeometrySurfaces.size());
                    
                    ImVec4 OverdrawColor = Overdraw.overdraw > 2.0f ? ImVec4(1,0.5f,0.5f,1) : ImVec4(0.8f,0.8f,0.8f,1);
                    SetColoredColumnWithColor(4, OverdrawColor, "{:.2f}", Overdraw.overdraw);
                    
                    ImVec4 FetchColor = VertexFetch.overfetch > 2.0f ? ImVec4(1,0.5f,0.5f,1) : ImVec4(0.8f,0.8f,0.8f,1);
                    SetColoredColumnWithColor(5, FetchColor, "{:.2f}", VertexFetch.overfetch);
                };
    
                for (size_t i = 0; i < ImportedData->Resources.size(); ++i)
                {
                    DrawRow(*ImportedData->Resources[i], ImportedData->MeshStatistics.OverdrawStatics[i], ImportedData->MeshStatistics.VertexFetchStatics[i]);
                }
    
                ImGui::EndTable();
            }
            
            ImGui::PopStyleVar();
    
            if (!ImportedData->Textures.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.8f, 0.6f, 0.8f, 0.8f));
                ImGui::SeparatorText(LE_ICON_ACCOUNT_BOX "Texture Preview");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 8));
                if (ImGui::BeginTable("ImportTextures", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
                {
                    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
                    
                    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.4f, 0.3f, 0.4f, 1.0f));
                    ImGui::TableHeadersRow();
                    ImGui::PopStyleColor();
    
                    ImGuiListClipper Clipper;
                    Clipper.Begin(((int)ImportedData->Textures.size()));
                    
                    TVector<FMeshImportImage> TextureVector;
                    TextureVector.assign(ImportedData->Textures.begin(), ImportedData->Textures.end());
                    
                    while (Clipper.Step())
                    {
                        for (int i = Clipper.DisplayStart; i < Clipper.DisplayEnd; i++)
                        {
                            const FMeshImportImage& Image = TextureVector[i];
                    
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                        
                            ImVec2 ImageSize(128.0f, 128.0f);
                            ImVec2 CursorPos = ImGui::GetCursorScreenPos();
                            ImGui::GetWindowDrawList()->AddRect(
                                CursorPos, 
                                ImVec2(CursorPos.x + ImageSize.x + 4, CursorPos.y + ImageSize.y + 4),
                                IM_COL32(100, 100, 100, 255), 2.0f);
                        
                            ImGui::SetCursorScreenPos(ImVec2(CursorPos.x + 2, CursorPos.y + 2));

                            if (Image.DisplayImage)
                            {
                                ImTextureRef Texture = ImGuiX::ToImTextureRef(Image.DisplayImage);
                                ImGui::Image(Texture, ImageSize);
                            }
                            ImGui::Dummy({});
                            ImGui::TableSetColumnIndex(1);
                            
                            if (!Image.RelativePath.empty())
                            {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                                ImGuiX::TextWrapped("{0}", Image.RelativePath);
                                ImGui::PopStyleColor();
                            }
                        }
                    }
                    
                    ImGui::EndTable();
                    ImGui::PopStyleVar();
                }
            }
            
            if (!ImportedData->Skeletons.empty())
            {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.8f, 0.6f, 0.8f, 0.8f));
                ImGui::SeparatorText(LE_ICON_ANIMATION "Skeletons Preview");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 8));
                if (ImGui::BeginTable("ImportSkeletons", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
                {
                    ImGui::TableSetupColumn("Import", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Bones", ImGuiTableColumnFlags_WidthStretch);
                    
                    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.4f, 0.3f, 0.4f, 1.0f));
                    ImGui::TableHeadersRow();
                    ImGui::PopStyleColor();
                    
                    for (const TUniquePtr<FSkeletonResource>& Skeleton : ImportedData->Skeletons)
                    {
                        ImGui::TableNextRow();
                        ImGui::PushID(Skeleton.get());
                        
                        ImGui::TableNextColumn();
                        bool bImport = Skeleton->bShouldImport;
                        if (ImGui::Checkbox("##import", &bImport))
                        {
                            Skeleton->bShouldImport = bImport;
                        }
                        
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", Skeleton->Name.c_str());
                        
                        ImGui::TableNextColumn();
                        if (ImGui::TreeNodeEx("Bone Hierarchy", ImGuiTreeNodeFlags_Framed))
                        {
                            auto DisplayBoneHierarchy = [&](int32 BoneIndex, int Depth, auto& Self) -> void
                            {
                                const FSkeletonResource::FBoneInfo& Bone = Skeleton->Bones[BoneIndex];
                                
                                FFixedString NodeName(FFixedString::CtorSprintf(), "%s (Index: %d)", Bone.Name.c_str(), BoneIndex);
                                if (ImGui::TreeNodeEx(NodeName.c_str()))
                                {
                                    TVector<int32> Children = Skeleton->GetChildBones(BoneIndex);
                                    for (int32 ChildIdx : Children)
                                    {
                                        Self(ChildIdx, Depth + 1, Self);
                                    }
                                    ImGui::TreePop();
                                }
                            };
                            
                            for (int32 RootIdx : Skeleton->GetRootBones())
                            {
                                DisplayBoneHierarchy(RootIdx, 0, DisplayBoneHierarchy);
                            }
                            
                            ImGui::TreePop();
                        }
                        
                        ImGui::PopID();
                    }
                    
                    ImGui::EndTable();
                }
                
                ImGui::PopStyleVar();
            }
            
            if (!ImportedData->Animations.empty())
            {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.8f, 0.6f, 0.8f, 0.8f));
                ImGui::SeparatorText(LE_ICON_ANIMATION "Animations Preview");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                
                for (size_t i = 0; i < ImportedData->Animations.size(); ++i)
                {
                    const TUniquePtr<FAnimationResource>& Animation = ImportedData->Animations[i];
                    
                    ImGui::PushID((int)i);
                    
                    bool bOpen = ImGui::TreeNodeEx(Animation->Name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
                    
                    if (bOpen)
                    {
                        ImGui::Indent();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                        ImGui::Text("Duration: %.2f seconds", Animation->Duration);
                        ImGui::Text("Total Channels: %zu", Animation->Channels.size());
                        ImGui::PopStyleColor();
                        ImGui::Spacing();
                        
                        if (!Animation->Channels.empty())
                        {
                            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6, 4));
                            if (ImGui::BeginTable("ChannelTable", 4, 
                                ImGuiTableFlags_Borders | 
                                ImGuiTableFlags_RowBg | 
                                ImGuiTableFlags_SizingStretchProp))
                            {
                                ImGui::TableSetupColumn("Bone", ImGuiTableColumnFlags_WidthStretch);
                                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                                ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                                ImGui::TableSetupColumn("Time Range", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                                
                                ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.35f, 0.25f, 0.35f, 1.0f));
                                ImGui::TableHeadersRow();
                                ImGui::PopStyleColor();
                                
                                for (const auto& Channel : Animation->Channels)
                                {
                                    ImGui::TableNextRow();
                                    
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%s", Channel.TargetBone.c_str());
                                    
                                    ImGui::TableNextColumn();
                                    const char* pathName = "Unknown";
                                    ImVec4 typeColor = ImVec4(1, 1, 1, 1);
                                    switch (Channel.TargetPath)
                                    {
                                        case FAnimationChannel::ETargetPath::Translation: 
                                            pathName = "Translation"; 
                                            typeColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
                                            break;
                                        case FAnimationChannel::ETargetPath::Rotation: 
                                            pathName = "Rotation"; 
                                            typeColor = ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
                                            break;
                                        case FAnimationChannel::ETargetPath::Scale: 
                                            pathName = "Scale"; 
                                            typeColor = ImVec4(1.0f, 0.7f, 0.4f, 1.0f);
                                            break;
                                        case FAnimationChannel::ETargetPath::Weights: 
                                            pathName = "Weights"; 
                                            typeColor = ImVec4(1.0f, 0.4f, 0.8f, 1.0f);
                                            break;
                                    }
                                    ImGui::TextColored(typeColor, "%s", pathName);
                                    
                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::Text("%zu", Channel.Timestamps.size());
                                    
                                    ImGui::TableSetColumnIndex(3);
                                    if (!Channel.Timestamps.empty())
                                    {
                                        float minTime = Channel.Timestamps.front();
                                        float maxTime = Channel.Timestamps.back();
                                        ImGui::Text("%.2f - %.2f", minTime, maxTime);
                                    }
                                    else
                                    {
                                        ImGui::TextDisabled("N/A");
                                    }
                                }
                                
                                ImGui::EndTable();
                            }
                            ImGui::PopStyleVar();
                        }
                        
                        ImGui::Unindent();
                        ImGui::TreePop();
                    }
                    
                    ImGui::PopID();
                    ImGui::Spacing();
                }
            }
        }
    
        ImGui::Separator();
    
        float buttonWidth = 100.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float totalWidth = (buttonWidth * 2) + spacing;
        float offsetX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;
        if (offsetX > 0)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 8));
        
        if (ImGui::Button("Import", ImVec2(buttonWidth, 0)))
        {
            bShouldImport = true;
            bShouldClose = true;
        }
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
    
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.4f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 8));
        
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
        {
            bShouldImport = false;
            bShouldClose = true;
        }
        
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
        
        return bShouldImport;
    }
    
    void CMeshFactory::TryImport(const FFixedString& RawPath, const FFixedString& DestinationPath, const Import::FImportSettings* Settings)
    {
        using namespace Import::Mesh;

        const FMeshImportData& ImportData = Settings->As<FMeshImportData>();
        
        FFixedString DestinationDir;
        FFixedString BaseName;
        const size_t LastSlashPos = DestinationPath.find_last_of('/');
        if (LastSlashPos == FFixedString::npos)
        {
            BaseName = DestinationPath;
        }
        else
        {
            DestinationDir = DestinationPath.substr(0, LastSlashPos + 1);
            BaseName       = DestinationPath.substr(LastSlashPos + 1, FFixedString::npos);
        }
        // Strip the source-file extension (.fbx/.gltf/...) from BaseName so
        // the resulting asset paths don't end up with the wrong extension.
        const size_t DotPos = BaseName.find_last_of('.');
        if (DotPos != FFixedString::npos)
        {
            BaseName = BaseName.substr(0, DotPos);
        }

        auto BuildPath = [&](FStringView Suffix) -> FFixedString
        {
            FFixedString Path = DestinationDir;
            Path.append(BaseName);
            if (!Suffix.empty())
            {
                Path.append("_");
                Path.append_convert(Suffix.data(), Suffix.length());
            }
            return Path;
        };

        // Avoid clobbering existing assets when the user is reimporting next
        // to a previous import, or when sub-asset names happen to collide.
        auto EnsureUniquePath = [](FFixedString Path) -> FFixedString
        {
            if (FindObject<CPackage>(Path) == nullptr)
            {
                return Path;
            }
            for (uint32 N = 1; N < 10000; ++N)
            {
                FFixedString Candidate = Path;
                Candidate.append("_");
                Candidate.append_convert(eastl::to_string(N));
                if (FindObject<CPackage>(Candidate) == nullptr)
                {
                    return Candidate;
                }
            }
            return Path;
        };
        
        TVector<CObject*> CreatedObjects;
        CreatedObjects.reserve(ImportData.Skeletons.size() + ImportData.Resources.size() + ImportData.Animations.size());

        TObjectPtr<CSkeleton> PrimarySkeleton;
        const bool bMultipleSkeletons = ImportData.Skeletons.size() > 1;

        for (size_t i = 0; i < ImportData.Skeletons.size(); ++i)
        {
            TUniquePtr<FSkeletonResource>& SkeletonRes = const_cast<TUniquePtr<FSkeletonResource>&>(ImportData.Skeletons[i]);
            if (!SkeletonRes || !SkeletonRes->bShouldImport)
            {
                continue;
            }

            // Multiple skeletons in one source file is rare but legal (e.g.
            // an FBX with several rigs); disambiguate by internal name.
            FFixedString SkeletonPath = bMultipleSkeletons
                ? BuildPath(SkeletonRes->Name.ToString())
                : BuildPath("Skeleton");
            SkeletonPath = EnsureUniquePath(SkeletonPath);

            CSkeleton* NewSkeleton = CreateNewOf<CSkeleton>(SkeletonPath);
            NewSkeleton->SetFlag(OF_NeedsPostLoad);
            NewSkeleton->SkeletonResource = Move(SkeletonRes);

            if (!PrimarySkeleton)
            {
                PrimarySkeleton = NewSkeleton;
            }
            CreatedObjects.push_back(NewSkeleton);
        }

        const bool bMultipleMeshes = ImportData.Resources.size() > 1;
        for (size_t i = 0; i < ImportData.Resources.size(); ++i)
        {
            TUniquePtr<FMeshResource>& MeshResource = const_cast<TUniquePtr<FMeshResource>&>(ImportData.Resources[i]);
            if (!MeshResource)
            {
                continue;
            }


            FFixedString MeshPath = bMultipleMeshes
                ? BuildPath(MeshResource->Name.ToString())
                : BuildPath({});
            MeshPath = EnsureUniquePath(MeshPath);

            CMesh* NewMesh = nullptr;
            if (!MeshResource->bSkinnedMesh)
            {
                NewMesh = CreateNewOf<CStaticMesh>(MeshPath);
            }
            else
            {
                CSkeletalMesh* NewSkeletalMesh = CreateNewOf<CSkeletalMesh>(MeshPath);
                if (PrimarySkeleton)
                {
                    NewSkeletalMesh->Skeleton = PrimarySkeleton;
                    if (!PrimarySkeleton->PreviewMesh)
                    {
                        PrimarySkeleton->PreviewMesh = NewSkeletalMesh;
                    }
                }
                NewMesh = NewSkeletalMesh;
            }

            NewMesh->SetFlag(OF_NeedsPostLoad);


            // Size the material slot array from the highest referenced
            // surface index, not the surface count: merge-mode imports
            // dedup primitives onto shared slots, and any importer that
            // references a sparse source index needs every slot up to it.
            size_t MaterialSlotCount = 0;
            bool   bAnyExplicitMaterial = false;
            for (const FGeometrySurface& Surface : MeshResource->GeometrySurfaces)
            {
                if (Surface.MaterialIndex >= 0)
                {
                    bAnyExplicitMaterial = true;
                    MaterialSlotCount = eastl::max(MaterialSlotCount, (size_t)Surface.MaterialIndex + 1);
                }
            }
            if (!bAnyExplicitMaterial)
            {
                MaterialSlotCount = MeshResource->GeometrySurfaces.size();
            }
            NewMesh->Materials.clear();
            NewMesh->Materials.resize(MaterialSlotCount);

            NewMesh->MeshResources = Move(MeshResource);
            CreatedObjects.push_back(NewMesh);
        }

        const bool bMultipleAnims = ImportData.Animations.size() > 1;
        for (size_t i = 0; i < ImportData.Animations.size(); ++i)
        {
            TUniquePtr<FAnimationResource>& Clip = const_cast<TUniquePtr<FAnimationResource>&>(ImportData.Animations[i]);
            if (!Clip)
            {
                continue;
            }

            FFixedString AnimPath = bMultipleAnims
                ? BuildPath(Clip->Name.ToString())
                : BuildPath("Animation");
            AnimPath = EnsureUniquePath(AnimPath);

            CAnimation* NewAnimation = CreateNewOf<CAnimation>(AnimPath);
            NewAnimation->SetFlag(OF_NeedsPostLoad);
            NewAnimation->AnimationResource = Move(Clip);
            NewAnimation->Skeleton = PrimarySkeleton;

            CreatedObjects.push_back(NewAnimation);
        }
        
        if (!ImportData.Textures.empty())
        {
            TVector<FMeshImportImage> Images(ImportData.Textures.begin(), ImportData.Textures.end());
            CTextureFactory* TextureFactory = CTextureFactory::StaticClass()->GetDefaultObject<CTextureFactory>();

            for (const FMeshImportImage& Texture : Images)
            {
                if (Texture.IsBytes())
                {
                    FFixedString QualifiedPath = Paths::Combine(Paths::Parent(DestinationPath), Texture.RelativePath);
                    if (!FindObject<CPackage>(QualifiedPath))
                    {
                        CPackage::AddPackageExt(QualifiedPath);
                        TextureFactory->Import({}, QualifiedPath, &Texture);
                    }
                }
                else
                {
                    FStringView ParentPath = VFS::Parent(RawPath, true);
                    FFixedString TexturePath;
                    TexturePath.append_convert(ParentPath.data(), ParentPath.length()).append("/").append_convert(Texture.RelativePath);
                    FStringView TextureFileName = VFS::FileName(TexturePath, true);

                    FFixedString QualifiedPath = DestinationDir;
                    QualifiedPath.append_convert(TextureFileName.data(), TextureFileName.length());

                    if (!FindObject<CPackage>(QualifiedPath))
                    {
                        CPackage::AddPackageExt(QualifiedPath);
                        TextureFactory->Import(TexturePath, QualifiedPath, nullptr);
                    }
                }
            }
        }
        
        for (CObject* Obj : CreatedObjects)
        {
            CPackage* Package = Obj->GetPackage();
            CPackage::SavePackage(Package, Package->GetPackagePath());
            FAssetRegistry::Get().AssetCreated(Obj);
        }
        
        for (auto It = CreatedObjects.rbegin(); It != CreatedObjects.rend(); ++It)
        {
            (*It)->ForceDestroyNow();
        }
    }
}
