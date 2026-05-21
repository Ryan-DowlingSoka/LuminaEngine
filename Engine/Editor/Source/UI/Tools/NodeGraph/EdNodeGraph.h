#pragma once

#include "EdGraphNode.h"
#include "EdGraphSchema.h"
#include "GraphActionMenu.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/String.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "EdNodeGraph.generated.h"

namespace Lumina
{
    class CEdGraphNode;
    class CPackage;
}

namespace Lumina
{
    REFLECT()
    class CEdNodeGraph : public CObject
    {
        GENERATED_BODY()
        
    public:

        struct FNodeFactory
        {
            FString Name;
            FString Tooltip;
            TFunction<CEdGraphNode*()> CreationCallback;
        };

        
        CEdNodeGraph();
        ~CEdNodeGraph() override;

        virtual void Initialize();
        virtual void Shutdown();
        void Serialize(FArchive& Ar) override;
        void PostLoad() override;

        void DrawGraph();
        virtual void DrawGraphContextMenu();
        virtual void DrawNodeContextMenu(CEdGraphNode* Node);
        virtual void DrawPinContextMenu(CEdNodeGraphPin* Pin);

        virtual void ValidateGraph()  { }

        virtual CEdGraphNode* CreateNode(CClass* NodeClass);

        // Picks a pin on NewNode that should be auto-connected to SourcePin when the user drops a
        // drag-from-pin on empty space and creates a node from the popup. Default implementation
        // returns the first opposite-direction pin the schema accepts. Override for type-aware matching.
        virtual CEdNodeGraphPin* FindAutoConnectPin(CEdGraphNode* NewNode, CEdNodeGraphPin* SourcePin) const;

        // Connects SourcePin to TargetPin honoring the graph schema (breaks existing single-input links
        // first). No-op if either pin is null or the schema rejects the connection.
        void TryAutoConnect(CEdNodeGraphPin* SourcePin, CEdNodeGraphPin* TargetPin);

        virtual CEdGraphNode* OnNodeRemoved(CEdGraphNode* Node) { return nullptr; }

        // Quick-place hooks.
        virtual void HandleQuickPlace(int Digit, ImVec2 CanvasPos) {}
        virtual void HandleQuickPlace(char Key, ImVec2 CanvasPos) {}

        // Class returned here (when non-null) is instantiated when the user double-clicks an
        // existing wire. The node is inserted at the click position and the wire is rerouted
        // through it. Default is null -- graphs that want this UX (e.g. material) override.
        virtual CClass* GetRerouteNodeClass() const { return nullptr; }

        // Splits the link between OutputPin -> InputPin by inserting a freshly-constructed reroute
        // node at CanvasPos. No-op if GetRerouteNodeClass() returns null. Used by the double-click
        // wire handler in DrawGraph; safe to call directly from custom UI as well.
        CEdGraphNode* InsertRerouteOnLink(CEdNodeGraphPin* OutputPin, CEdNodeGraphPin* InputPin, ImVec2 CanvasPos);

        
        void SetNodeSelectedCallback(const TFunction<void(CEdGraphNode*)>& Callback) { NodeSelectedCallback = Callback; }
        void SetPreNodeDeletedCallback(const TFunction<void(CEdGraphNode*)>& Callback) { PreNodeDeletedCallback = Callback; }

        // Fired when a node is double-clicked on the canvas. The animation graph
        // editor uses this to descend into a node's sub-graph.
        void SetNodeDoubleClickedCallback(const TFunction<void(CEdGraphNode*)>& Callback) { NodeDoubleClickedCallback = Callback; }

        // Fired every frame with the two pins of the currently selected link, or
        // (null, null) when no single link is selected. Used by the state machine
        // canvas to surface the selected transition in the properties panel.
        void SetLinkSelectedCallback(const TFunction<void(CEdNodeGraphPin*, CEdNodeGraphPin*)>& Callback) { LinkSelectedCallback = Callback; }

