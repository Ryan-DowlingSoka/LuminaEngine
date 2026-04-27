#include "pch.h"
#include "InputProcessor.h"

#include "Core/Application/Application.h"
#include "Core/Engine/Engine.h"
#include "Core/Windows/Window.h"
#include "Events/Event.h"
#if WITH_EDITOR
#include "imgui.h"
#endif

namespace Lumina
{
	FInputProcessor& FInputProcessor::Get()
	{
		static FInputProcessor Instance;
		return Instance;
	}

	bool FInputProcessor::OnEvent(FEvent& Event)
	{
		if (Event.IsA<FMouseMovedEvent>())
		{
			FMouseMovedEvent& MouseEvent = Event.As<FMouseMovedEvent>();
			MouseDeltaX += MouseEvent.GetDeltaX();
			MouseDeltaY += MouseEvent.GetDeltaY();
			MouseX = MouseEvent.GetX();
			MouseY = MouseEvent.GetY();
		}
		else if (Event.IsA<FMouseButtonPressedEvent>())
		{
			FMouseButtonEvent& MouseButtonEvent = Event.As<FMouseButtonPressedEvent>();
			uint32 MouseCode = static_cast<uint32>(MouseButtonEvent.GetButton());
			MouseStates[MouseCode]			= Input::EMouseState::Pressed;
			MouseKeyDownTimes[MouseCode]	= 0.0f;
		}
		else if (Event.IsA<FMouseButtonReleasedEvent>())
		{
			FMouseButtonEvent& MouseButtonEvent = Event.As<FMouseButtonReleasedEvent>();
			uint32 MouseCode = static_cast<uint32>(MouseButtonEvent.GetButton());
			MouseStates[MouseCode]			= Input::EMouseState::Released;
			MouseKeyDownTimes[MouseCode]	= -1.0f;
		}
		else if (Event.IsA<FKeyPressedEvent>())
		{
			FKeyPressedEvent& KeyEvent = Event.As<FKeyPressedEvent>();
			uint32 KeyCode = static_cast<uint32>(KeyEvent.GetKeyCode());
			KeyStates[KeyCode] = KeyEvent.IsRepeat() ? Input::EKeyState::Repeated : Input::EKeyState::Pressed;
		}
		else if (Event.IsA<FKeyReleasedEvent>())
		{
			FKeyReleasedEvent& KeyEvent = Event.As<FKeyReleasedEvent>();
			uint32 KeyCode = static_cast<uint32>(KeyEvent.GetKeyCode());
			KeyStates[KeyCode] = Input::EKeyState::Released;
		}
		else if (Event.IsA<FMouseScrolledEvent>())
		{
			FMouseScrolledEvent& MouseEvent = Event.As<FMouseScrolledEvent>();
			MouseZ = MouseEvent.GetOffset();
			uint32 KeyCode = static_cast<uint32>(MouseEvent.GetCode());
			MouseStates[KeyCode] = MouseZ > 0.0 ? Input::EMouseState::Up : Input::EMouseState::Held;
		}

		return false;
	}

	
	void FInputProcessor::SetMouseMode(EMouseMode Mode)
	{
		int DesiredInputMode = GLFW_CURSOR_NORMAL;
		switch (Mode)
		{
		case EMouseMode::Hidden:
			DesiredInputMode = GLFW_CURSOR_HIDDEN;
			break;
		case EMouseMode::Normal:
			DesiredInputMode = GLFW_CURSOR_NORMAL;
			break;
		case EMouseMode::Captured:
			DesiredInputMode = GLFW_CURSOR_DISABLED;
			break;
		}
		
		glfwSetInputMode(Windowing::GetPrimaryWindowHandle()->GetWindow(), GLFW_CURSOR, DesiredInputMode);
		
		#if WITH_EDITOR
		ImGuiIO& IO = ImGui::GetIO();
		if (Mode == EMouseMode::Captured || Mode == EMouseMode::Hidden)
		{
			IO.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		}
		else
		{
			IO.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		}
		#endif
	}

	void FInputProcessor::EndFrame()
	{
		for (auto& State : MouseStates)
		{
			if (State == Input::EMouseState::Pressed)
			{
				State = Input::EMouseState::Held;
			}
			if (State == Input::EMouseState::Released)
			{
				State = Input::EMouseState::Up;
			}
		}

		for (auto& State : KeyStates)
		{
			if (State == Input::EKeyState::Pressed)
			{
				State = Input::EKeyState::Held;
			}
			if (State == Input::EKeyState::Released)
			{
				State = Input::EKeyState::Up;
			}
		}

		for (uint32 i = 0; i < (uint32)EMouseKey::Num; i++)
		{
			if (MouseKeyDownTimes[i] >= 0.0f)
			{
				MouseKeyDownTimes[i] += (float)GEngine->GetDeltaTime();
			}
		}

		MouseDeltaX = 0.0;
		MouseDeltaY = 0.0;
		MouseZ      = 0.0;
	}
}
