#include "pch.h"
#include "EditorEntityMovementSystem.h"
#include <glm/gtx/string_cast.hpp>
#include "Input/InputProcessor.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/VelocityComponent.h"
#ifdef _WIN32
#include <Windows.h>
#endif
#include "World/WorldManager.h"

namespace Lumina
{
	void SEditorEntityMovementSystem::Update(const FSystemContext& SystemContext) noexcept
	{
		LUMINA_PROFILE_SCOPE();
		
		if (SystemContext.GetWorldType() != EWorldType::Simulation && SystemContext.GetWorldType() != EWorldType::Editor)
		{
			return;
		}

		double DeltaTime = SystemContext.GetDeltaTime();
		auto View = SystemContext.CreateView<STransformComponent, FEditorComponent, SCameraComponent, SVelocityComponent>();
		
		for (entt::entity EditorEntity : View)
		{
			SystemContext.DispatchEvent<FSwitchActiveCameraEvent>(FSwitchActiveCameraEvent{ EditorEntity });

			STransformComponent& Transform = View.get<STransformComponent>(EditorEntity);
			SVelocityComponent& Velocity = View.get<SVelocityComponent>(EditorEntity);
			FEditorComponent& Editor = View.get<FEditorComponent>(EditorEntity);
			
			if (!Editor.bEnabled)
			{
				return;
			}
			
			glm::vec3 Forward = Transform.GetForward();
			glm::vec3 Right = Transform.GetRight();
			glm::vec3 Up = Transform.GetUp();

			float Speed = Velocity.Speed;
			if (FInputProcessor::Get().IsKeyDown(EKey::LeftShift))
			{
				Speed *= 10.0f;
			}

			glm::vec3 Acceleration(0.0f);

			if (FInputProcessor::Get().IsKeyDown(EKey::W))
			{
				Acceleration += Forward; // W = forward (+Z)
			}
			if (FInputProcessor::Get().IsKeyDown(EKey::S))
			{
				Acceleration -= Forward; // S = backward (-Z)
			}
			if (FInputProcessor::Get().IsKeyDown(EKey::D))
			{
				Acceleration += Right; // D = right (+X)
			}
			if (FInputProcessor::Get().IsKeyDown(EKey::A))
			{
				Acceleration -= Right; // A = left (-X)
			}
			if (FInputProcessor::Get().IsKeyDown(EKey::E))
			{
				Acceleration += Up; // E = up (+Y)
			}
			if (FInputProcessor::Get().IsKeyDown(EKey::Q))
			{
				Acceleration -= Up; // Q = down (-Y)
			}

			if (glm::length(Acceleration) > 0.0f)
			{
				Acceleration = glm::normalize(Acceleration) * Speed;
			}

			Velocity.Velocity += Acceleration * static_cast<float>(DeltaTime);

			constexpr float Drag = 10.0f;
			Velocity.Velocity -= Velocity.Velocity * Drag * static_cast<float>(DeltaTime);

			double MouseDeltaZ = FInputProcessor::Get().GetMouseZ();

			Transform.Translate(Velocity.Velocity * static_cast<float>(DeltaTime) * Velocity.Scale);

			if (FInputProcessor::Get().IsMouseButtonDown(EMouseKey::ButtonRight))
			{
				FInputProcessor::Get().SetMouseMode(EMouseMode::Captured);

				double MouseDeltaX = FInputProcessor::Get().GetMouseDeltaX();
				double MouseDeltaY = FInputProcessor::Get().GetMouseDeltaY();

				Transform.AddYaw(static_cast<float>(MouseDeltaX * 0.1));
				Transform.AddPitch(static_cast<float>(MouseDeltaY * 0.1));

				Velocity.Scale += Math::Pow(1.05f, Velocity.Scale) * static_cast<float>(MouseDeltaZ);
				Velocity.Scale = Math::Clamp(Velocity.Scale, 0.2f, 100.0f);
			}

			if (FInputProcessor::Get().IsMouseButtonUp(EMouseKey::ButtonRight))
			{
				FInputProcessor::Get().SetMouseMode(EMouseMode::Normal);
			}
		}
	}
}
