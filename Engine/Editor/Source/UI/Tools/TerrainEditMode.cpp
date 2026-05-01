#include "TerrainEditMode.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ImGuizmo.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/TerrainComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Subsystems/TerrainSculptSystem.h"
#include "World/World.h"

namespace Lumina
{
    namespace
    {
        void BuildRayFromScreen(const SCameraComponent& Camera, ImVec2 PixelWithinViewport, ImVec2 ViewportSize, glm::vec3& OutOrigin, glm::vec3& OutDir)
        {
            const float W = std::max(ViewportSize.x, 1.0f);
            const float H = std::max(ViewportSize.y, 1.0f);

            const float Sx = (PixelWithinViewport.x / W) * 2.0f - 1.0f;
            // ImGui pixel.y = 0 at the top of the viewport; the ray must tilt
            // *up* in world for a top-of-screen cursor, so flip the sign here.
            // (BuildRayFromScreen used to read correct against an inverted Up
            // vector before the camera-basis fix in 5827d9d9.)
            const float Sy = 1.0f - (PixelWithinViewport.y / H) * 2.0f;

            const FViewVolume& View    = Camera.GetViewVolume();
            const glm::vec3    Forward = View.GetForwardVector();
            const glm::vec3    Up      = View.GetUpVector();

            const glm::vec3    Right   = glm::normalize(glm::cross(Up, Forward));

            const float AspectRatio = W / H;
            const float TanHalfFov  = std::tan(glm::radians(View.GetFOV()) * 0.5f);

            OutOrigin = Camera.GetPosition();
            OutDir    = glm::normalize(Forward
                                       + Right * (Sx * TanHalfFov * AspectRatio)
                                       + Up    * (Sy * TanHalfFov));
        }
    }

    entt::entity FTerrainEditMode::FindPreferredTerrain(CWorld* World) const
    {
        if (!World)
        {
            return entt::null;
        }
        auto View = World->GetEntityRegistry().view<STerrainComponent>();
        for (auto Entity : View)
        {
            return Entity;
        }
        return entt::null;
    }

    entt::entity FTerrainEditMode::CreateDefaultTerrain(CWorld* World)
    {
        if (!World)
        {
            return entt::null;
        }

        entt::entity Entity = World->ConstructEntity("Terrain");
        STerrainComponent& Terrain = World->GetEntityRegistry().emplace<STerrainComponent>(Entity);
        Terrain.Resolution      = 513;
        Terrain.ChunkResolution = 64;
        Terrain.TileWorldSize   = 4096.0f;
        Terrain.MaxHeight       = 256.0f;

        const int32 Res = Terrain.Resolution;
        const size_t N = static_cast<size_t>(Res) * static_cast<size_t>(Res);
        Terrain.Heightmap.assign(N, 0.0f);

        // Seed a gentle central mound so a freshly created terrain has visible silhouette immediately.
        const float Center = float(Res - 1) * 0.5f;
        const float FalloffRadius = float(Res) * 0.35f;
        for (int32 Y = 0; Y < Res; ++Y)
        {
            for (int32 X = 0; X < Res; ++X)
            {
                const float Dx = (static_cast<float>(X) - Center) / FalloffRadius;
                const float Dy = (static_cast<float>(Y) - Center) / FalloffRadius;
                const float D2 = Dx * Dx + Dy * Dy;
                const float H  = std::max(0.0f, 1.0f - D2);
                Terrain.Heightmap[static_cast<size_t>(Y) * static_cast<size_t>(Res) + static_cast<size_t>(X)] = H * 0.25f;
            }
        }

        // Seed four layers so a freshly created terrain can drive a four-input
        // TerrainLayerBlend out of the box. Weights start at zero; painting
        // increases them and the shader normalises the blend at sample time.
        static const char* LayerNames[4] = { "Layer0", "Layer1", "Layer2", "Layer3" };
        for (int i = 0; i < 4; ++i)
        {
            STerrainLayer& L = Terrain.Layers.emplace_back();
            L.Name    = LayerNames[i];
            L.UVScale = 1.0f / 16.0f;
        }

        Terrain.LayerWeights.assign(N * Terrain.Layers.size(), uint8(0));
        Terrain.GPUState.bFullHeightmapDirty = true;
        Terrain.GPUState.bFullWeightsDirty   = true;
        return Entity;
    }

