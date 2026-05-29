#pragma once

#include "$RUNTIMEMODULEAPI.h"
#include "Core/Module/ModuleInterface.h"

namespace Lumina
{
    class $RUNTIMEMODULEUPPER_API F$RUNTIMEMODULEModule : public IModuleInterface
    {
    public:
        void StartupModule()  override;
        void ShutdownModule() override;
    };
}
