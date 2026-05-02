#include "pch.h"
#include "MiniaudioContext.h"

#include <glm/gtx/quaternion.hpp>
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Memory/Memory.h"
#include "MiniAudio/miniaudio.h"

namespace Lumina
{
	void* MiniAudioMalloc(size_t Size, void*)
	{
		return Memory::Malloc(Size);
	}
	
	void* MiniAudioRealloc(void* P, size_t Size, void*)
	{
		return Memory::Realloc(P, Size);
	}
	
	void MiniAudioFree(void* P, void*)
	{
		Memory::Free(P);
	}
	
	FMiniaudioContext::FMiniaudioContext()
	{
		ma_allocation_callbacks Callbacks;
		Callbacks.pUserData = nullptr;
		Callbacks.onMalloc	= MiniAudioMalloc;
		Callbacks.onRealloc = MiniAudioRealloc;
		Callbacks.onFree	= MiniAudioFree;
		
		ma_engine_config Config = ma_engine_config_init();
		Config.allocationCallbacks = Callbacks;
		
		ma_result Result = ma_engine_init(&Config, &Engine);
		if (Result != MA_SUCCESS)
		{
			LOG_ERROR("FMiniaudioContext: Failed to initialize miniaudio engine (error {})", (int)Result);
			return;
		}
		
		bRunning.store(true, Atomic::MemoryOrderRelease);

		AudioThread = FThread([this]()
		{
			AudioThreadMain();
		});
	}

	FMiniaudioContext::~FMiniaudioContext()
	{
		bRunning.store(false, Atomic::MemoryOrderRelease);

		if (AudioThread.joinable())
		{
			AudioThread.join();
		}

		for (TUniquePtr<FActiveSound>& Sound : ActiveSounds)
		{
			UninitSound(*Sound);
		}
		ActiveSounds.clear();

		ma_engine_uninit(&Engine);
	}
	

	FAudioHandle FMiniaudioContext::AllocateHandle()
	{
		FAudioHandle Handle;
		Handle.Generation = NextGeneration.fetch_add(1, Atomic::MemoryOrderRelaxed);
		Handle.Index      = 0; // Assigned on audio thread.
		return Handle;
	}

	FAudioHandle FMiniaudioContext::PlaySound2D(FStringView File, float Volume, float Pitch, bool bLooping)
	{
		FAudioHandle Handle = AllocateHandle();
		CommandQueue.enqueue(FAudioCommand::MakePlay(
			Handle, File, false, glm::vec3(0.0f), Volume, Pitch, 1.0f, 50.0f, bLooping));
		return Handle;
	}

	FAudioHandle FMiniaudioContext::PlaySoundAtLocation(FStringView File, glm::vec3 Location,
		float Volume, float Pitch, float MinDistance, float MaxDistance, bool bLooping)
	{
		FAudioHandle Handle = AllocateHandle();
		CommandQueue.enqueue(FAudioCommand::MakePlay(
			Handle, File, true, Location, Volume, Pitch, MinDistance, MaxDistance, bLooping));
		return Handle;
	}

	void FMiniaudioContext::StopSound(FAudioHandle Handle, EAudioStopMode Mode)
	{
		CommandQueue.enqueue(FAudioCommand::MakeStop(Handle, Mode));
	}

	void FMiniaudioContext::SetVolume(FAudioHandle Handle, float Volume)
	{
		CommandQueue.enqueue(FAudioCommand::MakeSetVolume(Handle, Volume));
	}

	void FMiniaudioContext::SetPitch(FAudioHandle Handle, float Pitch)
	{
		CommandQueue.enqueue(FAudioCommand::MakeSetPitch(Handle, Pitch));
	}

	void FMiniaudioContext::SetLooping(FAudioHandle Handle, bool bLooping)
	{
		CommandQueue.enqueue(FAudioCommand::MakeSetLooping(Handle, bLooping));
	}

	void FMiniaudioContext::SetPosition(FAudioHandle Handle, glm::vec3 Position)
	{
		CommandQueue.enqueue(FAudioCommand::MakeSetPosition(Handle, Position));
	}

