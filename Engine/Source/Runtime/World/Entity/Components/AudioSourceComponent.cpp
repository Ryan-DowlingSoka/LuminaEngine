#include "pch.h"
#include "AudioSourceComponent.h"

#include "Assets/AssetTypes/Audio/AudioStream.h"
#include "Audio/AudioGlobals.h"

namespace Lumina
{
	void SAudioSourceComponent::Play()
	{
		if (bPlaying && ActiveHandle.IsValid())
		{
			GAudioContext->StopSound(ActiveHandle);
		}

		if (Sound == nullptr || !Sound->IsValid())
		{
			return;
		}

		ActiveHandle = GAudioContext->PlayAudioAtLocation(
			Sound->GetAudioData(),
			FVector3(0.0f), // Position will be set by the system from the transform.
			Volume,
			Pitch,
			MinDistance,
			MaxDistance,
			bLooping
		);

		bPlaying = true;
	}

	void SAudioSourceComponent::Stop(EAudioStopMode Mode)
	{
		if (bPlaying && ActiveHandle.IsValid())
		{
			GAudioContext->StopSound(ActiveHandle, Mode);
			ActiveHandle = FAudioHandle::Invalid();
			bPlaying = false;
		}
	}
}
