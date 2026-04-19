#pragma once

#include "EdNodeGraphPin.h"
#include <imgui.h>
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Math/Math.h"
#include "Core/Object/Object.h"
#include "EdGraphNode.generated.h"
#include "Core/Object/ObjectHandleTyped.h"

namespace Lumina
{
    enum class EMaterialInputType : uint8;
    class CEdNodeGraphPin;
    
    enum class ENodePinDirection : uint8
    {
        Input       = 0,
        Output      = 1,

        Count       = 2,
    };
    
    namespace EdNodeGraph
    {
        struct FError
        {
            FString             Name;
            FString             Description;
            CEdGraphNode*       Node = nullptr;
        };
    }

    REFLECT()
    class CEdGraphNode : public CObject
    {
        GENERATED_BODY()
        
    public:
        
        friend class CEdNodeGraph;

        void PostCreateCDO() override;
        
        virtual void BuildNode() { }
        
        virtual FFixedString GetNodeCategory() const { return "General"; }
        
        FString GetNodeFullName() { return FullName; }
        virtual bool WantsTitlebar() const { return true; }
        virtual FString GetNodeDisplayName() const { return "Node"; }
        virtual FString GetNodeTooltip() const { return "No Tooltip"; }
        virtual uint32 GetNodeTitleColor() const { return IM_COL32(200, 35, 35, 255); }
        virtual ImVec2 GetMinNodeBodySize() const { return ImVec2(80, 150); }
        virtual ImVec2 GetMinNodeTitleBarSize() const;

        virtual void DrawNodeBody() { }

        virtual bool IsDeletable() const { return true; }

        void SetDebugExecutionOrder(uint32 Order) { DebugExecutionOrder = Order; }
        uint32 GetDebugExecutionOrder() const { return DebugExecutionOrder; }

        virtual void PushNodeStyle();
        virtual void PopNodeStyle();

        virtual void DrawContextMenu() { }
        virtual void DrawNodeTitleBar();

        void SetError(const EdNodeGraph::FError& InError) { Error = InError; }
        const EdNodeGraph::FError& GetError() const { return Error.value(); }
        bool HasError() const { return Error.has_value(); }
        void ClearError() { Error = eastl::nullopt; }
        
        CEdNodeGraphPin* GetPin(uint16 ID, ENodePinDirection Direction);
        CEdNodeGraphPin* GetPinByIndex(uint32 Index, ENodePinDirection Direction);
        
        int64 GetNodeID() const { return NodeID; }

        void SetGridPos(float X, float Y) { GridX = X; GridY = Y; }
        float GetNodeX() const { return GridX; }
        float GetNodeY() const { return GridY; }

        const TVector<TObjectPtr<CEdNodeGraphPin>>& GetInputPins() const { return NodePins[static_cast<uint32>(ENodePinDirection::Input)]; }
        const TVector<TObjectPtr<CEdNodeGraphPin>>& GetOutputPins() const { return NodePins[static_cast<uint32>(ENodePinDirection::Output)]; }

        CEdNodeGraphPin* CreatePin(CClass* InClass, const FString& Name, ENodePinDirection Direction);

        /** Horizontal position of the node in the graph canvas. */
        PROPERTY(DuplicateTransient)
        float GridX;

        /** Vertical position of the node in the graph canvas. */
        PROPERTY(DuplicateTransient)
        float GridY;

        /** Unique identifier for this node within the graph. */
        PROPERTY(DuplicateTransient)
        int64 NodeID = 0;
        
        
    protected:

        TArray<TVector<TObjectPtr<CEdNodeGraphPin>>, static_cast<uint32>(ENodePinDirection::Count)> NodePins;

        uint32 DebugExecutionOrder;


        FString                             FullName;
        TOptional<EdNodeGraph::FError>      Error;
        bool                                bWasBuild = false;
    };
    
}
