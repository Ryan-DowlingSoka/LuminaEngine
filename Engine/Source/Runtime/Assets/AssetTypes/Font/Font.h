#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/Object.h"
#include "Font.generated.h"

namespace Lumina
{
    // Self-contained font asset; the TTF/OTF bytes are embedded so cooked builds need no source file.
    // Consumers (ImGui / RmlUi) load the face from GetFontData().
    REFLECT()
    class RUNTIME_API CFont : public CObject
    {
        GENERATED_BODY()

    public:

        void Serialize(FArchive& Ar) override;
        bool IsAsset() const override { return true; }

        const TVector<uint8>& GetFontData() const { return FontData; }
        bool IsValid() const { return !FontData.empty(); }

        // Source file the face was imported from; empty for in-place created assets.
        PROPERTY()
        FString SourcePath;

        PROPERTY()
        FString FamilyName;

        PROPERTY()
        FString StyleName;

        PROPERTY()
        int32 NumGlyphs = 0;

        PROPERTY()
        bool bIsScalable = false;

        PROPERTY()
        bool bHasKerning = false;

        // Raw face bytes; serialized manually (not a reflected property).
        TVector<uint8> FontData;
    };
}
