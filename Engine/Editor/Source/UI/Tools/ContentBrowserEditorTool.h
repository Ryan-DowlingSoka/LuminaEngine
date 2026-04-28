#pragma once
#include "EditorTool.h"
#include "Assets/AssetRegistry/AssetData.h"
#include "Core/LuminaCommonTypes.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/DirectoryWatcher.h"
#include "Tools/Actions/DeferredActions.h"
#include "Tools/UI/ImGui/imfilebrowser.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "Tools/UI/ImGui/Widgets/TileViewWidget.h"
#include "Tools/UI/ImGui/Widgets/TreeListView.h"

namespace Lumina
{
    class CObjectRedirector;
    struct FAssetData;
}


namespace Lumina
{
    class FContentBrowserEditorTool : public FEditorTool
    {
    public:

        struct FPendingOSDrop
        {
            FFixedString Path;
            ImVec2 MousePos;
        };

        struct FPendingRename
        {
            FFixedString OldName;
            FFixedString NewName;
        };

        struct FPendingDestroy
        {
            FFixedString PendingDestroy;
        };

        struct FContentBrowserListViewItemData
        {
            FFixedString Path;
        };

        class FContentBrowserTileViewItem : public FTileViewItem
        {
        public:
            
            FContentBrowserTileViewItem(FTileViewItem* InParent, const VFS::FFileInfo& InInfo, bool bInProtected)
                : FTileViewItem(InParent)
                , bProtected(bInProtected)
                , FileInfo(InInfo)
            {
            }

            constexpr static const char* DragDropID = "ContentBrowserItem";
            
            void SetDragDropPayloadData() const override
            {
                uintptr_t IntPtr = reinterpret_cast<uintptr_t>(this);
                ImGui::SetDragDropPayload(DragDropID, &IntPtr, sizeof(uintptr_t));
            }

            void DrawTooltip() const override
            {
                ImGuiX::Text("Name: {}", FileInfo.Name);
                ImGuiX::Text("Virtual Path: {}", FileInfo.VirtualPath);
                ImGuiX::Text("Source Path: {}", FileInfo.PathSource);
            }
            
            NODISCARD bool HasContextMenu() override { return true; }

            NODISCARD FStringView GetName() const override
            {
                return VFS::FileName(FileInfo.PathSource, true);
            }
            
            NODISCARD const VFS::FFileInfo& GetFileInfo() const { return FileInfo; }
            NODISCARD FStringView GetPathSource() const { return FileInfo.PathSource; }
            NODISCARD FStringView GetVirtualPath() const { return FileInfo.VirtualPath; }
            NODISCARD bool IsAsset() const { return FileInfo.IsLAsset(); }
            NODISCARD bool IsDirectory() const { return FileInfo.IsDirectory(); }
            NODISCARD bool IsLuaScript() const { return FileInfo.IsLua(); }
            NODISCARD FString GetExtension() const { return FileInfo.GetExt(); }
            NODISCARD bool IsProtected() const { return bProtected; }
            
        private:
            
            bool            bProtected = false;
            VFS::FFileInfo  FileInfo;
        };

        LUMINA_SINGLETON_EDITOR_TOOL(FContentBrowserEditorTool)

        FContentBrowserEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Content Browser", nullptr)
        {
        }
        
        bool OnEvent(FEvent& Event) override;
        
        void RefreshContentBrowser();
        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_FORMAT_LIST_BULLETED_TYPE; }
        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override { }

        void Update(const FUpdateContext& UpdateContext) override;
        void EndFrame() override;

        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

        void DrawToolMenu(const FUpdateContext& UpdateContext) override;

        void HandleContentBrowserDragDrop(FStringView DropPath, FStringView PayloadPath);
        
    private:

        void OpenDeletionWarningPopup(const FContentBrowserTileViewItem* Item, const TFunction<void(EYesNo)>& Callback = TFunction<void(EYesNo)>());
        void OnProjectLoaded();

        void TryImport(const FFixedString& Path);
        
        void PushRenameModal(FContentBrowserTileViewItem* ContentItem);
        
        void DrawDirectoryBrowser(bool bIsFocused, ImVec2 Size);
        void DrawContentBrowser(bool bIsFocused, ImVec2 Size);
        
        void DrawAssetContextMenu(FContentBrowserTileViewItem* ContentItem);
        
        void DrawContentDirectoryContextMenu();
        
        float                       ContentBrowserTileSize = 84.0f;

        FDeferredActionRegistry     ActionRegistry;
        FDirectoryWatcher           Watcher;
        
        FTreeListView               DirectoryListView;
        FTreeListViewContext        DirectoryContext;

        FTileViewWidget             ContentBrowserTileView;
        FTileViewContext            ContentBrowserTileViewContext;

        FFixedString                SelectedPath;
        THashMap<FName, bool>       FilterState;
    };
}
