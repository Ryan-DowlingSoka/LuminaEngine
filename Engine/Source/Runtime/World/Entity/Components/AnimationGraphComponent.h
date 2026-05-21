#pragma once

#include "Animation/AnimationGraphVM.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "AnimationGraphComponent.generated.h"

namespace Lumina
{
    class CAnimationGraph;

    // Drives a skeletal mesh from a compiled animation graph. SAnimationGraphSystem
    // runs the graph's bytecode each frame through FAnimationGraphVM, writing the
    // resolved skinning matrices into the entity's SSkeletalMeshComponent. The
    // graph's parameters are exposed to Lua via the Set/Get functions below so
    // gameplay code can drive blend weights and playback speeds.
    REFLECT(Component, Category = "Animation")
    struct SAnimationGraphComponent
    {
        GENERATED_BODY()

        /** Compiled animation graph asset evaluated each frame for this entity. */
        PROPERTY(Script, Editable, Category = "Animation")
        TObjectPtr<CAnimationGraph> Graph;

        /**
         * Per-instance VM execution state: register files, persistent playback
         * clocks, and current parameter values. Sized lazily from Graph by the
         * system (or by a parameter access below); not serialized.
         */
        FAnimGraphVMState VMState;

        /** Sets a named float parameter. No-op if the graph has no such parameter. */
        FUNCTION(Script)
        void SetFloat(const FName& ParameterName, float Value);

        /** Returns a named float parameter, or Default if the graph has no such parameter. */
        FUNCTION(Script)
        float GetFloat(const FName& ParameterName, float Default = 0.0f) const;

        /** Sets a named bool parameter (stored as 0/1). No-op if the graph has no such parameter. */
        FUNCTION(Script)
        void SetBool(const FName& ParameterName, bool bValue);

        /** Returns a named bool parameter, or Default if the graph has no such parameter. */
        FUNCTION(Script)
        bool GetBool(const FName& ParameterName, bool Default = false) const;

        /** True if the graph declares a parameter with the given name. */
        FUNCTION(Script)
        bool HasParameter(const FName& ParameterName) const;

        // Sizes VMState from the current graph if it has not been initialized
        // yet, so Lua / the system can set parameters before the VM's first tick.
        void EnsureStateInitialized();
    };
}
