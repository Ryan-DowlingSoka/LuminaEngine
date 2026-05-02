#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "EditorAction.h"
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
    enum class EEditorCameraMode : uint8
    {
        Free,    // WASD + RMB-drag look (DCC-style flythrough)
        Orbit,   // RMB-drag yaw/pitch around a focal point, MMB pan, wheel zoom
    };

    // Per-tool editor-camera state; each tool ticks its own camera so mode/focus is per-editor.
    struct FEditorCameraState
    {
        EEditorCameraMode Mode = EEditorCameraMode::Free;

        float       Speed       = 50.0f;
        float       SpeedScale  = 1.0f;
        glm::vec3   Velocity    = glm::vec3(0.0f);

        // Yaw/pitch in degrees on +Z forward. OrbitAnchor is "home"; MMB-pan moves OrbitTarget, ResetOrbitPan snaps back.
        glm::vec3   OrbitTarget   = glm::vec3(0.0f);
        glm::vec3   OrbitAnchor   = glm::vec3(0.0f);
        float       OrbitDistance = 5.0f;
        float       OrbitYaw      = 0.0f;
        float       OrbitPitch    = -15.0f;

        // Trailing-edge: release captured mouse mode once on RMB-up, not every non-looking frame.
        bool        bWasLooking = false;

        // Smooth focus interp; user movement input cancels mid-lerp.
        bool        bFocusInterp        = false;
        // Exponential-decay rate (1/s); ~12 yields ~250ms to ~95%.
        float       FocusInterpRate     = 12.0f;
        glm::vec3   FocusFreePosition   = glm::vec3(0.0f);
        glm::quat   FocusFreeRotation   = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3   FocusOrbitTarget    = glm::vec3(0.0f);
        float       FocusOrbitDistance  = 5.0f;
    };

    class FEditorTool : public IEventHandler
    {
    public:
        
        friend class FEditorUI;

        constexpr static char const* const ViewportWindowName = "Viewport";

        
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
        
        
        struct FTransaction
        {
            FName           Name;
            TVector<uint8>  BeforeState;
            TVector<uint8>  AfterState;
        };


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

        NODISCARD virtual uint32 GetUniqueTypeID() const = 0;
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

        /** Per-frame update; overrides should call base (or TickEditorCamera) so look/orbit input works. */
        virtual void Update(const FUpdateContext& UpdateContext);

        /** Called once at the end of frame */
        virtual void EndFrame() { }
        
        /** Optionally draw a toolbar at the top of the window */
        void DrawMainToolbar(const FUpdateContext& UpdateContext);

        /** Drives the editor-entity camera; called from FEditorTool::Update. */
        void TickEditorCamera(double DeltaTime);

        FEditorCameraState&       GetCameraState()       { return CameraState; }
        const FEditorCameraState& GetCameraState() const { return CameraState; }

        /** Switch camera mode; entering Orbit derives target/yaw/pitch/distance from the current transform. */
        void SetCameraMode(EEditorCameraMode Mode);

        /** Re-anchor orbit on a new world point; updates OrbitTarget and the OrbitAnchor ResetOrbitPan returns to. */
        void SetOrbitTarget(const glm::vec3& Target, float Distance = -1.0f);

        /** Snap OrbitTarget back to OrbitAnchor (undo MMB-drag pan). */
        void ResetOrbitPan();

    private:

        /** Push current orbit state onto the editor entity's transform. */
        void ApplyOrbitTransform();

    public:

        /** Free/Orbit combo for DrawViewportOverlayElements overrides. */
        void DrawCameraModeSelector(float ItemWidth = 95.0f);

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

    public:

        /** Returns a transform placed in front of the active editor camera by the given distance. */
        FTransform GetCameraSpawnTransform(float DistanceForward = 5.0f) const;

        /** Dispatches a content-browser asset drop by asset class. Returns the spawned entity (or entt::null). */
        entt::entity HandleContentBrowserAssetDrop(FStringView VirtualPath, entt::entity DropTarget);

    protected:

        /** Begin a transaction; captures before-state. */
        virtual void BeginTransaction();

        /** End a transaction; captures after-state and pushes onto the undo stack. */
        virtual void EndTransaction(FName Name);

        virtual void Undo();
        virtual void Redo();

        /** After a registry round-trip in Undo/Redo; override to rebuild caches mirroring registry state. */
        virtual void OnPostUndoRedo() { }

        /** Drops every transaction; call when the world is replaced or an asset reloads. */
        void ClearTransactionHistory();

        void Internal_CreateViewportTool();
        
        FToolWindow* CreateToolWindow(FName InName, const TFunction<void(bool)>& DrawFunction, const ImVec2& WindowPadding = ImVec2(-1, -1), bool DisableScrolling = false);
        
        /** Override to add tool-specific rows in a 2-column HelpTable. */
        virtual void DrawHelpMenu() { DrawHelpTextRow("No Help Available", ""); }

        void DrawHelpTextRow(const char* Label, const char* Text) const;

    private:

        /** Renders registered actions as Help > Keybinds; auto-called from DrawMainToolbar. */
        void DrawKeybindsMenu();

    public:

        /** Register a keybind-driven command; call from OnInitialize. Surfaces in Help > Keyboard Shortcuts. */
        void RegisterAction(FEditorAction Action) { EditorActions.push_back(eastl::move(Action)); }

        const TVector<FEditorAction>& GetRegisteredActions() const { return EditorActions; }

    protected:

        /** Polls action chords and fires callbacks; called from FEditorTool::Update; gated against text-input focus. */
        void TickEditorActions();

    private:

        TVector<FEditorAction>              EditorActions;
    
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
        FEditorCameraState                  CameraState;
        ImTextureID                         SceneViewportTexture = 0;

        TUniquePtr<FInputViewport>          InputViewport;

        EEditorToolFlags                    ToolFlags = EEditorToolFlags::Tool_WantsToolbar;

        bool                                bViewportFocused = false;
        bool                                bViewportHovered = false;
		bool							    bWorldGridEnabled = true;

        // F11 fullscreen viewport: draws as borderless overlay; other tool windows are suppressed.
        bool                                bViewportFullscreen = false;

    public:
        NODISCARD bool IsViewportFullscreen() const { return bViewportFullscreen; }
        void SetViewportFullscreen(bool bInFullscreen) { bViewportFullscreen = bInFullscreen; }
        void ToggleViewportFullscreen() { bViewportFullscreen = !bViewportFullscreen; }
    };
    
}

#define LUMINA_EDITOR_TOOL( TypeName ) \
constexpr static char const* const s_uniqueTypeName = #TypeName;\
constexpr static uint32 const s_toolTypeID = Lumina::Hash::FNV1a::GetHash32( #TypeName );\
constexpr static bool const s_isSingleton = false; \
virtual char const* GetUniqueTypeName() const override { return s_uniqueTypeName; }\
virtual uint32 GetUniqueTypeID() const override final { return TypeName::s_toolTypeID; }


#define LUMINA_SINGLETON_EDITOR_TOOL( TypeName ) \
constexpr static char const* const s_uniqueTypeName = #TypeName;\
constexpr static uint32 const s_toolTypeID = Lumina::Hash::FNV1a::GetHash32( #TypeName ); \
constexpr static bool const s_isSingleton = true; \
virtual char const* GetUniqueTypeName() const { return s_uniqueTypeName; }\
virtual uint32 GetUniqueTypeID() const override final { return TypeName::s_toolTypeID; }\
virtual bool IsSingleton() const override final { return true; }