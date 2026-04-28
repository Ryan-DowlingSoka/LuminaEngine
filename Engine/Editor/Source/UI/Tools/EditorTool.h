#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "ToolFlags.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/String.h"
#include "Events/EventProcessor.h"
#include "Memory/SmartPtr.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "World/World.h"

namespace Lumina
{
    class FPrimitiveDrawManager;
    enum class EEditorToolFlags : uint8;
    class IEditorToolContext;
    class FUpdateContext;
    class FInputViewport;
}

namespace Lumina
{
    class FEditorTool : public IEventHandler
    {
    public:
        
        friend class FEditorUI;

        constexpr static char const* const ViewportWindowName = "Viewport";

        //--------------------------------------------------------------------------
        
        class FToolWindow
        {
            friend class FEditorTool;
            friend class FEditorUI;

        public:

            FToolWindow(const FName& InName, const TFunction<void(bool)>& InDrawFunction, const ImVec2& InWindowPadding = ImVec2(-1, -1), bool bDisableScrolling = false)
                : Name(InName)
                , DrawFunction(InDrawFunction)
                , WindowPadding(InWindowPadding)
            {}
        
        protected:
            
            FName                 Name;
            TFunction<void(bool)> DrawFunction;
            ImVec2                WindowPadding;
            bool                  bViewport = false;
            bool                  bOpen = true;
            
        };
        
        //--------------------------------------------------------------------------
        
        struct FTransaction
        {
            FName           Name;
            TVector<uint8>  BeforeState;
            TVector<uint8>  AfterState;
        };

        //--------------------------------------------------------------------------

    public:

        FEditorTool(IEditorToolContext* Context, const FString& DisplayName, CWorld* InWorld = nullptr);
        virtual ~FEditorTool();
        LE_NO_COPYMOVE(FEditorTool);

        virtual void Initialize();
        virtual void Deinitialize(const FUpdateContext& UpdateContext);
        NODISCARD virtual FName GetToolName() const { return ToolName; }
        
        NODISCARD ImGuiID CalculateDockspaceID() const;

        NODISCARD FFixedString GetToolWindowName(const FString& Name) const { return GetToolWindowName(Name.c_str(), CurrDockspaceID); }
        
        NODISCARD ImGuiWindowClass* GetWindowClass() { return &ToolWindowsClass; }
        NODISCARD EEditorToolFlags GetToolFlags() const { return ToolFlags; }
        NODISCARD bool HasFlag(EEditorToolFlags Flag) const {  return (ToolFlags & Flag) == Flag; }

        NODISCARD CWorld* GetWorld() const { return World; }
        NODISCARD bool HasWorld() const { return World != nullptr; }
        NODISCARD ImGuiID GetCurrentDockspaceID() const { return CurrDockspaceID; }

        virtual void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const;
        
        virtual void OnInitialize() = 0;
        virtual void OnDeinitialize(const FUpdateContext& UpdateContext) = 0;
        
        virtual bool ShouldGenerateThumbnailOnSave() const { return false; }
        
        virtual void GenerateThumbnail(CPackage* Package);

        NODISCARD virtual bool IsSingleWindowTool() const { return false; }

        // Get the hash of the unique type ID for this tool
        NODISCARD virtual uint32 GetUniqueTypeID() const = 0;

        // Get the unique typename for this tool to be used for docking
        NODISCARD virtual char const* GetUniqueTypeName() const = 0;

        /** Replaces the world: destroys the old, creates a fresh editor context, sets up entities. */
        virtual void SetWorld(CWorld* InWorld);

        /** Pointer-only swap. World lifetime is owned elsewhere (e.g. PIE). */
        virtual void RebindToWorld(CWorld* InWorld);

        /** Called to set up the world for the tool */
        virtual void SetupWorldForTool();
        
        /** Creates a plane at world 0 */
        virtual entt::entity CreateFloorPlane(float YOffset = 0.0f, float ScaleX = 10.0f, float ScaleY = 10.0f);
        
        /** Called just before updating the world at each stage */
        virtual void WorldUpdate(const FUpdateContext& UpdateContext) { }

        /** Once per-frame update */
        virtual void Update(const FUpdateContext& UpdateContext) { }

        /** Called once at the end of frame */
        virtual void EndFrame() { }
        
        /** Optionally draw a toolbar at the top of the window */
        void DrawMainToolbar(const FUpdateContext& UpdateContext);

        /** Allows the child to draw specific menu actions */
        virtual void DrawToolMenu(const FUpdateContext& UpdateContext) { }

        /** Viewport overlay to draw any elements to the window's viewport */
        virtual void DrawViewportOverlayElements(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture, ImVec2 ViewportSize);
        
        /** Draw the optional viewport for this tool window, returns true if focused. */
        virtual bool DrawViewport(const FUpdateContext& UpdateContext, ImTextureRef ViewportTexture);

        /** Draws overlay elements on the viewport for tool actions. */
        virtual void DrawViewportToolbar(const FUpdateContext& UpdateContext);
        
        /** Moves the viewport to focus on the desired entity */
        virtual void FocusViewportToEntity(entt::entity Entity);
        
        /** Draws an editor viewport grid if a world exists */
        virtual void DrawWorldGrid(int Scale = 100, int Spacing = 1);

        bool BeginViewportToolbarGroup(char const* GroupID, ImVec2 GroupSize, const ImVec2& Padding);
        void EndViewportToolbarGroup();