    void FTerrainEditMode::DrawToolbar(CWorld* World, float ButtonSize)
    {
        if (!World)
        {
            return;
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        const char* Label = bActive ? "Exit Terrain Edit" : "Edit Terrain";
        ImGui::PushStyleColor(ImGuiCol_Button, bActive ? ImVec4(0.35f, 0.55f, 0.25f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(Label, ImVec2(0, ButtonSize)))
        {
            bActive = !bActive;
            if (bActive && FindPreferredTerrain(World) == entt::null)
            {
                CreateDefaultTerrain(World);
            }
        }
        ImGui::PopStyleColor();

        if (!bActive)
        {
            return;
        }

        ImGui::SameLine();
        if (ImGui::Button("+ Terrain", ImVec2(0, ButtonSize)))
        {
            CreateDefaultTerrain(World);
        }

        ImGui::SameLine();
        if (ImGui::Button(bShowSettings ? "Hide Brush" : "Brush", ImVec2(0, ButtonSize)))
        {
            bShowSettings = !bShowSettings;
        }

        if (!bShowSettings)
        {
            return;
        }
        
        ImVec2 AnchorPos = ImGui::GetWindowPos();
        AnchorPos.y += ImGui::GetWindowSize().y + 4.0f;
        ImGui::SetNextWindowPos(AnchorPos);
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("##TerrainBrushSettings", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

        const char* ModeNames[] = { "Sculpt", "Flatten", "Smooth", "Paint" };
        int ModeIndex = (int)Mode;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Mode", &ModeIndex, ModeNames, IM_ARRAYSIZE(ModeNames)))
        {
            Mode = (ETerrainBrushMode)ModeIndex;
        }

        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Radius",   &Radius,   8.0f,  4096.0f, "%.0f");
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Strength", &Strength, 0.0f,  256.0f,   "%.2f");
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Falloff",  &Falloff,  0.0f,  1.0f,    "%.2f");

        if (Mode == ETerrainBrushMode::Flatten)
        {
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("Height", &FlattenHeight, 0.0f, 1024.0f, "%.0f");
        }

        if (Mode == ETerrainBrushMode::Paint)
        {
            entt::entity TerrainEntity = FindPreferredTerrain(World);
            STerrainComponent* Terrain = (TerrainEntity != entt::null)
                ? &World->GetEntityRegistry().get<STerrainComponent>(TerrainEntity)
                : nullptr;

            const int32 LayerCount = Terrain ? (int32)Terrain->Layers.size() : 0;

            ImGui::SetNextItemWidth(140.0f);
            ImGui::InputInt("Layer", &ActiveLayer);
            ActiveLayer = std::max(ActiveLayer, 0);
            if (LayerCount > 0)
            {
                ActiveLayer = std::min(ActiveLayer, LayerCount - 1);
            }

            if (Terrain)
            {
                ImGui::Text("Layers: %d", LayerCount);
                ImGui::SameLine();
                if (ImGui::Button("+##AddLayer"))
                {
                    STerrainLayer& L = Terrain->Layers.emplace_back();
                    L.Name.sprintf("Layer%d", LayerCount);
                    L.UVScale = 1.0f / 16.0f;

                    Terrain->GPUState.bFullWeightsDirty = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("-##RemoveLayer") && LayerCount > 1)
                {
                    Terrain->Layers.pop_back();
                    Terrain->GPUState.bFullWeightsDirty = true;
                    if (ActiveLayer >= (int32)Terrain->Layers.size())
                    {
                        ActiveLayer = (int32)Terrain->Layers.size() - 1;
                    }
                }
            }
        }

        ImGui::End();
    }

    void FTerrainEditMode::Tick(CWorld* World, float DeltaSeconds, const SCameraComponent& Camera, bool bViewportHovered, ImVec2 ViewportScreenOrigin, ImVec2 ViewportSize)
    {
        bHitValid = false;
        if (!bActive || !World)
        {
            return;
        }

        entt::entity TerrainEntity = FindPreferredTerrain(World);
        if (TerrainEntity == entt::null)
        {
            return;
        }
        STerrainComponent& Terrain = World->GetEntityRegistry().get<STerrainComponent>(TerrainEntity);
        if (Terrain.Resolution < 2)
        {
            return;
        }

        // The renderer centers the terrain on the owning entity's transform, so the
        // sculpt system needs that origin to match sample coords to rendered world coords.
        const STransformComponent& TerrainTransform = World->GetEntityRegistry().get<STransformComponent>(TerrainEntity);
        const glm::vec3 TerrainOrigin = TerrainTransform.GetWorldLocation();

        if (!bViewportHovered)
        {
            return;
        }

        // Mouse position relative to the viewport image.
        const ImVec2 MousePos = ImGui::GetMousePos();
        const ImVec2 Local    = ImVec2(MousePos.x - ViewportScreenOrigin.x, MousePos.y - ViewportScreenOrigin.y);
        if (Local.x < 0.0f || Local.y < 0.0f || Local.x > ViewportSize.x || Local.y > ViewportSize.y)
        {
            return;
        }

        glm::vec3 RayOrigin, RayDir;
        BuildRayFromScreen(Camera, Local, ViewportSize, RayOrigin, RayDir);

        glm::vec3 Hit;
        if (!FTerrainSculptSystem::Raycast(Terrain, TerrainOrigin, RayOrigin, RayDir, Hit))
        {
            return;
        }

        LastHit   = Hit;
        bHitValid = true;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            return;
        }

        // Guizmo gets first claim on the click so we don't fight with transforms.
        if (ImGuizmo::IsOver() || ImGuizmo::IsUsing())
        {
            return;
        }

        FTerrainSculptDab Dab;
        Dab.Mode          = Mode;
        Dab.WorldPosition = Hit;
        Dab.TerrainOrigin = TerrainOrigin;
        Dab.Radius        = Radius;
        Dab.Strength      = Strength;
        Dab.Falloff       = Falloff;
        Dab.FlattenHeight = FlattenHeight;
        Dab.DeltaSeconds  = DeltaSeconds;
        Dab.ActiveLayer   = ActiveLayer;
        Dab.SculptSign    = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? static_cast<int8>(-1) : static_cast<int8>(1);

        FTerrainSculptSystem::ApplyDab(Terrain, Dab);
    }

    void FTerrainEditMode::DrawOverlay(CWorld* World, ImVec2 ViewportScreenOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera)
    {
        if (!bActive || !bHitValid || !World)
        {
            return;
        }

        // The brush footprint is a horizontal disk in world space; at oblique angles
        // it projects to an ellipse on screen. Drawing a circle from a single extent
        // looks distorted, so sample points along the world-space ring and project
        // each one individually. NDC.y is mapped directly (not inverted) to match
        // BuildRayFromScreen and the Vulkan-native framebuffer convention used by
        // the renderer.
        const glm::mat4 ViewProj = Camera.GetProjectionMatrix() * Camera.GetViewMatrix();
        auto ProjectToScreen = [&](const glm::vec3& W, ImVec2& Out) -> bool
        {
            glm::vec4 Clip = ViewProj * glm::vec4(W, 1.0f);
            if (Clip.w <= 0.0f)
            {
                return false;
            }
            glm::vec3 Ndc = glm::vec3(Clip) / Clip.w;
            Out.x = ViewportScreenOrigin.x + (Ndc.x * 0.5f + 0.5f) * ViewportSize.x;
            Out.y = ViewportScreenOrigin.y + (Ndc.y * 0.5f + 0.5f) * ViewportSize.y;
            return true;
        };

        ImVec2 Center;
        if (!ProjectToScreen(LastHit, Center))
        {
            return;
        }

        ImDrawList* Draw = ImGui::GetWindowDrawList();

        auto AddWorldRing = [&](float WorldRadius, ImU32 Color, float Thickness)
        {
            constexpr int Segments = 64;
            ImVec2 Points[Segments];
            int Count = 0;
            for (int i = 0; i < Segments; ++i)
            {
                const float A = (static_cast<float>(i) / static_cast<float>(Segments)) * glm::two_pi<float>();
                const glm::vec3 P = LastHit + glm::vec3(std::cos(A) * WorldRadius, 0.0f, std::sin(A) * WorldRadius);
                ImVec2 S;
                if (ProjectToScreen(P, S))
                {
                    Points[Count++] = S;
                }
            }
            if (Count >= 2)
            {
                Draw->AddPolyline(Points, Count, Color, ImDrawFlags_Closed, Thickness);
            }
        };

        AddWorldRing(Radius, IM_COL32(255, 220,  80, 220), 2.0f);
        AddWorldRing(Radius * std::max(0.0f, 1.0f - Falloff * 0.5f),  IM_COL32(255, 220,  80, 120), 1.0f);
        Draw->AddCircleFilled(Center, 3.0f, IM_COL32(255, 255, 255, 220));
    }
}
