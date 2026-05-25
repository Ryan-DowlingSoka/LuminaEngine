#pragma once

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
}
