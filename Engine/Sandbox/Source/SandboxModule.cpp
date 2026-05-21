#include "SandboxModule.h"

#include "Core/Module/ModuleManager.h"
#include "Log/Log.h"
#include "Tools/UI/ImGui/ImGuiAllocator.h"

using namespace Lumina;

IMPLEMENT_MODULE(FSandboxModule, "SandboxModule");

void FSandboxModule::StartupModule()
{
    LOG_INFO("Sandbox Startup Module");

    // Route the Sandbox's own ImGui copy through our allocator (see ImGuiAllocator.h).
    ImGuiX::InstallImGuiAllocator();
}

void FSandboxModule::ShutdownModule()
{
    LOG_INFO("Sandbox Shutdown Module");

}
