#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "UI/Properties/PropertyTable.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"
#include "UI/Tools/NodeGraph/EdGraphNode.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"

namespace Lumina
{
    class CAnimationGraphNodeGraph;
    class CAnimStateTransition;
    class CEdNodeGraph;
    class CEdNodeGraphPin;
    class CEnum;
    class CWorld;

    // Asset editor for CAnimationGraph. Hosts the node graph canvas, a 3D
    // preview viewport that runs the compiled graph on the skeleton's preview
    // mesh, and a properties panel; compiles the graph into bytecode on save.
    class FAnimationGraphEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FAnimationGraphEditorTool)

        FAnimationGraphEditorTool(IEditorToolContext* Context, CObject* InAsset);

        bool IsSingleWindowTool() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_RUN_FAST; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;

        void SetupWorldForTool() override;
        void Update(const FUpdateContext& UpdateContext) override;

        // Lazily creates / updates / removes the preview mesh entity to track the
        // graph asset's skeleton. The skeleton is often assigned after the tool is
        // already open, so this runs every frame rather than only at world setup.
        void SyncPreviewMesh();

        void DrawToolMenu(const FUpdateContext& UpdateContext) override;
        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;
        bool ShouldGenerateThumbnailOnSave() const override { return false; }

        void OnSave() override;

        void DrawGraphWindow();
        void DrawPropertiesWindow();
        void DrawPreviewControls();

        // Lists the compiled graph's parameters with editable live values, and
        // pushes those values into the preview mesh so state-machine transition
        // conditions (and Get Parameter nodes) can be exercised in the viewport.
        void DrawParametersWindow();
        void PushParameterOverrides();

        // Resolves a reflected enum by registered name (lazy cache), for the
        // Parameters panel's enum-key value combos.
        CEnum* ResolveReflectedEnum(const FName& Name);

        // When a State node is selected, lists its outgoing transitions inline
        // so the user can edit each condition without having to click the
        // (often hard to hit) transition wire itself.
        void DrawOutgoingTransitionsForState(class CAnimGraphNode_State* State);

        // Draws the navigation breadcrumb (Animation Graph > State Machine >
        // State ...) above the canvas; clicking a crumb pops back to that level.
        void DrawBreadcrumbBar();

        // Topologically compiles the node graph into the runtime asset's bytecode.
        // bMarkPackageDirty is skipped for the per-frame live-preview recompile so
        // the asset is not perpetually marked unsaved.
        void Compile(bool bMarkPackageDirty = true);

        // Builds the live debug overlay (pin values, active state highlight) from
        // the selected target's VM state and pushes it onto the displayed graph.
        // No-op when the Debug toggle is off.
        void UpdateDebugOverlay();

        // Dropdown to choose which instance the debug overlay reads from: the
        // editor preview, or any live entity (across worlds) whose anim component
        // uses this graph asset.
        void DrawDebugTargetCombo();

    private:

        // One level of the graph navigation stack: the graph being drawn and the
        // label shown for it in the breadcrumb bar.
        struct FGraphStackEntry
        {
            CEdNodeGraph* Graph = nullptr;
            FString       Label;
        };

        // Descends into a sub-graph (state machine canvas, or a state's blend
        // tree): readies it, pushes it on the stack, resets the inspector.
        void EnterGraph(CEdNodeGraph* Graph, const FString& Label);

        // Pops the navigation stack back to the given level.
        void PopToLevel(int32 Index);

        // Initializes a graph (creates its node-editor context) and wires its
        // selection / double-click callbacks. Idempotent per graph.
        void EnsureGraphReady(CEdNodeGraph* Graph);
        void WireGraphCallbacks(CEdNodeGraph* Graph);

        TObjectPtr<CAnimationGraphNodeGraph>    NodeGraph;
        CEdGraphNode*                           SelectedNode = nullptr;
        CAnimStateTransition*                   SelectedTransition = nullptr;
        FString                                 CompilationLog;
        bool                                    bHasCompilationErrors = false;

        // Graph navigation: GraphStack[0] is the top-level graph, back() is the
        // canvas currently drawn. InitializedGraphs tracks every graph whose
        // context this tool created, so they can be shut down on close.
        TVector<FGraphStackEntry>               GraphStack;
        THashSet<CEdNodeGraph*>                 InitializedGraphs;

        // Editor-driven live values for the graph's parameters. Pushed into the
        // preview mesh every frame so transition conditions can be tested; keyed
        // by parameter name so they survive recompiles / parameter reordering.
        THashMap<FName, float>                  ParameterOverrides;

        // Lazily-built name -> reflected enum lookup for enum-key value combos.
        THashMap<FName, CEnum*>                 ReflectedEnumCache;
        bool                                    bEnumCacheBuilt = false;

        // Cached inline property tables for outgoing transitions of the selected
        // State node, keyed by transition pointer. Built lazily; we never let
        // them outlive their CAnimStateTransition (cleared whenever the state
        // machine canvas's transition list changes underneath us).
        THashMap<CAnimStateTransition*, TUniquePtr<FPropertyTable>> TransitionTables;

        // Live preview: recompile the graph every frame so node edits show up
        // in the viewport immediately. Cheap for preview-sized graphs.
        bool                                    bAutoCompile = true;

        // Debug overlay: when on, the graph animates link flow, prints live pin
        // values, and highlights the active state machine state.
        bool                                    bDebugEnabled = false;

        // Which instance the debug overlay reads from. Null world = the editor
        // preview (MeshEntity); otherwise a live entity in another world.
        TWeakObjectPtr<CWorld>                  DebugTargetWorld;
        entt::entity                            DebugTargetEntity = entt::null;

        // Captured from the compiler after each compile: maps editor pins to VM
        // registers and editor State nodes to their runtime state slot/index.
        // Refreshed every frame by the auto-compile, so pin pointers stay valid.
        THashMap<const CEdNodeGraphPin*, uint16> DebugPinRegisters;
        TVector<FAnimGraphDebugStateNode>        DebugStateNodes;

        // Per-frame overlay payload handed to the displayed graph; kept alive as
        // members so the pointers in FGraphDebugContext stay valid during draw.
        THashMap<CEdNodeGraphPin*, FString>      DebugPinValues;
        THashSet<const CEdGraphNode*>            DebugActiveNodes;

        entt::entity                            MeshEntity = entt::null;
        entt::entity                            DirectionalLightEntity = entt::null;
    };
}
