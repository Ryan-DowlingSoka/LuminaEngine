#pragma once

namespace Lumina
{
    class CAnimationGraph;

    // Editor-only context for FName properties tagged ParameterPicker. The
    // animation-graph tool pushes its asset before drawing properties; the
    // FName customization reads it back and renders a parameter dropdown
    // instead of a plain text field.
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
