#pragma once

#include "ModuleAPI.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Networking/NetworkTypes.h"
#include "Platform/GenericPlatform.h"

namespace Lumina::Lua
{
    // A single key/value (or bare flag) inside an annotation's parens, e.g. ClampMin=0, Category="Move",
    // or a bare RPC specifier like `multicast` / `reliable` (stored with an empty Value).
    struct FScriptAnnotationArg
    {
        FName   Key;
        FString Value;
    };

    // The annotations attached to one script-table member (Script.<Member> / function Script:<Member>).
    // Comment annotations (--@export / --@replicated / --@rpc) bind to the declaration directly below them.
    // Annotations only apply to table members; locals are ignored (not addressable cross-peer / by the editor).
    struct FScriptMemberAnnotation
    {
        FName Member;
        bool  bIsFunction = false;

        bool  bExport     = false;
        bool  bReplicated = false;
        bool  bRpc        = false;

        // PROPERTY-consistent metadata for --@export (ClampMin/ClampMax/Delta/Units/Category/Tooltip/...).
        TVector<FScriptAnnotationArg> ExportMeta;

        // Unreal-style --@rpc specifiers as bare flags: server|client|multicast, reliable|unreliable.
        TVector<FScriptAnnotationArg> RpcArgs;
    };

    // Scan Lua/Luau source text for --@ annotations and return one entry per annotated table member,
    // in source order (so derived ids/rep-indices are stable across peers). Pure text pass; no Lua state.
    RUNTIME_API TVector<FScriptMemberAnnotation> ScanScriptAnnotations(FStringView Source);

    // One networked script function, derived from a --@rpc annotation. Index in the registry is the
    // stable wire id (same source order on every peer). ERpcMode is a core net type (NetworkTypes.h).
    struct FScriptRpc
    {
        FName    Name;
        ERpcMode Mode      = ERpcMode::Multicast;
        bool     bReliable = true;
    };

    // Scan + collect only the --@rpc functions, in source order, into a wire-stable registry.
    RUNTIME_API TVector<FScriptRpc> BuildRpcRegistry(FStringView Source);

    // As above, from an already-scanned annotation list (avoids re-scanning the source).
    RUNTIME_API TVector<FScriptRpc> BuildRpcRegistry(const TVector<FScriptMemberAnnotation>& Annotations);
}
