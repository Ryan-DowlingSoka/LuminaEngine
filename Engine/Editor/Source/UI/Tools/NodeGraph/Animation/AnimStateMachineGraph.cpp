#include "AnimStateMachineGraph.h"
#include "AnimGraphSchema.h"
#include "AnimStateTransition.h"
#include "Nodes/AnimGraphNode_State.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Class.h"
#include "Containers/Array.h"
#include "UI/Tools/NodeGraph/EdNodeGraphPin.h"

namespace Lumina
{
    namespace
    {
        // State node IDs sit in the uint32 range (see CEdNodeGraph::AddNode), so
        // a directed (from, to) pair packs losslessly into a uint64 key.
        FORCEINLINE uint64 MakeTransitionKey(int64 FromNodeID, int64 ToNodeID)
        {
            return (uint64((uint32)FromNodeID) << 32) | (uint32)ToNodeID;
        }
    }

    void CAnimStateMachineGraph::EnsureSetup()
    {
        // Context-free: only touches nodes / the creatable-node registry, so it
        // is safe to call on a state machine the compiler readies but never opens.
        if (bSetupDone)
        {
            return;
        }
        bSetupDone = true;

        // Every state machine has exactly one (undeletable) Entry node.
        bool bHasEntry = false;
        for (const TObjectPtr<CEdGraphNode>& Node : Nodes)
        {
            if (Node.IsValid() && Node->IsA<CAnimGraphNode_StateEntry>())
            {
                bHasEntry = true;
                break;
            }
        }

        if (!bHasEntry)
        {
            CreateNode(CAnimGraphNode_StateEntry::StaticClass());
        }

        RegisterGraphNode(CAnimGraphNode_State::StaticClass());

        ValidateGraph();
    }

    void CAnimStateMachineGraph::Initialize()
    {
        if (bInitialized)
        {
            return;
        }
        bInitialized = true;

        Super::Initialize();
        EnsureSetup();
    }

    void CAnimStateMachineGraph::Shutdown()
    {
        Super::Shutdown();
    }

    const FEdGraphSchema& CAnimStateMachineGraph::GetSchema() const
    {
        return GetAnimGraphSchema();
    }

    CAnimStateTransition* CAnimStateMachineGraph::FindTransition(int64 FromStateNodeID, int64 ToStateNodeID) const
    {
        for (const TObjectPtr<CAnimStateTransition>& Transition : Transitions)
        {
            if (Transition.IsValid() &&
                Transition->FromStateNodeID == FromStateNodeID &&
                Transition->ToStateNodeID == ToStateNodeID)
            {
                return Transition.Get();
            }
        }
        return nullptr;
    }

    void CAnimStateMachineGraph::ValidateGraph()
    {
        // Flatten live pin connections into the serialized list (PostLoad reads
        // it back to rewire pins on open) -- same contract as the other graphs.
        Connections.clear();
        Connections.reserve(16);

        for (CEdGraphNode* Node : Nodes)
        {
            for (CEdNodeGraphPin* InputPin : Node->GetInputPins())
            {
                for (CEdNodeGraphPin* Connection : InputPin->GetConnections())
                {
                    Connections.push_back(InputPin->PinID);
                    Connections.push_back(Connection->PinID);
                }
            }
        }

        // Reconcile transition objects against live State->State wires; Entry wires have no
        // transition data, so only State->State links count.
        THashSet<uint64> LiveKeys;

        for (CEdGraphNode* Node : Nodes)
        {
            CAnimGraphNode_State* ToState = Cast<CAnimGraphNode_State>(Node);
            if (ToState == nullptr)
            {
                continue;
            }

            for (CEdNodeGraphPin* InputPin : Node->GetInputPins())
            {
                for (CEdNodeGraphPin* Connection : InputPin->GetConnections())
                {
                    CAnimGraphNode_State* FromState = Cast<CAnimGraphNode_State>(Connection->GetOwningNode());
                    if (FromState == nullptr)
                    {
                        continue;
                    }
                    LiveKeys.insert(MakeTransitionKey(FromState->GetNodeID(), ToState->GetNodeID()));
                }
            }
        }

        // Drop transitions whose wire no longer exists.
        for (int32 i = (int32)Transitions.size() - 1; i >= 0; --i)
        {
            CAnimStateTransition* Transition = Transitions[i].Get();
            const bool bStale = Transition == nullptr ||
                LiveKeys.find(MakeTransitionKey(Transition->FromStateNodeID, Transition->ToStateNodeID)) == LiveKeys.end();
            if (bStale)
            {
                Transitions.erase(Transitions.begin() + i);
            }
        }

        // Create transition objects for newly-wired links.
        for (uint64 Key : LiveKeys)
        {
            const int64 FromNodeID = (int64)(uint32)(Key >> 32);
            const int64 ToNodeID   = (int64)(uint32)(Key & 0xFFFFFFFFull);

            if (FindTransition(FromNodeID, ToNodeID) == nullptr)
            {
                CAnimStateTransition* NewTransition = NewObject<CAnimStateTransition>(GetPackage());
                NewTransition->FromStateNodeID = FromNodeID;
                NewTransition->ToStateNodeID   = ToNodeID;
                Transitions.push_back(NewTransition);
            }
        }
    }
}
