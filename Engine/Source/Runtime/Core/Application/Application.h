#pragma once

#include "Core/LuminaMacros.h"
#include "Core/Engine/Engine.h"
#include "Events/EventProcessor.h"
#include "Prism/PrismApplication.h"

namespace Lumina
{
	struct FWindowSpecs;

	enum class RUNTIME_API EApplicationFlags : uint32
	{
		DevelopmentTools =		1 << 0,
	};

	constexpr EApplicationFlags operator|(EApplicationFlags lhs, EApplicationFlags rhs)
	{
		return static_cast<EApplicationFlags>(static_cast<uint32>(lhs) | static_cast<uint32>(rhs));
	}

	constexpr EApplicationFlags operator&(EApplicationFlags lhs, EApplicationFlags rhs)
	{
		return static_cast<EApplicationFlags>(static_cast<uint32>(lhs) & static_cast<uint32>(rhs));
	}
	
	class RUNTIME_API FApplication
	{
	public:

		FApplication() = default;
		~FApplication() = default;
		LE_NO_COPYMOVE(FApplication);
		
		int32 Run(int argc, char** argv);

		void Shutdown();
		
		void WindowResized(FWindow* Window, const glm::uvec2& Extent);

		static void RequestExit();

		FEventProcessor& GetEventProcessor()	{ return EventProcessor; }
		Prism::FPrismApplication& GetPrismApp() { return PrismApplication; }
	
	private:

		void PreInitStartup();
		bool CreateApplicationWindow();
		
		bool ShouldExit() const;
		
	protected:

		Prism::FPrismApplication	PrismApplication;
		FEventProcessor				EventProcessor;
		FWindow*					MainWindow = nullptr;
		
		bool bExitRequested			= false;
	
	public:

	};
	
	RUNTIME_API extern FApplication* GApp;


	/* Implemented by client */
	static FApplication* CreateApplication(int argc, char** argv);
}
