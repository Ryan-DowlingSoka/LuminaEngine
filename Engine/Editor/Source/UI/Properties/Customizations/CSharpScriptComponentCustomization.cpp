#include "CSharpScriptComponentCustomization.h"
#include "ScriptPropertyDrawer.h"
#include "imgui.h"
#include "Lumina.h"
#include "Scripting/DotNet/DotNetHost.h"
#include "Scripting/ScriptExports.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "World/Entity/Components/CSharpScriptComponent.h"
#include "FileSystem/FileSystem.h"
#include "Platform/Process/PlatformProcess.h"
#include "Core/Plugin/Plugin.h"
#include "Core/Plugin/PluginManager.h"
#include "Log/Log.h"

namespace Lumina
{
    namespace
    {
        using namespace Scripting;
        
        void RebindScript(SCSharpScriptComponent* Component, const FString& NewClass)
        {
            if (Component->Instance != nullptr && Component->Generation == DotNet::GetScriptGeneration())
            {
                DotNet::DestroyEntityScript(Component->Instance);
            }
            Component->Instance = nullptr;
            Component->BindState = ECSharpBindState::Unbound;
            Component->ScriptClass = NewClass;
            Component->Generation = -1;
        }
        
        FString FindScriptSourceFile(FStringView ScriptClass)
        {
            FStringView ClassName = ScriptClass;
            const size_t Dot = ScriptClass.rfind('.');
            if (Dot != FStringView::npos)
            {
                ClassName = ScriptClass.substr(Dot + 1);
            }
            if (ClassName.empty())
            {
                return FString();
            }

            TVector<FString> Roots;
            Roots.emplace_back("/Game/Scripts");
            Roots.emplace_back("/Engine/Resources/Scripts");
            for (const FPlugin* Plugin : FPluginManager::Get().GetAllPlugins())
            {
                if (Plugin != nullptr && Plugin->IsEnabled() && Plugin->IsContentMounted())
                {
                    Roots.emplace_back(Plugin->GetMountAlias() + "/Scripts");
                }
            }

            const FString Needle = FString("class ") + FString(ClassName.data(), ClassName.size());
            FString FoundDisk;
            for (const FString& Root : Roots)
            {
                if (!FoundDisk.empty())
                {
                    break;
                }
                VFS::RecursiveDirectoryIterator(FStringView(Root.c_str(), Root.size()), [&](const VFS::FFileInfo& Info)
                {
                    if (!FoundDisk.empty() || Info.IsDirectory())
                    {
                        return;
                    }
                    if (Info.GetExt() != ".cs")
                    {
                        return;
                    }
                    const FStringView VPath(Info.VirtualPath.c_str(), Info.VirtualPath.size());
                    if (VPath.find("/obj/") != FStringView::npos || VPath.find("/bin/") != FStringView::npos)
                    {
                        return;
                    }
                    FString Text;
                    if (!VFS::ReadFile(Text, VPath))
                    {
                        return;
                    }
                    // Word-boundary "class <ClassName>" so it doesn't match "class <ClassName>Foo".
                    for (size_t Pos = Text.find(Needle.c_str(), 0); Pos != FString::npos; Pos = Text.find(Needle.c_str(), Pos + Needle.size()))
                    {
                        const size_t After = Pos + Needle.size();
                        const char C = (After < Text.size()) ? Text[After] : ' ';
                        const bool bIdent = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_';
                        if (!bIdent)
                        {
                            FoundDisk.assign(Info.PathSource.c_str(), Info.PathSource.size());
                            return;
                        }
                    }
                });
            }

            return FoundDisk;
        }

        void OpenScriptSource(const FString& ScriptClass)
        {
            if (ScriptClass.empty())
            {
                return;
            }
            const FString Path = FindScriptSourceFile(FStringView(ScriptClass.c_str(), ScriptClass.size()));
            if (Path.empty())
            {
                LOG_WARN("Open script: no .cs file declaring '{}' found under the script roots.", ScriptClass.c_str());
                return;
            }
            Platform::LaunchURL(UTF8_TO_TCHAR(Path.c_str()));
        }
        
