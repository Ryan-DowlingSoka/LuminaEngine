#include "ContentBrowserEditorTool.h"

#include "EditorToolContext.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetRegistry/TextAssetTypes.h"
#include "Assets/AssetRegistry/TextAssetSidecar.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Assets/Factories/Factory.h"
#include "Core/Object/Package/Package.h"
#include "EASTL/sort.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "Platform/Process/PlatformProcess.h"
#include "Scripting/Lua/Scripting.h"
#include "TaskSystem/TaskSystem.h"
#include "TaskSystem/ThreadedCallback.h"
#include "Tools/Dialogs/Dialogs.h"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "World/WorldManager.h"
#include <string.h>
#include <cstdarg>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <Assets/AssetRegistry/AssetData.h>
#include <Containers/Array.h>
#include <Containers/Function.h>
#include <Containers/String.h>
#include <Core/LuminaCommonTypes.h>
#include <Core/Math/Hash/Hash.h>
#include <Core/Object/Class.h>
#include <Core/Object/Object.h>
#include <Core/Object/ObjectCore.h>
#include <Core/Templates/LuminaTemplate.h>
#include <Events/Event.h>
#include <FileSystem/FileInfo.h>
#include <Memory/SmartPtr.h>
#include <Core/Plugin/Plugin.h>
#include <Core/Plugin/PluginManager.h>
#include <Memory/SmartPtr.h>
#include <Platform/Filesystem/DirectoryWatcher.h>
#include <Platform/GenericPlatform.h>
#include <Platform/Platform.h>
#include <Tools/UI/ImGui/ImGuiDesignIcons.h>
#include <Tools/UI/ImGui/Widgets/TileViewWidget.h>
#include <Tools/UI/ImGui/Widgets/TreeListView.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "Core/Object/Package/Thumbnail/PackageThumbnail.h"
#include "Thumbnails/ThumbnailManager.h"
#include <LuminaEditor.h>

#include "Config/Config.h"
#include "Core/Object/ObjectCore.h"
#include "Settings/EditorSettings.h"
#include "Tools/Import/ImportHelpers.h"

namespace Lumina
{
    // Starter contents for a freshly created Lua entity script. Lives at
    // Templates/Scripts/EntityScript.luau so it can be edited without a
    // rebuild; the inline fallback only fires if that file is missing.
    FString LoadNewEntityScriptTemplate()
    {
        const FString& EngineDir = Paths::GetEngineInstallDirectory();
        if (!EngineDir.empty())
        {
            const FFixedString TemplatePath =
                Paths::Combine(EngineDir, "Templates", "Scripts", "EntityScript.luau");

            std::ifstream Input(TemplatePath.c_str(), std::ios::binary);
            if (Input.is_open())
            {
                std::string Contents(
                    (std::istreambuf_iterator<char>(Input)),
                    std::istreambuf_iterator<char>());
                if (!Contents.empty())
                {
                    return FString(Contents.c_str(), Contents.size());
                }
            }
        }

        return
            "local EntityScript = require(\"Stdlib/EntityScript\")\n"
            "local Script: EntityScript = EntityScript.new()\n"
            "\n"
            "function Script:OnReady()\n"
            "end\n"
            "\n"
            "-- Per-frame tick in play mode. Define it and the engine ticks this entity.\n"
            "function Script:OnUpdate(DeltaTime)\n"
            "end\n"
            "\n"
            "return Script\n";
    }

    namespace
    {

        constexpr ImVec4 kMenuBg            = ImVec4(0.10f, 0.10f, 0.12f, 0.98f);
        constexpr ImVec4 kMenuBorder        = ImVec4(0.22f, 0.23f, 0.27f, 1.00f);
        constexpr ImVec4 kMenuText          = ImVec4(0.90f, 0.90f, 0.93f, 1.00f);
        constexpr ImVec4 kMenuTextDim       = ImVec4(0.55f, 0.56f, 0.62f, 1.00f);
        constexpr ImVec4 kMenuTextSection   = ImVec4(0.50f, 0.58f, 0.72f, 1.00f);
        constexpr ImVec4 kMenuAccent        = ImVec4(0.36f, 0.66f, 1.00f, 1.00f);
        constexpr ImVec4 kMenuAccentFolder  = ImVec4(1.00f, 0.78f, 0.40f, 1.00f);
        constexpr ImVec4 kMenuAccentScript  = ImVec4(0.52f, 0.85f, 0.55f, 1.00f);
        constexpr ImVec4 kMenuDanger        = ImVec4(0.96f, 0.36f, 0.38f, 1.00f);
        constexpr ImVec4 kMenuDangerHover   = ImVec4(0.85f, 0.22f, 0.24f, 0.45f);
        constexpr ImVec4 kMenuHeaderHover   = ImVec4(0.24f, 0.46f, 0.78f, 0.55f);
        constexpr ImVec4 kMenuHeader        = ImVec4(0.24f, 0.46f, 0.78f, 0.30f);
        constexpr ImVec4 kMenuHeaderActive  = ImVec4(0.24f, 0.46f, 0.78f, 0.85f);
        constexpr ImVec4 kMenuSeparator     = ImVec4(0.24f, 0.25f, 0.30f, 0.65f);
        constexpr ImVec4 kMenuHeaderBg      = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);

