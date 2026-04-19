#include "ScriptComponentCustomization.h"
#include "imgui.h"
#include "World/World.h"
#include "Platform/Process/PlatformProcess.h"
#include "Scripting/Lua/Scripting.h"
#include "UI/Tools/ContentBrowserEditorTool.h"

namespace Lumina
{
    static constexpr ImVec2 GButtonSize(42, 0);
    
    TSharedPtr<FScriptComponentPropertyCustomization> FScriptComponentPropertyCustomization::MakeInstance()
    {
        return MakeShared<FScriptComponentPropertyCustomization>();
    }

    EPropertyChangeOp FScriptComponentPropertyCustomization::DrawProperty(TSharedPtr<FPropertyHandle> Property)
    {
        bool bWasChanged = false;

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        SScriptComponent* ScriptComponent = static_cast<SScriptComponent*>(Property->ContainerPtr);
        
        ImGui::PushID(this);
        if (ImGui::BeginChild("OP", ImVec2(-1, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            const auto& Style = ImGui::GetStyle();
            
            ImGui::BeginGroup();

            float const ComboArrowWidth = ImGui::GetFrameHeight();
            float const TotalPathWidgetWidth = ImGui::GetContentRegionAvail().x;
            float const TextWidgetWidth = TotalPathWidgetWidth - ComboArrowWidth;

            ImGui::SetNextItemWidth(TextWidgetWidth);
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        
            FFixedString PathString(ScriptComponent->ScriptPath.Path.begin(), ScriptComponent->ScriptPath.Path.length());
            ImGui::InputText("##ScriptPathText", PathString.data(), PathString.max_size(), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
        
            ImGuiX::TextTooltip("{}", ScriptComponent->ScriptPath.Path);
        
            ImGui::PopStyleColor();

            const ImVec2 ComboDropDownSize = ImMax(ImVec2(200, 200), ImVec2(TextWidgetWidth, 300.0f));

            ImGui::SameLine(0, 0);
        
            bool bComboOpen = ImGui::BeginCombo("##ScriptPath", "", ImGuiComboFlags_HeightLarge | ImGuiComboFlags_PopupAlignLeft | ImGuiComboFlags_NoPreview);
            
            if (bComboOpen)
            {
                SearchFilter.Draw("##Search", ComboDropDownSize.x - 30.0f);
                if (!SearchFilter.IsActive())
                {
                    ImDrawList* DrawList = ImGui::GetWindowDrawList();
                    ImVec2 TextPos = ImGui::GetItemRectMin();
                    TextPos.x += Style.FramePadding.x + 2.0f;
                    TextPos.y += Style.FramePadding.y;
                    DrawList->AddText(TextPos, IM_COL32(100, 100, 110, 255), LE_ICON_FILE_SEARCH " Search Scripts...");
                }
                
                ImGui::SameLine();
                ImGui::Button(LE_ICON_FILTER, ImVec2(30.0f, 0.0f));
                ImGui::SetNextWindowSizeConstraints(ImVec2(200, 200), ComboDropDownSize);

                if (ImGui::BeginChild("##OptList", ComboDropDownSize, false, ImGuiChildFlags_NavFlattened))
                {
                    VFS::RecursiveDirectoryIterator("/Game/Scripts", [&](const VFS::FFileInfo& FileInfo)
                    {
                        if (!FileInfo.IsLua())
                        {
                            return;
                        }
                        
                        if (!SearchFilter.PassFilter(FileInfo.VirtualPath.c_str()))
                        {
                            return;
                        }
                        
                        FFixedString SelectableLabel;
                        SelectableLabel.append(LE_ICON_LANGUAGE_LUA).append(" ").append(FileInfo.VirtualPath.c_str());
                        if (ImGui::Selectable(SelectableLabel.c_str()))
                        {
                            ScriptComponent->ScriptPath.Path = FileInfo.VirtualPath;
                            ScriptComponent->World->OnScriptComponentCreated(ScriptComponent->Entity, *ScriptComponent, true);
                            ImGui::CloseCurrentPopup();
                            bWasChanged = true;
                        }
                        
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        {
                            FString FileContents;
                            if (VFS::ReadFile(FileContents, FileInfo.VirtualPath))
                            {
                                FString Preview;
                                int LineCount = 0;
                                for (char C : FileContents)
                                {
                                    if (C == '\n' && ++LineCount >= 10)
                                    {
                                        Preview.append("\n...");
                                        break;
                                    }
                                    Preview.push_back(C);
                                }

                                ImGui::BeginTooltip();
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.6f, 1.0f));
                                ImGui::TextUnformatted(Preview.c_str());
                                ImGui::PopStyleColor();
                                ImGui::EndTooltip();
                            }
                        }
                    });
                }
                
                ImGui::EndChild();
                ImGui::EndCombo();
            }
            
            if (ImGui::Button(LE_ICON_OPEN_IN_NEW "##Open", GButtonSize))
            {
                VFS::PlatformOpen(ScriptComponent->ScriptPath.Path);
            }
            
            ImGuiX::TextTooltip("Open the script in your native editor");
            
            ImGui::SameLine();
            
            if (ImGui::Button(LE_ICON_CONTENT_COPY "##Copy", GButtonSize))
            {
                ImGui::SetClipboardText(ScriptComponent->ScriptPath.Path.c_str());
            }
            
            ImGuiX::TextTooltip("Copy the script-path to your native clipboard");

            ImGui::SameLine();
            
            if (ImGui::Button(LE_ICON_REFRESH "##Refresh", GButtonSize))
            {
                if (ScriptComponent->World)
                {
                    ScriptComponent->World->OnScriptComponentCreated(ScriptComponent->Entity, *ScriptComponent, true);
                }
                bWasChanged = true;
            }
            
            ImGuiX::TextTooltip("Reload the script, will attempt to keep any matching values");

            ImGui::SameLine();
        
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));

            if (ImGui::Button(LE_ICON_CLOSE_CIRCLE "##Clear", GButtonSize))
            {
                ScriptComponent->Script = {};
                ScriptComponent->AttachFunc = {};
                ScriptComponent->ReadyFunc = {};
                ScriptComponent->UpdateFunc = {};
                ScriptComponent->DetachFunc = {};
                ScriptComponent->ScriptPath = {};
                ScriptComponent->ScriptMetaTable = {};
                ScriptComponent->TickRate = 0.0f;
                ScriptComponent->bRunInEditor = false;
                bWasChanged = true;
            }
            
            ImGuiX::TextTooltip("Clears the script from this component");
            
            ImGui::Separator();
            
            if (ScriptComponent->Script && ScriptComponent->Script->Reference.IsValid()
                && ScriptComponent->Script->Reference.IsTable())
            {
                if (ImGui::BeginTable("ScriptReference", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Key");
                    ImGui::TableSetupColumn("Value");
                    ImGui::TableHeadersRow();

                    // @TODO Figure out why iterators broke.
                    //for (auto&& [Key, Value] : ScriptComponent->Script->Reference)
                    //{
                    //    ImGui::TableNextRow();
                    //    ImGui::TableSetColumnIndex(0);
                    //    ImGui::TextUnformatted(Key.ToString().c_str());
                    //    ImGui::TableSetColumnIndex(1);
                    //    ImGui::TextUnformatted(Value.ToString().c_str());
                    //}

                    ImGui::EndTable();
                }
            }
            
            ImGui::PopStyleColor(3);
            
            ImGui::EndGroup();
        }
        ImGui::EndChild();

        ImGui::PopID();

        ImGui::PopItemWidth();
        
        return bWasChanged ? EPropertyChangeOp::Updated : EPropertyChangeOp::None;
    }

    void FScriptComponentPropertyCustomization::UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property)
    {
        
    }

    void FScriptComponentPropertyCustomization::HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property)
    {

    }
}
