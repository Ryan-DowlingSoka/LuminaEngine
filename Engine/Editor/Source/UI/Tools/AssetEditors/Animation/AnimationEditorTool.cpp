#include "AnimationEditorTool.h"

#include <glm/ext/scalar_common.hpp>

#include "ImGuiDrawUtils.h"
#include "implot.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "assets/assettypes/mesh/skeleton/skeleton.h"
#include "glm/gtx/string_cast.hpp"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "world/entity/components/environmentcomponent.h"
#include "World/Entity/Components/LightComponent.h"
#include "world/Entity/Components/SimpleAnimationComponent.h"
#include "world/entity/components/skeletalmeshcomponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "world/entity/components/velocitycomponent.h"


namespace Lumina
{
    static const char* MeshPropertiesName       = "MeshProperties";
    static const char* SequencerName            = "Sequencer";

    FAnimationEditorTool::FAnimationEditorTool(IEditorToolContext* Context, CObject* InAsset)
        : FAssetEditorTool(Context, InAsset->GetName().c_str(), InAsset, NewObject<CWorld>())
    {
    }

    void FAnimationEditorTool::OnInitialize()
    {
        CreateToolWindow(MeshPropertiesName, [&](bool bFocused)
        {
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
            ImGui::SeparatorText("Asset Details");
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
            PropertyTable.DrawTree();
        });
        
        CreateToolWindow(SequencerName, [&](bool bFocused)
        {
            DrawSequencer();
        });
    }

    void FAnimationEditorTool::SetupWorldForTool()
    {
        FEditorTool::SetupWorldForTool();
        
        CreateFloorPlane();
        
        DirectionalLightEntity = World->ConstructEntity("Directional Light");
        World->GetEntityRegistry().emplace<SDirectionalLightComponent>(DirectionalLightEntity);
        World->GetEntityRegistry().emplace<SEnvironmentComponent>(DirectionalLightEntity);
        
        CAnimation* Animation = Cast<CAnimation>(Asset.Get());
        if (!Animation->Skeleton.IsValid())
        {
            return;
        }
        
        World->GetEntityRegistry().get<SVelocityComponent>(EditorEntity).Speed = 5.0f;
        
        MeshEntity = World->ConstructEntity("MeshEntity");
        World->GetEntityRegistry().emplace<SSkeletalMeshComponent>(MeshEntity).SkeletalMesh = Animation->Skeleton->PreviewMesh;
        World->GetEntityRegistry().emplace<SSimpleAnimationComponent>(MeshEntity).Animation = Animation;
        World->GetEntityRegistry().get<SSimpleAnimationComponent>(MeshEntity).bPlaying = false;
        
        STransformComponent& MeshTransform = World->GetEntityRegistry().get<STransformComponent>(MeshEntity);
        STransformComponent& EditorTransform = World->GetEntityRegistry().get<STransformComponent>(EditorEntity);

        glm::quat Rotation = Math::FindLookAtRotation(MeshTransform.GetLocation() + glm::vec3(0.0f, 0.85f, 0.0f), EditorTransform.GetLocation());
        EditorTransform.SetRotation(Rotation);
    }

    void FAnimationEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::Update(UpdateContext);
        
