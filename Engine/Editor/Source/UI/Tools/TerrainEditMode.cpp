#include "TerrainEditMode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ImGuizmo.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/TerrainComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Scene/RenderScene/TerrainRenderTypes.h"
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
            // ImGui Y=0 is top of viewport; negate so the ray tilts up in world space (camera-basis fix in 5827d9d9).
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

    void FTerrainEditMode::OnEnter(CWorld* World)
    {
        // Spawning a default terrain on enter makes the mode self-bootstrapping —
        // first time the user clicks "Terrain" there's something to paint on.
        if (World && FindPreferredTerrain(World) == entt::null)
        {
            CreateDefaultTerrain(World);
        }
    }

    void FTerrainEditMode::DrawToolbar(CWorld* World, float ButtonSize)
    {
        if (!World)
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

        const char* ModeNames[] = { "Sculpt", "Flatten", "Smooth", "Noise", "Ramp", "Paint" };
        int ModeIndex = (int)Mode;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Mode", &ModeIndex, ModeNames, IM_ARRAYSIZE(ModeNames)))
        {
            Mode = (ETerrainBrushMode)ModeIndex;
            bRampStarted   = false;
            bHasLastDabPos = false;
        }

        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Radius",   &Radius,   8.0f,  4096.0f, "%.0f");
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Strength", &Strength, 0.0f,  256.0f,   "%.2f");
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Falloff",  &Falloff,  0.0f,  1.0f,    "%.2f");
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Spacing",  &Spacing,  0.0f,  1.0f,    "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Minimum cursor travel between dabs as a fraction of brush radius.\nKeeps brush strength independent of FPS.");
        }

        if (Mode == ETerrainBrushMode::Sculpt || Mode == ETerrainBrushMode::Noise)
        {
            ImGui::Checkbox("Invert (LAlt momentary)", &bInvertSculpt);
        }

        if (Mode == ETerrainBrushMode::Flatten)
        {
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("Height", &FlattenHeight, 0.0f, 1024.0f, "%.0f");
            ImGui::TextDisabled("Ctrl+Click: sample height under cursor");
        }

        if (Mode == ETerrainBrushMode::Noise)
        {
            float FreqDisplay = NoiseFrequency * 1000.0f;
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::SliderFloat("Freq (per 1km)", &FreqDisplay, 0.1f, 100.0f, "%.2f"))
            {
                NoiseFrequency = std::max(FreqDisplay * 0.001f, 1e-6f);
            }
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderInt("Octaves", &NoiseOctaves, 1, 8);
        }

        if (Mode == ETerrainBrushMode::Ramp)
        {
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("Width", &RampHalfWidth, 8.0f, 4096.0f, "%.0f");
            ImGui::TextDisabled("Drag from A to B; release to commit.");
        }

        if (Mode == ETerrainBrushMode::Paint)
        {
            entt::entity TerrainEntity = FindPreferredTerrain(World);
            STerrainComponent* Terrain = (TerrainEntity != entt::null)
                ? &World->GetEntityRegistry().get<STerrainComponent>(TerrainEntity)
                : nullptr;
            if (Terrain)
            {
                DrawLayerPanel(*Terrain);
            }
        }

        ImGui::TextDisabled("[ / ] resize brush");

        ImGui::End();
    }

    void FTerrainEditMode::DrawLayerPanel(STerrainComponent& Terrain)
    {
        const int32 LayerCount = (int32)Terrain.Layers.size();

        ImGui::Separator();
        ImGui::TextUnformatted("Layers");

        // Swatch row: one button per layer plus an "Add" tile, capped at the shader's
        // 4-layer maximum so the editor can't paint into a slot the renderer ignores.
        constexpr ImU32 SwatchColors[GTerrainMaxLayers] = {
            IM_COL32(220,  80,  80, 255),
            IM_COL32( 80, 200, 110, 255),
            IM_COL32( 80, 140, 230, 255),
            IM_COL32(220, 200,  80, 255),
        };

        const ImVec2 SwatchSize(28, 28);
        for (int32 i = 0; i < LayerCount; ++i)
        {
            ImGui::PushID(i);
            const bool bSelected = (ActiveLayer == i);
            const ImU32 Border  = bSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 80, 80, 255);
            const ImU32 Fill    = SwatchColors[i % GTerrainMaxLayers];

            ImVec2 Pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##Swatch", SwatchSize);
            if (ImGui::IsItemClicked())
            {
                ActiveLayer = i;
            }
            ImDrawList* Dl = ImGui::GetWindowDrawList();
            Dl->AddRectFilled(Pos, ImVec2(Pos.x + SwatchSize.x, Pos.y + SwatchSize.y), Fill, 4.0f);
            Dl->AddRect(Pos, ImVec2(Pos.x + SwatchSize.x, Pos.y + SwatchSize.y), Border, 4.0f, 0, bSelected ? 2.5f : 1.0f);

            if (ImGui::IsItemHovered() && !Terrain.Layers[i].Name.empty())
            {
                ImGui::SetTooltip("%s", Terrain.Layers[i].Name.c_str());
            }
            ImGui::PopID();
            if (i + 1 < LayerCount)
            {
                ImGui::SameLine();
            }
        }

        if (LayerCount < (int32)GTerrainMaxLayers)
        {
            if (LayerCount > 0) ImGui::SameLine();
            if (ImGui::Button("+", SwatchSize))
            {
                STerrainLayer& L = Terrain.Layers.emplace_back();
                L.Name.sprintf("Layer%d", LayerCount);
                L.UVScale = 1.0f / 16.0f;
                EnsureLayerWeightStorage(Terrain);
                Terrain.GPUState.bFullWeightsDirty = true;
            }
        }
        else
        {
            if (LayerCount > 0) ImGui::SameLine();
            ImGui::TextDisabled("(max %d)", GTerrainMaxLayers);
        }

        ActiveLayer = std::clamp(ActiveLayer, 0, std::max(LayerCount - 1, 0));

        // Per-layer settings for the currently selected swatch: inline rename, UV
        // tiling, and reorder. Reorder mutates LayerWeights too so weights follow
        // the layer instead of swapping under the user.
        if (ActiveLayer >= 0 && ActiveLayer < LayerCount)
        {
            STerrainLayer& L = Terrain.Layers[ActiveLayer];

            char NameBuf[128];
            const FString& Cur = L.Name;
            std::strncpy(NameBuf, Cur.c_str(), sizeof(NameBuf) - 1);
            NameBuf[sizeof(NameBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::InputText("Name", NameBuf, sizeof(NameBuf), ImGuiInputTextFlags_AutoSelectAll))
            {
                L.Name = NameBuf;
            }

            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("UV Scale", &L.UVScale, 1.0f / 1024.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic);

            const bool bCanUp   = ActiveLayer > 0;
            const bool bCanDown = ActiveLayer + 1 < LayerCount;

            ImGui::BeginDisabled(!bCanUp);
            if (ImGui::Button("Up"))
            {
                SwapLayers(Terrain, ActiveLayer, ActiveLayer - 1);
                --ActiveLayer;
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(!bCanDown);
            if (ImGui::Button("Down"))
            {
                SwapLayers(Terrain, ActiveLayer, ActiveLayer + 1);
                ++ActiveLayer;
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(LayerCount <= 1);
            if (ImGui::Button("Remove"))
            {
                RemoveLayer(Terrain, ActiveLayer);
                ActiveLayer = std::min(ActiveLayer, (int32)Terrain.Layers.size() - 1);
            }
            ImGui::EndDisabled();
        }
    }

    void FTerrainEditMode::EnsureLayerWeightStorage(STerrainComponent& Terrain)
    {
        const size_t LayerStride = size_t(Terrain.Resolution) * size_t(Terrain.Resolution);
        const size_t Need = LayerStride * size_t(std::max<size_t>(Terrain.Layers.size(), 1));
        if (Terrain.LayerWeights.size() != Need)
        {
            Terrain.LayerWeights.resize(Need, uint8(0));
        }
    }

    void FTerrainEditMode::SwapLayers(STerrainComponent& Terrain, int32 A, int32 B)
    {
        if (A == B) return;
        const int32 LayerCount = (int32)Terrain.Layers.size();
        if (A < 0 || B < 0 || A >= LayerCount || B >= LayerCount) return;

        EnsureLayerWeightStorage(Terrain);
        std::swap(Terrain.Layers[A], Terrain.Layers[B]);

        const size_t LayerStride = size_t(Terrain.Resolution) * size_t(Terrain.Resolution);
        TVector<uint8> Tmp(LayerStride);
        uint8* PA = &Terrain.LayerWeights[size_t(A) * LayerStride];
        uint8* PB = &Terrain.LayerWeights[size_t(B) * LayerStride];
        std::memcpy(Tmp.data(), PA, LayerStride);
        std::memcpy(PA,         PB, LayerStride);
        std::memcpy(PB, Tmp.data(), LayerStride);

        Terrain.GPUState.bFullWeightsDirty = true;
    }

    void FTerrainEditMode::RemoveLayer(STerrainComponent& Terrain, int32 Index)
    {
        const int32 LayerCount = (int32)Terrain.Layers.size();
        if (Index < 0 || Index >= LayerCount || LayerCount <= 1) return;

        EnsureLayerWeightStorage(Terrain);
        const size_t LayerStride = size_t(Terrain.Resolution) * size_t(Terrain.Resolution);

        // Pack remaining layers down, then resize the trailing slot away.
        for (int32 i = Index; i + 1 < LayerCount; ++i)
        {
            uint8* Dst = &Terrain.LayerWeights[size_t(i)     * LayerStride];
            uint8* Src = &Terrain.LayerWeights[size_t(i + 1) * LayerStride];
            std::memcpy(Dst, Src, LayerStride);
        }
        Terrain.LayerWeights.resize(size_t(LayerCount - 1) * LayerStride);
        Terrain.Layers.erase(Terrain.Layers.begin() + Index);
        Terrain.GPUState.bFullWeightsDirty = true;
    }

    void FTerrainEditMode::Tick(CWorld* World, const SCameraComponent& Camera, bool bViewportHovered, ImVec2 ViewportScreenOrigin, ImVec2 ViewportSize)
    {
        bHitValid = false;
        if (!World)
        {
            bStrokeActive  = false;
            bRampStarted   = false;
            bHasLastDabPos = false;
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

        // Bracket keys resize the brush multiplicatively, like UE / Photoshop. Wired
        // to ImGui's repeat so holding the key still scrubs the size.
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true))
        {
            Radius = std::max(8.0f, Radius / 1.1f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true))
        {
            Radius = std::min(8192.0f, Radius * 1.1f);
        }

        if (!bViewportHovered)
        {
            bStrokeActive  = false;
            bRampStarted   = false;
            bHasLastDabPos = false;
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
            // No hit this frame -> stroke is broken; reset continuity bookkeeping.
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                bStrokeActive  = false;
                bRampStarted   = false;
                bHasLastDabPos = false;
            }
            return;
        }

        LastHit   = Hit;
        bHitValid = true;

        // Ctrl+click anywhere in Flatten mode samples the height under the cursor as
        // the target. Wired off of click rather than down so a held stroke can't
        // accidentally retarget itself.
        if (Mode == ETerrainBrushMode::Flatten
            && ImGui::IsKeyDown(ImGuiKey_LeftCtrl)
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
        {
            FlattenHeight = Hit.y - TerrainOrigin.y;
            return;
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            bStrokeActive  = false;
            bRampStarted   = false;
            bHasLastDabPos = false;
            return;
        }

        // Guizmo gets first claim on the click so we don't fight with transforms.
        if (ImGuizmo::IsOver() || ImGuizmo::IsUsing())
        {
            return;
        }

        // Use wall-clock delta so brushes integrate consistently even when the world
        // is paused. The pass-in DeltaSeconds is the simulated tick which is zero in
        // edit mode and would make every dab a no-op.
        const float RealDelta = std::max(ImGui::GetIO().DeltaTime, 0.0f);

        // Spacing gate: every brush except Ramp wants discrete dabs along the stroke
        // path. Ramp instead drags an endpoint continuously and re-applies each frame.
        if (Mode != ETerrainBrushMode::Ramp)
        {
            if (bHasLastDabPos)
            {
                const float DSq = glm::dot(Hit - LastDabWorldPos, Hit - LastDabWorldPos);
                const float Min = std::max(Radius * Spacing, 1.0f);
                if (DSq < Min * Min)
                {
                    return;
                }
            }
            LastDabWorldPos = Hit;
            bHasLastDabPos  = true;
        }

        // Ramp captures the start of the stroke once, then drags the end while held.
        if (Mode == ETerrainBrushMode::Ramp)
        {
            if (!bRampStarted)
            {
                RampStart    = Hit;
                bRampStarted = true;
            }
            RampEnd = Hit;
        }

        FTerrainSculptDab Dab;
        Dab.Mode          = Mode;
        Dab.WorldPosition = Hit;
        Dab.TerrainOrigin = TerrainOrigin;
        Dab.Radius        = Radius;
        Dab.Strength      = Strength;
        Dab.Falloff       = Falloff;
        Dab.FlattenHeight = FlattenHeight;
        Dab.DeltaSeconds  = RealDelta;
        Dab.ActiveLayer   = ActiveLayer;

        // Direction sign: explicit toggle + LAlt momentary flip. Shift was the old
        // modifier but is now reserved for future shortcuts (constraint, slow drag).
        const bool bInvertNow = bInvertSculpt != ImGui::IsKeyDown(ImGuiKey_LeftAlt);
        Dab.SculptSign = bInvertNow ? int8(-1) : int8(1);

        Dab.NoiseFrequency = NoiseFrequency;
        Dab.NoiseOctaves   = NoiseOctaves;
        Dab.RampStart      = RampStart;
        Dab.RampEnd        = RampEnd;
        Dab.RampHalfWidth  = std::max(RampHalfWidth, 1.0f);

        bStrokeActive = true;
        FTerrainSculptSystem::ApplyDab(Terrain, Dab);
    }

    void FTerrainEditMode::DrawOverlay(CWorld* World, ImVec2 ViewportScreenOrigin, ImVec2 ViewportSize, const SCameraComponent& Camera)
    {
        if (!bHitValid || !World)
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

        // Ramp preview: anchor + live cursor + a thin perpendicular pair showing
        // the configured half-width corridor.
        if (Mode == ETerrainBrushMode::Ramp && bRampStarted)
        {
            ImVec2 Anchor;
            if (ProjectToScreen(RampStart, Anchor))
            {
                Draw->AddCircleFilled(Anchor, 4.0f, IM_COL32( 80, 220, 255, 220));
                Draw->AddLine(Anchor, Center, IM_COL32( 80, 220, 255, 220), 2.0f);
            }

            const glm::vec2 AB2 = glm::vec2(RampEnd.x - RampStart.x, RampEnd.z - RampStart.z);
            const float Len = glm::length(AB2);
            if (Len > 1e-3f)
            {
                const glm::vec2 N = glm::vec2(-AB2.y, AB2.x) / Len;
                const glm::vec3 N3 = glm::vec3(N.x, 0.0f, N.y) * RampHalfWidth;

                ImVec2 P0, P1;
                if (ProjectToScreen(RampStart + N3, P0) && ProjectToScreen(RampEnd + N3, P1))
                {
                    Draw->AddLine(P0, P1, IM_COL32( 80, 220, 255, 160), 1.0f);
                }
                if (ProjectToScreen(RampStart - N3, P0) && ProjectToScreen(RampEnd - N3, P1))
                {
                    Draw->AddLine(P0, P1, IM_COL32( 80, 220, 255, 160), 1.0f);
                }
            }
        }
    }
}
