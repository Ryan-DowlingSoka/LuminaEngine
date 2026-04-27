#pragma once

// Cross-DLL plumbing for entt's meta system.
//
// entt::locator<entt::meta_ctx> is a per-DLL singleton. Every Lumina DLL that
// links Runtime gets its own locator instance by default, which means
// component / system / event meta types registered in Runtime are invisible
// to Editor and to game DLLs unless those modules explicitly seed their
// locator from Runtime's.
//
// This header exists separately from Engine.h so the heavy <entt/entt.hpp>
// include doesn't bleed into the ~22 sites that include Engine.h. Only code
// that actually does meta registration or DLL meta hand-off includes this
// file.
//
// Usage:
//   - At runtime registration (Component.h / ECSEvent.h / EntitySystem.h):
//       entt::meta_factory<T>(Lumina::GetEngineMetaContext())...
//   - At DLL initialization (Editor/Game module entry):
//       entt::locator<entt::meta_ctx>::reset(Lumina::GetEngineMetaService());
//     This aliases the new DLL's locator to the runtime's handle, so all
//     subsequent meta_factory calls write into the same shared registry.

#include <entt/entt.hpp>

#include "Platform/GenericPlatform.h"

namespace Lumina
{
    // Returns the meta context the runtime uses for component/system/event
    // registration. Always lives in the Runtime DLL.
    RUNTIME_API entt::meta_ctx& GetEngineMetaContext();

    // Returns the locator handle the runtime uses to publish its meta_ctx.
    // Editor / game DLLs pass this to entt::locator<>::reset() so their
    // meta_factory<> calls land in the runtime's registry.
    RUNTIME_API entt::locator<entt::meta_ctx>::node_type GetEngineMetaService();
}
