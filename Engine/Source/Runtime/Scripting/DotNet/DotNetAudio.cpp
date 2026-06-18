#include "Platform/GenericPlatform.h"
#include "Core/Math/Math.h"
#include "Memory/SmartPtr.h"
#include "Audio/AudioGlobals.h"
#include "Audio/AudioContext.h"
#include "Audio/AudioTypes.h"
#include "Assets/AssetTypes/Audio/AudioStream.h"
#include "Scripting/DotNet/DotNetExport.h"

//================================================================================================
// World.Audio: play and control sounds from script (LuminaSharp.Audio). The engine audio context is a
// process-wide global (GAudioContext); the leading World handle is reserved (unused today) to keep the
// facade uniform with the other World.* surfaces and to leave room for per-world listeners/mixers. A sound
// is identified by CAudioStream* (the loaded asset, passed as a handle) and controlled afterward by the
// returned FAudioHandle (mirrored byte-for-byte by LuminaSharp.AudioHandle). Thread-safe: each call queues
// a command drained by the audio pump. With no context (audio disabled) every call is a safe no-op.
//================================================================================================

using namespace Lumina;
using namespace Lumina::DotNet;

namespace
{
    // Resolves the shared decoded bytes of a script-passed CAudioStream*, or an empty ptr when the
    // asset handle is null/invalid (a null FAudioData makes the play calls return an invalid handle).
    const TSharedPtr<FAudioData>& AudioDataOf(void* StreamPtr)
    {
        static const TSharedPtr<FAudioData> None;
        const CAudioStream* Stream = reinterpret_cast<const CAudioStream*>(StreamPtr);
        return (Stream != nullptr && Stream->IsValid()) ? Stream->GetAudioData() : None;
    }
}

// Play a 2D (non-spatialized) sound, e.g. UI or music. Returns the controlling handle (invalid on failure).
LUMINA_DOTNET_EXPORT(FAudioHandle, Audio_PlaySound2D)(uint64 World, void* Stream, float Volume, float Pitch, int32 bLoop)
{
    (void)World;
    const TSharedPtr<FAudioData>& Data = AudioDataOf(Stream);
    if (GAudioContext == nullptr || Data.get() == nullptr)
    {
        return FAudioHandle::Invalid();
    }
    return GAudioContext->PlayAudio2D(Data, Volume, Pitch, bLoop != 0);
}

// Play a 3D sound attenuated between MinDistance and MaxDistance around Location.
LUMINA_DOTNET_EXPORT(FAudioHandle, Audio_PlaySoundAtLocation)(uint64 World, void* Stream, FVector3 Location,
    float Volume, float Pitch, float MinDistance, float MaxDistance, int32 bLoop)
{
    (void)World;
    const TSharedPtr<FAudioData>& Data = AudioDataOf(Stream);
    if (GAudioContext == nullptr || Data.get() == nullptr)
    {
        return FAudioHandle::Invalid();
    }
    return GAudioContext->PlayAudioAtLocation(Data, Location, Volume, Pitch, MinDistance, MaxDistance, bLoop != 0);
}

LUMINA_DOTNET_EXPORT(void, Audio_Stop)(uint64 World, FAudioHandle Handle, int32 bAllowFadeOut)
{
    (void)World;
    if (GAudioContext != nullptr)
    {
        GAudioContext->StopSound(Handle, bAllowFadeOut != 0 ? EAudioStopMode::AllowFadeOut : EAudioStopMode::Immediate);
    }
}

LUMINA_DOTNET_EXPORT(void, Audio_StopAll)(uint64 World)
{
    (void)World;
    if (GAudioContext != nullptr)
    {
        GAudioContext->StopAllSounds();
    }
}

LUMINA_DOTNET_EXPORT(void, Audio_SetVolume)(uint64 World, FAudioHandle Handle, float Volume)
{
    (void)World;
    if (GAudioContext != nullptr)
    {
        GAudioContext->SetVolume(Handle, Volume);
    }
}

LUMINA_DOTNET_EXPORT(void, Audio_SetPitch)(uint64 World, FAudioHandle Handle, float Pitch)
{
    (void)World;
    if (GAudioContext != nullptr)
    {
        GAudioContext->SetPitch(Handle, Pitch);
    }
}

LUMINA_DOTNET_EXPORT(void, Audio_SetLooping)(uint64 World, FAudioHandle Handle, int32 bLoop)
{
    (void)World;
    if (GAudioContext != nullptr)
    {
        GAudioContext->SetLooping(Handle, bLoop != 0);
    }
}

LUMINA_DOTNET_EXPORT(void, Audio_SetPosition)(uint64 World, FAudioHandle Handle, FVector3 Position)
{
    (void)World;
    if (GAudioContext != nullptr)
    {
        GAudioContext->SetPosition(Handle, Position);
    }
}

LUMINA_DOTNET_EXPORT(void, Audio_SetMinMaxDistance)(uint64 World, FAudioHandle Handle, float MinDistance, float MaxDistance)
{
    (void)World;
    if (GAudioContext != nullptr)
    {
        GAudioContext->SetMinMaxDistance(Handle, MinDistance, MaxDistance);
    }
}
