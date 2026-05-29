#include "$EDITORMODULEModule.h"

#include "Core/Module/ModuleManager.h"
#include "Log/Log.h"

using namespace Lumina;

IMPLEMENT_MODULE(F$EDITORMODULEModule, "$EDITORMODULE");

void F$EDITORMODULEModule::StartupModule()
{
    LOG_INFO("[$PLUGINNAME] Editor module ready");

    // Customize the editor here. For example, register an editor tool for one
    // of your asset types via the editor tool registry:
    //
    //   #include "UI/Tools/EditorToolRegistry.h"
    //   FEditorToolRegistry::Get().RegisterAssetEditor<CMyAsset, FMyAssetEditorTool>();
}

void F$EDITORMODULEModule::ShutdownModule()
{
    LOG_INFO("[$PLUGINNAME] Editor module shutdown");
}
