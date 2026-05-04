#include "pch.h"
#include "ProceduralAudioComponent.h"

#include "Audio/AudioGlobals.h"
#include "Audio/ProceduralAudioStream.h"

namespace Lumina
{
	void SProceduralAudioComponent::Start()
	{
		if (bPlaying)
		{
			return;
		}

		if (!Stream)
		{
			Stream = GAudioContext->CreateProceduralStream(SampleRate, ChannelCount, BufferFrames);
			if (!Stream)
			{
				return;
			}
		}

		ActiveHandle = GAudioContext->PlayProceduralStream(
			Stream,
			bSpatialized,
			glm::vec3(0.0f), // Updated each tick by SAudioSystem.
			Volume, Pitch, MinDistance, MaxDistance);

		bPlaying = true;
	}

	void SProceduralAudioComponent::Stop()
	{
		if (bPlaying && ActiveHandle.IsValid())
		{
			GAudioContext->StopSound(ActiveHandle, EAudioStopMode::Immediate);
			ActiveHandle = FAudioHandle::Invalid();
			bPlaying = false;
		}
	}

	uint32 SProceduralAudioComponent::QueueSamples(const TVector<float>& Samples)
	{
		if (!Stream || ChannelCount == 0)
		{
			return 0;
		}

		const uint32 NumFrames = (uint32)(Samples.size() / ChannelCount);
		if (NumFrames == 0)
		{
			return 0;
		}

		return Stream->Write(Samples.data(), NumFrames);
	}

	uint32 SProceduralAudioComponent::GetQueuedFrameCount()
	{
		return Stream ? Stream->GetAvailableReadFrames() : 0;
	}

	uint32 SProceduralAudioComponent::GetFreeFrameCount()
	{
		return Stream ? Stream->GetAvailableWriteFrames() : 0;
	}
}
