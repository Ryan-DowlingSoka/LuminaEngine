#include "pch.h"
#include "CharacterComponent.h"

// Pulling JoltCharacterHandle.h here -- and only here -- gives the destructor
// of TUniquePtr<FJoltCharacterHandle> a complete type to work with, while the
// header (CharacterComponent.h) stays Jolt-free for the rest of the codebase.
#include "Physics/API/Jolt/JoltCharacterHandle.h"

namespace Lumina
{
    SCharacterPhysicsComponent::SCharacterPhysicsComponent() = default;
    SCharacterPhysicsComponent::~SCharacterPhysicsComponent() = default;
    SCharacterPhysicsComponent::SCharacterPhysicsComponent(const SCharacterPhysicsComponent&) = default;
    SCharacterPhysicsComponent& SCharacterPhysicsComponent::operator=(const SCharacterPhysicsComponent&) = default;
    SCharacterPhysicsComponent::SCharacterPhysicsComponent(SCharacterPhysicsComponent&&) noexcept = default;
    SCharacterPhysicsComponent& SCharacterPhysicsComponent::operator=(SCharacterPhysicsComponent&&) noexcept = default;

    uint32 SCharacterPhysicsComponent::GetBodyID() const
    {
        if (Character == nullptr || Character->Ref == nullptr)
        {
            return 0xFFFFFFFF;
        }
        return Character->Ref->GetInnerBodyID().GetIndexAndSequenceNumber();
    }
}
