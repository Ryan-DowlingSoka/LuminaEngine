#pragma once

#include "WindowTypes.h"
#include "Core/Delegates/Delegate.h"
#include "Memory/SmartPtr.h"

// Opaque GLFW handle; full glfw3.h is included only in TUs that need it (via GLFWInclude.h).
struct GLFWwindow;

namespace Lumina
{
	class FWindow;
	struct FWindowImpl;

	DECLARE_MULTICAST_DELEGATE(FWindowResizeDelegate, FWindow*, const FUIntVector2&);

	class FWindow
	{
	public:

		FWindow(const FWindowSpecs& InSpecs);
		virtual ~FWindow();
		LE_NO_COPYMOVE(FWindow);

		void Init();
		void ProcessMessages();

		// Native handle for code that genuinely needs it (Vulkan surface, ImGui GLFW backend).
		GLFWwindow* GetWindow() const;

		RUNTIME_API FUIntVector2 GetExtent() const;
		RUNTIME_API uint32 GetWidth() const;
		RUNTIME_API uint32 GetHeight() const;

		RUNTIME_API void GetWindowPosition(int& X, int& Y);
		RUNTIME_API void SetWindowPosition(int X, int Y);

		RUNTIME_API void SetWindowSize(int X, int Y);

		RUNTIME_API void SetTitleBarHovered(bool bHovered);
		RUNTIME_API void SetCursorMode(ECursorMode Mode);

		RUNTIME_API bool ShouldClose() const;
		RUNTIME_API bool IsWindowMinimized() const;
		RUNTIME_API bool IsWindowMaximized() const;
		RUNTIME_API void Minimize();
		RUNTIME_API void Restore();
		RUNTIME_API void Maximize();
		RUNTIME_API void Close();

		// Inverse of the close flag: usually set when the OS asks the window
		// to close (X button, Alt+F4) and we want to keep the editor alive —
		// e.g. user cancels the dirty-packages prompt.
		RUNTIME_API void CancelClose();

		RUNTIME_API static FWindowResizeDelegate OnWindowResized;

	private:

		TUniquePtr<FWindowImpl> Impl;
	};

	namespace Windowing
	{
		RUNTIME_API extern FWindow* PrimaryWindow;
		RUNTIME_API FWindow* GetPrimaryWindowHandle();
		void SetPrimaryWindowHandle(FWindow* InWindow);
	}

}
