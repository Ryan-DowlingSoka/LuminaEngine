#include "AnimGraphNode_State.h"
#include "Core/Object/Class.h"
#include "Core/Object/Package/Package.h"
#include "UI/Tools/NodeGraph/Animation/AnimationGraphNodeGraph.h"

namespace Lumina
{
    void CAnimGraphNode_State::BuildNode()
    {
        // StateFlow pins: the In pin accepts many incoming transitions (and the
        // entry wire); the Out pin fans out to many outgoing transitions.
        InPin  = CreateAnimPin("In", ENodePinDirection::Input, EAnimPinType::StateFlow);
        InPin->bAllowMultipleConnections = true;

        OutPin = CreateAnimPin("Out", ENodePinDirection::Output, EAnimPinType::StateFlow);
    }

    CAnimationGraphNodeGraph* CAnimGraphNode_State::GetOrCreateBlendTree()
    {
        if (!BlendTree.IsValid())
        {
            const FString GraphName = FString("StateBlendTree_") + eastl::to_string(GetNodeID());
            BlendTree = NewObject<CAnimationGraphNodeGraph>(GetPackage(), GraphName);
        }

        // Context-free setup so the compiler can evaluate a state whose blend
        // tree the user has never opened. Initialize() (context creation) is
        // deferred to the editor tool, on first descent into this state.
        BlendTree->EnsureSetup();
        return BlendTree.Get();
    }

    CEdNodeGraph* CAnimGraphNode_State::GetEnterableSubGraph()
    {
        return GetOrCreateBlendTree();
    }

    void CAnimGraphNode_StateEntry::BuildNode()
    {
        OutPin = CreateAnimPin("Entry", ENodePinDirection::Output, EAnimPinType::StateFlow);
    }
}
