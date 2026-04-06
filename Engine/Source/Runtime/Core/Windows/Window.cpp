#include "pch.h"
#include "Window.h"
#include "stb_image.h"
#include "Core/Application/Application.h"
#include "Events/Event.h"
#include "Paths/Paths.h"
#include "Platform/Platform.h"

namespace
{
	void GLFWErrorCallback(int error, const char* description)
	{
		// Ignore invalid scancode.
		if (error == 65540)
		{
			return;
		}
		LOG_CRITICAL("GLFW Error: {0} | {1}", error, description);
	}

	void* CustomGLFWAllocate(size_t size, void* user)
	{
		return Lumina::Memory::Malloc(size);
	}

	void* CustomGLFWReallocate(void* block, size_t size, void* user)
	{
		return Lumina::Memory::Realloc(block, size);
	}

	void CustomGLFWDeallocate(void* block, void* user)
	{
		Lumina::Memory::Free(block);
	}

	GLFWallocator CustomAllocator =
	{
		CustomGLFWAllocate,
		CustomGLFWReallocate,
		CustomGLFWDeallocate,
		nullptr
	};
}


namespace Lumina
{
	FWindowResizeDelegate FWindow::OnWindowResized;

	GLFWmonitor* GetCurrentMonitor(GLFWwindow* window)
	{
		int windowX, windowY, windowWidth, windowHeight;
		glfwGetWindowPos(window, &windowX, &windowY);
		glfwGetWindowSize(window, &windowWidth, &windowHeight);

		int monitorCount;
		GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

		GLFWmonitor* bestMonitor = nullptr;
		int maxOverlap = 0;

		for (int i = 0; i < monitorCount; ++i)
		{
			int monitorX, monitorY, monitorWidth, monitorHeight;
			glfwGetMonitorWorkarea(monitors[i], &monitorX, &monitorY, &monitorWidth, &monitorHeight);

			int overlapX = std::max(0, std::min(windowX + windowWidth, monitorX + monitorWidth) - std::max(windowX, monitorX));
			int overlapY = std::max(0, std::min(windowY + windowHeight, monitorY + monitorHeight) - std::max(windowY, monitorY));
			int overlapArea = overlapX * overlapY;

			if (overlapArea > maxOverlap)
			{
				maxOverlap = overlapArea;
				bestMonitor = monitors[i];
			}
		}

		return bestMonitor;
	}

	FWindow::~FWindow()
	{
		glfwDestroyWindow(Window);
		glfwTerminate();
	}

	void FWindow::Init()
	{
		if (LIKELY(!bInitialized))
		{
			glfwInitAllocator(&CustomAllocator);
			glfwInit();
			glfwSetErrorCallback(GLFWErrorCallback);

			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#if WITH_EDITOR
			glfwWindowHint(GLFW_TITLEBAR, GLFW_FALSE);
#else
			glfwWindowHint(GLFW_TITLEBAR, GLFW_TRUE);

#endif
			//glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

			GLFWmonitor* Monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* Mode = glfwGetVideoMode(Monitor);

			Specs.Extent.x = Mode->width - 300;
			Specs.Extent.y = Mode->height - 300;

			Window = glfwCreateWindow(Specs.Extent.x, Specs.Extent.y, Specs.Title.c_str(), nullptr, nullptr);
			glfwSetWindowAttrib(Window, GLFW_RESIZABLE, GLFW_TRUE);
			
			const int PosX = (Mode->width  - Specs.Extent.x) / 2;
			const int PosY = (Mode->height - Specs.Extent.y) / 2;
			glfwSetWindowPos(Window, PosX, PosY);			
			
			LOG_TRACE("Initializing Window: {} (Width: {}p Height: {}p)", Specs.Title, Specs.Extent.x, Specs.Extent.y);

			GLFWimage Icon;
			int Channels;
			FString IconPathStr = Paths::GetEngineResourceDirectory() + "/Textures/Lumina.png";
			Icon.pixels = stbi_load(IconPathStr.c_str(), &Icon.width, &Icon.height, &Channels, 4);
			if (Icon.pixels)
			{
				glfwSetWindowIcon(Window, 1, &Icon);
				stbi_image_free(Icon.pixels);
			}

			glfwSetMouseButtonCallback(Window, MouseButtonCallback);
			glfwSetCursorPosCallback(Window, MousePosCallback);
			glfwSetScrollCallback(Window, MouseScrollCallback);
			glfwSetKeyCallback(Window, KeyCallback);
			glfwSetWindowUserPointer(Window, this);
			glfwSetWindowSizeCallback(Window, WindowResizeCallback);
			glfwSetDropCallback(Window, WindowDropCallback);
			glfwSetWindowCloseCallback(Window, WindowCloseCallback);
		}
	}

	void FWindow::ProcessMessages()
	{
		glfwPollEvents();
	}

	glm::uvec2 FWindow::GetExtent() const
	{
		glm::ivec2 ReturnVal;
		glfwGetWindowSize(Window, &ReturnVal.x, &ReturnVal.y);

		return ReturnVal;
	}

	uint32 FWindow::GetWidth() const
	{
		return GetExtent().x;
	}

	uint32 FWindow::GetHeight() const
	{
		return GetExtent().y;
	}

	bool FWindow::IsWindowMaximized() const
	{
		return glfwGetWindowAttrib(Window, GLFW_MAXIMIZED);
	}

