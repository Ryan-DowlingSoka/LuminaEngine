#pragma once

#include "Platform/GenericPlatform.h"

namespace Lumina
{
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
		UpdateListener,
	};

	enum class EAudioStopMode : uint8
	{
		Immediate,
		AllowFadeOut,
	};
}
