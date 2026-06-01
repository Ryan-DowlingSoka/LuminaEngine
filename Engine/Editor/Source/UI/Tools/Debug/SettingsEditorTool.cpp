#include "SettingsEditorTool.h"

#include "Config/Config.h"
#include "Core/Object/Class.h"

namespace Lumina
{
    namespace
    {
        FString DisplayNameOf(CClass* Class)
        {
            if (Class->HasMeta("DisplayName"))
            {
                return Class->GetMeta("DisplayName");
            }
            return FConfig::GetSettingsSection(Class);
        }

        FString CategoryOf(CClass* Class)
        {
            if (Class->HasMeta("Category"))
            {
                return Class->GetMeta("Category");
            }
            return "General";
        }
    }

    void FSettingsEditorTool::OnInitialize()
    {
        CreateToolWindow("Settings", [this](bool bIsFocused)
        {
            DrawWindow(bIsFocused);
        });
    }

    void FSettingsEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FSettingsEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("What's Listed",
            "Every CDeveloperSettings subclass. Each is drawn with the standard reflected property table, "
            "so it behaves exactly like an asset or component details panel.");
        DrawHelpTextRow("Editing",
            "Changes write immediately to the class's ConfigFile (grouped JSON). Right-click a row > "
            "'Reset to Default' restores the value declared in code.");
        DrawHelpTextRow("Adding settings",
            "Declare a CDeveloperSettings subclass with PROPERTY members and a REFLECT(ConfigFile=...) tag. "
            "It is discovered automatically; read it anywhere via GetDefault<CMySettings>().");
    }

    FPropertyTable* FSettingsEditorTool::GetOrCreateTable(CClass* SettingsClass)
    {
        auto It = Tables.find(SettingsClass);
        if (It != Tables.end())
        {
            return It->second.get();
        }

        CObject* CDO = SettingsClass->GetDefaultObject();
        if (CDO == nullptr)
        {
            return nullptr;
        }

        // Edit the CDO directly (that is where the live config values live); diff/reset against the
        // pristine code-default snapshot the manager captured before loading the file.
        void* Snapshot = GConfig->GetSettingsDefault(SettingsClass);
        TUniquePtr<FPropertyTable> Table = MakeUnique<FPropertyTable>((void*)CDO, SettingsClass, Snapshot);

        CClass* Captured = SettingsClass;
        Table->SetPostEditCallback([Captured](const FPropertyChangedEvent&)
        {
            GConfig->SaveSettings(Captured);
        });

        FPropertyTable* Raw = Table.get();
        Tables.emplace(SettingsClass, Move(Table));
        return Raw;
    }

    void FSettingsEditorTool::DrawWindow(bool bIsFocused)
    {
        if (GConfig == nullptr)
        {
            ImGui::TextDisabled("Config system not initialized.");
            return;
        }

        // Gather discovered classes once per frame.
        TVector<CClass*> Classes;
        GConfig->ForEachSettingsClass([&](CClass* Class)
        {
            Classes.push_back(Class);
        });

        if (Classes.empty())
        {
            ImGui::TextDisabled("No settings registered.");
            return;
        }

        if (SelectedClass == nullptr)
        {
            SelectedClass = Classes[0];
        }

        const float Avail = ImGui::GetContentRegionAvail().x;
        const float LeftWidth = Avail * 0.28f;

        ImGui::BeginChild("##SettingsCategories", ImVec2(LeftWidth, 0), true);
        {
            // Group entries by category; render a header per category, a selectable per class.
            FString CurrentCategory;
            for (CClass* Class : Classes)
            {
                const FString Category = CategoryOf(Class);
                if (Category != CurrentCategory)
                {
                    CurrentCategory = Category;
                    ImGui::SeparatorText(Category.c_str());
                }

                const FString Label = DisplayNameOf(Class);
                if (ImGui::Selectable(Label.c_str(), SelectedClass == Class))
                {
                    SelectedClass = Class;
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##SettingsDetails", ImVec2(0, 0), true);
        {
            if (FPropertyTable* Table = GetOrCreateTable(SelectedClass))
            {
                ImGui::TextUnformatted(DisplayNameOf(SelectedClass).c_str());
                ImGui::Separator();
                Table->DrawTree();
            }
        }
        ImGui::EndChild();
    }
}
