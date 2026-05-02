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
#include "World/Scene/RenderScene/SceneRenderTypes.h"


namespace Lumina
{
    // Sum the triangle counts of meshlets in [Offset, Offset + Count). Bounded
    // by Meshlets.size() defensively so a malformed asset can't run off the
    // end while we're rendering the editor UI.
    static uint32 SumTrianglesInRange(const TVector<FMeshlet>& Meshlets, uint32 Offset, uint32 Count)
    {
        uint32 Tris = 0;
        const uint32 End = glm::min(Offset + Count, (uint32)Meshlets.size());
        for (uint32 m = Offset; m < End; ++m)
        {
            Tris += Meshlets[m].TriangleCount;
        }
        return Tris;
    }

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

                // Maximum NumLODs across surfaces -- thumbnail-style summary
                // for the LOD ladder. Per-surface detail lives below.
                uint32 MaxLODsAcrossSurfaces = 0;
                for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
                {
                    MaxLODsAcrossSurfaces = glm::max(MaxLODsAcrossSurfaces, Surface.NumLODs);
                }
                PropertyRow("LOD Levels", eastl::to_string(MaxLODsAcrossSurfaces));
                
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
            
            ImGui::SeparatorText("Meshlets");
            ImGui::Spacing();

            const TVector<FMeshlet>&       Meshlets = Resource.MeshletData.Meshlets;
            const TVector<FMeshletBounds>& Bounds   = Resource.MeshletData.MeshletBounds;
            ImGui::Text("Total Meshlets: %zu", Meshlets.size());
            ImGui::Spacing();

            if (ImGui::BeginTable("##Meshlets", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                ImVec2(0, 300)))
            {
                ImGui::TableSetupColumn("Index",     ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Verts",     ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Tris",      ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Bounds (center, radius)", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                ImGuiListClipper Clipper;
                Clipper.Begin((int)Meshlets.size());
                while (Clipper.Step())
                {
                    for (int i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i)
                    {
                        const FMeshlet& M = Meshlets[i];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::Text("%d", i);
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%u", M.VertexCount);
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%u", M.TriangleCount);
                        ImGui::TableSetColumnIndex(3);
                        if (i < (int)Bounds.size())
                        {
                            const FMeshletBounds& B = Bounds[i];
                            ImGui::Text("(%.2f, %.2f, %.2f)  r=%.2f", B.Center.x, B.Center.y, B.Center.z, B.Radius);
                        }
                    }
                }
                ImGui::EndTable();
            }
            
            ImGui::Spacing();
            ImGui::Spacing();

            // LOD configuration + visualization. Lets the user override which
            // LOD the preview entity renders (forces all surfaces to the
            // chosen LOD) and tune per-LOD distance thresholds. The forced
            // LOD is pushed onto the SStaticMeshComponent each frame in
            // Update(); thresholds live on FGeometrySurface and persist with
            // the asset.
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Levels of Detail");
            ImGuiX::Font::PopFont();

            ImGui::Spacing();

            {
                // Preview combo: -1 = automatic, 0..MAX-1 = forced index.
                // Names built at runtime so the combo tracks MAX_MESH_LODS
                // without a parallel hand-maintained string list.
                char        LODNames[MAX_MESH_LODS][24];
                const char* PreviewItems[MAX_MESH_LODS + 1];
                PreviewItems[0] = "Automatic (distance)";
                for (uint32 i = 0; i < MAX_MESH_LODS; ++i)
                {
                    if (i == 0)
                    {
                        snprintf(LODNames[i], sizeof(LODNames[i]), "LOD %u (full detail)", i);
                    }
                    else
                    {
                        snprintf(LODNames[i], sizeof(LODNames[i]), "LOD %u", i);
                    }
                    PreviewItems[i + 1] = LODNames[i];
                }

                int PreviewItem = (PreviewLODIndex < 0) ? 0 : PreviewLODIndex + 1;
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::Combo("Preview LOD", &PreviewItem, PreviewItems, IM_ARRAYSIZE(PreviewItems)))
                {
                    PreviewLODIndex = (PreviewItem == 0) ? -1 : PreviewItem - 1;
                }
                ImGuiX::TextTooltip("Force the viewport mesh to a specific LOD regardless of camera distance.");
            }

            ImGui::Spacing();

