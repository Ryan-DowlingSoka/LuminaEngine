#pragma once

#include "Containers/Array.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
	// Immutable encoded audio bytes (e.g. a .wav file image) shared between an asset and any
	// in-flight sounds; the audio pump decodes from these bytes, so they must stay alive for
	// the duration of playback. Share via TSharedPtr.
	struct FAudioData
	{
		TVector<uint8> Bytes;
	};

	struct FAudioHandle
	{
		uint32 Generation = 0;
		uint32 Index      = 0;

		constexpr bool IsValid() const { return Generation != 0; }

		constexpr bool operator==(const FAudioHandle& Other) const
		{
			return Generation == Other.Generation && Index == Other.Index;
		}

		constexpr bool operator!=(const FAudioHandle& Other) const
		{
			return !(*this == Other);
		}

		static constexpr FAudioHandle Invalid() { return FAudioHandle{}; }
	};

	enum class EAudioCommandType : uint8
	{
		PlaySound,
		StopSound,
		StopAll,
		SetVolume,
		SetPitch,
		SetLooping,
		SetPosition,
		SetMinMaxDistance,
		SeekToFrame,
		UpdateListener,
	};

	enum class EAudioStopMode : uint8
	{
		Immediate,
		AllowFadeOut,
	};
}