        // Transient per-frame debug overlay data, pushed by an asset editor while
        // a graph is "running" (e.g. the animation graph preview). The draw loop
        // animates link flow, prints live pin values, and highlights active nodes.
        // All pointers are owned by the caller and only valid for the frame.
        struct FGraphDebugContext
        {
            bool                                            bEnabled = false;
            bool                                            bFlowLinks = false;
            const THashMap<CEdNodeGraphPin*, FString>*      PinValues = nullptr;
            const THashSet<const CEdGraphNode*>*            ActiveNodes = nullptr;
        };

        void SetDebugContext(const FGraphDebugContext& InContext) { DebugContext = InContext; }
        void ClearDebugContext() { DebugContext = FGraphDebugContext(); }

        // Schema that governs what connections are allowed in this graph.
        virtual const FEdGraphSchema& GetSchema() const { return GetDefaultEdGraphSchema(); }

        // When true, the draw loop calls CEdNodeGraphPin::DrawPin() for each
        // unconnected input pin so pins can render inline editors (default
        // values, enum combos) on the node face. Off for graphs that don't want
        // it so their pin layout is unaffected.
        virtual bool ShouldDrawInlinePinEditors() const { return false; }

        // Package under which newly constructed nodes are allocated. Defaults to this graph's package,
        // so nodes live alongside the asset that owns the graph.
        virtual CPackage* GetNodeOuter();


    private:

        static bool GraphSaveSettings(const char* data, size_t size, ax::NodeEditor::SaveReasonFlags reason, void* userPointer);
        static size_t GraphLoadSettings(char* data, void* userPointer);

        // Compact reroute renderer: single small dot, no header, no padding. Pushes the node's
        // input/output connections into OutLinks just like the regular per-node loop so the link
        // pass downstream picks up the wires going through this reroute.
        void DrawRerouteNode(CEdGraphNode* Node, TVector<TPair<CEdNodeGraphPin*, CEdNodeGraphPin*>>& OutLinks);

        // Draws a pin's live debug value (when the debug context supplies one) as a
        // small colored token inline in the pin row. No-op when debug is off.
        void DrawPinDebugValue(CEdNodeGraphPin* Pin);
        
    public:

        void RegisterGraphNode(CClass* InClass);
        
        uint64 AddNode(CEdGraphNode* InNode);

        /** All nodes currently in this graph. */
        PROPERTY()
        TVector<TObjectPtr<CEdGraphNode>>               Nodes;

        /** Serialized connection data encoding pin-to-pin links. */
        PROPERTY()
        TVector<uint16>                                 Connections;

        /** Serialized node editor layout state (positions, zoom, etc.). */
        PROPERTY()
        FString GraphSaveData;
        
        THashSet<CClass*>                               SupportedNodes;

        TFunction<void(CEdGraphNode*)>                  NodeSelectedCallback;
        TFunction<void(CEdGraphNode*)>                  PreNodeDeletedCallback;
        TFunction<void(CEdGraphNode*)>                  NodeDoubleClickedCallback;
        TFunction<void(CEdNodeGraphPin*, CEdNodeGraphPin*)> LinkSelectedCallback;

        int64                                           NextID = 0;

        FGraphActionMenu                                ActionMenu;

        // Pin the user dragged off when releasing on empty space. Consumed by the action menu
        // popup to auto-connect the new node back to this pin. Cleared when the popup closes.
        CEdNodeGraphPin*                                PendingSourcePin = nullptr;
        bool                                            bOpenCreateFromPin = false;

        TVector<CEdGraphNode*>                          CopiedNodes;
        
        bool                                            bFirstDraw = true;
        bool                                            bDebug = false;
        
        ax::NodeEditor::EditorContext* GetEditorContext() const { return Context; }

    private:

        ax::NodeEditor::EditorContext* Context = nullptr;

        // Set each frame by an asset editor's debug overlay; default is "off".
        FGraphDebugContext DebugContext;
    };
    
    
}
