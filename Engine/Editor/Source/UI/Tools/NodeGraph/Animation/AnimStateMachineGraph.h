#pragma once

#include "UI/Tools/NodeGraph/EdNodeGraph.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "AnimStateMachineGraph.generated.h"

namespace Lumina
{
    class CAnimStateTransition;

    // The state machine canvas. Its nodes are State nodes plus a single Entry
    // node; the wires between State nodes are transitions. Transition data lives
    // in CAnimStateTransition objects, reconciled against the live links each
    // time the graph is edited. Owned by a CAnimGraphNode_StateMachine.
    REFLECT()
    class CAnimStateMachineGraph : public CEdNodeGraph
    {
        GENERATED_BODY()
    public:

        void Initialize() override;
        void Shutdown() override;

        // Context-free setup: ensures the Entry node exists and registers the
        // State node as creatable. Safe to call without a node-editor context
        // so the compiler can ready a never-opened state machine. Idempotent.
        void EnsureSetup();

        const FEdGraphSchema& GetSchema() const override;

        // Rebuilds the serialized connection list and reconciles the transition
        // objects against the live State -> State wires.
        void ValidateGraph() override;

        // Finds the transition object bound to a State -> State link, or null
        // (e.g. the Entry wire, which has no transition data).
        CAnimStateTransition* FindTransition(int64 FromStateNodeID, int64 ToStateNodeID) const;

        const TVector<TObjectPtr<CAnimStateTransition>>& GetTransitions() const { return Transitions; }

        /** Transition data behind each State -> State wire. Reconciled in ValidateGraph. */
        PROPERTY()
        TVector<TObjectPtr<CAnimStateTransition>> Transitions;

    private:

        // One-shot guards; not serialized. bSetupDone covers context-free Entry
        // node / registration; bInitialized covers the full Initialize() which
        // additionally creates the node-editor context.
        bool bSetupDone = false;
        bool bInitialized = false;
    };
}
