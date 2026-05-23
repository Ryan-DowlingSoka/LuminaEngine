#pragma once

#include "WindowTypes.h"
#include "Core/Delegates/Delegate.h"

#include "GLFW/glfw3.h"

namespace Lumina
{
	class FWindow;

	DECLARE_MULTICAST_DELEGATE(FWindowResizeDelegate, FWindow*, const glm::uvec2&);

	class FWindow
	{
	public:

		FWindow(const FWindowSpecs& InSpecs)
			: LastMouseX(0)
			, LastMouseY(0)
			, Specs(InSpecs)
		{
			Init();
		}

		virtual ~FWindow();
		LE_NO_COPYMOVE(FWindow);

		void Init();
		void ProcessMessages();

		GLFWwindow* GetWindow() const { return Window; }

		RUNTIME_API glm::uvec2 GetExtent() const;
		RUNTIME_API uint32 GetWidth() const;
		RUNTIME_API uint32 GetHeight() const;

		RUNTIME_API void GetWindowPosition(int& X, int& Y);
		RUNTIME_API void SetWindowPosition(int X, int Y);

		RUNTIME_API void SetWindowSize(int X, int Y);
		
		RUNTIME_API void SetTitleBarHovered(bool bHovered) { bTitleBarHovered = bHovered; }

		RUNTIME_API bool ShouldClose() const;
		RUNTIME_API bool IsWindowMinimized() const;
		RUNTIME_API bool IsWindowMaximized() const;
		RUNTIME_API void Minimize();
		RUNTIME_API void Restore();
		RUNTIME_API void Maximize();
		RUNTIME_API void Close();


		static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
		static void MousePosCallback(GLFWwindow* window, double xpos, double ypos);
		static void MouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
		static void KeyCallback(GLFWwindow* window, int Key, int Scancode, int Action, int Mods);

		static void TitleBarHitTestCallback(GLFWwindow* window, int x, int y, int* hit);
		static void WindowResizeCallback(GLFWwindow* window, int width, int height);
		static void WindowDropCallback(GLFWwindow* Window, int PathCount, const char* Paths[]);
		static void WindowCloseCallback(GLFWwindow* window);

		RUNTIME_API static FWindowResizeDelegate OnWindowResized;

	private:

		bool bFirstMouseUpdate = true;
		double LastMouseX, LastMouseY;

		bool bInitialized = false;
		bool bTitleBarHovered = false;
		GLFWwindow* Window = nullptr;
		FWindowSpecs Specs;
	};

	namespace Windowing
	{
		RUNTIME_API extern FWindow* PrimaryWindow;
		RUNTIME_API FWindow* GetPrimaryWindowHandle();
		void SetPrimaryWindowHandle(FWindow* InWindow);
	}

}
