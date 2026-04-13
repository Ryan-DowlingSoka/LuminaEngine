#pragma once
#include "Core/Module/ModuleInterface.h"

namespace Lumina
{
    class $PROJECTNAMEUPPER_API F$PROJECTNAMEModule : public IModuleInterface
    {
    public:
        
        void StartupModule() override;
        void ShutdownModule() override;
        
    };
}