	void FMiniaudioContext::SetMinMaxDistance(FAudioHandle Handle, float MinDistance, float MaxDistance)
	{
		CommandQueue.enqueue(FAudioCommand::MakeSetMinMaxDistance(Handle, MinDistance, MaxDistance));
	}

	void FMiniaudioContext::UpdateListenerPosition(glm::vec3 Location, glm::quat Rotation)
	{
		CommandQueue.enqueue(FAudioCommand::MakeUpdateListener(Location, Rotation));
	}

	void FMiniaudioContext::StopAllSounds()
	{
		CommandQueue.enqueue(FAudioCommand::MakeStopAll());
	}
	
	void FMiniaudioContext::AudioThreadMain()
	{
		Memory::InitializeThreadHeap();
		Threading::SetThreadName("Audio Thread");

		constexpr int32 MaxCommandsPerTick = 64;
		FAudioCommand Cmd;

		while (bRunning.load(std::memory_order_acquire))
		{
			LUMINA_PROFILE_SECTION_COLORED("Audio-Thread-Tick", tracy::Color::Orange);

			int32 Processed = 0;
			while (Processed < MaxCommandsPerTick && CommandQueue.try_dequeue(Cmd))
			{
				ProcessCommand(Cmd);
				++Processed;
			}

			CleanupFinishedSounds();

			Threading::Sleep(5);
		}
	}

