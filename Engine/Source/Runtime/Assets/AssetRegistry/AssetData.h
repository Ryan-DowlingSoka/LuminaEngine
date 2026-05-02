#pragma once

#include "Containers/Name.h"
#include "GUID/GUID.h"

namespace Lumina
{
    struct FAssetData
    {
        FGuid AssetGUID;
        FFixedString Path;
        FName AssetName;
        FName AssetClass;
    };
}
