#include "pch.h"
#include "AudioContext.h"

#include "AudioGlobals.h"
#include "Miniaudio/MiniaudioContext.h"


namespace Lumina
{
	void Audio::Initialize()
	{
		GAudioContext = new FMiniaudioContext{};
	}

	void Audio::Shutdown()
	{
		delete GAudioContext;
		GAudioContext = nullptr;
	}

	void Audio::Update()
	{
		if (GAudioContext != nullptr)
		{
			GAudioContext->Update();
		}
	}
}
