#pragma once

#include "imgui.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Math/Color.h"
#include "Core/Object/Object.h"
#include "EdNodeGraphPin.generated.h"

namespace Lumina
{
    // Forward decl breaks the EdGraphNode.h <-> EdNodeGraphPin.h include cycle.
    // Only pointers / templates need the type here; full def is required at the
    // call site of the templated GetOwningNode<T>() and is pulled in by the .cpp.
    class CEdGraphNode;

    REFLECT()
    class CEdNodeGraphPin : public CObject
    {
        GENERATED_BODY()
    public:

        friend class CEdNodeGraph;
        friend class CEdGraphNode;

        CEdNodeGraphPin()
            : PinID(0)
            , bSingleInput(0)
            , bInputPin(0)
            , bDrawPinEditor(false)
            , bHidePinDuringConnection(true)
        {}
        
        virtual float DrawPin() { return 1.5f; }
        
        void SetPinName(const FString& Name) { PinName = Name; }
        const FString& GetPinName() const { return PinName; }
        const FString& GetPinTooltip() const { return PinName; }

        bool ShouldHideDuringConnection() const { return bHidePinDuringConnection; }
        void SetHideDuringConnection(bool bHide) { bHidePinDuringConnection = bHide; }
        
        virtual uint32 GetPinColor() const { return PinColor; }
        void SetPinColor(uint32 Color) { PinColor = Color;}
                
        void AddConnection(CEdNodeGraphPin* Pin) { Connections.push_back(Pin); }
        void RemoveConnection(CEdNodeGraphPin* Pin);
        FORCEINLINE TVector<CEdNodeGraphPin*> GetConnections() const { return Connections; }
        FORCEINLINE CEdNodeGraphPin* GetConnection(size_t Index) { return Connections[Index]; }
        void DisconnectFrom(CEdNodeGraphPin* OtherPin);
        void ClearConnections();

        bool IsSingleInput() const { return bSingleInput; }
        bool HasConnection() const { return !Connections.empty(); }
        uint16 GetPinGUID() const { return PinID; }
        CEdGraphNode* GetOwningNode() const { return OwningNode; }

        bool ShouldDrawEditor() const { return bDrawPinEditor; }
        void SetShouldDrawEditor(bool bNew) { bDrawPinEditor = bNew; }
        
        template<typename T>
        requires(std::is_base_of_v<CEdGraphNode, T>)
        T* GetOwningNode()
        {
            return static_cast<T*>(GetOwningNode());
        }

        template<typename T>
        requires(eastl::is_base_of_v<CEdNodeGraphPin, T>)
        T* GetConnection(size_t Index)
        {
            return static_cast<T*>(Connections[Index]);
        }
        
    
    public:

        FString                         PinName;
        uint32                          PinColor = IM_COL32(255, 255, 255, 255);

        /** Index of the pin relative to the owning node */
        uint16                          PinID;
        
        TVector<CEdNodeGraphPin*>       Connections;
        
        CEdGraphNode*                   OwningNode = nullptr;
        uint8                           bSingleInput:1;
        uint8                           bInputPin:1;
        uint8                           bDrawPinEditor:1;
        uint8                           bHidePinDuringConnection:1;
    };
}