	void FMiniaudioContext::ProcessCommand(const FAudioCommand& Cmd)
	{
		switch (Cmd.Type)
		{
		case EAudioCommandType::PlaySound:
		{
			TVector<uint8> Bytes;
			if (!VFS::ReadFile(Bytes, FStringView(Cmd.Play.Path)))
			{
				LOG_WARN("Audio: Failed to read file '{}'", Cmd.Play.Path);
				return;
			}

			// Heap-allocate; ma_decoder/ma_sound store pointers in the engine node graph and must not move.
			TUniquePtr<FActiveSound> NewSound = MakeUnique<FActiveSound>();
			NewSound->Handle = Cmd.Handle;
			NewSound->Bytes  = eastl::move(Bytes);

			if (ma_decoder_init_memory(NewSound->Bytes.data(), NewSound->Bytes.size(), nullptr, &NewSound->Decoder) != MA_SUCCESS)
			{
				LOG_WARN("Audio: Failed to decode '{}'", Cmd.Play.Path);
				return;
			}

			if (ma_sound_init_from_data_source(&Engine, &NewSound->Decoder, 0, nullptr, &NewSound->Sound) != MA_SUCCESS)
			{
				ma_decoder_uninit(&NewSound->Decoder);
				LOG_WARN("Audio: Failed to init sound '{}'", Cmd.Play.Path);
				return;
			}

			NewSound->bInitialized = true;

			ma_sound_set_volume(&NewSound->Sound, Cmd.Play.Volume);
			ma_sound_set_pitch(&NewSound->Sound, Cmd.Play.Pitch);
			ma_sound_set_looping(&NewSound->Sound, Cmd.Play.bLooping ? MA_TRUE : MA_FALSE);

			if (Cmd.Play.bSpatialized)
			{
				ma_sound_set_spatialization_enabled(&NewSound->Sound, MA_TRUE);
				ma_sound_set_position(&NewSound->Sound, Cmd.Play.Position.x, Cmd.Play.Position.y, Cmd.Play.Position.z);
				ma_sound_set_min_distance(&NewSound->Sound, Cmd.Play.MinDistance);
				ma_sound_set_max_distance(&NewSound->Sound, Cmd.Play.MaxDistance);
			}
			else
			{
				ma_sound_set_spatialization_enabled(&NewSound->Sound, MA_FALSE);
			}

			ma_sound_start(&NewSound->Sound);
			ActiveSounds.push_back(eastl::move(NewSound));
			break;
		}

		case EAudioCommandType::StopSound:
		{
			FActiveSound* Sound = FindSound(Cmd.Handle);
			if (Sound)
			{
				if (Cmd.Stop.Mode == EAudioStopMode::AllowFadeOut)
				{
					constexpr ma_uint64 FadeMs = 500;
					ma_sound_set_fade_in_milliseconds(&Sound->Sound, -1.0f, 0.0f, FadeMs);
					ma_uint64 Now = ma_engine_get_time_in_milliseconds(&Engine);
					ma_sound_set_stop_time_in_milliseconds(&Sound->Sound, Now + FadeMs);
				}
				else
				{
					ma_sound_stop(&Sound->Sound);
				}
			}
			break;
		}

		case EAudioCommandType::StopAll:
		{
			for (TUniquePtr<FActiveSound>& Sound : ActiveSounds)
			{
				UninitSound(*Sound);
			}
			ActiveSounds.clear();
			break;
		}

		case EAudioCommandType::SetVolume:
		{
			FActiveSound* Sound = FindSound(Cmd.Handle);
			if (Sound)
			{
				ma_sound_set_volume(&Sound->Sound, Cmd.SetFloat.Value);
			}
			break;
		}

		case EAudioCommandType::SetPitch:
		{
			FActiveSound* Sound = FindSound(Cmd.Handle);
			if (Sound)
			{
				ma_sound_set_pitch(&Sound->Sound, Cmd.SetFloat.Value);
			}
			break;
		}

		case EAudioCommandType::SetLooping:
		{
			FActiveSound* Sound = FindSound(Cmd.Handle);
			if (Sound)
			{
				ma_sound_set_looping(&Sound->Sound, Cmd.SetBool.Value ? MA_TRUE : MA_FALSE);
			}
			break;
		}

		case EAudioCommandType::SetPosition:
		{
			FActiveSound* Sound = FindSound(Cmd.Handle);
			if (Sound)
			{
				ma_sound_set_position(&Sound->Sound, Cmd.SetPosition.Position.x, Cmd.SetPosition.Position.y, Cmd.SetPosition.Position.z);
			}
			break;
		}

		case EAudioCommandType::SetMinMaxDistance:
		{
			FActiveSound* Sound = FindSound(Cmd.Handle);
			if (Sound)
			{
				ma_sound_set_min_distance(&Sound->Sound, Cmd.SetMinMax.MinDistance);
				ma_sound_set_max_distance(&Sound->Sound, Cmd.SetMinMax.MaxDistance);
			}
			break;
		}

		case EAudioCommandType::UpdateListener:
		{
			glm::vec3 Forward = glm::normalize(glm::rotate(Cmd.Listener.Rotation, glm::vec3(0.0f, 0.0f, 1.0f)));
			glm::vec3 Up      = glm::normalize(glm::rotate(Cmd.Listener.Rotation, glm::vec3(0.0f, 1.0f, 0.0f)));

			ma_engine_listener_set_position(&Engine, 0,
				Cmd.Listener.Position.x, Cmd.Listener.Position.y, Cmd.Listener.Position.z);
			ma_engine_listener_set_direction(&Engine, 0, Forward.x, Forward.y, Forward.z);
			ma_engine_listener_set_world_up(&Engine, 0, Up.x, Up.y, Up.z);
			break;
		}
		}
	}

	void FMiniaudioContext::CleanupFinishedSounds()
	{
		for (int32 i = (int32)ActiveSounds.size() - 1; i >= 0; --i)
		{
			FActiveSound& Sound = *ActiveSounds[i];
			if (Sound.bInitialized && ma_sound_at_end(&Sound.Sound))
			{
				UninitSound(Sound);
				// Swap-and-pop; the FActiveSound heap address stays stable as miniaudio requires.
				if (i < (int32)ActiveSounds.size() - 1)
				{
					ActiveSounds[i] = eastl::move(ActiveSounds.back());
				}
				ActiveSounds.pop_back();
			}
		}
	}

	FMiniaudioContext::FActiveSound* FMiniaudioContext::FindSound(FAudioHandle Handle)
	{
		for (TUniquePtr<FActiveSound>& Sound : ActiveSounds)
		{
			if (Sound->Handle == Handle)
			{
				return Sound.get();
			}
		}
		return nullptr;
	}

	void FMiniaudioContext::UninitSound(FActiveSound& Sound)
	{
		if (Sound.bInitialized)
		{
			ma_sound_uninit(&Sound.Sound);
			ma_decoder_uninit(&Sound.Decoder);
			Sound.bInitialized = false;
		}
	}
}
