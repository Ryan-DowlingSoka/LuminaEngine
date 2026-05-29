#include "EditorToolRegistry.h"

#include "Core/Object/Class.h"
#include "Core/Object/Object.h"
#include "FileSystem/FileSystem.h"

namespace Lumina
{
    namespace
    {
        // Extensions are stored lowercase with a leading dot so lookups are
        // case-insensitive regardless of how the path was cased on disk.
        FString NormalizeExtension(FStringView Extension)
        {
            FString Result;
            if (!Extension.empty() && Extension.front() != '.')
            {
                Result.push_back('.');
            }

            for (char Ch : Extension)
            {
                Result.push_back((Ch >= 'A' && Ch <= 'Z') ? char(Ch - 'A' + 'a') : Ch);
            }

            return Result;
        }
    }

    FEditorToolRegistry& FEditorToolRegistry::Get()
    {
        static FEditorToolRegistry Instance;
        return Instance;
    }

    void FEditorToolRegistry::RegisterAssetEditor(CClass* AssetClass, FAssetEditorFactory Factory)
    {
        if (AssetClass == nullptr || !Factory)
        {
            return;
        }

        AssetEditors.insert_or_assign(AssetClass, Move(Factory));
    }

    void FEditorToolRegistry::RegisterFileEditor(FStringView Extension, FFileEditorFactory Factory)
    {
        if (Extension.empty() || !Factory)
        {
            return;
        }

        FileEditors.insert_or_assign(NormalizeExtension(Extension), Move(Factory));
    }

    FEditorTool* FEditorToolRegistry::CreateAssetEditor(IEditorToolContext* Context, CObject* Asset) const
    {
        if (Asset == nullptr)
        {
            return nullptr;
        }

        // Most-derived first: walk up the class chain so a registration for the
        // concrete type wins over one for a base type.
        for (CClass* Class = Asset->GetClass(); Class != nullptr; Class = Class->GetSuperClass())
        {
            auto Itr = AssetEditors.find(Class);
            if (Itr != AssetEditors.end())
            {
                return Itr->second(Context, Asset);
            }
        }

        return nullptr;
    }

    FEditorTool* FEditorToolRegistry::CreateFileEditor(IEditorToolContext* Context, FStringView VirtualPath) const
    {
        const FString Ext = NormalizeExtension(VFS::Extension(VirtualPath));

        auto Itr = FileEditors.find(Ext);
        if (Itr != FileEditors.end())
        {
            return Itr->second(Context, VirtualPath);
        }

        return nullptr;
    }

    bool FEditorToolRegistry::HasFileEditor(FStringView Extension) const
    {
        return FileEditors.find(NormalizeExtension(Extension)) != FileEditors.end();
    }
}
