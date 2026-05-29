#include "pch.h"
#include "AudioSystem.h"
#include "Audio/AudioGlobals.h"
#include "World/Entity/EntityHandle.h"
#include "World/Entity/Components/AudioSourceComponent.h"
#include "World/Entity/Components/ProceduralAudioComponent.h"

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

		auto ProceduralView = Context.CreateView<SProceduralAudioComponent>();
		ProceduralView.each([](SProceduralAudioComponent& Audio)
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
		
		auto&& XFormStorage = SystemContext.GetStorage<STransformComponent>();

		{
			auto ListenerView = SystemContext.CreateView<SAudioListenerComponent>();
			ListenerView.each([&](FEntity Entity, const SAudioListenerComponent&)
			{
				const STransformComponent& Transform = XFormStorage.get(Entity);
				GAudioContext->UpdateListenerPosition(Transform.GetWorldLocation(), Transform.GetWorldRotation());
			});
		}

		{
			auto SourceView = SystemContext.CreateView<SAudioSourceComponent>();
			SourceView.each([&](FEntity Entity, SAudioSourceComponent& Audio)
			{

				if (!Audio.bReady)
				{
					Audio.bReady = true;

					if (Audio.bPlayOnReady && !Audio.SoundFile.empty())
					{
						const STransformComponent& Transform = XFormStorage.get(Entity);
						Audio.ActiveHandle = GAudioContext->PlaySoundAtLocation(
							FStringView(Audio.SoundFile),
							Transform.GetWorldLocation(),
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
					const STransformComponent& Transform = XFormStorage.get(Entity);
					GAudioContext->SetPosition(Audio.ActiveHandle, Transform.GetWorldLocation());

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

		{
			auto ProceduralView = SystemContext.CreateView<SProceduralAudioComponent>();
			ProceduralView.each([&](FEntity Entity, SProceduralAudioComponent& Audio)
			{
				if (!Audio.bReady)
				{
					Audio.bReady = true;

					if (Audio.bPlayOnReady)
					{
						Audio.Start();
					}
				}

				if (Audio.bPlaying && Audio.ActiveHandle.IsValid())
				{
					if (Audio.bSpatialized)
					{
						const STransformComponent& Transform = XFormStorage.get(Entity);
						GAudioContext->SetPosition(Audio.ActiveHandle, Transform.GetWorldLocation());
					}

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
				}
			});
		}
	}
}
