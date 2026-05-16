#pragma once

#include "UI/Tools/NodeGraph/EdNodeGraph.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "AnimationGraphNodeGraph.generated.h"

namespace Lumina
{
    class CAnimationGraph;
    class FAnimationGraphCompiler;

    // Editor-side node graph for animation graphs. Mirrors CMaterialNodeGraph:
    // the editor authors nodes here and CompileGraph walks them in topological
    // order, emitting bytecode into the runtime CAnimationGraph asset.
    REFLECT()
    class CAnimationGraphNodeGraph : public CEdNodeGraph
    {
        GENERATED_BODY()
    public:

        void Initialize() override;
        void Shutdown() override;

        // Context-free setup: ensures an Output node exists and registers the
        // creatable node types. Safe to call without a node-editor context, so
        // the compiler can ready a never-opened state blend tree. Idempotent.
        void EnsureSetup();

        // Rebuilds the serialized Connections array from live pin state. The base
        // graph calls this after every connection edit; without the override the
        // array stays empty and links are lost on save (mirrors the material graph).
        void ValidateGraph() override;

        const FEdGraphSchema& GetSchema() const override;

        // Topologically sorts from the Output node and emits bytecode for each
        // contributing node into Compiler, finishing with an Output opcode.
        // Errors are attached to their nodes. Used for the top-level graph.
        void CompileGraph(FAnimationGraphCompiler& Compiler);

        // Compiles this graph's node network into the shared compiler register
        // space WITHOUT emitting a final Output opcode, returning the pose
        // register feeding the graph's Output node in OutPoseReg. Used to
        // compile a state's blend tree as a sub-graph of a state machine.
        // Returns false (and pushes a compiler error) on a cycle / missing Output.
        bool CompileNodes(FAnimationGraphCompiler& Compiler, uint16& OutPoseReg);

        // Registers every parameter declared anywhere in this graph tree,
        // including from nodes (Get Parameter, state machine transition
        // conditions) that the topo-sort would skip because they aren't wired
        // into the Output. Recurses into nested state machines and per-state
        // blend trees. Run once from the top-level CompileGraph.
        void CollectAllParameters(FAnimationGraphCompiler& Compiler);

        void SetAnimationGraph(CAnimationGraph* InGraph) { AnimationGraph = InGraph; }
        CAnimationGraph* GetAnimationGraph() const { return AnimationGraph; }

    private:

        TObjectPtr<CAnimationGraph> AnimationGraph;

        // One-shot guards; not serialized. bSetupDone covers the context-free
        // Output-node / node-registration setup; bInitialized covers the full
        // Initialize() (which additionally creates the node-editor context).
        bool bSetupDone = false;
        bool bInitialized = false;
    };
}
