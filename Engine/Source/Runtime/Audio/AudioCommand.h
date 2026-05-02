#pragma once

#include "AudioTypes.h"
#include "Containers/String.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Lumina
{
	// Tagged-union command from game thread to audio thread; avoids per-command heap alloc.
	struct FAudioCommand
	{
		EAudioCommandType Type;
		FAudioHandle Handle;

		union
		{
			struct
			{
				// Inline path (up to 255 chars) to avoid heap alloc.
				char Path[256];
				float Volume;
				float Pitch;
				float MinDistance;
				float MaxDistance;
				glm::vec3 Position;
				bool bSpatialized;
				bool bLooping;
			} Play;

			struct
			{
				EAudioStopMode Mode;
			} Stop;

			struct
			{
				float Value;
			} SetFloat;

			struct
			{
				bool Value;
			} SetBool;

			struct
			{
				glm::vec3 Position;
			} SetPosition;

			struct
			{
				float MinDistance;
				float MaxDistance;
			} SetMinMax;

			struct
			{
				glm::vec3 Position;
				glm::quat Rotation;
			} Listener;
		};
		
		static FAudioCommand MakePlay(FAudioHandle InHandle, FStringView Path, bool bSpatialized,
			glm::vec3 Position, float Volume, float Pitch, float MinDistance, float MaxDistance, bool bLooping)
		{
			FAudioCommand Cmd;
			Cmd.Type   = EAudioCommandType::PlaySound;
			Cmd.Handle = InHandle;

			size_t Len = Path.size() < 255 ? Path.size() : 255;
			memcpy(Cmd.Play.Path, Path.data(), Len);
			Cmd.Play.Path[Len]     = '\0';
			Cmd.Play.Volume        = Volume;
			Cmd.Play.Pitch         = Pitch;
			Cmd.Play.MinDistance    = MinDistance;
			Cmd.Play.MaxDistance    = MaxDistance;
			Cmd.Play.Position      = Position;
			Cmd.Play.bSpatialized  = bSpatialized;
			Cmd.Play.bLooping      = bLooping;
			return Cmd;
		}

		static FAudioCommand MakeStop(FAudioHandle InHandle, EAudioStopMode Mode = EAudioStopMode::Immediate)
		{
			FAudioCommand Cmd;
			Cmd.Type        = EAudioCommandType::StopSound;
			Cmd.Handle      = InHandle;
			Cmd.Stop.Mode   = Mode;
			return Cmd;
		}

		static FAudioCommand MakeStopAll()
		{
			FAudioCommand Cmd;
			Cmd.Type   = EAudioCommandType::StopAll;
			Cmd.Handle = FAudioHandle::Invalid();
			return Cmd;
		}

		static FAudioCommand MakeSetVolume(FAudioHandle InHandle, float Volume)
		{
			FAudioCommand Cmd;
			Cmd.Type            = EAudioCommandType::SetVolume;
			Cmd.Handle          = InHandle;
			Cmd.SetFloat.Value  = Volume;
			return Cmd;
		}

		static FAudioCommand MakeSetPitch(FAudioHandle InHandle, float Pitch)
		{
			FAudioCommand Cmd;
			Cmd.Type            = EAudioCommandType::SetPitch;
			Cmd.Handle          = InHandle;
			Cmd.SetFloat.Value  = Pitch;
			return Cmd;
		}

		static FAudioCommand MakeSetLooping(FAudioHandle InHandle, bool bLooping)
		{
			FAudioCommand Cmd;
			Cmd.Type           = EAudioCommandType::SetLooping;
			Cmd.Handle         = InHandle;
			Cmd.SetBool.Value  = bLooping;
			return Cmd;
		}

		static FAudioCommand MakeSetPosition(FAudioHandle InHandle, glm::vec3 Position)
		{
			FAudioCommand Cmd;
			Cmd.Type                 = EAudioCommandType::SetPosition;
			Cmd.Handle               = InHandle;
			Cmd.SetPosition.Position = Position;
			return Cmd;
		}

		static FAudioCommand MakeSetMinMaxDistance(FAudioHandle InHandle, float MinDistance, float MaxDistance)
		{
			FAudioCommand Cmd;
			Cmd.Type                = EAudioCommandType::SetMinMaxDistance;
			Cmd.Handle              = InHandle;
			Cmd.SetMinMax.MinDistance = MinDistance;
			Cmd.SetMinMax.MaxDistance = MaxDistance;
			return Cmd;
		}

		static FAudioCommand MakeUpdateListener(glm::vec3 Position, glm::quat Rotation)
		{
			FAudioCommand Cmd;
			Cmd.Type                = EAudioCommandType::UpdateListener;
			Cmd.Handle              = FAudioHandle::Invalid();
			Cmd.Listener.Position   = Position;
			Cmd.Listener.Rotation   = Rotation;
			return Cmd;
		}
	};
}
