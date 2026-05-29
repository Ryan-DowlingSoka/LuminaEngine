#include "ProjectSettingsEditorTool.h"

#include <EASTL/sort.h>
#include "Core/Math/Math.h"

#include "Config/Config.h"
#include "FileSystem/FileSystem.h"
#include "Platform/Process/PlatformProcess.h"

namespace Lumina
{
    namespace
    {
        // Comma-joined "Editor/Rendering" -> { "Editor", "Rendering" }.
        // Empty input yields a single "General" bucket so undeclared-category
        // settings still have somewhere to live.
        TVector<FString> SplitCategory(const FString& Category)
        {
            TVector<FString> Out;
            if (Category.empty())
            {
                Out.emplace_back("General");
                return Out;
            }

            size_t Start = 0;
            for (size_t i = 0; i < Category.size(); ++i)
            {
                if (Category[i] == '/')
                {
                    if (i > Start)
                    {
                        Out.emplace_back(Category.data() + Start, Category.data() + i);
                    }
                    Start = i + 1;
                }
            }
            if (Start < Category.size())
            {
                Out.emplace_back(Category.data() + Start, Category.data() + Category.size());
            }

            if (Out.empty())
            {
                Out.emplace_back("General");
            }
            return Out;
        }

        bool MatchesFilter(const FConfigSetting& Setting, const char* SearchText)
        {
            if (SearchText == nullptr || SearchText[0] == '\0')
            {
                return true;
            }
            return Setting.Key.find(SearchText) != FString::npos
                || Setting.Description.find(SearchText) != FString::npos;
        }
    }

    void FProjectSettingsEditorTool::OnInitialize()
    {
        CreateToolWindow("Project Settings", [this](bool bIsFocused)
        {
            DrawWindow(bIsFocused);
        });
    }

