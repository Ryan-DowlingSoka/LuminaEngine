#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_State.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Package/Package.h"
#include "Containers/Array.h"
#include "UI/Tools/NodeGraph/EdNodeGraphPin.h"
#include "UI/Tools/NodeGraph/Animation/AnimStateMachineGraph.h"
#include "UI/Tools/NodeGraph/Animation/AnimStateTransition.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphCompiler.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphNodeGraph.h"

namespace Lumina
{
    void CAnimGraphNode_StateMachine::BuildNode()
    {
        ResultPin = CreateAnimPin("Result", ENodePinDirection::Output, EAnimPinType::Pose);
    }

    CAnimStateMachineGraph* CAnimGraphNode_StateMachine::GetOrCreateStateMachineGraph()
    {
        if (!StateMachineGraph.IsValid())
        {
            const FString GraphName = FString("StateMachine_") + eastl::to_string(GetNodeID());
            StateMachineGraph = NewObject<CAnimStateMachineGraph>(GetPackage(), GraphName);
        }

        // Context-free setup so the compiler can walk a state machine the user
        // has never opened. Initialize() (context creation) is deferred to the
        // editor tool, on first descent into this node.
        StateMachineGraph->EnsureSetup();
        return StateMachineGraph.Get();
    }

    CEdNodeGraph* CAnimGraphNode_StateMachine::GetEnterableSubGraph()
    {
        return GetOrCreateStateMachineGraph();
    }

    void CAnimGraphNode_StateMachine::GenerateBytecode(FAnimationGraphCompiler& Compiler)
    {
        FAnimGraphStateMachine StateMachine;

        if (!StateMachineGraph.IsValid())
        {
            // Never opened -> nothing to evaluate. Emit a bind pose so the
            // graph still resolves; the user gets an error to act on.
            EdNodeGraph::FError Error;
            Error.Name        = "Empty State Machine";
            Error.Description = "State Machine has no states; double-click it to add some.";
            Error.Node        = this;
            Compiler.AddError(Error);

            const uint16 BindPose = Compiler.EmitRefPose();
            Compiler.SetPinRegister(ResultPin, BindPose);
            return;
        }

        CAnimStateMachineGraph* SMGraph = GetOrCreateStateMachineGraph();

        // Compile every State's blend tree into the shared register space and
        // record which pose register each state resolved to.
        THashMap<int64, int32> NodeIDToStateIndex;
        TVector<TPair<CEdGraphNode*, int32>> StateNodesForDebug;

        for (CEdGraphNode* Node : SMGraph->Nodes)
        {
            CAnimGraphNode_State* StateNode = Cast<CAnimGraphNode_State>(Node);
            if (StateNode == nullptr)
            {
                continue;
            }

            CAnimationGraphNodeGraph* BlendTree = StateNode->GetOrCreateBlendTree();

            uint16 PoseReg = 0;
            if (!BlendTree->CompileNodes(Compiler, PoseReg))
            {
                // CompileNodes already reported the error; fall back to a bind
                // pose so state indices stay consistent with the canvas.
                PoseReg = Compiler.EmitRefPose();
            }

            const int32 StateIndex = (int32)StateMachine.StatePoseRegisters.size();
            StateMachine.StatePoseRegisters.push_back(PoseReg);
            NodeIDToStateIndex[StateNode->GetNodeID()] = StateIndex;
            StateNodesForDebug.emplace_back(StateNode, StateIndex);
        }

        if (StateMachine.StatePoseRegisters.empty())
        {
            EdNodeGraph::FError Error;
            Error.Name        = "Empty State Machine";
            Error.Description = "State Machine has no State nodes; it will evaluate to the bind pose.";
            Error.Node        = this;
            Compiler.AddError(Error);
        }

        // Entry state: follow the Entry node's single outgoing wire.
        StateMachine.EntryState = 0;
        for (CEdGraphNode* Node : SMGraph->Nodes)
        {
            CAnimGraphNode_StateEntry* EntryNode = Cast<CAnimGraphNode_StateEntry>(Node);
            if (EntryNode == nullptr || EntryNode->OutPin == nullptr)
            {
                continue;
            }

            if (EntryNode->OutPin->HasConnection())
            {
                CEdNodeGraphPin* TargetPin = EntryNode->OutPin->GetConnection(0);
                CAnimGraphNode_State* TargetState = Cast<CAnimGraphNode_State>(TargetPin->GetOwningNode());
                if (TargetState != nullptr)
                {
                    auto It = NodeIDToStateIndex.find(TargetState->GetNodeID());
                    if (It != NodeIDToStateIndex.end())
                    {
                        StateMachine.EntryState = It->second;
                    }
                }
            }
            else if (!StateMachine.StatePoseRegisters.empty())
            {
                EdNodeGraph::FError Error;
                Error.Name        = "No Entry State";
                Error.Description = "State Machine's Entry node is not wired to a State; defaulting to the first state.";
                Error.Node        = this;
                Compiler.AddError(Error);
            }
            break;
        }

        // Transitions: resolve each transition object's endpoint node IDs to
        // state indices and copy the condition through.
        for (const TObjectPtr<CAnimStateTransition>& Transition : SMGraph->GetTransitions())
        {
            if (!Transition.IsValid())
            {
                continue;
            }

            auto FromIt = NodeIDToStateIndex.find(Transition->FromStateNodeID);
            auto ToIt   = NodeIDToStateIndex.find(Transition->ToStateNodeID);
            if (FromIt == NodeIDToStateIndex.end() || ToIt == NodeIDToStateIndex.end())
            {
                // Endpoint state missing -- skip rather than emit a bad index.
                continue;
            }

            FAnimGraphTransition Runtime;
            Runtime.FromState          = FromIt->second;
            Runtime.ToState            = ToIt->second;
            Runtime.ConditionParameter = Transition->ConditionParameter;
            Runtime.Compare            = Transition->Compare;
            Runtime.CompareValue       = Transition->CompareValue;
            Runtime.BlendDuration      = Transition->BlendDuration;
            Runtime.bCanInterrupt      = Transition->bCanInterrupt;

            // Make sure the condition parameter exists in the compiled table,
            // and warn if it doesn't match a blackboard key (renamed / retyped).
            Compiler.ValidateParameterKey(Transition->ConditionParameter, this);
            Compiler.AddParameter(Transition->ConditionParameter, EAnimGraphParamType::Float, 0.0f);

            StateMachine.Transitions.push_back(Runtime);
        }

        // Allocate the four persistent bookkeeping slots.
        StateMachine.CurrentStateSlot = Compiler.AllocStateSlot();
        StateMachine.FromStateSlot    = Compiler.AllocStateSlot();
        StateMachine.TimerSlot        = Compiler.AllocStateSlot();
        StateMachine.DurationSlot     = Compiler.AllocStateSlot();

        // Record which slot/index each State node maps to so the editor's debug
        // overlay can highlight the state the VM is currently in.
        for (const TPair<CEdGraphNode*, int32>& Entry : StateNodesForDebug)
        {
            Compiler.AddDebugStateNode(Entry.first, StateMachine.CurrentStateSlot, Entry.second);
        }

        const uint16 ResultReg = Compiler.EmitEvalStateMachine(Move(StateMachine));
        Compiler.SetPinRegister(ResultPin, ResultReg);
    }
}