        // Popup-window-level styles. Push BEFORE BeginPopup* so the popup window picks them up.
        void PushContextMenuWindowStyle()
        {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, kMenuBg);
            ImGui::PushStyleColor(ImGuiCol_Border,  kMenuBorder);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,   8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(6.0f, 6.0f));
        }

        void PopContextMenuWindowStyle()
        {
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(2);
        }

        // Per-item styles. Push inside BeginPopup..EndPopup for consistent menu items.
        void PushContextMenuItemStyle()
        {
            ImGui::PushStyleColor(ImGuiCol_Text,          kMenuText);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kMenuHeaderHover);
            ImGui::PushStyleColor(ImGuiCol_Header,        kMenuHeader);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  kMenuHeaderActive);
            ImGui::PushStyleColor(ImGuiCol_Separator,     kMenuSeparator);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,    4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(8.0f, 3.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,     ImVec2(10.0f, 5.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8.0f, 4.0f));
        }

        void PopContextMenuItemStyle()
        {
            ImGui::PopStyleVar(4);
            ImGui::PopStyleColor(5);
        }

        void DrawMenuSection(const char* Label)
        {
            ImGui::Spacing();
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::TinyBold);
            ImGui::PushStyleColor(ImGuiCol_Text, kMenuTextSection);
            const float OldX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(OldX + 4.0f);
            ImGui::TextUnformatted(Label);
            ImGui::PopStyleColor();
            ImGuiX::Font::PopFont();
        }

        void DrawMenuHeader(const char* Icon, const char* TitleStr, const char* SubtitleStr, const ImVec4& IconColor)
        {
            const bool bHasSubtitle = SubtitleStr && *SubtitleStr;

            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::SmallBold);
            const float TitleH = ImGui::GetTextLineHeight();
            ImGuiX::Font::PopFont();

            float SubH = 0.0f;
            if (bHasSubtitle)
            {
                ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Tiny);
                SubH = ImGui::GetTextLineHeight();
                ImGuiX::Font::PopFont();
            }

            constexpr float TopPad   = 3.0f;
            constexpr float BotPad   = 3.0f;
            const     float Gap      = bHasSubtitle ? 0.0f : 0.0f;
            const     float HeaderH  = TopPad + TitleH + Gap + SubH + BotPad;

            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            const float  Avail = ImGui::GetContentRegionAvail().x;
            const ImVec2 P0    = ImGui::GetCursorScreenPos();
            const ImVec2 P1    = ImVec2(P0.x + Avail, P0.y + HeaderH);

            DrawList->AddRectFilled(P0, P1, ImGui::ColorConvertFloat4ToU32(kMenuHeaderBg), 4.0f);
            DrawList->AddRectFilled(P0, ImVec2(P0.x + 3.0f, P1.y), ImGui::ColorConvertFloat4ToU32(IconColor), 4.0f);

            ImGui::SetCursorScreenPos(ImVec2(P0.x + 9.0f, P0.y + TopPad));
            ImGui::PushStyleColor(ImGuiCol_Text, IconColor);
            ImGui::TextUnformatted(Icon);
            ImGui::PopStyleColor();

            ImGui::SameLine(0, 6.0f);
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::SmallBold);
            ImGui::PushStyleColor(ImGuiCol_Text, kMenuText);
            ImGui::TextUnformatted(TitleStr);
            ImGui::PopStyleColor();
            ImGuiX::Font::PopFont();

            if (bHasSubtitle)
            {
                ImGui::SetCursorScreenPos(ImVec2(P0.x + 9.0f, P0.y + TopPad + TitleH));
                ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Tiny);
                ImGui::PushStyleColor(ImGuiCol_Text, kMenuTextDim);
                ImGui::TextUnformatted(SubtitleStr);
                ImGui::PopStyleColor();
                ImGuiX::Font::PopFont();
            }

            ImGui::SetCursorScreenPos(ImVec2(P0.x, P1.y));
            ImGui::Dummy(ImVec2(Avail, 1.0f));
        }
    }

    template<size_t BufferSize = 42>
    class FRenameModalState
    {
    public:
        
        void Initialize(FStringView CurrentName)
        {
            Buffer.assign(CurrentName.begin(), CurrentName.end());
        }

        
        // ReSharper disable once CppMemberFunctionMayBeStatic
        NODISCARD constexpr size_t Capacity() const { return BufferSize; }
        FORCEINLINE NODISCARD char* CStr() { return Buffer.data(); }
        FORCEINLINE NODISCARD bool IsValid() const { return !Buffer.empty(); }
        
    private:
        
        TFixedString<BufferSize> Buffer;
    };

    bool FContentBrowserEditorTool::OnEvent(FEvent& Event)
    {
        if (Event.IsA<FFileDropEvent>())
        {
            FFileDropEvent& FileEvent = Event.As<FFileDropEvent>();

            ImVec2 DropCursor = ImVec2(FileEvent.GetMouseX(), FileEvent.GetMouseY());

            for (const FFixedString& Path : FileEvent.GetPaths())
            {
                ActionRegistry.EnqueueAction<FPendingOSDrop>(FPendingOSDrop{ Path, DropCursor });
            }

            return true;
        }

        return false;
    }

    void FContentBrowserEditorTool::RefreshContentBrowser()
    {
        ContentBrowserTileView.MarkTreeDirty();
        DirectoryListView.MarkTreeDirty();
    }

    void FContentBrowserEditorTool::OnInitialize()
    {
        (void)FAssetRegistry::Get().GetOnAssetRegistryUpdated().AddMember(this, &FContentBrowserEditorTool::RefreshContentBrowser);
        (void)GEditorEngine->GetProjectLoadedDelegate().AddMember(this, &FContentBrowserEditorTool::OnProjectLoaded);
        
        ContentBrowserTileSize = GetDefault<CContentBrowserSettings>()->TileSize;
        ContentBrowserTileView.SetTileSize(ContentBrowserTileSize);

        if (GEditorEngine->HasLoadedProject())
        {
            // Virtual mount path, not the native content dir, the browser iterates VFS.
            SelectedPath = "/Game";
        }

        const TVector<CFactory*>& Factories = CFactoryRegistry::Get().GetFactories();
        for (CFactory* Factory : Factories)
        {
            if (CClass* AssetClass = Factory->GetAssetClass())
            {
                FilterState.emplace(AssetClass->GetName().c_str(), true);
            }
        }
        
        CreateToolWindow("Content", [&] (bool bIsFocused)
        {
            float Left = 225.0f;
            float Right = ImGui::GetContentRegionAvail().x - Left;
            
            DrawDirectoryBrowser(bIsFocused, ImVec2(Left, 0));
            
            ImGui::SameLine();

            DrawContentBrowser(bIsFocused, ImVec2(Right, 0));
        });
        
        ContentBrowserTileViewContext.DragDropFunction = [this] (FTileViewItem* DropItem, const TVector<FTileViewItem*>& Selections)
        {
            auto* TypedDroppedItem = static_cast<FContentBrowserTileViewItem*>(DropItem);
            if (!TypedDroppedItem->IsDirectory())
            {
                return;
            }

            const DragDrop::FPayload* Peek = DragDrop::PeekPayload();
            if (Peek == nullptr || !DragDrop::IsDelivered())
            {
                return;
            }

            FStringView SourcePath;
            if (Peek->Kind == DragDrop::EPayloadKind::Asset)
            {
                SourcePath = FStringView(Peek->AssetPath.c_str(), Peek->AssetPath.size());
            }
            else if (Peek->Kind == DragDrop::EPayloadKind::File)
            {
                SourcePath = FStringView(Peek->FilePath.c_str(), Peek->FilePath.size());
            }
            else
            {
                return;
            }

            if (SourcePath != TypedDroppedItem->GetVirtualPath())
            {
                HandleContentBrowserDragDrop(TypedDroppedItem->GetVirtualPath(), SourcePath);
            }

            for (FTileViewItem* Item : Selections)
            {
                auto* SourceItem = reinterpret_cast<FContentBrowserTileViewItem*>(Item);

                if (SourceItem->GetVirtualPath() == SourcePath)
                {
                    continue;
                }

                if (SourceItem == TypedDroppedItem)
                {
                    continue;
                }

                HandleContentBrowserDragDrop(TypedDroppedItem->GetVirtualPath(), SourceItem->GetVirtualPath());
            }
        };

        ContentBrowserTileViewContext.DrawItemOverrideFunction = [this] (FTileViewItem* Item)
        {
            FContentBrowserTileViewItem* ContentItem = static_cast<FContentBrowserTileViewItem*>(Item);
            
            ImVec4 TintColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

            // Cache static icon refs; asset thumbnails are re-queried per frame until they stream in.
            ImTextureRef ImTexture;
            switch (ContentItem->GetIconKind())
            {
            case EIconKind::Directory:
                {
                    ImTexture = ImGuiX::ToImTextureRef(Paths::GetEngineResourceDirectory() + "/Textures/Folder.png");
                    TintColor  = ImVec4(1.0f, 0.9f, 0.6f, 1.0f);
                    break;
                }
            case EIconKind::Asset:
                {
                    if (FPackageThumbnail* MaybeThumbnail = CThumbnailManager::Get().GetThumbnailForPackage(ContentItem->GetVirtualPath()))
                    {
                        ImTexture = ImGuiX::ToImTextureRef(MaybeThumbnail->LoadedImage);
                    }
                    else
                    {
                        ImTexture = ImGuiX::ToImTextureRef(Paths::GetEngineResourceDirectory() + "/Textures/Asset.png");
                    }
                    break;
                }
            case EIconKind::LuaScript:
                {
                    ImTexture = ImGuiX::ToImTextureRef(Paths::GetEngineResourceDirectory() + "/Textures/LuaScript.png");
                    break;
                }
            case EIconKind::Markup:
                {
                    ImTexture = ImGuiX::ToImTextureRef(Paths::GetEngineResourceDirectory() + "/Textures/rmlui.png");
                    break;
                }
            case EIconKind::Audio:
                {
                    ImTexture = ImGuiX::ToImTextureRef(Paths::GetEngineResourceDirectory() + "/Textures/Audio.png");
                    break;
                }
            case EIconKind::Generic:
                break;
            }

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.17f, 1.0f)); 
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.26f, 0.28f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        
            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImVec2 Pos = ImGui::GetCursorScreenPos();
            ImVec2 Size = ImVec2(ContentBrowserTileView.GetTileSize(), ContentBrowserTileView.GetTileSize());
            
            DrawList->AddRectFilled(
                ImVec2(Pos.x + 3, Pos.y + 3),
                ImVec2(Pos.x + Size.x + 11, Pos.y + Size.y + 11),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.3f)),
                8.0f
            );
            
            ImGui::ImageButton("##", ImTexture, Size, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), TintColor);
        
            if (ImGui::IsItemHovered())
            {
                DrawList->AddRect(
                    Pos, 
                    ImVec2(Pos.x + Size.x + 8, Pos.y + Size.y + 8), 
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.6f, 0.9f, 0.7f)), 
                    8.0f, 
                    0, 
                    2.0f
                );
            }
            
            if (Item->IsSelected())
            {
                DrawList->AddRect(
                    Pos, 
                    ImVec2(Pos.x + Size.x + 8, Pos.y + Size.y + 8), 
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.6f, 0.1f, 0.9f)), 
                    8.0f, 
                    0, 
                    2.5f
                ); 
            }
        
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
                {
                    return FTileViewItem::EClickState::SingleWithCtrl;
                }
                
                return FTileViewItem::EClickState::Single;
            }
            
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                return FTileViewItem::EClickState::Double;
            }
        
            return FTileViewItem::EClickState::None;
        };
        
        ContentBrowserTileViewContext.ItemDoubleClickedFunction = [this] (FTileViewItem* Item)
        {
            FContentBrowserTileViewItem* ContentItem = static_cast<FContentBrowserTileViewItem*>(Item);
            FFixedString Path {ContentItem->GetVirtualPath().data(), ContentItem->GetVirtualPath().size()};
            
            if (ContentItem->IsDirectory())
            {
                SelectedPath = Move(Path);
                RefreshContentBrowser();
            }
            else if (ContentItem->IsAsset())
            {
                if (const FAssetData* Asset = FAssetRegistry::Get().GetAssetByPath(Path))
                {
                    ToolContext->OpenAssetEditor(Asset->AssetGUID);
                }
            }
            else
            {
                // Non-CObject files (e.g. .rml): OpenFileEditor falls back to OS launcher if no editor is registered.
                ToolContext->OpenFileEditor(ContentItem->GetVirtualPath());
            }
        };
        
        ContentBrowserTileViewContext.ItemSelectedFunction = [this] (FTileViewItem* Item)
        {
            
        };
        
        ContentBrowserTileViewContext.DrawItemContextMenuFunction = [this] (const TVector<FTileViewItem*>& Items)
        {
            bool bMultipleItems = Items.size() > 1;
            
            for (FTileViewItem* Item : Items)
            {
                FContentBrowserTileViewItem* ContentItem = static_cast<FContentBrowserTileViewItem*>(Item);

                if (bMultipleItems)
                {
                    continue;
                }

                DrawAssetContextMenu(ContentItem);

            }
        };

        ContentBrowserTileViewContext.RebuildTreeFunction = [this] (FTileViewWidget* Tree)
        {
            // The Filter menu toggles asset classes. Directories, scripts, and loose files are not
            // class-filterable, so they are always shown; assets are hidden when their class is off.
            auto PassesFilter = [this](const VFS::FFileInfo& Info) -> bool
            {
                if (!Info.IsLAsset())
                {
                    return true;
                }

                const FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(FStringView(Info.VirtualPath.c_str(), Info.VirtualPath.size()));
                if (Data == nullptr)
                {
                    return true;
                }

                auto It = FilterState.find(Data->AssetClass);
                return It == FilterState.end() || It->second;
            };

            TVector<VFS::FFileInfo> SortedPaths;

            VFS::DirectoryIterator(SelectedPath, [&](const VFS::FFileInfo& FileInfo)
            {
                // Hide dot-entries (e.g. the .lmeta identity-sidecar tree) and OS-hidden files.
                if (FileInfo.IsHidden() || (!FileInfo.Name.empty() && FileInfo.Name[0] == '.') || !PassesFilter(FileInfo))
                {
                    return;
                }

                SortedPaths.emplace_back(FileInfo);
            });
            
            eastl::sort(SortedPaths.begin(), SortedPaths.end(), [&](const VFS::FFileInfo& LHS, const VFS::FFileInfo& RHS)
            {
                if (LHS.IsDirectory() != RHS.IsDirectory())
                {
                    return LHS.IsDirectory();
                }
                
                return LHS.Name < RHS.Name;
            });
            
            for (const VFS::FFileInfo& Info : SortedPaths)
            {
                if (Info.VirtualPath == "/Game")
                {
                    ContentBrowserTileView.AddItemToTree<FContentBrowserTileViewItem>(nullptr, Info, true);
                }
                else
                {
                    ContentBrowserTileView.AddItemToTree<FContentBrowserTileViewItem>(nullptr, Info, false);
                }
                
            }
        };

        ContentBrowserTileViewContext.KeyPressedFunction = [this] (FTileViewItem& Item, ImGuiKey Key) -> bool
        {
            if (Key == ImGuiKey_F2)
            {
                FContentBrowserTileViewItem* ContentItem = static_cast<FContentBrowserTileViewItem*>(&Item);
                PushRenameModal(ContentItem);
                return true;
            }

            if (Key == ImGuiKey_Delete)
            {
                FContentBrowserTileViewItem* ContentItem = static_cast<FContentBrowserTileViewItem*>(&Item);
                if (ContentItem->IsProtected())
                {
                    ImGuiX::Notifications::NotifyError("Cannot delete a core directory");
                    return true;
                }
                
                OpenDeletionWarningPopup(ContentItem);
                return true;
            }

            return false;
        };
        
        DirectoryContext.ItemContextMenuFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {

        };

        DirectoryContext.DragDropFunction = [this](FTreeListView& Tree, FTreeNodeID Item)
        {
            FContentBrowserListViewItemData& Data = Tree.Get<FContentBrowserListViewItemData>(Item);

            const DragDrop::FPayload* Peek = DragDrop::PeekPayload();
            if (Peek == nullptr || !DragDrop::IsDelivered())
            {
                return;
            }

            FStringView SourcePath;
            if (Peek->Kind == DragDrop::EPayloadKind::Asset)
            {
                SourcePath = FStringView(Peek->AssetPath.c_str(), Peek->AssetPath.size());
            }
            else if (Peek->Kind == DragDrop::EPayloadKind::File)
            {
                SourcePath = FStringView(Peek->FilePath.c_str(), Peek->FilePath.size());
            }
            else
            {
                return;
            }

            HandleContentBrowserDragDrop(Data.Path, SourcePath);

            for (FTileViewItem* TileItem : ContentBrowserTileView.GetSelections())
            {
                auto* SourceItem = static_cast<FContentBrowserTileViewItem*>(TileItem);

                if (SourceItem->GetVirtualPath() == SourcePath)
                {
                    continue;
                }

                HandleContentBrowserDragDrop(Data.Path, SourceItem->GetVirtualPath());
            }
        };
        
        // Helper: add a single folder node, flag it as having lazy children if it actually has subdirectories.
        auto AddFolderNode = [this](FTreeListView& Tree, FTreeNodeID Parent, const VFS::FFileInfo& Info)
        {
            FFixedString DisplayName;
            DisplayName.append(LE_ICON_FOLDER).append(" ").append(Info.Name.begin(), Info.Name.end());

            FTreeNodeID ItemEntity = Tree.CreateNode(Parent, FStringView(DisplayName.data(), DisplayName.length()), Hash::GetHash64(Info.PathSource));
            Tree.EmplaceUserData<FContentBrowserListViewItemData>(ItemEntity).Path.assign(Info.VirtualPath.begin(), Info.VirtualPath.end());

            FTreeNodeDisplay& FolderDisplay = Tree.Get<FTreeNodeDisplay>(ItemEntity);
            FolderDisplay.IconText = LE_ICON_FOLDER;
            FolderDisplay.IconColor = ImVec4(0.93f, 0.79f, 0.36f, 1.0f);

            if (Info.VirtualPath == SelectedPath)
            {
                FTreeNodeState& State = Tree.Get<FTreeNodeState>(ItemEntity);
                State.bSelected = true;
            }

            // Probe for at least one subdirectory; if any exists, mark lazy so the arrow appears.
            bool bHasSubdirs = false;
            VFS::DirectoryIterator(Info.VirtualPath, [&](const VFS::FFileInfo& Child)
            {
                if (Child.IsDirectory())
                {
                    bHasSubdirs = true;
                }
            });
            if (bHasSubdirs)
            {
                Tree.MarkHasLazyChildren(ItemEntity);
            }
            return ItemEntity;
        };

        DirectoryContext.RebuildTreeFunction = [this, AddFolderNode](FTreeListView& Tree)
        {
            // Roots are always built; their immediate children are loaded on first expand.
            auto AddRoot = [&](const char* Path, const char* Label)
            {
                FFixedString Name;
                Name.assign(LE_ICON_FOLDER).append(" ").append(Label);
                FTreeNodeID RootItem = Tree.CreateNode(InvalidTreeNode, FStringView(Name.data(), Name.length()), Hash::GetHash64(FStringView(Path).data(), FStringView(Path).length()));
                Tree.EmplaceUserData<FContentBrowserListViewItemData>(RootItem).Path = Path;

                FTreeNodeDisplay& RootDisplay = Tree.Get<FTreeNodeDisplay>(RootItem);
                RootDisplay.IconText = LE_ICON_FOLDER;
                RootDisplay.IconColor = ImVec4(0.93f, 0.79f, 0.36f, 1.0f);

                Tree.MarkHasLazyChildren(RootItem);
                return RootItem;
            };
            AddRoot("/Game", "Game");
            AddRoot("/Engine/Resources/Content", "Engine");
        };

        DirectoryContext.BuildChildrenFunction = [this, AddFolderNode](FTreeListView& Tree, FTreeNodeID Parent)
        {
            FContentBrowserListViewItemData& Data = Tree.Get<FContentBrowserListViewItemData>(Parent);
            VFS::DirectoryIterator(FStringView(Data.Path.data(), Data.Path.length()), [&](const VFS::FFileInfo& Info)
            {
                if (!Info.IsDirectory())
                {
                    return;
                }
                AddFolderNode(Tree, Parent, Info);
            });
        };

        DirectoryContext.ItemSelectedFunction = [this] (FTreeListView& Tree, FTreeNodeID Item, bool)
        {
            if (!Item.IsValid())
            {
                return;
            }

            FContentBrowserListViewItemData& Data = Tree.Get<FContentBrowserListViewItemData>(Item);

            SelectedPath = Data.Path;

            RefreshContentBrowser();
        };

        DirectoryContext.KeyPressedFunction = [this] (FTreeListView& Tree, FTreeNodeID Item, ImGuiKey Key) -> bool
        {
            return false;
        };
        
        DirectoryListView.MarkTreeDirty();
        ContentBrowserTileView.MarkTreeDirty();
    }

    void FContentBrowserEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        
    }
    
    // True while a Play-In-Editor or Simulate session is running. Deleting an asset out from under a
    // live world can free objects the simulation still references, so deletes are blocked meanwhile.
    static bool IsAnyWorldPlayingOrSimulating()
    {
        if (GWorldManager == nullptr)
        {
            return false;
        }
        for (const TUniquePtr<FWorldContext>& Context : GWorldManager->GetContexts())
        {
            if (Context && (Context->Type == EWorldType::Game || Context->Type == EWorldType::Simulation))
            {
                return true;
            }
        }
        return false;
    }

    void FContentBrowserEditorTool::EndFrame()
    {
        bool bWroteSomething = false;

        // Drop (don't queue) any delete requests while playing/simulating -- one notification covers them.
        const bool bWorldActive = IsAnyWorldPlayingOrSimulating();
        bool bBlockNotified = false;

        ActionRegistry.ProcessAllOf<FPendingDestroy>([&] (const FPendingDestroy& Destroy)
        {
            if (bWorldActive)
            {
                if (!bBlockNotified)
                {
                    ImGuiX::Notifications::NotifyError("Cannot delete assets while playing or simulating. Stop play first.");
                    bBlockNotified = true;
                }
                return;
            }

            if (VFS::IsDirectory(Destroy.PendingDestroy))
            {
                // Text-asset sidecars live in the hidden .lmeta tree (not under this folder), so collect
                // contained text files first and drop their identities explicitly after the bulk remove.
                TVector<FFixedString> TextPaths;
                VFS::RecursiveDirectoryIterator(Destroy.PendingDestroy, [&](const VFS::FFileInfo& FileInfo)
                {
                    if (FileInfo.IsDirectory()) return;
                    const FStringView Vp(FileInfo.VirtualPath.c_str(), FileInfo.VirtualPath.size());
                    if (TextAsset::IsTextAssetPath(Vp))
                    {
                        TextPaths.emplace_back(FileInfo.VirtualPath);
                    }
                });

                VFS::RemoveAll(Destroy.PendingDestroy);

                for (const FFixedString& Tp : TextPaths)
                {
                    FAssetRegistry::Get().TextAssetDeleted(FStringView(Tp.c_str(), Tp.size()));
                }

                ImGuiX::Notifications::NotifySuccess("Deleted Directory {0}", Destroy.PendingDestroy);
                bWroteSomething = true;
                return;
            }

            if (VFS::HasExtension(Destroy.PendingDestroy, ".lasset"))
            {
                CObject* AliveObject = nullptr;
                if (const FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(Destroy.PendingDestroy))
                {
                    if (CObject* Object = FindObject<CObject>(Data->AssetGUID))
                    {
                        AliveObject = Object;
                        if (AliveObject->IsA<CWorld>())
                        {
                            ImGuiX::Notifications::NotifyError("Cannot destroy a world that's open {0}", Destroy.PendingDestroy);
                            return;
                        }
                    }
                }

                if (AliveObject)
                {
                    ToolContext->OnDestroyAsset(AliveObject);
                }

                if (CPackage::DestroyPackage(Destroy.PendingDestroy))
                {
                    ImGuiX::Notifications::NotifySuccess("Deleted Asset {0}", Destroy.PendingDestroy);
                    bWroteSomething = true;
                }
                return;
            }

            // Plain file (script, widget, audio, etc.), disk-level remove only.
            if (VFS::Remove(Destroy.PendingDestroy))
            {
                // Drop the text-asset identity + its sidecar.
                if (TextAsset::IsTextAssetPath(Destroy.PendingDestroy))
                {
                    FAssetRegistry::Get().TextAssetDeleted(Destroy.PendingDestroy);
                }
                ImGuiX::Notifications::NotifySuccess("Deleted {0}", Destroy.PendingDestroy);
                bWroteSomething = true;
            }
            else
            {
                ImGuiX::Notifications::NotifyError("Failed to delete {0}", Destroy.PendingDestroy);
            }
		});
        
        ActionRegistry.ProcessAllOf<FPendingRename>([&](FPendingRename& Rename)
        {
            FStringView Extension = VFS::Extension(Rename.OldName);

            if (Extension == ".lasset")
            {
                // RenamePackage owns the disk move + in-memory rename; only update registry on success.
                if (!CPackage::RenamePackage(Rename.OldName, Rename.NewName))
                {
                    ImGuiX::Notifications::NotifyError("Rename Failed: {0}", Rename.OldName);
                    return;
                }

                FAssetRegistry::Get().AssetRenamed(Rename.OldName, Rename.NewName);
                ImGuiX::Notifications::NotifySuccess("Rename Success");
                bWroteSomething = true;
            }
            else if (Extension.empty())
            {
                // Snapshot contained .lasset files before touching the filesystem to map old→new paths.
                struct FFolderRenameEntry
                {
                    FFixedString OldPath;
                    FFixedString NewPath;
                };
                TVector<FFolderRenameEntry> Entries;

                FStringView OldFolder(Rename.OldName.data(), Rename.OldName.size());
                FStringView NewFolder(Rename.NewName.data(), Rename.NewName.size());

                VFS::RecursiveDirectoryIterator(Rename.OldName, [&](const VFS::FFileInfo& FileInfo)
                {
                    if (FileInfo.IsDirectory() || !FileInfo.IsLAsset())
                    {
                        return;
                    }

                    FStringView Old(FileInfo.VirtualPath.data(), FileInfo.VirtualPath.size());
                    if (!Old.starts_with(OldFolder))
                    {
                        return;
                    }

                    FFixedString NewPath(NewFolder.data(), NewFolder.size());
                    NewPath.append(Old.data() + OldFolder.size(), Old.size() - OldFolder.size());

                    Entries.push_back({ FFixedString(Old.data(), Old.size()), Move(NewPath) });
                });

                if (!VFS::Rename(Rename.OldName, Rename.NewName))
                {
                    ImGuiX::Notifications::NotifyError("Folder Rename Failed: {0}", Rename.OldName);
                    return;
                }

                // File names unchanged (only directory portion); no content rewrite needed.
                for (const FFolderRenameEntry& Entry : Entries)
                {
                    CPackage::OnPackageMovedExternally(Entry.OldPath, Entry.NewPath);
                    FAssetRegistry::Get().AssetRenamed(Entry.OldPath, Entry.NewPath);
                }

                // Relocate the identities (and sidecars) of every contained text asset.
                FAssetRegistry::Get().TextAssetFolderRenamed(OldFolder, NewFolder);

                ImGuiX::Notifications::NotifySuccess("Folder Rename Success");
                bWroteSomething = true;
            }
            else
            {
                // Plain file (non-asset)
                if (!VFS::Rename(Rename.OldName, Rename.NewName))
                {
                    ImGuiX::Notifications::NotifyError("Rename Failed: {0}", Rename.OldName);
                    return;
                }
                // Carry the text-asset identity (sidecar) across the rename so references survive.
                if (TextAsset::IsTextAssetPath(Rename.OldName) || TextAsset::IsTextAssetPath(Rename.NewName))
                {
                    FAssetRegistry::Get().TextAssetRenamed(Rename.OldName, Rename.NewName);
                }
                ImGuiX::Notifications::NotifySuccess("Rename Success");
                bWroteSomething = true;
            }
        });


        if (bWroteSomething)
        {
            RefreshContentBrowser();
        }
    }
    
    void FContentBrowserEditorTool::InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const
    {
        ImGuiID topDockID = 0, bottomLeftDockID = 0, bottomCenterDockID = 0, bottomRightDockID = 0;
        ImGui::DockBuilderSplitNode(InDockspaceID, ImGuiDir_Down, 0.5f, &bottomCenterDockID, &topDockID);
        ImGui::DockBuilderSplitNode(bottomCenterDockID, ImGuiDir_Right, 0.66f, &bottomCenterDockID, &bottomLeftDockID);
        ImGui::DockBuilderSplitNode(bottomCenterDockID, ImGuiDir_Right, 0.5f, &bottomRightDockID, &bottomCenterDockID);

        ImGui::DockBuilderDockWindow(GetToolWindowName("Content").c_str(), bottomCenterDockID);
    }

    void FContentBrowserEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Browse",
            "Left panel is the directory tree, right is the tile grid for the selected folder. "
            "Double-click a folder to enter, double-click an asset to open its editor.");
        DrawHelpTextRow("Create",
            "Right-click empty space in the tile grid for the New menu (Material, Prefab, Lua script, etc). "
            "Right-click a folder for create-in-place.");
        DrawHelpTextRow("Import",
            "Drag external files (FBX, PNG, WAV, ...) onto the tile grid to import. "
            "Each importer maps to a CObject asset class.");
        DrawHelpTextRow("Drag & Drop",
            "Drag an asset tile into the world viewport, outliner, or a property field that accepts its type. "
            "Filtering happens at drop-time based on the asset class.");
        DrawHelpTextRow("Filter",
            "Filter menu hides asset classes you don't want to see. View Options changes tile size.");
        DrawHelpTextRow("Rename / Delete",
            "F2 renames; Delete removes. Renames update inbound references via redirectors.");
    }

    void FContentBrowserEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
        if (ImGui::BeginMenu(LE_ICON_FILTER " Filter"))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 2));

            for (auto& [Name, State] : FilterState)
            {
                if (ImGui::Checkbox(Name.c_str(), &State))
                {
                    RefreshContentBrowser();
                }
            }

            ImGui::PopStyleVar(2);
            ImGui::EndMenu();
        }

		if (ImGui::BeginMenu(LE_ICON_COGS " View Options"))
        {
            ImGui::SetNextItemWidth(128.0f);
            if (ImGui::SliderFloat("##Zoom", &ContentBrowserTileSize, 46.0f, 256.0f, "Tile: %.1fx"))
            {
                GetMutableDefault<CContentBrowserSettings>()->TileSize = ContentBrowserTileSize;
                GConfig->SaveSettings(CContentBrowserSettings::StaticClass());
                ContentBrowserTileView.SetTileSize(ContentBrowserTileSize);
            }
            
            ImGui::EndMenu();
        }
    }

    void FContentBrowserEditorTool::HandleContentBrowserDragDrop(FStringView DropPath, FStringView PayloadPath)
    {
        size_t Pos = PayloadPath.find_last_of('/');
        FStringView DirName = (Pos != FString::npos) ? PayloadPath.substr(Pos + 1) : PayloadPath;
        
        FFixedString OldName(PayloadPath.data(), PayloadPath.length());
        FFixedString NewName = Paths::Combine(DropPath, DirName);

        ActionRegistry.EnqueueAction<FPendingRename>(FPendingRename{ OldName, NewName });
    }

    void FContentBrowserEditorTool::OpenDeletionWarningPopup(const FContentBrowserTileViewItem* Item, const TFunction<void(EYesNo)>& Callback)
    {
        if (VFS::IsEmpty(Item->GetVirtualPath()))
        {
            if (Callback)
            {
                Callback(EYesNo::Yes);
            }
            ActionRegistry.EnqueueAction<FPendingDestroy>(FPendingDestroy{ FFixedString(Item->GetVirtualPath().data(), Item->GetVirtualPath().size()) });
        }
        else if (Dialogs::Confirmation("Confirm Deletion", "Are you sure you want to delete \"{0}\"?\n""\nThis action cannot be undone.", Item->GetName()))
        {
            if (Callback)
            {
                Callback(EYesNo::Yes);
            }
            ActionRegistry.EnqueueAction<FPendingDestroy>(FPendingDestroy{ FFixedString(Item->GetVirtualPath().data(), Item->GetVirtualPath().size()) });
        }
        
        if (Callback)
        {
            Callback(EYesNo::No);
        }
    }

    void FContentBrowserEditorTool::OnProjectLoaded()
    {
        // Tear down any prior set; clearing is safe since FDirectoryWatcher's
        // destructor stops its worker thread. Project reload rebuilds from scratch.
        for (FContentWatcher& W : Watchers)
        {
            if (W.Watcher) W.Watcher->Stop();
        }
        Watchers.clear();

        auto TrimTrailingSeparators = [](FFixedString& Path)
        {
            while (!Path.empty() && (Path.back() == '/' || Path.back() == '\\'))
            {
                Path.pop_back();
            }
        };

        auto SpawnWatcher = [this, &TrimTrailingSeparators](FFixedString DiskRoot, FStringView VirtualPrefix)
        {
            Paths::Normalize(DiskRoot);
            TrimTrailingSeparators(DiskRoot);
            if (DiskRoot.empty()) return;

            FContentWatcher Entry;
            Entry.VirtualPrefix.assign_convert(VirtualPrefix.data(), VirtualPrefix.size());
            Entry.WatchRootLen = DiskRoot.size();
            Entry.Watcher      = MakeUnique<FDirectoryWatcher>();

            // Capture prefix + root length by value so the callback is self-contained
            // even if Watchers reallocates (the TUniquePtr'd watcher itself is stable).
            const FFixedString Prefix = Entry.VirtualPrefix;
            const size_t       RootLen = Entry.WatchRootLen;

            auto MakeVirtualPath = [Prefix, RootLen](FStringView AbsPath) -> FFixedString
            {
                FFixedString Out;
                Out.append_convert(Prefix.c_str(), Prefix.size());
                if (AbsPath.size() > RootLen)
                {
                    FStringView Tail = AbsPath.substr(RootLen);
                    if (!Tail.empty() && Tail.front() != '/')
                    {
                        Out.append_convert("/");
                    }
                    Out.append_convert(Tail.data(), Tail.size());
                }
                return Out;
            };

            Entry.Watcher->Watch(DiskRoot, [this, MakeVirtualPath](const FFileEvent& Event)
            {
                const FFixedString RelativePath = MakeVirtualPath(Event.Path);
                const FStringView  RelView(RelativePath.c_str(), RelativePath.size());

                // Our own hidden identity sidecars: ignore so writing one doesn't churn the browser.
                if (TextAssetSidecar::IsSidecarPath(RelView))
                {
                    return;
                }

                // Central content-change signal: subsystems (UI hot-reload, etc.) subscribe and
                // filter by extension, so none has to run its own watcher or hard-code paths.
                FCoreDelegates::OnContentFileModified.Broadcast(RelView);

                // Keep text-asset identities in sync with edits made outside the editor (idempotent for
                // edits we already handled in-process). Sidecar I/O is marshalled to the main thread so it
                // can't race the content-browser handler (which also mutates sidecars) -- concurrent
                // filesystem access on the same sidecar tree caused a sharing-violation crash.
                if (TextAsset::IsTextAssetPath(RelView))
                {
                    const EFileAction Action  = Event.Action;
                    const FFixedString NewPath = RelativePath;
                    const FFixedString OldPath = (Action == EFileAction::Renamed) ? MakeVirtualPath(Event.OldPath) : FFixedString();

                    MainThread::Enqueue([Action, NewPath, OldPath]
                    {
                        FAssetRegistry& Reg = FAssetRegistry::Get();
                        const FStringView New(NewPath.c_str(), NewPath.size());
                        switch (Action)
                        {
                        case EFileAction::Added:   Reg.TextAssetCreated(New); break;
                        case EFileAction::Removed: Reg.TextAssetDeleted(New); break;
                        case EFileAction::Renamed: Reg.TextAssetRenamed(FStringView(OldPath.c_str(), OldPath.size()), New); break;
                        default: break;
                        }
                    });
                }

                if (!VFS::HasExtension(Event.Path, ".luau"))
                {
                    // Non-script text edits still want a browser refresh for add/remove/rename.
                    if (TextAsset::IsTextAssetPath(RelView) && Event.Action != EFileAction::Modified)
                    {
                        RefreshContentBrowser();
                    }
                    return;
                }

                switch (Event.Action)
                {
                case EFileAction::Added:
                    Lua::FScriptingContext::Get().ScriptCreated(RelativePath);
                    RefreshContentBrowser();
                    break;
                case EFileAction::Modified:
                    Lua::FScriptingContext::Get().ScriptReloaded(RelativePath);
                    RefreshContentBrowser();
                    break;
                case EFileAction::Removed:
                    Lua::FScriptingContext::Get().ScriptDeleted(RelativePath);
                    RefreshContentBrowser();
                    break;
                case EFileAction::Renamed:
                {
                    FFixedString RelativeOldPath = MakeVirtualPath(Event.OldPath);
                    Lua::FScriptingContext::Get().ScriptRenamed(RelativePath, RelativeOldPath);
                    RefreshContentBrowser();
                    break;
                }
                }
            });

            Watchers.emplace_back(Move(Entry));
        };

        // Project's /Game content. Always present.
        SpawnWatcher(FFixedString(GEditorEngine->GetProjectContentDirectory()), FStringView("/Game"));

        // Every enabled plugin with a content mount. Same callback shape,
        // virtual prefix is the plugin's mount alias ("/<PluginName>").
        for (const FPlugin* Plugin : FPluginManager::Get().GetAllPlugins())
        {
            if (!Plugin->IsEnabled())        continue;
            if (!Plugin->IsContentMounted()) continue;
            const FString Disk  = Plugin->GetContentDirectory();
            const FString Mount = Plugin->GetMountAlias();
            SpawnWatcher(FFixedString(Disk.c_str(), Disk.size()),
                         FStringView(Mount.c_str(), Mount.size()));
        }

        // Land on the project's /Game root so the browser shows content immediately after a
        // load instead of sitting on a stale/empty path.
        SelectedPath = "/Game";
        RefreshContentBrowser();
    }

    void FContentBrowserEditorTool::TryImport(const FFixedString& Path)
    {
        const TVector<CFactory*>& Factories = CFactoryRegistry::Get().GetFactories();
        for (CFactory* Factory : Factories)
        {
            if (!Factory->CanImport())
            {
                continue;
            }
        
            FStringView Ext = VFS::Extension(Path);
            if (!Factory->IsExtensionSupported(Ext))
            {
                continue;
            }
            
            
            FStringView FileName = VFS::FileName(Path);
            FFixedString DestinationPath = Paths::Combine(SelectedPath, FileName);
            DestinationPath = VFS::MakeUniqueFilePath(DestinationPath);
            
            if (Factory->HasImportDialogue())
            {
                struct FModalState
                {
                    TUniquePtr<Import::FImportSettings> ImportSettings;
                    bool bShouldClose = false;
                };

                // Prepare the import off-thread first (parsing shows the slow-task popup);
                // the options dialog is pushed only once the settings have landed.
                Factory->PrepareImportAsync(Path, DestinationPath,
                    [this, Factory, Path, DestinationPath](TUniquePtr<Import::FImportSettings> Settings)
                    {
                        if (!Settings)
                        {
                            ImGuiX::Notifications::NotifyError("Failed to import: \"{0}\"", Path);
                            return;
                        }

                        auto SharedState = MakeShared<FModalState>();
                        SharedState->ImportSettings = Move(Settings);

                        ToolContext->PushModal("Import", {700, 800},
                            [Factory, Path, DestinationPath, SharedState]() mutable
                            {
                                if (Factory->DrawImportDialogue(Path, DestinationPath, SharedState->ImportSettings, SharedState->bShouldClose))
                                {
                                    Task::AsyncTask(1, 1, [Factory, Path, DestinationPath, ImportSettings = Move(SharedState->ImportSettings)](uint32, uint32, uint32)
                                    {
                                        Factory->Import(Path, DestinationPath, ImportSettings.get());

                                        MainThread::Enqueue([Path]()
                                        {
                                            ImGuiX::Notifications::NotifySuccess("Successfully Imported: \"{0}\"", Path);
                                        });
                                    });
                                }

                                return SharedState->bShouldClose;
                            });
                    });
            }
            else
            {
                Task::AsyncTask(1, 1, [this, Factory, Path = Move(Path), PathString = Move(DestinationPath)] (uint32, uint32, uint32)
                {
                    Factory->Import(Path, PathString, nullptr);
                    
                    MainThread::Enqueue([Path = Move(Path)] ()
                    {
                        ImGuiX::Notifications::NotifySuccess("Successfully Imported: \"{0}\"", Path);
                    });            
                });
            }
        }
    }

    void FContentBrowserEditorTool::PushRenameModal(FContentBrowserTileViewItem* ContentItem)
    {
        ToolContext->PushModal("Rename", ImVec2(480.0f, 300.0f), [this, ContentItem, RenameState = MakeUnique<FRenameModalState<>>()]
        {
            RenameState->Initialize(ContentItem->GetName());
            
            const ImGuiStyle& style = ImGui::GetStyle();
            const float ContentWidth = ImGui::GetContentRegionAvail().x;
            
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::MediumBold);
            ImGuiX::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), LE_ICON_ARCHIVE_EDIT " Rename {0}", ContentItem->IsDirectory() ? "Folder" : "Asset");
            ImGuiX::Font::PopFont();
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Spacing();
            
            ImGuiX::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "Current name:");
            
            ImGui::SameLine();
            
            ImGuiX::TextColored(ImVec4(0.85f, 0.85f, 0.9f, 1.0f), "{0}", ContentItem->GetName());
            
            ImGui::Spacing();
            ImGui::Spacing();
            
            ImGuiX::TextColoredUnformatted(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "New name:");
            
            ImGui::Spacing();
            
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            
            ImGui::SetNextItemWidth(-1);
            
            bool bSubmitted = ImGui::InputText("##RenameInput", RenameState->CStr(), RenameState->Capacity(), ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_EnterReturnsTrue);
            
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            
            ImGui::Spacing();
            ImGui::Spacing();
            
            bool bIsValid = RenameState->IsValid();
            bool bNameUnchanged = strcmp(RenameState->CStr(), ContentItem->GetName().data()) == 0;
            FString ValidationMessage;
            bool bHasError = false;
            
            if (RenameState->IsValid())
            {
                if (bNameUnchanged)
                {
                    ValidationMessage = "Name unchanged - please enter a different name";
                    bHasError = true;
                    bIsValid = false;
                }
                else
                {
                    FStringView Extension = ContentItem->GetExtension();
                    FStringView PathNoExt = VFS::RemoveExtension(ContentItem->GetVirtualPath());
                    FFixedString TestPath = Paths::Combine(PathNoExt, RenameState->CStr());
                    TestPath.append_convert(Extension.data(), Extension.length());
                    
                    if (VFS::Exists(TestPath))
                    {
                        ValidationMessage = std::format("Path already exists: {}", TestPath.c_str()).c_str();
                        bHasError = true;
                        bIsValid = false;
                    }
                }
            }
            
            if (bHasError && !ValidationMessage.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.3f, 0.1f, 0.1f, 0.3f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.8f, 0.2f, 0.2f, 0.4f));
                
                ImGui::BeginChild("##ValidationError", ImVec2(-1, 45.0f), true);
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::Text(LE_ICON_ALERT_OCTAGON);
                ImGui::SameLine();
                ImGui::TextWrapped("%s", ValidationMessage.c_str());
                ImGui::PopStyleColor();
                
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
                
                ImGui::Spacing();
            }
            
            if (bSubmitted && bIsValid)
            {
                FStringView PathNoExt = VFS::RemoveExtension(ContentItem->GetVirtualPath());
                FFixedString TestPath = Paths::Combine(VFS::Parent(PathNoExt), RenameState->CStr());
                TestPath.append_convert(ContentItem->GetExtension());
                
                ActionRegistry.EnqueueAction<FPendingRename>(FPendingRename{ FFixedString(ContentItem->GetVirtualPath().data(), ContentItem->GetVirtualPath().length()), TestPath });
                return true;
            }
            
            ImGui::Spacing();
            
            ImGui::Separator();
            ImGui::Spacing();

            constexpr float ButtonHeight = 32.0f;
            const float ButtonWidth = (ContentWidth - style.ItemSpacing.x) * 0.5f;
            
            if (!bIsValid)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.45f, 0.85f, 1.0f));
            }
            
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            
            if (ImGui::Button(LE_ICON_CHECK " Rename", ImVec2(ButtonWidth, ButtonHeight)))
            {
                if (bIsValid)
                {
                    ImGui::PopStyleColor(3);
                    ImGui::PopStyleVar();

                    FStringView PathNoExt = VFS::RemoveExtension(ContentItem->GetVirtualPath());
                    FFixedString TestPath = Paths::Combine(VFS::Parent(PathNoExt), RenameState->CStr());
                    TestPath.append_convert(ContentItem->GetExtension());
                
                    ActionRegistry.EnqueueAction<FPendingRename>(FPendingRename{ FFixedString(ContentItem->GetVirtualPath().data(), ContentItem->GetVirtualPath().length()), TestPath });
                    return true;
                }
            }
            
            ImGui::PopStyleVar();
            
            if (!bIsValid)
            {
                ImGui::PopItemFlag();
                ImGui::PopStyleVar();
            }
            else
            {
                ImGui::PopStyleColor(3);
            }
            
            ImGui::SameLine();
            
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));
            
            if (ImGui::Button(LE_ICON_CANCEL " Cancel", ImVec2(ButtonWidth, ButtonHeight)))
            {
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar();
                
                return true;
            }
            
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                return true;
            }
            
            return false;
        });
    }

    void FContentBrowserEditorTool::DrawDirectoryBrowser(bool bIsFocused, ImVec2 Size)
    {
        ImGui::BeginChild("Directories", Size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

        DirectoryListView.Draw(DirectoryContext);
        
        ImGui::EndChild();
    }

    void FContentBrowserEditorTool::DrawContentBrowser(bool bIsFocused, ImVec2 Size)
    {
        constexpr float Padding = 1.0f;

        ImVec2 AdjustedSize = ImVec2(Size.x - 2 * Padding, 0.0f);

        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(Padding, Padding));

        ImGui::BeginChild("Content", AdjustedSize, true, ImGuiWindowFlags_None);
        
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("ContentContextMenu");
            ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f, 0.0f), ImVec2(360.0f, FLT_MAX));
        }

        PushContextMenuWindowStyle();

        if (ImGui::BeginPopup("ContentContextMenu"))
        {
            PushContextMenuItemStyle();
            DrawContentDirectoryContextMenu();
            PopContextMenuItemStyle();

            ImGui::EndPopup();
        }

        PopContextMenuWindowStyle();
        
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            ContentBrowserTileView.ClearSelections();
        }
        
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ContentBrowserTileView.GetSelections().empty())
        {
            if (Dialogs::Confirmation("Confirm Deletion", "Are you sure you want to delete these files/directories?\n This action cannot be undone."))
            {
                for (FTileViewItem* Selection : ContentBrowserTileView.GetSelections())
                {
                    FContentBrowserTileViewItem* ContentBrowserItem = static_cast<FContentBrowserTileViewItem*>(Selection);
                    DEBUG_ASSERT(Selection->IsSelected());
                    
                    ActionRegistry.EnqueueAction<FPendingDestroy>(
                    FPendingDestroy
                    { 
                        FFixedString(ContentBrowserItem->GetVirtualPath().data(), ContentBrowserItem->GetVirtualPath().size())
                    });
                }
            }
        }
        
        ImGui::BeginHorizontal("Breadcrumbs");

        auto GameDirPos = SelectedPath.find("Game");
        if (GameDirPos != std::string::npos)
        {
            FFixedString BasePathStr = SelectedPath.substr(0, GameDirPos);
            std::filesystem::path BasePath(BasePathStr.c_str());
            std::filesystem::path RelativePath = std::filesystem::path(SelectedPath.c_str()).lexically_relative(BasePath);
    
            std::filesystem::path BuildingPath = BasePath;
    
            for (auto it = RelativePath.begin(); it != RelativePath.end(); ++it)
            {
                BuildingPath /= *it;
        
                ImGui::PushID(static_cast<int>(std::distance(RelativePath.begin(), it)));
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 2));
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
            
                    if (ImGui::Button(it->string().c_str()))
                    {
                        SelectedPath = BuildingPath.generic_string().c_str();
                        ContentBrowserTileView.MarkTreeDirty();
                    }
            
                    ImGui::PopStyleVar(2);
                }
                ImGui::PopID();
        
                if (std::next(it) != RelativePath.end())
                {
                    ImGui::TextUnformatted(LE_ICON_ARROW_RIGHT);
                }
            }
        }

        ImGui::EndHorizontal();

        ImGui::Separator();

        // Per-item context menu popups are opened from inside the tile view's Draw().
        // Push the popup window styles here so they apply when those popups are created.
        PushContextMenuWindowStyle();
        ContentBrowserTileView.Draw(ContentBrowserTileViewContext);
        PopContextMenuWindowStyle();

        ImVec2 ChildMin = ImGui::GetWindowPos();
        ImVec2 ChildMax = ImVec2(ChildMin.x + ImGui::GetWindowWidth(), ChildMin.y + ImGui::GetWindowHeight());
        
        ImRect Rect(ChildMin, ChildMax);

        ActionRegistry.ProcessAllOf<FPendingOSDrop>([&](const FPendingOSDrop& Drop)
        {
            if (Rect.Contains(Drop.MousePos))
            {
                TryImport(Drop.Path);
            }
		});
        
        ImGui::EndChild();
    
    }

    void FContentBrowserEditorTool::DrawAssetContextMenu(FContentBrowserTileViewItem* ContentItem)
    {
        const bool bIsAsset      = ContentItem->IsAsset();
        const bool bIsDirectory  = ContentItem->IsDirectory();
        const bool bIsScript     = ContentItem->IsLuaScript();
        const bool bIsProtected  = ContentItem->IsProtected();
        const FString  Extension = ContentItem->GetExtension();

        const char* HeaderIcon;
        ImVec4      HeaderTint;
        const char* TypeLabel;
        if (bIsDirectory)
        {
            HeaderIcon = LE_ICON_FOLDER_OPEN;
            HeaderTint = kMenuAccentFolder;
            TypeLabel  = "Folder";
        }
        else if (bIsAsset)
        {
            HeaderIcon = LE_ICON_FILE_DOCUMENT;
            HeaderTint = kMenuAccent;
            TypeLabel  = "Asset";
        }
        else if (bIsScript)
        {
            HeaderIcon = LE_ICON_LANGUAGE_LUA;
            HeaderTint = kMenuAccentScript;
            TypeLabel  = "Lua Script";
        }
        else
        {
            HeaderIcon = LE_ICON_FILE;
            HeaderTint = ImVec4(0.78f, 0.78f, 0.82f, 1.0f);
            TypeLabel  = "File";
        }

        FFixedString TitleBuf(ContentItem->GetName().data(), ContentItem->GetName().size());
        FFixedString SubtitleBuf(ContentItem->GetVirtualPath().data(), ContentItem->GetVirtualPath().size());
        (void)TypeLabel;

        PushContextMenuItemStyle();

        DrawMenuHeader(HeaderIcon, TitleBuf.c_str(), SubtitleBuf.c_str(), HeaderTint);

        DrawMenuSection("OPEN");

        if (bIsDirectory)
        {
            if (ImGui::MenuItem(LE_ICON_FOLDER_OPEN " Open Folder", "Dbl-Click"))
            {
                SelectedPath = FFixedString(ContentItem->GetVirtualPath().data(), ContentItem->GetVirtualPath().size());
                RefreshContentBrowser();
            }
        }
        else if (bIsAsset)
        {
            if (ImGui::MenuItem(LE_ICON_FOLDER_OPEN " Open Asset", "Dbl-Click"))
            {
                FFixedString Path(ContentItem->GetVirtualPath().data(), ContentItem->GetVirtualPath().size());
                if (const FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(Path))
                {
                    ToolContext->OpenAssetEditor(Data->AssetGUID);
                }
            }
        }
        else
        {
            if (ImGui::MenuItem(LE_ICON_FOLDER_OPEN " Open", "Dbl-Click"))
            {
                ToolContext->OpenFileEditor(ContentItem->GetVirtualPath());
            }
            if (ImGui::MenuItem(LE_ICON_OPEN_IN_NEW " Open Externally"))
            {
                Platform::LaunchURL(UTF8_TO_TCHAR(ContentItem->GetPathSource().data()));
            }
        }

        if (ImGui::MenuItem(LE_ICON_MICROSOFT_WINDOWS " Show in Explorer"))
        {
            FString Parent = Paths::Parent(ContentItem->GetPathSource());
            Platform::LaunchURL(StringUtils::ToWideString(Parent).c_str());
        }

        DrawMenuSection("EDIT");

        if (ImGui::MenuItem(LE_ICON_RENAME " Rename", "F2", false, !bIsProtected))
        {
            PushRenameModal(ContentItem);
        }

        DrawMenuSection("CLIPBOARD");

        if (ImGui::MenuItem(LE_ICON_CONTENT_COPY " Copy Path"))
        {
            ImGui::SetClipboardText(ContentItem->GetVirtualPath().data());
            ImGuiX::Notifications::NotifyInfo("Path copied to clipboard");
        }
        if (ImGui::MenuItem(LE_ICON_TAG " Copy Name"))
        {
            FFixedString Name(ContentItem->GetName().data(), ContentItem->GetName().size());
            ImGui::SetClipboardText(Name.c_str());
            ImGuiX::Notifications::NotifyInfo("Name copied to clipboard");
        }
        if (bIsAsset)
        {
            if (ImGui::MenuItem(LE_ICON_LINK " Copy Reference"))
            {
                FFixedString Path(ContentItem->GetVirtualPath().data(), ContentItem->GetVirtualPath().size());
                if (const FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(Path))
                {
                    FFixedString Reference;
                    Reference.append("Asset(").append(Data->AssetClass.c_str()).append("'").append(Path.c_str()).append("')");
                    ImGui::SetClipboardText(Reference.c_str());
                    ImGuiX::Notifications::NotifyInfo("Reference copied to clipboard");
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Text,          kMenuDanger);
        const bool bWorldActive = IsAnyWorldPlayingOrSimulating();
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kMenuDangerHover);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.85f, 0.22f, 0.24f, 0.85f));
        const bool bDeleteClicked = ImGui::MenuItem(LE_ICON_TRASH_CAN " Delete", "Del", false, !bIsProtected && !bWorldActive);
        ImGui::PopStyleColor(3);

        if (bIsProtected)
        {
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Tiny);
            ImGui::PushStyleColor(ImGuiCol_Text, kMenuTextDim);
            const float OldX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(OldX + 4.0f);
            ImGui::TextUnformatted(LE_ICON_LOCK " Protected, cannot be deleted");
            ImGui::PopStyleColor();
            ImGuiX::Font::PopFont();
        }
        else if (bWorldActive)
        {
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Tiny);
            ImGui::PushStyleColor(ImGuiCol_Text, kMenuTextDim);
            const float OldX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(OldX + 4.0f);
            ImGui::TextUnformatted(LE_ICON_LOCK " Stop play to delete");
            ImGui::PopStyleColor();
            ImGuiX::Font::PopFont();
        }

        if (bDeleteClicked)
        {
            OpenDeletionWarningPopup(ContentItem);
        }

        PopContextMenuItemStyle();
    }
    
    void FContentBrowserEditorTool::DrawContentDirectoryContextMenu()
    {
        FStringView FolderName = VFS::FileName(FStringView(SelectedPath.c_str(), SelectedPath.size()), true);
        FFixedString FolderTitle(FolderName.data(), FolderName.size());
        if (FolderTitle.empty())
        {
            FolderTitle.assign("Content");
        }

        DrawMenuHeader(LE_ICON_FOLDER_OPEN, FolderTitle.c_str(), SelectedPath.c_str(), kMenuAccentFolder);

        DrawMenuSection("CREATE");

        if (ImGui::MenuItem(LE_ICON_FOLDER_PLUS " New Folder"))
        {
            FFixedString FinalPath = VFS::MakeUniqueFilePath(SelectedPath + "/NewFolder");
            VFS::CreateDir(FinalPath);
            RefreshContentBrowser();
        }

        // Aggregated asset creation submenu (factory-driven), grouped into per-category submenus.
        const TVector<CFactory*>& Factories = CFactoryRegistry::Get().GetFactories();
        if (ImGui::BeginMenu(LE_ICON_PLUS_BOX " New Asset"))
        {
            auto CreateFromFactory = [this](CFactory* Factory)
            {
                FFixedString Path = Paths::Combine(SelectedPath, Factory->GetDefaultAssetCreationName());
                CPackage::AddPackageExt(Path);
                Path = VFS::MakeUniqueFilePath(Path);

                if (Factory->HasCreationDialogue())
                {
                    ToolContext->PushModal("Create New", {500, 500}, [this, Factory, Path = Move(Path)]
                    {
                        bool bShouldClose = CFactory::ShowCreationDialogue(Factory, Path);
                        if (bShouldClose)
                        {
                            ImGuiX::Notifications::NotifySuccess("Successfully Created: \"{0}\"", Path);
                        }
                        return bShouldClose;
                    });
                }
                else if (CObject* Object = Factory->TryCreateNew(Path))
                {
                    if (CPackage::SavePackage(Object->GetPackage(), Path))
                    {
                        FAssetRegistry::Get().AssetCreated(Object);
                        ImGuiX::Notifications::NotifySuccess("Successfully Created: \"{0}\"", Path);
                    }
                    else
                    {
                        ImGuiX::Notifications::NotifyError("Failed to save new asset: \"{0}\"", Path);
                    }
                }
                else
                {
                    ImGuiX::Notifications::NotifyError("Failed to create new: \"{0}\"", Path);
                }
            };

            // Collect the creatable factories and their distinct categories.
            TVector<CFactory*> Creatable;
            TVector<FString>   Categories;
            for (CFactory* Factory : Factories)
            {
                if (Factory->CanImport() || Factory->GetAssetClass() == nullptr)
                {
                    continue;
                }
                Creatable.push_back(Factory);

                const FString Category = Factory->GetCategory();
                bool bSeen = false;
                for (const FString& Existing : Categories)
                {
                    if (Existing == Category) { bSeen = true; break; }
                }
                if (!bSeen)
                {
                    Categories.push_back(Category);
                }
            }

            eastl::sort(Categories.begin(), Categories.end());

            for (const FString& Category : Categories)
            {
                if (!ImGui::BeginMenu(Category.c_str()))
                {
                    continue;
                }

                TVector<CFactory*> InCategory;
                for (CFactory* Factory : Creatable)
                {
                    if (Factory->GetCategory() == Category)
                    {
                        InCategory.push_back(Factory);
                    }
                }
                eastl::sort(InCategory.begin(), InCategory.end(), [](CFactory* A, CFactory* B)
                {
                    return A->GetAssetName() < B->GetAssetName();
                });

                for (CFactory* Factory : InCategory)
                {
                    FString DisplayName = FString(LE_ICON_FILE_DOCUMENT_PLUS) + " " + Factory->GetAssetName();
                    if (ImGui::MenuItem(DisplayName.c_str()))
                    {
                        CreateFromFactory(Factory);
                    }
                }

                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem(LE_ICON_LANGUAGE_LUA " New Lua Script"))
        {
            FFixedString NewScriptPath = SelectedPath + "/" + "NewScript.luau";
            NewScriptPath = VFS::MakeUniqueFilePath(NewScriptPath);
            VFS::WriteFile(NewScriptPath, LoadNewEntityScriptTemplate());
            RefreshContentBrowser();
        }

        if (ImGui::MenuItem(LE_ICON_LANGUAGE_CSS3 " New UI Widget"))
        {
            FFixedString NewWidgetPath = SelectedPath + "/" + "NewWidget.rml";
            NewWidgetPath = VFS::MakeUniqueFilePath(NewWidgetPath);
            VFS::WriteFile(NewWidgetPath, "");
            RefreshContentBrowser();
        }

        if (ImGui::MenuItem(LE_ICON_LANGUAGE_CSS3 " New UI Stylesheet"))
        {
            FFixedString NewSheetPath = SelectedPath + "/" + "NewStylesheet.rcss";
            NewSheetPath = VFS::MakeUniqueFilePath(NewSheetPath);
            VFS::WriteFile(NewSheetPath,
                "/* New RCSS stylesheet. Link it from a document (relative path):\n"
                "       <link type=\"text/rcss\" href=\"NewStylesheet.rcss\"/>\n"
                "   CPU-safe styling only: gradients (vertical-/horizontal-gradient),\n"
                "   border-radius, transforms, transitions, @keyframes, font-effect.\n"
                "   Avoid box-shadow / filter / linear-gradient (need shaders). */\n\n"
                "body\n"
                "{\n"
                "    color: #cdd6f4;\n"
                "}\n");
            RefreshContentBrowser();
        }

        // IMPORT -----------------------------------------------------------
        DrawMenuSection("IMPORT");

        if (ImGui::MenuItem(LE_ICON_IMPORT " Import Asset..."))
        {
            FFixedString SelectedFile;
            const char* Filter = "Supported Assets (*.png;*.jpg;*.hdr;*.fbx;*.gltf;*.glb;*.obj;*.ttf;*.otf)\0*.png;*.jpg;*.hdr;*.fbx;*.gltf;*.glb;*.obj;*.ttf;*.otf\0All Files (*.*)\0*.*\0";
            if (Platform::OpenFileDialogue(SelectedFile, "Import Asset", Filter))
            {
                TryImport(SelectedFile);
            }
        }

        // VIEW -------------------------------------------------------------
        DrawMenuSection("VIEW");

        if (ImGui::MenuItem(LE_ICON_REFRESH " Refresh"))
        {
            RefreshContentBrowser();
        }

        if (ImGui::MenuItem(LE_ICON_MICROSOFT_WINDOWS " Show in Explorer"))
        {
            FFixedString Resolved = VFS::ResolvePath(FStringView(SelectedPath.c_str(), SelectedPath.size()));
            const char* Target = Resolved.empty() ? SelectedPath.c_str() : Resolved.c_str();
            Platform::LaunchURL(StringUtils::ToWideString(Target).c_str());
        }
    }
}