        bool DrawScriptProperties(SCSharpScriptComponent* Component)
        {
            FScriptExportSchema Schema;
            TVector<FScriptPropertyEntry> Defaults;
            if (!DotNet::GatherScriptSchema(FStringView(Component->ScriptClass.c_str(), Component->ScriptClass.size()), Schema, Defaults)
                || Schema.Fields.empty())
            {
                return false;
            }
            
            ReconcileOverrides(Schema, Defaults, Component->PropertyOverrides.Items);

            ImGui::SeparatorText("Script Properties");
            bool bChanged = false;

            if (ImGui::BeginTable("##CSharpScriptProperties", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::GetContentRegionAvail().x * 0.4f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                for (const FScriptExportField& Field : Schema.Fields)
                {
                    if (!Field.Type)
                    {
                        continue;
                    }

                    FScriptPropertyValue* Value = nullptr;
                    for (FScriptPropertyEntry& Entry : Component->PropertyOverrides.Items)
                    {
                        if (Entry.Name == Field.Name)
                        {
                            Value = &Entry.Value;
                            break;
                        }
                    }
                    if (Value == nullptr)
                    {
                        continue;
                    }

                    FString Label = Field.Name.ToString();
                    ScriptPropertyDrawer::DrawValueRows(*Field.Type, *Value, Label.c_str(), Field.Meta, bChanged);
                }

                ImGui::EndTable();
            }

            // Push edits to the running instance immediately (else they'd only take effect on the next
            // bind). No-op in edit mode where there's no live instance.
            if (bChanged && Component->Instance != nullptr)
            {
                DotNet::ApplyScriptProperties(Component->Instance, Component->PropertyOverrides);
            }
            return bChanged;
        }
    }

    TSharedPtr<FCSharpScriptComponentPropertyCustomization> FCSharpScriptComponentPropertyCustomization::MakeInstance()
    {
        return MakeShared<FCSharpScriptComponentPropertyCustomization>();
    }

    EPropertyChangeOp FCSharpScriptComponentPropertyCustomization::DrawProperty(const TSharedPtr<FPropertyHandle>& Property)
    {
        bool bWasChanged = false;
        auto* Component = static_cast<SCSharpScriptComponent*>(Property->ContainerPtr);

        ImGui::PushID(this);

        // Discover the loaded C# EntityScript types (managed crossing; only while this component is inspected).
        TVector<FString> Types;
        DotNet::GatherEntityScriptTypes(Types);

        int32 Current = INDEX_NONE;
        for (int32 i = 0; i < (int32)Types.size(); ++i)
        {
            if (Types[i] == Component->ScriptClass)
            {
                Current = i;
                break;
            }
        }

        const FString Preview = Component->ScriptClass.empty() ? FString("Select a script...") : Component->ScriptClass;

        const float ButtonWidth = ImGui::GetFrameHeight();
        const float Spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 2.0f * ButtonWidth - 2.0f * Spacing);
        const int32 Picked = ImGuiX::SearchableCombo(
            "##CSharpScript", Preview.c_str(), (int32)Types.size(), Current,
            [&Types](int32 Index) { return FFixedString(Types[Index].c_str(), Types[Index].size()); },
            LE_ICON_LANGUAGE_CSHARP);
        ImGui::PopItemWidth();

        if (Picked != INDEX_NONE && Picked != Current)
        {
            FString NewClass = Types[Picked];
            PendingMutation = [Component, NewClass] { RebindScript(Component, NewClass); };
            bWasChanged = true;
        }

        // Open the bound script's source file in the user's IDE (the auto-generated <Project>.Scripts.csproj
        // beside it supplies full IntelliSense). Resolved by the class declaration, not the filename.
        ImGui::SameLine();
        ImGui::BeginDisabled(Component->ScriptClass.empty());
        if (ImGui::Button(LE_ICON_OPEN_IN_NEW "##OpenCSharpScript", ImVec2(ButtonWidth, 0)))
        {
            OpenScriptSource(Component->ScriptClass);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Open script in your IDE");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(Component->ScriptClass.empty());
        if (ImGui::Button(LE_ICON_CLOSE "##ClearCSharpScript", ImVec2(ButtonWidth, 0)))
        {
            PendingMutation = [Component] { RebindScript(Component, FString()); };
            bWasChanged = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Clear script");
        }

        if (!Component->ScriptClass.empty() && DrawScriptProperties(Component))
        {
            bWasChanged = true;
        }

        ImGui::PopID();

        if (bWasChanged)
        {
            if (bFinishPending)
            {
                return EPropertyChangeOp::Updated;
            }
            bFinishPending = true;
            return EPropertyChangeOp::Started;
        }
        if (bFinishPending)
        {
            bFinishPending = false;
            return EPropertyChangeOp::Finished;
        }
        return EPropertyChangeOp::None;
    }

    void FCSharpScriptComponentPropertyCustomization::UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property)
    {
        // Runs after BeginTransaction so the undo snapshot captured the pre-change state.
        if (PendingMutation)
        {
            PendingMutation();
            PendingMutation = {};
        }
    }

    void FCSharpScriptComponentPropertyCustomization::HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property)
    {
    }
}
