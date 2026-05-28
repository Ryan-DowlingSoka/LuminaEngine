#pragma once

#include "Core/Math/Math.h"
#include "Containers/String.h"
#include "Core/Object/ObjectMacros.h"
#include "Renderer/RenderResource.h"
#include "WidgetComponent.generated.h"

namespace Rml
{
    class Context;
    class ElementDocument;
}

namespace Lumina
{
    // Per-instance live state for a world widget: its own Rml context laid out into a
    // private offscreen RT. Not reflected/serialized -- rebuilt on demand by the RmlUi
    // bridge. Copies (entity duplication) start EMPTY so the duplicate doesn't alias the
    // source's context/RT; moves transfer ownership (entt swap-and-pop relocation).
    struct FWidgetRuntime
    {
        Rml::Context*         Context = nullptr;
        Rml::ElementDocument* Document = nullptr;
        FRHIImageRef          Target;
        FUIntVector2            BuiltSize{0, 0};
        FString               LoadedPath;
        int64                 DocWriteTime = 0;   // last-seen .rml mtime, for hot-reload
        int32                 ResourceID   = -1;  // bindless id of Target, read by the gather
        bool                  bVisible     = true;// set by the render gather (frustum); gates Update/rasterize
        bool                  bRmlIdle     = false;// last Update reported no pending animation/transition (GetNextUpdateDelay infinite)

        FWidgetRuntime() = default;
        ~FWidgetRuntime() = default;

        FWidgetRuntime(const FWidgetRuntime&) {}
        FWidgetRuntime& operator=(const FWidgetRuntime&) { return *this; }
        FWidgetRuntime(FWidgetRuntime&&) noexcept = default;
        FWidgetRuntime& operator=(FWidgetRuntime&&) noexcept = default;
    };

    // Renders an RmlUi document onto a quad in world space (like Unreal's world-space
    // UWidgetComponent). The document is laid out and rasterized into Runtime.Target each
    // frame (RmlUi bridge); the render scene's widget pass draws a textured quad that
    // samples that RT bindlessly.
    REFLECT(Component, Category = "UI")
    struct RUNTIME_API SWidgetComponent
    {
        GENERATED_BODY()

        /** RML document to display, e.g. "/Game/UI/MyWidget.rml". Empty = nothing drawn. */
        PROPERTY(Editable, Category = "Widget")
        FString DocumentPath;

        /** Offscreen render-target resolution the document is laid out at (pixels). */
        PROPERTY(Editable, Category = "Widget")
        int32 DrawWidth = 512;

        PROPERTY(Editable, Category = "Widget")
        int32 DrawHeight = 512;

        /** Physical quad size in world units. */
        PROPERTY(Editable, Category = "Widget")
        FVector2 WorldSize = FVector2(1.0f, 1.0f);

        /** When true the quad always faces the camera; otherwise it uses the entity's orientation. */
        PROPERTY(Editable, Category = "Widget")
        bool bBillboard = true;

        /** RGBA tint multiplied with the widget color. */
        PROPERTY(Editable, Color, Category = "Widget")
        FVector4 Tint = FVector4(1.0f);

        FWidgetRuntime Runtime;
    };
}