            // Per-LOD aggregate stats across surfaces. Sums let you see how
            // much geometry each LOD costs at a glance without expanding
            // every surface.
            uint32 LODAggMeshlets[MAX_MESH_LODS]  = { 0 };
            uint32 LODAggTriangles[MAX_MESH_LODS] = { 0 };
            uint32 LODAggSurfaces[MAX_MESH_LODS]  = { 0 };
            for (const FGeometrySurface& Surface : Resource.GeometrySurfaces)
            {
                for (uint32 lod = 0; lod < Surface.NumLODs; ++lod)
                {
                    const uint32 MCnt = Surface.LODMeshletCount[lod];
                    LODAggMeshlets[lod]  += MCnt;
                    LODAggSurfaces[lod]  += (MCnt > 0) ? 1u : 0u;
                    LODAggTriangles[lod] += SumTrianglesInRange(Resource.MeshletData.Meshlets, Surface.LODMeshletOffset[lod], MCnt);
                }
            }

            const uint32 LOD0Tris = LODAggTriangles[0];

            if (ImGui::BeginTable("##LODSummary", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("LOD",       ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Surfaces",  ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Meshlets",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("vs LOD 0",  ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (uint32 lod = 0; lod < MAX_MESH_LODS; ++lod)
                {
                    if (LODAggSurfaces[lod] == 0)
                    {
                        continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%u", lod);
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%u", LODAggSurfaces[lod]);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%u", LODAggMeshlets[lod]);
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%u", LODAggTriangles[lod]);
                    ImGui::TableSetColumnIndex(4);
                    if (LOD0Tris > 0)
                    {
                        const float Ratio = (float)LODAggTriangles[lod] / (float)LOD0Tris;
                        ImGui::Text("%.1f%%", Ratio * 100.0f);
                    }
                    else
                    {
                        ImGui::TextDisabled("--");
                    }
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Distance thresholds (distance / radius). The renderer picks the highest LOD whose threshold the instance has exceeded.");
            ImGui::Spacing();

            // Aggregate threshold editor. Most assets share the same threshold
            // ramp across surfaces, so editing once here writes to every
            // surface in lockstep. Per-surface customization still works via
            // the surface details panel below.
            if (!Resource.GeometrySurfaces.empty())
            {
                float SharedThresholds[MAX_MESH_LODS];
                for (uint32 i = 0; i < MAX_MESH_LODS; ++i)
                {
                    SharedThresholds[i] = Resource.GeometrySurfaces[0].LODScreenThreshold[i];
                }

                bool bThresholdChanged = false;
                if (ImGui::BeginTable("##LODThresholds", 2,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("LOD",       ImGuiTableColumnFlags_WidthFixed, 50.0f);
                    ImGui::TableSetupColumn("Threshold", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (uint32 lod = 0; lod < MAX_MESH_LODS; ++lod)
                    {
                        if (LODAggSurfaces[lod] == 0)
                        {
                            continue;
                        }

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::Text("%u", lod);
                        ImGui::TableSetColumnIndex(1);

                        if (lod == 0)
                        {
                            ImGui::TextDisabled("0  (always active)");
                            continue;
                        }

                        ImGui::PushID((int)lod);
                        ImGui::SetNextItemWidth(-FLT_MIN);

                        float Value = SharedThresholds[lod];
                        // Min clamp = previous LOD's threshold + epsilon to
                        // keep the ramp monotonic. The pick loop's
                        // "first-miss-wins" rule degenerates without it.
                        const float MinValue = SharedThresholds[lod - 1] + 0.01f;
                        if (ImGui::DragFloat("##Threshold", &Value, 0.5f, MinValue, 1024.0f, "%.2f"))
                        {
                            SharedThresholds[lod] = glm::max(Value, MinValue);
                            bThresholdChanged = true;
                        }

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                if (bThresholdChanged)
                {
                    // Replicate to every surface; persist via package dirty.
                    for (FGeometrySurface& Surface : Resource.GeometrySurfaces)
                    {
                        for (uint32 i = 0; i < MAX_MESH_LODS; ++i)
                        {
                            Surface.LODScreenThreshold[i] = SharedThresholds[i];
                        }
                    }
                    Asset->GetPackage()->MarkDirty();
                }
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
                ImGui::TextDisabled("Click a surface to highlight its bounds in the viewport.");
                ImGui::Spacing();

                for (size_t i = 0; i < Resource.GeometrySurfaces.size(); ++i)
                {
                    const FGeometrySurface& Surface = Resource.GeometrySurfaces[i];
                    ImGui::PushID((int)i);

                    CMaterialInterface* Mat        = StaticMesh->GetMaterialAtSlot((size_t)Surface.MaterialIndex);
                    const FString       MaterialName = IsValid(Mat) ? Mat->GetName().ToString() : FString("(none)");

                    const bool   bSelected     = ((int32)i == SelectedSurfaceIndex);
                    const uint32 LOD0Meshlets  = Surface.LODMeshletCount[0];
                    FString      Label         = "Surface " + eastl::to_string(i)
                                               + "  |  " + MaterialName
                                               + "  |  " + eastl::to_string(Surface.IndexCount / 3) + " tris, "
                                               + eastl::to_string(LOD0Meshlets) + " meshlets";

                    if (ImGui::Selectable(Label.c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        SelectedSurfaceIndex = bSelected ? -1 : (int32)i;
                    }

                    if (bSelected)
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

                            const uint32 LOD0Off = Surface.LODMeshletOffset[0];
                            const uint32 LOD0Cnt = Surface.LODMeshletCount[0];

                            DetailRow("Material:",       MaterialName);
                            DetailRow("Material Index:", eastl::to_string(Surface.MaterialIndex));
                            DetailRow("Start Index:",    eastl::to_string(Surface.StartIndex));
                            DetailRow("Index Count:",    eastl::to_string(Surface.IndexCount));
                            DetailRow("Triangles:",      eastl::to_string(Surface.IndexCount / 3));
                            DetailRow("LOD 0 Range:",    eastl::to_string(LOD0Off) + " .. " + eastl::to_string(LOD0Off + LOD0Cnt));
                            DetailRow("LOD 0 Meshlets:", eastl::to_string(LOD0Cnt));
                            DetailRow("LOD Levels:",     eastl::to_string(Surface.NumLODs));

                            ImGui::EndTable();
                        }

                        // Per-LOD breakdown for this surface, with per-surface
                        // threshold overrides. Useful when one surface should
                        // pop to a coarser LOD earlier than the rest of the
                        // mesh (e.g. a small bolt vs. a big hull).
                        ImGui::Spacing();
                        if (ImGui::BeginTable("##SurfaceLODs", 5,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                        {
                            ImGui::TableSetupColumn("LOD",       ImGuiTableColumnFlags_WidthFixed, 50.0f);
                            ImGui::TableSetupColumn("Meshlets",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                            ImGui::TableSetupColumn("Range",     ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableSetupColumn("Threshold", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                            ImGui::TableHeadersRow();

                            FGeometrySurface&        SurfaceRW = Resource.GeometrySurfaces[i];
                            const TVector<FMeshlet>& MeshletsRef = Resource.MeshletData.Meshlets;

                            for (uint32 lod = 0; lod < SurfaceRW.NumLODs; ++lod)
                            {
                                const uint32 MOff = SurfaceRW.LODMeshletOffset[lod];
                                const uint32 MCnt = SurfaceRW.LODMeshletCount[lod];
                                const uint32 Tris = SumTrianglesInRange(MeshletsRef, MOff, MCnt);

                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0); ImGui::Text("%u", lod);
                                ImGui::TableSetColumnIndex(1); ImGui::Text("%u", MCnt);
                                ImGui::TableSetColumnIndex(2); ImGui::Text("%u", Tris);
                                ImGui::TableSetColumnIndex(3); ImGui::Text("%u .. %u", MOff, MOff + MCnt);
                                ImGui::TableSetColumnIndex(4);

                                if (lod == 0)
                                {
                                    ImGui::TextDisabled("0");
                                }
                                else
                                {
                                    ImGui::PushID((int)lod);
                                    ImGui::SetNextItemWidth(-FLT_MIN);
                                    float Value = SurfaceRW.LODScreenThreshold[lod];
                                    const float MinValue = SurfaceRW.LODScreenThreshold[lod - 1] + 0.01f;
                                    if (ImGui::DragFloat("##SurfaceThreshold", &Value, 0.5f, MinValue, 1024.0f, "%.2f"))
                                    {
                                        SurfaceRW.LODScreenThreshold[lod] = glm::max(Value, MinValue);
                                        Asset->GetPackage()->MarkDirty();
                                    }
                                    ImGui::PopID();
                                }
                            }

                            ImGui::EndTable();
                        }

                        ImGui::Unindent(16.0f);
                    }

                    ImGui::PopID();
                }

                // Defensive: an asset reload may shrink the surface list below
                // our selection. Clear it rather than draw garbage.
                if (SelectedSurfaceIndex >= (int32)Resource.GeometrySurfaces.size())
                {
                    SelectedSurfaceIndex = -1;
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
        
        World->GetRenderer()->GetSceneRenderSettings().bDrawBillboards = false;
        
        DirectionalLightEntity = World->ConstructEntity("Directional Light");
        World->GetEntityRegistry().emplace<SDirectionalLightComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SEnvironmentComponent>(DirectionalLightEntity);
        
        CStaticMesh* StaticMesh = Cast<CStaticMesh>(Asset.Get());

        CameraState.Speed = 5.0f;

        MeshEntity = World->ConstructEntity("MeshEntity");
        World->GetEntityRegistry().emplace<SStaticMeshComponent>(MeshEntity).StaticMesh = StaticMesh;
        STransformComponent& MeshTransform = World->GetEntityRegistry().get<STransformComponent>(MeshEntity);

        float FloorY = MeshTransform.GetLocation().y + StaticMesh->GetAABB().Min.y;
        CreateFloorPlane(FloorY);

        // Frame the mesh and default to orbit so the user can immediately tumble around it.
        const FAABB Bounds = StaticMesh->GetAABB();
        const glm::vec3 Center = MeshTransform.GetLocation() + Bounds.GetCenter();
        const float Radius = glm::max(glm::length(Bounds.GetSize() * 0.5f), 0.5f);
        SetOrbitTarget(Center, Radius * 3.0f);
        SetCameraMode(EEditorCameraMode::Orbit);
    }

    void FStaticMeshEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::Update(UpdateContext);

        if (!World.IsValid())
        {
            return;
        }

        SStaticMeshComponent& StaticMeshComponent = World->GetEntityRegistry().get<SStaticMeshComponent>(MeshEntity);
        STransformComponent&  Transform           = World->GetEntityRegistry().get<STransformComponent>(MeshEntity);

        // Push the editor's preview override onto the component every frame.
        // The renderer reads ForcedLODIndex during instance build, so this is
        // the cheapest way to keep the viewport in sync with the LOD combo
        // even as the user scrubs through values.
        StaticMeshComponent.ForcedLODIndex = PreviewLODIndex;

        if (bShowAABB)
        {
            FAABB AABB = StaticMeshComponent.StaticMesh->GetAABB().ToWorld(Transform.GetWorldMatrix());
            World->DrawBox(AABB.GetCenter(), AABB.GetSize() * 0.5f, glm::quat(1, 0, 0, 0), FColor::Green);
        }

        // Highlight the selected surface by deriving its AABB from the per-
        // meshlet LoInt range of whichever LOD is *currently being rendered*.
        // Forcing the LOD changes the meshlet set in the viewport, so the
        // overlay should follow -- LOD 0's bounds will look stale otherwise.
        if (SelectedSurfaceIndex >= 0 && IsValid(StaticMeshComponent.StaticMesh))
        {
            const FMeshResource& Resource = StaticMeshComponent.StaticMesh->GetMeshResource();
            if (SelectedSurfaceIndex < (int32)Resource.GeometrySurfaces.size())
            {
                const FGeometrySurface& Surface = Resource.GeometrySurfaces[SelectedSurfaceIndex];
                const FMeshletData&     MD      = Resource.MeshletData;

                // Pick the LOD whose meshlet range we'll bound. Auto preview
                // shows LOD 0; forced preview shows that LOD (clamped to
                // what the surface actually has).
                const uint32 OverlayLOD = (PreviewLODIndex >= 0 && Surface.NumLODs > 0)
                    ? (uint32)glm::min((int32)Surface.NumLODs - 1, PreviewLODIndex)
                    : 0u;

                const uint32 OverlayOffset = Surface.LODMeshletOffset[OverlayLOD];
                const uint32 OverlayCount  = Surface.LODMeshletCount[OverlayLOD];

                if (OverlayCount > 0 && !MD.Meshlets.empty())
                {
                    glm::vec3 Lo( FLT_MAX);
                    glm::vec3 Hi(-FLT_MAX);
                    const uint32 End = OverlayOffset + OverlayCount;
                    for (uint32 m = OverlayOffset; m < End; ++m)
                    {
                        const FMeshlet& Mesh = MD.Meshlets[m];
                        const glm::vec3 BoxLo = MD.MeshOrigin + glm::vec3(Mesh.LoInt) * MD.MeshGridStep;
                        const glm::vec3 BoxHi = BoxLo + glm::vec3(1023.0f) * MD.MeshGridStep;
                        Lo = glm::min(Lo, BoxLo);
                        Hi = glm::max(Hi, BoxHi);
                    }

                    FAABB SurfaceAABB;
                    SurfaceAABB.Min = Lo;
                    SurfaceAABB.Max = Hi;
                    SurfaceAABB     = SurfaceAABB.ToWorld(Transform.GetWorldMatrix());
                    World->DrawBox(SurfaceAABB.GetCenter(), SurfaceAABB.GetSize() * 0.5f, glm::quat(1, 0, 0, 0), FColor::Yellow, 2.0f);
                }
            }
        }
    }

    void FStaticMeshEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FStaticMeshEditorTool::DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize)
    {
        DrawCameraModeSelector();
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
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);

        ImGuiID leftDockID = 0, rightDockID = 0;
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.3f, &rightDockID, &leftDockID);

        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(), leftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(MeshPropertiesName.data()).c_str(), rightDockID);
    }
}