	void FWindow::GetWindowPosition(int& X, int& Y)
	{
		glfwGetWindowPos(Window, &X, &Y);
	}

	void FWindow::SetWindowPosition(int X, int Y)
	{
		glfwSetWindowPos(Window, X, Y);
	}

	void FWindow::SetWindowSize(int X, int Y)
	{
		glfwSetWindowSize(Window, X, Y);
	}

	bool FWindow::ShouldClose() const
	{
		return glfwWindowShouldClose(Window);
	}

	bool FWindow::IsWindowMinimized() const
	{
		return glfwGetWindowAttrib(Window, GLFW_ICONIFIED);
	}
	
	void FWindow::Minimize()
	{
		glfwIconifyWindow(Window);
	}

	void FWindow::Restore()
	{
		glfwRestoreWindow(Window);
	}

	void FWindow::Maximize()
	{
		glfwMaximizeWindow(Window);
	}

	void FWindow::Close()
	{
		glfwSetWindowShouldClose(Window, GLFW_TRUE);
	}

	void FWindow::MouseButtonCallback(GLFWwindow* window, int Button, int Action, int Mods)
	{
		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);

		switch (Action)
		{
		case GLFW_PRESS:
		{
			GApp->GetEventProcessor().Dispatch<FMouseButtonPressedEvent>(static_cast<EMouseKey>(Button), xpos, ypos);
		}
		break;

		case GLFW_RELEASE:
		{
			GApp->GetEventProcessor().Dispatch<FMouseButtonReleasedEvent>(static_cast<EMouseKey>(Button), xpos, ypos);
		}
		break;
		}
	}

	void FWindow::MousePosCallback(GLFWwindow* window, double xpos, double ypos)
	{
		FWindow* WindowHandle = (FWindow*)glfwGetWindowUserPointer(window);

		if (WindowHandle->bFirstMouseUpdate)
		{
			WindowHandle->LastMouseX = xpos;
			WindowHandle->LastMouseY = ypos;
			WindowHandle->bFirstMouseUpdate = false;

			GApp->GetEventProcessor().Dispatch<FMouseMovedEvent>(xpos, ypos, 0.0, 0.0);
			return;
		}

		double DeltaX = xpos - WindowHandle->LastMouseX;
		double DeltaY = ypos - WindowHandle->LastMouseY;

		WindowHandle->LastMouseX = xpos;
		WindowHandle->LastMouseY = ypos;

		GApp->GetEventProcessor().Dispatch<FMouseMovedEvent>(xpos, ypos, DeltaX, DeltaY);
	}

	void FWindow::MouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
	{
		//We're only dealing with vertical here.
		GApp->GetEventProcessor().Dispatch<FMouseScrolledEvent>(EMouseKey::Scroll, yoffset);
	}

	void FWindow::KeyCallback(GLFWwindow* window, int Key, int Scancode, int Action, int Mods)
	{
		if (Key == GLFW_KEY_UNKNOWN)
		{
			return;
		}

		bool Ctrl = Mods & GLFW_MOD_CONTROL;
		bool Shift = Mods & GLFW_MOD_SHIFT;
		bool Alt = Mods & GLFW_MOD_ALT;
		bool Super = Mods & GLFW_MOD_SUPER;

		switch (Action)
		{
		case GLFW_RELEASE:
		{
			GApp->GetEventProcessor().Dispatch<FKeyReleasedEvent>(static_cast<EKey>(Key), Ctrl, Shift, Alt, Super);
		}
		break;
		case GLFW_PRESS:
		{
			GApp->GetEventProcessor().Dispatch<FKeyPressedEvent>(static_cast<EKey>(Key), Ctrl, Shift, Alt, Super);
		}
		break;
		case GLFW_REPEAT:
		{
			GApp->GetEventProcessor().Dispatch<FKeyPressedEvent>(static_cast<EKey>(Key), Ctrl, Shift, Alt, Super, /* Repeat */ true);
		}
		break;
		}
	}

	void FWindow::WindowResizeCallback(GLFWwindow* window, int width, int height)
	{
		auto WindowHandle = (FWindow*)glfwGetWindowUserPointer(window);
		WindowHandle->Specs.Extent.x = width;
		WindowHandle->Specs.Extent.y = height;

		GApp->GetEventProcessor().Dispatch<FWindowResizeEvent>(width, height);

		OnWindowResized.Broadcast(WindowHandle, WindowHandle->Specs.Extent);
	}

	void FWindow::WindowDropCallback(GLFWwindow* Window, int PathCount, const char* Paths[])
	{
		double xpos, ypos;
		glfwGetCursorPos(Window, &xpos, &ypos);

		TVector<FFixedString> StringPaths;

		for (int i = 0; i < PathCount; ++i)
		{
			StringPaths.emplace_back(Paths[i]);
		}

		GApp->GetEventProcessor().Dispatch<FFileDropEvent>(StringPaths, static_cast<float>(xpos), static_cast<float>(ypos));
	}

	void FWindow::WindowCloseCallback(GLFWwindow* window)
	{
		FApplication::RequestExit();
	}

	namespace Windowing
	{
		FWindow* PrimaryWindow;

		FWindow* GetPrimaryWindowHandle()
		{
			ASSERT(PrimaryWindow != nullptr);
			return PrimaryWindow;
		}

		void SetPrimaryWindowHandle(FWindow* InWindow)
		{
			ASSERT(PrimaryWindow == nullptr);
			PrimaryWindow = InWindow;
		}
	}
}
