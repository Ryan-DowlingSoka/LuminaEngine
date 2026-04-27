#pragma once

// Jolt headers must be preceded by <Jolt/Jolt.h>; pull both in here so any
// translation unit that needs to talk to the Jolt character can include just
// this one file.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace Lumina
{
    // Pimpl handle that wraps Jolt's intrusive ref to CharacterVirtual.
    //
    // Existed purely so SCharacterPhysicsComponent (a public component header
    // included throughout the ECS) can hold a CharacterVirtual without leaking
    // <Jolt/Physics/Character/CharacterVirtual.h> into every translation unit
    // that touches a component header.
    //
    // The component stores a TUniquePtr<FJoltCharacterHandle>, so destruction
    // of the component just runs ~TUniquePtr -> ~FJoltCharacterHandle ->
    // ~JPH::Ref, which decrements Jolt's intrusive count.
    struct FJoltCharacterHandle
    {
        JPH::Ref<JPH::CharacterVirtual> Ref;
    };
}