        /** Is this editor tool for editing assets? */
        NODISCARD virtual bool IsAssetEditorTool() const { return false; }
        
        /** Can there only ever be one of this tool? */
        NODISCARD virtual bool IsSingleton() const { return HasFlag(EEditorToolFlags::Tool_Singleton); }
        
        /** Optional title bar icon override */
        NODISCARD virtual const char* GetTitlebarIcon() const { return LE_ICON_CAR_WRENCH; }

        /** Called when the save icon is pressed. */
        virtual void OnSave() { }

        /** Called when the new icon is pressed */
        virtual void OnNew() { }
        
        NODISCARD virtual bool IsUnsavedDocument() { return false; }

        /** @TODO Cache and compare */
        NODISCARD uint64 GetID() const { return GetToolName().GetID(); }
        
        FORCEINLINE ImGuiID GetCurrDockID() const        { return CurrDockID; }
        FORCEINLINE ImGuiID GetDesiredDockID() const     { return DesiredDockID; }
        FORCEINLINE ImGuiID GetCurrLocationID() const    { return CurrLocationID; }
        FORCEINLINE ImGuiID GetPrevLocationID() const    { return PrevLocationID; }
        FORCEINLINE ImGuiID GetCurrDockspaceID() const   { return CurrDockspaceID; }
        FORCEINLINE ImGuiID GetPrevDockspaceID() const   { return PrevDockspaceID; }
        

        static FFixedString GetToolWindowName(char const* ToolWindowName, ImGuiID InDockspaceID)
        {
            DEBUG_ASSERT(ToolWindowName != nullptr);
            return { FFixedString::CtorSprintf(), "%s##%08X", ToolWindowName, InDockspaceID };
        }

    protected:
        
        /** Called when the user begins manipulating something to be transacted. Captures the before-state for the transaction. */
        virtual void BeginTransaction() { }

        /** Called when the user finishes manipulating something that was transacted. Captures the after-state and pushes the transaction onto the undo stack. */
        virtual void EndTransaction(FName Name) { }

        /** Restores the registry to the state before the last transaction. Pushes the current state onto the redo stack. */
        virtual void Undo() { }

        /** Restores the registry to the state after the last undone transaction. Pushes the current state onto the undo stack. */
        virtual void Redo() { }

        void Internal_CreateViewportTool();
        
        FToolWindow* CreateToolWindow(FName InName, const TFunction<void(bool)>& DrawFunction, const ImVec2& WindowPadding = ImVec2(-1, -1), bool DisableScrolling = false);
        
        /** Draw a help menu for this tool */
        virtual void DrawHelpMenu() { DrawHelpTextRow("No Help Available", ""); }
        
        /** Helper to add a simple entry to the help menu */
        void DrawHelpTextRow(const char* Label, const char* Text) const;
    
    protected:

        TVector<FTransaction>               UndoStack;
        TVector<FTransaction>               RedoStack;
        TVector<uint8>                      PendingBeforeState;
        
        ImGuiID                             CurrDockID = 0;
        ImGuiID                             DesiredDockID = 0;      // The dock we wish to be in
        ImGuiID                             CurrLocationID = 0;     // Current Dock node we are docked into _OR_ window ID if floating window
        ImGuiID                             PrevLocationID = 0;     // Previous dock node we are docked into _OR_ window ID if floating window
        ImGuiID                             CurrDockspaceID = 0;    // Dockspace ID ~~ Hash of LocationID + DocType (with MYEDITOR_CONFIG_SAME_LOCATION_SHARE_LAYOUT=1)
        ImGuiID                             PrevDockspaceID = 0;
        ImGuiWindowClass                    ToolWindowsClass;       // All our tools windows will share the same WindowClass (based on ID) to avoid mixing tools from different top-level editor

        IEditorToolContext*                 ToolContext = nullptr;
        FName                               ToolName;
        
        TVector<TUniquePtr<FToolWindow>>    ToolWindows;
        
        TObjectPtr<CWorld>                  World;
        entt::entity                        EditorEntity;
        ImTextureID                         SceneViewportTexture = 0;

        TUniquePtr<FInputViewport>          InputViewport;

        EEditorToolFlags                    ToolFlags = EEditorToolFlags::Tool_WantsToolbar;

        bool                                bViewportFocused = false;
        bool                                bViewportHovered = false;
		bool							    bWorldGridEnabled = true;
    };
    
}

#define LUMINA_EDITOR_TOOL( TypeName ) \
constexpr static char const* const s_uniqueTypeName = #TypeName;\
constexpr static uint32 const s_toolTypeID = Lumina::Hash::FNV1a::GetHash32( #TypeName );\
constexpr static bool const s_isSingleton = false; \
virtual char const* GetUniqueTypeName() const override { return s_uniqueTypeName; }\
virtual uint32 GetUniqueTypeID() const override final { return TypeName::s_toolTypeID; }

//-------------------------------------------------------------------------

#define LUMINA_SINGLETON_EDITOR_TOOL( TypeName ) \
constexpr static char const* const s_uniqueTypeName = #TypeName;\
constexpr static uint32 const s_toolTypeID = Lumina::Hash::FNV1a::GetHash32( #TypeName ); \
constexpr static bool const s_isSingleton = true; \
virtual char const* GetUniqueTypeName() const { return s_uniqueTypeName; }\
virtual uint32 GetUniqueTypeID() const override final { return TypeName::s_toolTypeID; }\
virtual bool IsSingleton() const override final { return true; }