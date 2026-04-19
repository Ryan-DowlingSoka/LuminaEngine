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

        virtual CEdGraphNode* OnNodeRemoved(CEdGraphNode* Node) { return nullptr; }

        void SetNodeSelectedCallback(const TFunction<void(CEdGraphNode*)>& Callback) { NodeSelectedCallback = Callback; }
        void SetPreNodeDeletedCallback(const TFunction<void(CEdGraphNode*)>& Callback) { PreNodeDeletedCallback = Callback; }

        // Schema that governs what connections are allowed in this graph.
        virtual const FEdGraphSchema& GetSchema() const { return GetDefaultEdGraphSchema(); }

        // Package under which newly constructed nodes are allocated. Defaults to this graph's package,
        // so nodes live alongside the asset that owns the graph.
        virtual CPackage* GetNodeOuter();


    private:

        static bool GraphSaveSettings(const char* data, size_t size, ax::NodeEditor::SaveReasonFlags reason, void* userPointer);
        static size_t GraphLoadSettings(char* data, void* userPointer);
        
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

        int64                                           NextID = 0;

        FGraphActionMenu                                ActionMenu;

        TVector<CEdGraphNode*>                          CopiedNodes;
        
        bool                                            bFirstDraw = true;
        bool                                            bDebug = false;
        
    private:

        ax::NodeEditor::EditorContext* Context = nullptr;
    };
    
    
}
