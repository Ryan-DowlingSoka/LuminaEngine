#pragma once

#include "EdGraphNode.h"
#include "EdNodeGraphPin.h"
#include "Containers/Array.h"

namespace Lumina::GraphAlgorithms
{
    // Topologically sorts the subset of Nodes reachable (via input edges) from the first node matching
    // RootPredicate. SortedNodes is written with nodes in dependency order (roots-last).
    // Returns the first node participating in a cycle, or nullptr on success / when no root is found.
    template <typename TRootPredicate>
    CEdGraphNode* TopologicalSortFromRoot(const TVector<TObjectPtr<CEdGraphNode>>& Nodes, TVector<CEdGraphNode*>& SortedNodes, TRootPredicate&& IsRoot)
    {
        CEdGraphNode* RootNode = nullptr;
        for (CEdGraphNode* Node : Nodes)
        {
            if (IsRoot(Node))
            {
                RootNode = Node;
                break;
            }
        }

        if (RootNode == nullptr)
        {
            SortedNodes.clear();
            return nullptr;
        }

        THashSet<CEdGraphNode*> ReachableNodes;
        TQueue<CEdGraphNode*> ReverseQueue;
        ReverseQueue.push(RootNode);
        ReachableNodes.insert(RootNode);

        while (!ReverseQueue.empty())
        {
            CEdGraphNode* Node = ReverseQueue.front();
            ReverseQueue.pop();

            for (CEdNodeGraphPin* InputPin : Node->GetInputPins())
            {
                for (CEdNodeGraphPin* ConnectedPin : InputPin->GetConnections())
                {
                    CEdGraphNode* ConnectedNode = ConnectedPin->GetOwningNode();
                    if (ReachableNodes.insert(ConnectedNode).second)
                    {
                        ReverseQueue.push(ConnectedNode);
                    }
                }
            }
        }

        THashMap<CEdGraphNode*, uint32> InDegree;
        for (CEdGraphNode* Node : Nodes)
        {
            if (ReachableNodes.find(Node) != ReachableNodes.end())
            {
                InDegree[Node] = 0;
            }
        }

        for (CEdGraphNode* Node : Nodes)
        {
            if (ReachableNodes.find(Node) == ReachableNodes.end())
            {
                continue;
            }

            for (CEdNodeGraphPin* OutputPin : Node->GetOutputPins())
            {
                for (CEdNodeGraphPin* ConnectedPin : OutputPin->GetConnections())
                {
                    CEdGraphNode* ConnectedNode = ConnectedPin->GetOwningNode();
                    if (ReachableNodes.find(ConnectedNode) != ReachableNodes.end())
                    {
                        InDegree[ConnectedNode]++;
                    }
                }
            }
        }

        TQueue<CEdGraphNode*> ReadyQueue;
        for (auto& Pair : InDegree)
        {
            if (Pair.second == 0)
            {
                ReadyQueue.push(Pair.first);
            }
        }

        uint32 ProcessedNodeCount = 0;
        while (!ReadyQueue.empty())
        {
            CEdGraphNode* Node = ReadyQueue.front();
            ReadyQueue.pop();
            SortedNodes.push_back(Node);
            ProcessedNodeCount++;

            for (CEdNodeGraphPin* OutputPin : Node->GetOutputPins())
            {
                for (CEdNodeGraphPin* ConnectedPin : OutputPin->GetConnections())
                {
                    CEdGraphNode* ConnectedNode = ConnectedPin->GetOwningNode();
                    if (ReachableNodes.find(ConnectedNode) == ReachableNodes.end())
                    {
                        continue;
                    }

                    if (--InDegree[ConnectedNode] == 0)
                    {
                        ReadyQueue.push(ConnectedNode);
                    }
                }
            }
        }

        if (ProcessedNodeCount != ReachableNodes.size())
        {
            for (auto& Pair : InDegree)
            {
                if (Pair.second > 0)
                {
                    SortedNodes.clear();
                    return Pair.first;
                }
            }
        }

        return nullptr;
    }
}
