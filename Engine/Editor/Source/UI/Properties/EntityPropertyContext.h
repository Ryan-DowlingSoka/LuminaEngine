#pragma once
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    class CWorld;

    // Draw-scoped context for entity-reference property pickers (a uint32 property marked
    // with the "Entity" metadata). Whoever draws a property table for a component that
    // lives in a world sets this around the draw so the picker can enumerate and select
    // entities in that world. Null outside such a scope (e.g. asset editors), in which
    // case the picker falls back to a plain read-only id field.
    CWorld* GetEntityPropertyContextWorld();

    // RAII: makes World the active entity-property context for the scope's lifetime and
    // restores the previous one on exit. Single-threaded editor UI use only.
    class FScopedEntityPropertyContext
    {
    public:
        explicit FScopedEntityPropertyContext(CWorld* InWorld);
        ~FScopedEntityPropertyContext();

        FScopedEntityPropertyContext(const FScopedEntityPropertyContext&) = delete;
        FScopedEntityPropertyContext& operator=(const FScopedEntityPropertyContext&) = delete;

    private:
        CWorld* Previous = nullptr;
    };

    // --- Eyedropper pick request ------------------------------------------------
    // Bridges an entity-reference property picker (in the details panel) and the
    // world viewport. The picker calls RequestEntityPick with a token identifying
    // itself; while a request is active the viewport tool shows a pick cursor and,
    // on the next entity click, calls FulfillEntityPick (or CancelEntityPick on
    // Esc / right-click / miss). The picker polls ConsumeEntityPickResult each frame
    // and writes the chosen id into its property. Only one pick is active at a time.

    // Begin a pick for the picker identified by Token (replaces any in-flight request).
    void RequestEntityPick(uint64 Token);

    // Abort the active pick, whoever owns it. Safe to call when none is active.
    void CancelEntityPick();

    // Viewport: is a picker waiting for a clicked entity this frame?
    bool IsEntityPickRequested();

    // Viewport: deliver the clicked entity's integral id to the waiting picker.
    void FulfillEntityPick(uint32 Entity);

    // Picker: true while Token owns the active request (drives the button highlight).
    bool IsEntityPickActiveFor(uint64 Token);

    // Picker: if a result is pending for Token, write it to OutEntity, clear it, return true.
    bool ConsumeEntityPickResult(uint64 Token, uint32& OutEntity);
}
