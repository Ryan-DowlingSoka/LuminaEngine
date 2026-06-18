#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/String.h"

namespace Lumina
{
    // A plugin-contributed entry in the editor Tools menu. Core EditorUI hard-codes its built-in
    // tools; this registry is the extension point that lets an editor plugin surface a standalone
    // tool (one without an asset/file association) WITHOUT editing core. Register at module
    // StartupModule; FEditorUI::DrawToolsMenu renders all entries under a "Plugins" section.
    //
    // The callbacks are type-erased so the registry stays decoupled from any specific tool type:
    // the plugin captures the concrete tool type (and fetches the live FEditorUI itself).
    struct FToolsMenuEntry
    {
        FString           Label;      // menu text, may embed an LE_ICON_* glyph
        TFunction<bool()> IsActive;   // optional; drives the checkmark (tool currently open?)
        TFunction<void()> OnToggle;   // required; open the tool if closed, else close it
    };

    // Process-wide registry of plugin-contributed Tools-menu entries. Entries persist for the
    // editor session (plugins don't unregister); duplicate labels are allowed but discouraged.
    class EDITOR_API FToolsMenuRegistry
    {
    public:

        static FToolsMenuRegistry& Get();

        void Register(FToolsMenuEntry Entry);

        const TVector<FToolsMenuEntry>& GetEntries() const { return Entries; }

        bool IsEmpty() const { return Entries.empty(); }

    private:

        TVector<FToolsMenuEntry> Entries;
    };
}
