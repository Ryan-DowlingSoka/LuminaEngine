#include "MeshEditorTool.h"

#include "ImGuiDrawUtils.h"
#include "Core/Object/Cast.h"
#include "glm/glm.hpp"
#include "glm/gtx/string_cast.hpp"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "world/entity/components/environmentcomponent.h"
#include "World/Entity/Components/LightComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "world/entity/components/velocitycomponent.h"
#include "World/Scene/RenderScene/SceneRenderTypes.h"


namespace Lumina
{
    FStaticMeshEditorTool::FStaticMeshEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset, NewObject<CWorld>())
    {
    }

    void FStaticMeshEditorTool::OnInitialize()
    {
        CreateToolWindow(MeshPropertiesName, [&](bool bFocused)
        {
            CStaticMesh* StaticMesh = Cast<CStaticMesh>(Asset.Get());
            if (!StaticMesh)
            {
                return;
            }
    
            FMeshResource& Resource = StaticMesh->GetMeshResource();
            const FAABB& BoundingBox = StaticMesh->GetAABB();
            
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Mesh Statistics");
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
            
            if (ImGui::BeginTable("##MeshStats", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
    
                auto PropertyRow = [](const char* label, const FString& value, const ImVec4* color = nullptr)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(label);
                    ImGui::TableSetColumnIndex(1);
                    if (color)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, *color);
                    }
                    ImGui::TextUnformatted(value.c_str());
                    if (color)
                    {
                        ImGui::PopStyleColor();
                    }
                };
    
                PropertyRow("Vertices", eastl::to_string(Resource.GetNumVertices()));
                PropertyRow("Meshlets", eastl::to_string(Resource.MeshletData.Meshlets.size()));
                PropertyRow("Surfaces", eastl::to_string(Resource.GetNumSurfaces()));
                
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Dummy(ImVec2(0, 4));
                
                const float vertexSizeKB     = (Resource.GetNumVertices() * Resource.GetVertexTypeSize()) / 1024.0f;
                const float meshletSizeKB    = (Resource.MeshletData.Meshlets.size() * sizeof(FMeshlet)
                                              + Resource.MeshletData.MeshletBounds.size() * sizeof(FMeshletBounds)
                                              + Resource.MeshletData.MeshletVertices.size() * sizeof(uint32)
                                              + Resource.MeshletData.MeshletTriangles.size() * sizeof(uint32)) / 1024.0f;
                const float totalSizeKB      = vertexSizeKB + meshletSizeKB;

                PropertyRow("Vertex Buffer", eastl::to_string(static_cast<int>(vertexSizeKB)) + " KB");
                PropertyRow("Meshlet Data",  eastl::to_string(static_cast<int>(meshletSizeKB)) + " KB");

                ImVec4 totalColor = totalSizeKB > 1024 ? ImVec4(1.0f, 0.7f, 0.3f, 1.0f) : ImVec4(0.7f, 1.0f, 0.7f, 1.0f);
                PropertyRow("Total Memory", eastl::to_string(static_cast<int>(totalSizeKB)) + " KB", &totalColor);
                
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Dummy(ImVec2(0, 4));
                
                PropertyRow("Bounds Min", glm::to_string(BoundingBox.Min).c_str());
                PropertyRow("Bounds Max", glm::to_string(BoundingBox.Max).c_str());
                
                glm::vec3 extents = BoundingBox.Max - BoundingBox.Min;
                PropertyRow("Bounds Extents", glm::to_string(extents).c_str());
    
                ImGui::EndTable();
            }
    
            ImGui::Spacing();
            ImGui::SeparatorText("Geometry Tools");
            ImGui::Spacing();
            
            ImGui::TextDisabled("UV Tools");
            ImGui::Spacing();
            
            if (ImGui::Button("Flip Vertical##UV", ImVec2(150, 0)))
            {
                Task::ParallelFor(Resource.GetNumVertices(), [&](uint32 Index)
                {
                    glm::vec2 UV = Resource.GetUVAt(Index);
                    UV.y = 1.0f - UV.y;
                    Resource.SetUVAt(Index, UV);
                });
                StaticMesh->PostLoad();
                Asset->GetPackage()->MarkDirty();
            }
            ImGuiX::TextTooltip("Flip UVs along the vertical axis.");
            
            ImGui::SameLine();
            
            if (ImGui::Button("Flip Horizontal##UV", ImVec2(150, 0)))
            {
                Task::ParallelFor(Resource.GetNumVertices(), [&](uint32 Index)
                {
                    glm::vec2 UV = Resource.GetUVAt(Index);
                    UV.x = 1.0f - UV.x;
                    Resource.SetUVAt(Index, UV);
                });
                StaticMesh->PostLoad();
                Asset->GetPackage()->MarkDirty();
            }
            ImGuiX::TextTooltip("Flip UVs along the horizontal axis.");
            
            ImGui::Spacing();
            ImGui::TextDisabled("Normal Tools");
            ImGui::Spacing();
            
            if (ImGui::Button("Flip Normals##Normals", ImVec2(150, 0)))
            {
                Task::ParallelFor(Resource.GetNumVertices(), [&](uint32 Index)
                {
                    glm::vec3 Normal = UnpackNormal(Resource.GetNormalAt(Index));
                    Resource.SetNormalAt(Index, PackNormal(-Normal));
                });
                StaticMesh->PostLoad();
                Asset->GetPackage()->MarkDirty();
            }
            ImGuiX::TextTooltip("Invert all vertex normals.");
            
            ImGui::Spacing();
            ImGui::TextDisabled("Transform Tools");
            ImGui::Spacing();
            
            if (ImGui::Button("Swap Y/Z##Transform", ImVec2(150, 0)))
            {
                Task::ParallelFor(Resource.GetNumVertices(), [&](uint32 Index)
                {
                    glm::vec3 Position = Resource.GetPositionAt(Index);
                    std::swap(Position.y, Position.z);
                    Resource.SetPositionAt(Index, Position);
            
                    glm::vec3 Normal = UnpackNormal(Resource.GetNormalAt(Index));
                    std::swap(Normal.y, Normal.z);
                    Resource.SetNormalAt(Index, PackNormal(Normal));
                });
                StaticMesh->PostLoad();
                Asset->GetPackage()->MarkDirty();
            }
            ImGuiX::TextTooltip("Swap Y and Z axes. Useful for correcting up-axis differences between tools.");
            
            ImGui::SameLine();
            
            if (ImGui::Button("Flip X Axis##Transform", ImVec2(150, 0)))
            {
                Task::ParallelFor(Resource.GetNumVertices(), [&](uint32 Index)
                {
                    glm::vec3 Position = Resource.GetPositionAt(Index);
                    Position.x = -Position.x;
                    Resource.SetPositionAt(Index, Position);
            
                    glm::vec3 Normal = UnpackNormal(Resource.GetNormalAt(Index));
                    Normal.x = -Normal.x;
                    Resource.SetNormalAt(Index, PackNormal(Normal));
                });
                StaticMesh->PostLoad();
                Asset->GetPackage()->MarkDirty();
            }
            ImGuiX::TextTooltip("Mirror the mesh along the X axis.");
            
            ImGui::Spacing();
            
            ImGui::Spacing();
            ImGui::TextDisabled("Pivot Tools");
            ImGui::Spacing();
            
            static float RotationAngles[3] = { 0.0f, 0.0f, 0.0f };
            
            ImGui::SetNextItemWidth(200.0f);
            ImGui::DragFloat3("Rotation##Transform", RotationAngles, 1.0f, -360.0f, 360.0f, "%.1f");
            
            if (ImGui::Button("Apply Rotation##Transform", ImVec2(150, 0)))
            {
                glm::mat4 RotX = glm::rotate(glm::mat4(1.0f), glm::radians(RotationAngles[0]), glm::vec3(1, 0, 0));
                glm::mat4 RotY = glm::rotate(glm::mat4(1.0f), glm::radians(RotationAngles[1]), glm::vec3(0, 1, 0));
                glm::mat4 RotZ = glm::rotate(glm::mat4(1.0f), glm::radians(RotationAngles[2]), glm::vec3(0, 0, 1));
                glm::mat4 Rotation = RotZ * RotY * RotX;
                glm::mat3 NormalMatrix = glm::transpose(glm::inverse(glm::mat3(Rotation)));

                Task::ParallelFor(Resource.GetNumVertices(), [&](uint32 Index)
                {
                    glm::vec3 Position = Resource.GetPositionAt(Index);
                    Resource.SetPositionAt(Index, glm::vec3(Rotation * glm::vec4(Position, 1.0f)));

                    glm::vec3 Normal = UnpackNormal(Resource.GetNormalAt(Index));
                    Resource.SetNormalAt(Index, PackNormal(glm::normalize(NormalMatrix * Normal)));
                });

                RotationAngles[0] = RotationAngles[1] = RotationAngles[2] = 0.0f;

                StaticMesh->PostLoad();
                Asset->GetPackage()->MarkDirty();
            }
            
            ImGuiX::TextTooltip("Apply a rotation to all vertices and normals.");
            
            ImGui::SameLine();
            
            if (ImGui::Button("Reset##Rotation", ImVec2(60, 0)))
            {
                RotationAngles[0] = RotationAngles[1] = RotationAngles[2] = 0.0f;
            }
            ImGuiX::TextTooltip("Reset rotation values to zero.");
            
            ImGui::SeparatorText("Geometry Data");
            
            ImGui::Spacing();
            
            if (ImGui::BeginTabBar("##GeometryTabs"))
            {
                if (ImGui::BeginTabItem("Vertices"))
                {
                    ImGui::Text("Total Vertices: %zu", Resource.GetNumVertices());
                    ImGui::Spacing();
                    
                    if (ImGui::BeginTable("##Vertices", 5, 
                        ImGuiTableFlags_Borders | 
                        ImGuiTableFlags_RowBg | 
                        ImGuiTableFlags_ScrollY | 
                        ImGuiTableFlags_SizingFixedFit,
                        ImVec2(0, 400)))
                    {
                        ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 250.0f);
                        ImGui::TableSetupColumn("Normal", ImGuiTableColumnFlags_WidthFixed, 250.0f);
                        ImGui::TableSetupColumn("UV", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 250.0f);
                        ImGui::TableHeadersRow();
                        
                        ImGuiListClipper clipper;
                        clipper.Begin(static_cast<int>(Resource.GetNumVertices()));
                        
                        while (clipper.Step())
                        {
                            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                            {
                                const glm::vec3& Position = Resource.GetPositionAt(i);
                                glm::vec3 Normal = UnpackNormal(Resource.GetNormalAt(i));
                                glm::vec2 UV = Resource.GetUVAt(i);
                                glm::vec4 Color = UnpackColor(Resource.GetColorAt(i));
                                
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%d", i);
                                
                                ImGui::TableSetColumnIndex(1);
                                ImGui::Text("%.3f, %.3f, %.3f", Position.x, Position.y, Position.z);
                                
                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%.3f, %.3f, %.3f", Normal.x, Normal.y, Normal.z);
                                
                                ImGui::TableSetColumnIndex(3);
                                ImGui::Text("%.3f, %.3f", UV.x, UV.y);
                                
                                ImGui::TableSetColumnIndex(4);
                                ImGui::Text("%f, %f, %f", Color.x, Color.y, Color.z);  
                            }
                        }
                        
                        ImGui::EndTable();
                    }
                    
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Indices"))
                {
                    ImGui::Text("Total Indices: %zu (Triangles: %zu)", 
                        Resource.Indices.size(), Resource.Indices.size() / 3);
                    ImGui::Spacing();
                    
                    static bool bShowAsTriangles = true;
                    ImGui::Checkbox("Show as Triangles", &bShowAsTriangles);
                    ImGui::Spacing();
                    
                    if (bShowAsTriangles)
                    {
                        if (ImGui::BeginTable("##Triangles", 4,
                            ImGuiTableFlags_Borders | 
                            ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_ScrollY | 
                            ImGuiTableFlags_SizingFixedFit,
                            ImVec2(0, 400)))
                        {
                            ImGui::TableSetupColumn("Triangle", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableSetupColumn("Index 0", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableSetupColumn("Index 1", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableSetupColumn("Index 2", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableHeadersRow();
                            
                            const int triangleCount = static_cast<int>(Resource.Indices.size() / 3);
                            ImGuiListClipper clipper;
                            clipper.Begin(triangleCount);
                            
                            while (clipper.Step())
                            {
                                for (int tri = clipper.DisplayStart; tri < clipper.DisplayEnd; tri++)
                                {
                                    const int baseIdx = tri * 3;
                                    
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::Text("%d", tri);
                                    
                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::Text("%u", Resource.Indices[baseIdx + 0]);
                                    
                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::Text("%u", Resource.Indices[baseIdx + 1]);
                                    
                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::Text("%u", Resource.Indices[baseIdx + 2]);
                                }
                            }
                            
                            ImGui::EndTable();
                        }
                    }
                    else
                    {
                        // Show raw index list
                        if (ImGui::BeginTable("##IndicesList", 2,
                            ImGuiTableFlags_Borders | 
                            ImGuiTableFlags_RowBg | 
                            ImGuiTableFlags_ScrollY | 
                            ImGuiTableFlags_SizingFixedFit,
                            ImVec2(0, 400)))
                        {
                            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableHeadersRow();
                            
                            ImGuiListClipper clipper;
                            clipper.Begin(static_cast<int>(Resource.Indices.size()));
                            
                            while (clipper.Step())
                            {
                                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                                {
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::Text("%d", i);
                                    
                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::Text("%u", Resource.Indices[i]);
                                }
                            }
                            
                            ImGui::EndTable();
                        }
                    }
                    
                    ImGui::EndTabItem();
                }
                
                ImGui::EndTabBar();
            }
            
            ImGui::Spacing();
            ImGui::Spacing();
    
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Geometry Surfaces");
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
            
            if (Resource.GeometrySurfaces.empty())
            {
                ImGui::TextDisabled("No surfaces defined");
            }
            else
            {
                for (size_t i = 0; i < Resource.GeometrySurfaces.size(); ++i)
                {
                    const FGeometrySurface& Surface = Resource.GeometrySurfaces[i];
                    ImGui::PushID(&Surface);
                    
                    FString HeaderLabel = "Surface " + eastl::to_string(i) + ": " + Surface.ID.ToString();
                    if (ImGui::CollapsingHeader(HeaderLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Indent(16.0f);
                        
                        if (ImGui::BeginTable("##SurfaceDetails", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
                        {
                            ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                            ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);
                            
                            auto DetailRow = [](const char* label, const FString& value)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", label);
                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextUnformatted(value.c_str());
                            };
                            
                            DetailRow("Material Index:", eastl::to_string(Surface.MaterialIndex));
                            DetailRow("Start Index:", eastl::to_string(Surface.StartIndex));
                            DetailRow("Index Count:", eastl::to_string(Surface.IndexCount));
                            DetailRow("Triangle Count:", eastl::to_string(Surface.IndexCount / 3));
                            
                            ImGui::EndTable();
                        }
                        
                        ImGui::Unindent(16.0f);
                    }
                    
                    ImGui::PopID();
                }
            }
    
            ImGui::Spacing();
    
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Asset Details");
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
            PropertyTable.DrawTree();
        });
    }

    void FStaticMeshEditorTool::SetupWorldForTool()
    {
        FEditorTool::SetupWorldForTool();
        
        DirectionalLightEntity = World->ConstructEntity("Directional Light");
        World->GetEntityRegistry().emplace<SDirectionalLightComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SEnvironmentComponent>(DirectionalLightEntity);
        
        CStaticMesh* StaticMesh = Cast<CStaticMesh>(Asset.Get());
        
        World->GetEntityRegistry().get<SVelocityComponent>(EditorEntity).Speed = 5.0f;

        MeshEntity = World->ConstructEntity("MeshEntity");
        World->GetEntityRegistry().emplace<SStaticMeshComponent>(MeshEntity).StaticMesh = StaticMesh;
        STransformComponent& MeshTransform = World->GetEntityRegistry().get<STransformComponent>(MeshEntity);
        
        float FloorY = MeshTransform.GetLocation().y + StaticMesh->GetAABB().Min.y;
        CreateFloorPlane(FloorY);
        
        STransformComponent& EditorTransform = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);

        glm::quat Rotation = Math::FindLookAtRotation(MeshTransform.GetLocation() + glm::vec3(0.0f, 0.85f, 0.0f), EditorTransform.GetLocation());
        EditorTransform.SetRotation(Rotation);
    }

    void FStaticMeshEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::Update(UpdateContext);
        
        if (bShowAABB && World.IsValid())
        {
            SStaticMeshComponent& StaticMeshComponent = World->GetEntityRegistry().get<SStaticMeshComponent>(MeshEntity);
            STransformComponent& Transform = World->GetEntityRegistry().get<STransformComponent>(MeshEntity);

            FAABB AABB = StaticMeshComponent.StaticMesh->GetAABB().ToWorld(Transform.GetWorldMatrix());
            
            World->DrawBox(AABB.GetCenter(), AABB.GetSize() * 0.5f, glm::quat(1, 0, 0, 0), FColor::Green);
        }
    }

    void FStaticMeshEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FStaticMeshEditorTool::OnAssetLoadFinished()
    {
    }

    void FStaticMeshEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::DrawToolMenu(UpdateContext);
        
        // Gizmo Control Dropdown
        if (ImGui::BeginMenu(LE_ICON_MOVE_RESIZE " Gizmo Control"))
        {
            const char* operations[] = { "Translate", "Rotate", "Scale" };
            static int currentOp = 0;

            if (ImGui::Combo("##", &currentOp, operations, IM_ARRAYSIZE(operations)))
            {
                switch (currentOp)
                {
                case 0: GuizmoOp = ImGuizmo::TRANSLATE; break;
                case 1: GuizmoOp = ImGuizmo::ROTATE;    break;
                case 2: GuizmoOp = ImGuizmo::SCALE;     break;
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(LE_ICON_DEBUG_STEP_INTO " Mesh Debug"))
        {
            ImGui::Checkbox(LE_ICON_CUBE_OUTLINE " Show AABB", &bShowAABB);
            
            if (ImGui::Button(LE_ICON_RELOAD " Reload Mesh Buffers"))
            {
                Cast<CStaticMesh>(Asset.Get())->PostLoad();
            }

            ImGui::EndMenu();
        }
    }

    void FStaticMeshEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGuiID leftDockID = 0, rightDockID = 0, bottomDockID = 0;

        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.3f, &rightDockID, &leftDockID);

        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Down, 0.3f, &bottomDockID, &InDockspaceID);

        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(), leftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(MeshPropertiesName.data()).c_str(), rightDockID);
    }
}
