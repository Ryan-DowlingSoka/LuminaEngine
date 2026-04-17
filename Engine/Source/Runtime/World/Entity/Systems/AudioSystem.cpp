#include "pch.h"
#include "AudioSystem.h"
#include "Audio/AudioGlobals.h"
#include "World/Entity/Components/AudioSourceComponent.h"

namespace Lumina
{
	void SAudioSystem::Startup(const FSystemContext& Context) noexcept
	{
	}

	void SAudioSystem::Teardown(const FSystemContext& Context) noexcept
	{
		// Stop all sounds owned by audio source components in this world.
		auto View = Context.CreateView<SAudioSourceComponent>();
		View.each([](SAudioSourceComponent& Audio)
		{
			if (Audio.bPlaying && Audio.ActiveHandle.IsValid())
			{
				GAudioContext->StopSound(Audio.ActiveHandle);
				Audio.ActiveHandle = FAudioHandle::Invalid();
				Audio.bPlaying = false;
			}
		});
	}

	void SAudioSystem::Update(const FSystemContext& SystemContext) noexcept
	{
		LUMINA_PROFILE_SCOPE();

		{
			auto ListenerView = SystemContext.CreateView<SAudioListenerComponent, STransformComponent>();
			ListenerView.each([](SAudioListenerComponent&, const STransformComponent& Transform)
			{
				GAudioContext->UpdateListenerPosition(Transform.GetLocation(), Transform.GetRotation());
			});
		}

		{
			auto SourceView = SystemContext.CreateView<SAudioSourceComponent, STransformComponent>();
			SourceView.each([](SAudioSourceComponent& Audio, const STransformComponent& Transform)
			{
				if (!Audio.bReady)
				{
					Audio.bReady = true;

					if (Audio.bPlayOnReady && !Audio.SoundFile.empty())
					{
						Audio.ActiveHandle = GAudioContext->PlaySoundAtLocation(
							FStringView(Audio.SoundFile),
							Transform.GetLocation(),
							Audio.Volume,
							Audio.Pitch,
							Audio.MinDistance,
							Audio.MaxDistance,
							Audio.bLooping);
						
						Audio.bPlaying = true;
					}
				}

				if (Audio.bPlaying && Audio.ActiveHandle.IsValid())
				{
					GAudioContext->SetPosition(Audio.ActiveHandle, Transform.GetLocation());

					if (Audio.bVolumeDirty)
					{
						GAudioContext->SetVolume(Audio.ActiveHandle, Audio.Volume);
						Audio.bVolumeDirty = false;
					}

					if (Audio.bPitchDirty)
					{
						GAudioContext->SetPitch(Audio.ActiveHandle, Audio.Pitch);
						Audio.bPitchDirty = false;
					}

					if (Audio.bLoopingDirty)
					{
						GAudioContext->SetLooping(Audio.ActiveHandle, Audio.bLooping);
						Audio.bLoopingDirty = false;
					}
				}
			});
		}
	}
}