        if (!World.IsValid())
        {
            return;
        }
        
    }

    void FAnimationEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FAnimationEditorTool::OnAssetLoadFinished()
    {
    }

    void FAnimationEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
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
    }

    void FAnimationEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        // Always start from a clean tree -- guards against stale child nodes left over from a prior
        // layout attempt or a stripped imgui.ini that left a partial dockspace behind.
        ImGui::DockBuilderRemoveNodeChildNodes(InDockspaceID);

        ImGuiID leftDockID = 0, rightDockID = 0, bottomDockID = 0;

        // Split the root: 70% viewport area on the left, 30% properties on the right.
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Right, 0.3f, &rightDockID, &leftDockID);

        // Sub-split the LEFT pane (not the root) into a viewport top and sequencer bottom. Splitting
        // the root again here would attempt to split a parent node and silently drop the second
        // split, leaving the sequencer dock target invalid.
        ImGui::DockBuilderSplitNode(leftDockID, ImGuiDir_Down, 0.3f, &bottomDockID, &leftDockID);

        ImGui::DockBuilderDockWindow(GetToolWindowName(ViewportWindowName).c_str(), leftDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(MeshPropertiesName).c_str(), rightDockID);
        ImGui::DockBuilderDockWindow(GetToolWindowName(SequencerName).c_str(), bottomDockID);
    }

    void FAnimationEditorTool::DrawSequencer()
    {
        CAnimation* Animation = GetAsset<CAnimation>();
        if (!Animation)
        {
            return;
        }
    
        float Duration = Animation->GetDuration();
        FAnimationResource* AnimationResource = Animation->GetAnimationResource();
        if (!AnimationResource)
        {
            return;
        }
        
        SSimpleAnimationComponent* AnimationComponent = World->GetEntityRegistry().try_get<SSimpleAnimationComponent>(MeshEntity);
        
        if (AnimationComponent == nullptr)
        {
            return;
        }
    
        float& CurrentTime = AnimationComponent->CurrentTime;
        static bool bIsPlaying = false;
        static int SelectedChannel = -1;
        static bool bShowCurveEditor = true;
        
        if (bIsPlaying)
        {
            CurrentTime += ImGui::GetIO().DeltaTime * Playrate;
            if (CurrentTime > Duration)
            {
                CurrentTime = fmod(CurrentTime, Duration);
            }
        }
        
        if (ImGui::Button(bIsPlaying ? LE_ICON_PAUSE " Pause" : LE_ICON_PLAY " Play", ImVec2(100, 0)))
        {
            bIsPlaying = !bIsPlaying;
        }
        
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_STOP " Stop", ImVec2(100, 0)))
        {
            bIsPlaying = false;
            CurrentTime = 0.0f;
        }
        
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_STEP_BACKWARD, ImVec2(40, 0)))
        {
            CurrentTime = fmax(0.0f, CurrentTime - 0.033f);
        }
        
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_STEP_FORWARD, ImVec2(40, 0)))
        {
            CurrentTime = fmin(Duration, CurrentTime + 0.033f);
        }
        
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::SliderFloat(LE_ICON_ARROW_VERTICAL_LOCK " Play-Rate", &Playrate, 0.01f, 10.0f);
        
        ImGui::Separator();
        
        ImGui::Text("Time: %.3fs / %.3fs", CurrentTime, Duration);
        
        static float LeftPanelWidth = 250.0f;
        
        ImGui::BeginChild("LeftPanel", ImVec2(LeftPanelWidth, 0), true);
        {
            if (ImGui::CollapsingHeader("Animation Channels", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (int i = 0; i < (int)AnimationResource->Channels.size(); ++i)
                {
                    const FAnimationChannel& Channel = AnimationResource->Channels[i];
                    
                    const char* PathIcon = "";
                    switch (Channel.TargetPath)
                    {
                        case FAnimationChannel::ETargetPath::Translation: PathIcon = LE_ICON_AXIS_ARROW; break;
                        case FAnimationChannel::ETargetPath::Rotation:    PathIcon = LE_ICON_ROTATE_360; break;
                        case FAnimationChannel::ETargetPath::Scale:       PathIcon = LE_ICON_ARROW_TOP_RIGHT_BOTTOM_LEFT; break;
                        case FAnimationChannel::ETargetPath::Weights:     PathIcon = LE_ICON_WEIGHT; break;
                    }
                    
                    ImGui::PushID(i);
                    bool bSelected = (SelectedChannel == i);
                    
                    if (ImGui::Selectable(std::format("{} {}", PathIcon, Channel.TargetBone.c_str()).c_str(), bSelected))
                    {
                        SelectedChannel = i;
                    }
                    
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Add Keyframe")) { /* Add keyframe */ }
                        if (ImGui::MenuItem("Delete Channel")) { /* Delete channel */ }
                        ImGui::EndPopup();
                    }
                    
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        
        if (bShowCurveEditor && SelectedChannel >= 0 && SelectedChannel < AnimationResource->Channels.size())
        {
            ImGui::BeginChild("CurveEditor", ImVec2(0, 0), true);
            {
                FAnimationChannel& Channel = AnimationResource->Channels[SelectedChannel];
                
                FStringView PathName;
                switch (Channel.TargetPath)
                {
                case FAnimationChannel::ETargetPath::Translation:
                    PathName = "Translation";
                    break;
                case FAnimationChannel::ETargetPath::Rotation:
                    PathName = "Rotation";
                    break;
                case FAnimationChannel::ETargetPath::Scale:
                    PathName = "Scale";
                    break;
                case FAnimationChannel::ETargetPath::Weights:
                    PathName = "Weights";
                    break;
                }
                
                ImGui::Text("Channel: %s - (%s)", Channel.TargetBone.c_str(), PathName.data());
                
                if (ImPlot::BeginPlot("##AnimCurves", ImVec2(-1, -1)))
                {
                    ImPlot::SetupAxisLimits(ImAxis_X1, 0, Duration, ImGuiCond_Always);
                    ImPlot::SetupAxis(ImAxis_X1, "Time (s)");
                    ImPlot::SetupAxis(ImAxis_Y1, "Value");
                    
                    double CurrentTimeDouble = CurrentTime;
                    ImPlot::DragLineX(0, &CurrentTimeDouble, ImVec4(1, 1, 0, 1), 2.0f);
                    CurrentTime = glm::clamp((float)CurrentTimeDouble, 0.0f, Duration);
                    
                    switch (Channel.TargetPath)
                    {
                        case FAnimationChannel::ETargetPath::Translation:
                        case FAnimationChannel::ETargetPath::Scale:
                        {
                            TVector<glm::vec3>& Data = (Channel.TargetPath == FAnimationChannel::ETargetPath::Translation) 
                                ? Channel.Translations 
                                : Channel.Scales;
                            
                            if (!Data.empty())
                            {
                                TVector<float> XVals, YVals, ZVals;
                                for (const auto& V : Data)
                                {
                                    XVals.push_back(V.x);
                                    YVals.push_back(V.y);
                                    ZVals.push_back(V.z);
                                }
                                
                                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                                ImPlot::PlotLine("X", Channel.Timestamps.data(), XVals.data(), (int)Data.size());
                                ImPlot::PlotScatter("##X_keys", Channel.Timestamps.data(), XVals.data(), (int)Data.size());
                                
                                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                                ImPlot::PlotLine("Y", Channel.Timestamps.data(), YVals.data(), (int)Data.size());
                                ImPlot::PlotScatter("##Y_keys", Channel.Timestamps.data(), YVals.data(), (int)Data.size());
                                
                                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                                ImPlot::PlotLine("Z", Channel.Timestamps.data(), ZVals.data(), (int)Data.size());
                                ImPlot::PlotScatter("##Z_keys", Channel.Timestamps.data(), ZVals.data(), (int)Data.size());
                            }
                            break;
                        }
                        
                        case FAnimationChannel::ETargetPath::Rotation:
                        {
                            if (!Channel.Rotations.empty())
                            {
                                TVector<float> XVals, YVals, ZVals;
                                for (const auto& Q : Channel.Rotations)
                                {
                                    glm::vec3 Euler = glm::degrees(glm::eulerAngles(Q));
                                    XVals.push_back(Euler.x);
                                    YVals.push_back(Euler.y);
                                    ZVals.push_back(Euler.z);
                                }
                                
                                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                                ImPlot::PlotLine("X (deg)", Channel.Timestamps.data(), XVals.data(), (int)Channel.Rotations.size());
                                ImPlot::PlotScatter("##X_keys", Channel.Timestamps.data(), XVals.data(), (int)Channel.Rotations.size());
                                
                                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                                ImPlot::PlotLine("Y (deg)", Channel.Timestamps.data(), YVals.data(), (int)Channel.Rotations.size());
                                ImPlot::PlotScatter("##Y_keys", Channel.Timestamps.data(), YVals.data(), (int)Channel.Rotations.size());
                                
                                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                                ImPlot::PlotLine("Z (deg)", Channel.Timestamps.data(), ZVals.data(), (int)Channel.Rotations.size());
                                ImPlot::PlotScatter("##Z_keys", Channel.Timestamps.data(), ZVals.data(), (int)Channel.Rotations.size());
                            }
                            break;
                        }
                    }
                    
                    ImPlot::EndPlot();
                }
            }
            
            ImGui::EndChild();
        }
    }
}
