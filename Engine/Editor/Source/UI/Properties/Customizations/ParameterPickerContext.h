#pragma once

namespace Lumina
{
    class CAnimationGraph;

    // Context for FName properties tagged ParameterPicker: the anim-graph tool pushes
    // its asset so the FName customization renders a parameter dropdown.
    namespace ParameterPickerContext
    {
        void PushGraph(CAnimationGraph* Graph);
        void PopGraph();

        CAnimationGraph* GetActiveGraph();

        struct FScope
        {
            explicit FScope(CAnimationGraph* Graph) { PushGraph(Graph); }
            ~FScope()                               { PopGraph(); }
            FScope(const FScope&) = delete;
            FScope& operator=(const FScope&) = delete;
        };
    }
}