    void FProjectSettingsEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FProjectSettingsEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("What's Listed",
            "Only settings declared via FConfig::RegisterSetting are shown. Editing a value here writes it "
            "into the project's config; unregistered keys read from disk are still writable elsewhere but invisible.");
        DrawHelpTextRow("Categories",
            "Settings are grouped by the Category field passed at registration. Pick one in the left panel "
            "to filter the right pane. The search box matches against display name and key.");
        DrawHelpTextRow("Defaults",
            "Right-click a row > 'Reset to Default' restores the value declared in code. Useful when a setting "
            "drifted from a one-off experiment.");
        DrawHelpTextRow("Where it lives",
            "Project config persists under <Project>/Config and is git-friendly text. Engine defaults are "
            "merged on top of it at load time.");
    }

    void FProjectSettingsEditorTool::DrawWindow(bool bIsFocused)
    {
        if (GConfig == nullptr)
        {
            ImGui::TextDisabled("Config system not initialized.");
            return;
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Search", LE_ICON_MAGNIFY " Search settings...", SearchBuffer, IM_ARRAYSIZE(SearchBuffer));
        ImGui::Separator();

        const float Avail = ImGui::GetContentRegionAvail().x;
        const float LeftWidth = Avail * 0.28f;

        ImGui::BeginChild("##CategoryPanel", ImVec2(LeftWidth, 0), true);
        DrawCategoryTree();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##SettingsPanel", ImVec2(0, 0), true);
        DrawSettingsForCategory();
        ImGui::EndChild();
    }

    void FProjectSettingsEditorTool::DrawCategoryTree()
    {
        // Build sorted unique category list from the registry.
        TVector<FString> Categories;
        GConfig->ForEachSetting([&](const FConfigSetting& Setting)
        {
            const FString Cat = Setting.Category.empty() ? FString("General") : Setting.Category;
            if (eastl::find(Categories.begin(), Categories.end(), Cat) == Categories.end())
            {
                Categories.push_back(Cat);
            }
        });
        eastl::sort(Categories.begin(), Categories.end());

        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Categories");
        ImGui::Separator();

        if (SelectedCategory.empty() && !Categories.empty())
        {
            SelectedCategory = Categories[0];
        }

        for (const FString& Cat : Categories)
        {
            const bool bSelected = (SelectedCategory == Cat);
            if (ImGui::Selectable(Cat.c_str(), bSelected))
            {
                SelectedCategory = Cat;
            }
        }
    }

    void FProjectSettingsEditorTool::DrawSettingsForCategory()
    {
        if (SelectedCategory.empty())
        {
            ImGui::TextDisabled("Select a category to view settings.");
            return;
        }

        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", SelectedCategory.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        if (!ImGui::BeginTable("##Settings", 2,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        {
            return;
        }

        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);

        GConfig->ForEachSetting([&](const FConfigSetting& Setting)
        {
            const FString Cat = Setting.Category.empty() ? FString("General") : Setting.Category;
            if (Cat != SelectedCategory)
            {
                return;
            }
            if (!MatchesFilter(Setting, SearchBuffer))
            {
                return;
            }
            DrawSettingRow(Setting);
        });

        ImGui::EndTable();
    }

    void FProjectSettingsEditorTool::DrawSettingRow(const FConfigSetting& Setting)
    {
        ImGui::PushID(Setting.Key.c_str());

        ImGui::TableNextRow();

        // Key column — show the leaf name big, with full dotted path + tooltip.
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        const size_t LastDot = Setting.Key.find_last_of('.');
        const char* LeafName = (LastDot == FString::npos)
            ? Setting.Key.c_str()
            : Setting.Key.c_str() + LastDot + 1;
        ImGui::TextUnformatted(LeafName);
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", Setting.Key.c_str());
            if (!Setting.Description.empty())
            {
                ImGui::Separator();
                ImGui::TextWrapped("%s", Setting.Description.c_str());
            }
            ImGui::EndTooltip();
        }

        // Value column — widget chosen by registered type.
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);

        switch (Setting.Type)
        {
        case EConfigValueType::Bool:
        {
            bool Value = GConfig->GetBool(Setting.Key);
            if (ImGui::Checkbox("##v", &Value))
            {
                GConfig->Set(Setting.Key, Value);
            }
            break;
        }
        case EConfigValueType::Int:
        {
            int32 Value = GConfig->GetInt(Setting.Key);
            const bool bChanged = Setting.bHasRange
                ? ImGui::SliderInt("##v", &Value, (int32)Setting.MinValue, (int32)Setting.MaxValue)
                : ImGui::DragInt("##v", &Value);
            if (bChanged)
            {
                GConfig->Set(Setting.Key, Value);
            }
            break;
        }
        case EConfigValueType::Float:
        {
            float Value = GConfig->GetFloat(Setting.Key);
            const bool bChanged = Setting.bHasRange
                ? ImGui::SliderFloat("##v", &Value, (float)Setting.MinValue, (float)Setting.MaxValue)
                : ImGui::DragFloat("##v", &Value, 0.05f);
            if (bChanged)
            {
                GConfig->Set(Setting.Key, Value);
            }
            break;
        }
        case EConfigValueType::String:
        {
            FString Value = GConfig->GetString(Setting.Key);
            char Buf[512];
            const size_t N = eastl::min<size_t>(Value.size(), sizeof(Buf) - 1);
            memcpy(Buf, Value.c_str(), N);
            Buf[N] = '\0';
            if (ImGui::InputText("##v", Buf, sizeof(Buf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                GConfig->Set(Setting.Key, std::string(Buf));
            }
            break;
        }
        case EConfigValueType::Path:
        case EConfigValueType::DirectoryPath:
        {
            FString Value = GConfig->GetString(Setting.Key);
            char Buf[512];
            const size_t N = eastl::min<size_t>(Value.size(), sizeof(Buf) - 1);
            memcpy(Buf, Value.c_str(), N);
            Buf[N] = '\0';

            const float ButtonWidth = 80.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ButtonWidth - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::InputText("##v", Buf, sizeof(Buf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                GConfig->Set(Setting.Key, std::string(Buf));
            }

            ImGui::SameLine();
            if (ImGui::Button(LE_ICON_FOLDER_OPEN " Browse", ImVec2(ButtonWidth, 0)))
            {
                FFixedString Picked;
                const char* Filter = Setting.FileFilter.empty() ? nullptr : Setting.FileFilter.c_str();
                if (Platform::OpenFileDialogue(Picked, Setting.Key.c_str(), Filter))
                {
                    // The dialog returns an absolute Windows path; convert to
                    // a VFS path so the value matches what the asset registry,
                    // LoadObject, and the cooker all key on. Falls back to the
                    // raw path if no mount matches.
                    const FFixedString Virtual = VFS::ResolveToVirtualPath(FStringView(Picked.c_str(), Picked.size()));
                    GConfig->Set(Setting.Key, std::string(Virtual.c_str()));
                }
            }
            break;
        }
        case EConfigValueType::Color:
        {
            FVector4 Value = GConfig->GetColor(Setting.Key);
            if (ImGui::ColorEdit4("##v", &Value.x))
            {
                std::vector<float> J = { Value.x, Value.y, Value.z, Value.w };
                GConfig->Set(Setting.Key, J);
            }
            break;
        }
        case EConfigValueType::Vec2:
        {
            FVector2 Value = GConfig->GetVec2(Setting.Key);
            if (ImGui::DragFloat2("##v", &Value.x, 0.05f))
            {
                std::vector<float> J = { Value.x, Value.y };
                GConfig->Set(Setting.Key, J);
            }
            break;
        }
        case EConfigValueType::Vec3:
        {
            FVector3 Value = GConfig->GetVec3(Setting.Key);
            if (ImGui::DragFloat3("##v", &Value.x, 0.05f))
            {
                std::vector<float> J = { Value.x, Value.y, Value.z };
                GConfig->Set(Setting.Key, J);
            }
            break;
        }
        case EConfigValueType::Vec4:
        {
            FVector4 Value = GConfig->GetVec4(Setting.Key);
            if (ImGui::DragFloat4("##v", &Value.x, 0.05f))
            {
                std::vector<float> J = { Value.x, Value.y, Value.z, Value.w };
                GConfig->Set(Setting.Key, J);
            }
            break;
        }
        case EConfigValueType::Enum:
        {
            FString Value = GConfig->GetString(Setting.Key);
            int32 Current = -1;
            for (int32 i = 0; i < (int32)Setting.EnumOptions.size(); ++i)
            {
                if (Setting.EnumOptions[i] == Value) { Current = i; break; }
            }
            const char* Preview = (Current >= 0) ? Setting.EnumOptions[Current].c_str() : "<unset>";
            if (ImGui::BeginCombo("##v", Preview))
            {
                for (int32 i = 0; i < (int32)Setting.EnumOptions.size(); ++i)
                {
                    const bool bSel = (i == Current);
                    if (ImGui::Selectable(Setting.EnumOptions[i].c_str(), bSel))
                    {
                        GConfig->Set(Setting.Key, std::string(Setting.EnumOptions[i].c_str()));
                    }
                    if (bSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case EConfigValueType::StringArray:
        {
            TVector<FString> Values = GConfig->GetStringArray(Setting.Key);

            // Compact preview row that expands into editable entries.
            FString Preview;
            for (size_t i = 0; i < Values.size(); ++i)
            {
                if (i > 0) Preview += ", ";
                Preview += Values[i];
            }
            if (Preview.empty()) Preview = "(empty)";

            if (ImGui::TreeNode("##list", "%s [%zu]", Preview.c_str(), Values.size()))
            {
                // When the setting has a FileFilter, treat the array as a
                // list of asset paths and offer a per-row Browse button
                // (mirrors the single-Path renderer above). Otherwise it's
                // just text inputs.
                const bool bHasPicker = !Setting.FileFilter.empty();
                const float PickerWidth = bHasPicker ? 80.0f : 0.0f;
                const float TrashWidth  = 28.0f;
                const float Spacing     = ImGui::GetStyle().ItemSpacing.x;

                bool bDirty = false;
                for (size_t i = 0; i < Values.size(); ++i)
                {
                    ImGui::PushID((int)i);

                    char Buf[256];
                    const size_t N = eastl::min<size_t>(Values[i].size(), sizeof(Buf) - 1);
                    memcpy(Buf, Values[i].c_str(), N);
                    Buf[N] = '\0';

                    const float Avail = ImGui::GetContentRegionAvail().x;
                    const float InputW = Avail - PickerWidth - (bHasPicker ? Spacing : 0.0f) - TrashWidth - Spacing;
                    ImGui::SetNextItemWidth(InputW);
                    // Commit on every keystroke: without this, ImGui's
                    // Buf gets reset from the stale Values[i] each frame
                    // because we re-fetch from GConfig at the top, so
                    // anything typed disappears between frames.
                    if (ImGui::InputText("##entry", Buf, sizeof(Buf)))
                    {
                        Values[i] = Buf;
                        bDirty = true;
                    }

                    if (bHasPicker)
                    {
                        ImGui::SameLine();
                        if (ImGui::Button(LE_ICON_FOLDER_OPEN " Browse", ImVec2(PickerWidth, 0)))
                        {
                            FFixedString Picked;
                            if (Platform::OpenFileDialogue(Picked, Setting.Key.c_str(), Setting.FileFilter.c_str()))
                            {
                                // Same VFS conversion as the single-Path
                                // renderer — keep the value comparable to
                                // anything the registry/cooker looks up.
                                const FFixedString Virtual = VFS::ResolveToVirtualPath(FStringView(Picked.c_str(), Picked.size()));
                                Values[i] = FString(Virtual.c_str(), Virtual.size());
                                bDirty = true;
                            }
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::SmallButton(LE_ICON_TRASH_CAN))
                    {
                        Values.erase(Values.begin() + i);
                        bDirty = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (ImGui::SmallButton(LE_ICON_PLUS " Add"))
                {
                    Values.emplace_back();
                    bDirty = true;
                }

                if (bDirty)
                {
                    std::vector<std::string> J;
                    J.reserve(Values.size());
                    for (const FString& V : Values) { J.emplace_back(V.c_str()); }
                    GConfig->Set(Setting.Key, J);
                }
                ImGui::TreePop();
            }
            break;
        }
        }

        ImGui::PopID();
    }
}
