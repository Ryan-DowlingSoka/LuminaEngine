#pragma once

#include <initializer_list>
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/String.h"
#include "Memory/Memory.h"

namespace Lumina
{
    class CObject;
    class CClass;
    class FEditorTool;
    class IEditorToolContext;
}

namespace Lumina
{
    // Builds an editor tool for a loaded asset. Returned tool is freshly
    // constructed but not yet initialized; FEditorUI owns the lifecycle.
    using FAssetEditorFactory = TFunction<FEditorTool*(IEditorToolContext*, CObject*)>;

    // Builds an editor tool for a raw (non-CObject) file, keyed by extension.
    using FFileEditorFactory = TFunction<FEditorTool*(IEditorToolContext*, FStringView)>;

    // Maps asset classes and file extensions to the editor tool that opens them. Built-ins
    // register at startup; plugins register in StartupModule (EditorInit phase).
    class EDITOR_API FEditorToolRegistry
    {
    public:

        static FEditorToolRegistry& Get();

        // Register an asset editor keyed by asset class. A later registration for
        // the same class overrides the earlier one (lets plugins replace built-ins).
        void RegisterAssetEditor(CClass* AssetClass, FAssetEditorFactory Factory);

        // Convenience: TTool must be constructible from (IEditorToolContext*, CObject*).
        template<typename TAsset, typename TTool>
        void RegisterAssetEditor()
        {
            RegisterAssetEditor(TAsset::StaticClass(), [](IEditorToolContext* Context, CObject* Asset) -> FEditorTool*
            {
                return Memory::New<TTool>(Context, Asset);
            });
        }

        // Register a file editor keyed by extension (leading-dot form, e.g. ".rml";
        // case-insensitive). A later registration overrides the earlier one.
        void RegisterFileEditor(FStringView Extension, FFileEditorFactory Factory);

        // Convenience: TTool must be constructible from (IEditorToolContext*, FStringView).
        template<typename TTool>
        void RegisterFileEditor(std::initializer_list<FStringView> Extensions)
        {
            FFileEditorFactory Factory = [](IEditorToolContext* Context, FStringView Path) -> FEditorTool*
            {
                return Memory::New<TTool>(Context, Path);
            };

            for (FStringView Ext : Extensions)
            {
                RegisterFileEditor(Ext, Factory);
            }
        }

        // Walks the asset's class hierarchy (most-derived first) and constructs the
        // first matching editor. Returns nullptr if no class in the chain is registered.
        FEditorTool* CreateAssetEditor(IEditorToolContext* Context, CObject* Asset) const;

        // Constructs the editor registered for the file's extension, or nullptr.
        FEditorTool* CreateFileEditor(IEditorToolContext* Context, FStringView VirtualPath) const;

        // True if some editor is registered for the given file extension.
        bool HasFileEditor(FStringView Extension) const;

    private:

        THashMap<CClass*, FAssetEditorFactory> AssetEditors;
        THashMap<FString, FFileEditorFactory>  FileEditors;
    };
}
