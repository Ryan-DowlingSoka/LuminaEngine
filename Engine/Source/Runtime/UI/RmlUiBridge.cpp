#include "pch.h"
#include "RmlUiBridge.h"

#include "RmlUiFileInterface.h"
#include "RmlUiRenderer.h"
#include "WorldUIContext.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Debugger.h>

#include "FileSystem/FileSystem.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Log/Log.h"
#include "Memory/Memory.h"
#include "Memory/MemoryTracking.h"
#include "Memory/SmartPtr.h"
#include "Core/Threading/Thread.h"
#include "Renderer/Format.h"
#include "Renderer/RHI.h"
#include "Renderer/RHITexture.h"
#include <filesystem>
#include "World/World.h"
#include "World/Scene/RenderScene/RenderScene.h"
#include "World/Entity/Components/WidgetComponent.h"

extern "C" void* LuminaRmlFreeTypeAlloc(size_t Size)
{
    LUMINA_MEMORY_SCOPE("RmlUi");
    return ::Lumina::Memory::Malloc(Size);
}

extern "C" void* LuminaRmlFreeTypeRealloc(void* Block, size_t NewSize)
{
    LUMINA_MEMORY_SCOPE("RmlUi");
    return ::Lumina::Memory::Realloc(Block, NewSize);
}

extern "C" void LuminaRmlFreeTypeFree(void* Block)
{
    ::Lumina::Memory::Free(Block);
}

namespace Lumina::RmlUi
{
    static TConsoleVar<int32> CVarWidgetMaxRendersPerFrame("UI.Widget.MaxRendersPerFrame", 8,
        "Max world-space widget RT rasterizations per frame; the rest reuse last frame's RT.");

    static TConsoleVar<int32> CVarWidgetDormancyFrames("UI.Widget.DormancyFrames", 4,
        "Frames of unchanged output before a world-space widget stops ticking; 0 = always tick.");

    namespace
    {
        class FLuminaSystemInterface final : public Rml::SystemInterface
        {
        public:
            FLuminaSystemInterface() : StartTime(std::chrono::steady_clock::now()) {}

            double GetElapsedTime() override
            {
                using namespace std::chrono;
                return duration<double>(steady_clock::now() - StartTime).count();
            }

            bool LogMessage(Rml::Log::Type Type, const Rml::String& Message) override
            {
                switch (Type)
                {
                case Rml::Log::LT_ERROR:
                case Rml::Log::LT_ASSERT:
                    LOG_ERROR("[RmlUi] {}", Message.c_str());
                    break;
                case Rml::Log::LT_WARNING:
                    LOG_WARN("[RmlUi] {}", Message.c_str());
                    break;
                case Rml::Log::LT_INFO:
                    LOG_INFO("[RmlUi] {}", Message.c_str());
                    break;
                case Rml::Log::LT_DEBUG:
                case Rml::Log::LT_ALWAYS:
                case Rml::Log::LT_MAX:
                default:
                    LOG_TRACE("[RmlUi] {}", Message.c_str());
                    break;
                }
                return true;
            }

            // Absolute virtual paths must pass through verbatim; base impl strips leading '/'.
            void JoinPath(Rml::String& OutPath, const Rml::String& DocumentPath, const Rml::String& Path) override
            {
                // Absolute virtual path -> use as-is.
                if (!Path.empty() && Path[0] == '/')
                {
                    OutPath = Path;
                    return;
                }

                // Scheme-prefixed source (e.g. "material:/Game/..") -- a ':' before
                // any '/'. Pass through so custom URI schemes survive path joining.
                const size_t Colon = Path.find(':');
                const size_t Slash = Path.find('/');
                if (Colon != Rml::String::npos && (Slash == Rml::String::npos || Colon < Slash))
                {
                    OutPath = Path;
                    return;
                }

                // Relative path -> resolve against the document's directory,
                // preserving its leading '/'.
                OutPath = DocumentPath;
                const size_t LastSlash = OutPath.rfind('/');
                if (LastSlash != Rml::String::npos)
                {
                    OutPath.resize(LastSlash + 1);
                }
                else
                {
                    OutPath.clear();
                }
                OutPath += Path;
            }

        private:
            std::chrono::steady_clock::time_point StartTime;
        };

        // Editor preview context not bound to any world; tool owns the target image.
        struct FEditorEntry
        {
            Rml::Context*         Context = nullptr;
            RHI::FTextureH        Target = {};
            FUIntVector2            Size{0, 0};
            Rml::ElementDocument* Document = nullptr;
            float                 DpiScale = 1.0f;
            FVector4             ClearColor{0.10f, 0.10f, 0.12f, 1.0f};
        };

        struct FState
        {
            TUniquePtr<FLuminaSystemInterface>  System;
            TUniquePtr<FRmlUiFileInterface>     Files;
            TUniquePtr<FRmlUiRenderer>          Renderer;
            TVector<TUniquePtr<FEditorEntry>>   EditorContexts;

            // Editor hot-reload: OnContentFileModified raises this on any .rml/.rcss save; next TickWorldUI restyles.
            TAtomic<bool>                       bUIReloadPending{false};

            CWorld*                             ActiveWorld = nullptr;

            Rml::Context*                       DebuggerHost = nullptr;
            bool                                bDebuggerVisible = false;
            bool                                bInitialized = false;

            uint32                              WidgetRenderCursor = 0;

            // Recursive: Update may fire event callbacks that re-enter the bridge on the same thread.
            FRecursiveMutex                     StateMutex;
        };

        FState& S()
        {
            static FState State;
            return State;
        }

        FWorldUIContext* ActiveUI()
        {
            CWorld* W = S().ActiveWorld;
            return W ? W->GetUIContext() : nullptr;
        }

        Rml::Context* ActiveContext()
        {
            FWorldUIContext* UI = ActiveUI();
            return UI ? UI->Context : nullptr;
        }

        void SyncDebuggerToActiveContext()
        {
            FState& State = S();
            Rml::Context* Active = ActiveContext();
            
            Rml::Context* DesiredHost = State.bDebuggerVisible ? Active : nullptr;

            if (State.DebuggerHost != DesiredHost)
            {
                if (State.DebuggerHost != nullptr)
                {
                    Rml::Debugger::Shutdown();
                    State.DebuggerHost = nullptr;
                }
                if (DesiredHost != nullptr)
                {
                    if (Rml::Debugger::Initialise(DesiredHost))
                    {
                        State.DebuggerHost = DesiredHost;
                    }
                    else
                    {
                        LOG_WARN("[RmlUi] Debugger failed to attach to context '{}'.", DesiredHost->GetName());
                    }
                }
            }

            Rml::Debugger::SetVisible(State.bDebuggerVisible);
        }

        struct FWorldTarget { RHI::FTextureH Image = {}; FUIntVector2 Size{0, 0}; };
        FWorldTarget GetWorldTarget(const CWorld* World)
        {
            if (World == nullptr)
            {
                return {};
            }
            IRenderScene* Scene = World->GetRenderer();
            if (Scene == nullptr)
            {
                return {};
            }
            const RHI::FTextureH Img = Scene->GetDisplayTexture();
            if (!RHI::IsValid(Img))
            {
                return {};
            }
            const RHI::FTextureDesc Desc = RHI::GetTextureDesc(Img);
            return { Img, FUIntVector2(Desc.Dimension.x, Desc.Dimension.y) };
        }

        int64 GetDocumentWriteTime(const FString& VirtualPath)
        {
            if (VirtualPath.empty())
            {
                return 0;
            }
            const FFixedString Physical = VFS::ResolvePath(VirtualPath);
            if (Physical.empty())
            {
                return 0;
            }
            std::error_code Ec;
            const auto Time = std::filesystem::last_write_time(Physical.c_str(), Ec);
            return Ec ? 0 : (int64)Time.time_since_epoch().count();
        }

        void DestroyWidgetRuntime(FWidgetRuntime& E)
        {
            if (E.Context != nullptr)
            {
                if (S().bInitialized)
                {
                    Rml::RemoveContext(E.Context->GetName());
                }
                E.Context  = nullptr;
                E.Document = nullptr;
            }
            // Drop the renderer's cached batch for this RT before the RT itself goes away.
            if (E.Target.IsValid() && S().Renderer != nullptr)
            {
                S().Renderer->ReleaseTargetBatch(E.Target.Texture);
            }
            if (E.Target.IsValid())
            {
                RHI::Textures::Release(E.Target);
            }
            E.ResourceID = -1;
            E.BuiltSize  = FUIntVector2(0, 0);
            E.LoadedPath.clear();
        }

        void EnsureWidgetResources(FWidgetRuntime& E, CWorld* World, entt::entity Entity, uint32 Width, uint32 Height)
        {
            DestroyWidgetRuntime(E);

            char NameBuf[80];
            std::snprintf(NameBuf, sizeof(NameBuf), "widget_%p_%u",
                static_cast<void*>(World), entt::to_integral(Entity));

            E.Context = Rml::CreateContext(NameBuf, Rml::Vector2i(int(Width), int(Height)));
            if (E.Context == nullptr)
            {
                LOG_ERROR("[RmlUi] CreateContext failed for widget {}.", NameBuf);
                return;
            }

            E.Target = RHI::Textures::Create(RHI::FTexture2DDesc
            {
                .Width  = Width,
                .Height = Height,
                .Format = EFormat::RGBA8_UNORM,
                .bRenderTarget = true,
            });
            E.ResourceID = E.Target.IsValid() ? (int32)E.Target.SampledSlot : -1;
            E.BuiltSize  = FUIntVector2(Width, Height);
        }

        void ProcessPendingUIReload()
        {
            FState& State = S();
            if (!State.bUIReloadPending.exchange(false, Atomic::MemoryOrderAcquire))
            {
                return;
            }

            Rml::Factory::ClearStyleSheetCache();
            Rml::Factory::ClearTemplateCache();

            const int NumContexts = Rml::GetNumContexts();
            for (int i = 0; i < NumContexts; ++i)
            {
                Rml::Context* Ctx = Rml::GetContext(i);
                if (Ctx == nullptr)
                {
                    continue;
                }
                const int NumDocs = Ctx->GetNumDocuments();
                for (int d = 0; d < NumDocs; ++d)
                {
                    if (Rml::ElementDocument* Doc = Ctx->GetDocument(d))
                    {
                        Doc->ReloadStyleSheet();
                    }
                }
            }
            LOG_INFO("[RmlUi] UI hot-reload: restyled all documents across {} context(s).", NumContexts);
        }

        void OnContentFileModified(FStringView VirtualPath)
        {
            auto EndsWith = [&](FStringView Suffix)
            {
                return VirtualPath.size() >= Suffix.size()
                    && VirtualPath.substr(VirtualPath.size() - Suffix.size()) == Suffix;
            };
            if (EndsWith(FStringView(".rml")) || EndsWith(FStringView(".rcss")))
            {
                S().bUIReloadPending.store(true, Atomic::MemoryOrderRelease);
            }
        }
    }

    bool Initialize()
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (State.bInitialized) return true;

        State.System   = MakeUnique<FLuminaSystemInterface>();
        State.Files    = MakeUnique<FRmlUiFileInterface>();
        State.Renderer = MakeUnique<FRmlUiRenderer>();

        Rml::SetSystemInterface(State.System.get());
        Rml::SetFileInterface(State.Files.get());
        Rml::SetRenderInterface(State.Renderer.get());

        auto ResetOnFailure = [&]()
        {
            State.System.reset();
            State.Files.reset();
            State.Renderer.reset();
            State.EditorContexts.clear();
            State.ActiveWorld = nullptr;
            State.DebuggerHost = nullptr;
            State.bDebuggerVisible = false;
            State.bInitialized = false;
        };

        if (!Rml::Initialise())
        {
            LOG_ERROR("[RmlUi] Rml::Initialize failed.");
            Rml::SetRenderInterface(nullptr);
            Rml::SetFileInterface(nullptr);
            Rml::SetSystemInterface(nullptr);
            ResetOnFailure();
            return false;
        }

        if (!State.Renderer->Initialize())
        {
            LOG_ERROR("[RmlUi] FRmlUiRenderer initialisation failed; tearing down.");
            Rml::Shutdown();
            Rml::SetRenderInterface(nullptr);
            Rml::SetFileInterface(nullptr);
            Rml::SetSystemInterface(nullptr);
            ResetOnFailure();
            return false;
        }

        if (!Rml::LoadFontFace("/Engine/Resources/UI/Fonts/LatoLatin-Regular.ttf", true /*fallback_face*/))
        {
            LOG_WARN("[RmlUi] Default font LatoLatin-Regular.ttf failed to load; text may not render.");
        }

        // Monospace face for digit-heavy HUDs (speedometer, RPM, etc.). Optional;
        // RmlUi will fall back to LatoLatin if the file is missing.
        Rml::LoadFontFace("/Engine/Resources/Fonts/JetbrainsMono/JetBrainsMono-ExtraBold.ttf", false);
        Rml::LoadFontFace("/Engine/Resources/Fonts/JetbrainsMono/JetBrainsMono-Bold.ttf", false);

        State.bInitialized = true;

        (void)FCoreDelegates::OnContentFileModified.AddStatic(&OnContentFileModified);

        LOG_INFO("[RmlUi] Initialized.");
        return true;
    }

    void Shutdown()
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized && !State.System)
        {
            return;
        }

        if (State.DebuggerHost != nullptr)
        {
            Rml::Debugger::Shutdown();
            State.DebuggerHost = nullptr;
        }
        
        for (auto& E : State.EditorContexts)
        {
            if (E->Context != nullptr)
            {
                Rml::RemoveContext(E->Context->GetName());
                E->Context = nullptr;
            }
        }

        // Widget contexts (owned by SWidgetComponent.Runtime) are torn down with their worlds
        // via the on_destroy hook; Rml::Shutdown drops any that outlive their world here.
        Rml::Shutdown();

        State.EditorContexts.clear();
        State.ActiveWorld = nullptr;
        State.bInitialized = false;

        if (State.Renderer) State.Renderer->Shutdown();

        Rml::SetRenderInterface(nullptr);
        Rml::SetFileInterface(nullptr);
        Rml::SetSystemInterface(nullptr);

        // Clear fields individually -- StateMutex is non-movable.
        State.System.reset();
        State.Files.reset();
        State.Renderer.reset();
        State.bDebuggerVisible = false;
    }

    TUniquePtr<FWorldUIContext> CreateWorldUI(CWorld* World)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);

        TUniquePtr<FWorldUIContext> UI = MakeUnique<FWorldUIContext>();
        if (!State.bInitialized || World == nullptr)
        {
            return UI;
        }

        // TickWorldUI resizes from the real RT each frame; initial size is a placeholder.
        const FWorldTarget Tgt = GetWorldTarget(World);
        const Rml::Vector2i InitialSize = (Tgt.Size.x > 0 && Tgt.Size.y > 0)
            ? Rml::Vector2i(int(Tgt.Size.x), int(Tgt.Size.y))
            : Rml::Vector2i(1280, 720);

        char NameBuf[64];
        std::snprintf(NameBuf, sizeof(NameBuf), "world_%p", static_cast<void*>(World));

        Rml::Context* Ctx = Rml::CreateContext(NameBuf, InitialSize);
        if (Ctx == nullptr)
        {
            LOG_ERROR("[RmlUi] CreateContext failed for world {}.", static_cast<void*>(World));
            return UI;
        }

        UI->Context = Ctx;

        // Newest world becomes the active UI target.
        State.ActiveWorld = World;
        SyncDebuggerToActiveContext();

        LOG_INFO("[RmlUi] Created context '{}' for world {} (initial {}x{}).", NameBuf, static_cast<void*>(World), InitialSize.x, InitialSize.y);
        return UI;
    }

    void DestroyWorldUI(CWorld* World)
    {
        if (World == nullptr)
        {
            return;
        }
        FWorldUIContext* UI = World->GetUIContext();
        if (UI == nullptr)
        {
            return;
        }

        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);

        // Backend already shut down (Rml::Shutdown destroyed every context): the pointer
        // is dangling, so just drop it without touching Rml.
        if (!State.bInitialized)
        {
            UI->Context = nullptr;
            UI->Documents.clear();
            if (State.ActiveWorld == World)
            {
                State.ActiveWorld = nullptr;
            }
            return;
        }

        if (UI->Context != nullptr)
        {
            // Detach debugger before removing host; otherwise it'd walk a freed element tree.
            if (State.DebuggerHost == UI->Context)
            {
                Rml::Debugger::Shutdown();
                State.DebuggerHost = nullptr;
                State.bDebuggerVisible = false;
            }
            // RmlUi tears down first (calls listener OnDetach), then we drop wrappers.
            Rml::RemoveContext(UI->Context->GetName());
            UI->Context = nullptr;
        }
        UI->Documents.clear();

        if (State.ActiveWorld == World)
        {
            State.ActiveWorld = nullptr;
            SyncDebuggerToActiveContext();
        }
    }

    void SetActiveWorld(CWorld* World)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized || World == nullptr || World->GetUIContext() == nullptr)
        {
            return;
        }
        State.ActiveWorld = World;
        SyncDebuggerToActiveContext();
    }

    void TickWorldUI(CWorld* World)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized || World == nullptr)
        {
            return;
        }

        // Once per frame (flag self-clears): restyle all docs if a UI file changed on disk.
        ProcessPendingUIReload();

        FWorldUIContext* UI = World->GetUIContext();
        if (UI == nullptr || UI->Context == nullptr)
        {
            return;
        }

        const FWorldTarget Tgt = GetWorldTarget(World);
        if (RHI::IsValid(Tgt.Image))
        {
            // DisplaySize override (set by editor each frame) lets the UI lay out at the
            // panel's aspect instead of the RT's. Falls back to RT size in standalone.
            const FUIntVector2 LayoutSize = (UI->DisplaySize.x > 0 && UI->DisplaySize.y > 0) ? UI->DisplaySize : Tgt.Size;

            constexpr float NominalHeight = 1080.0f;
            UI->Context->SetDimensions(Rml::Vector2i(int(LayoutSize.x), int(LayoutSize.y)));

            const float DpRatio = std::max(1.0f, float(LayoutSize.y) / NominalHeight);
            UI->Context->SetDensityIndependentPixelRatio(DpRatio);
        }
        UI->Context->Update();
    }

    void RenderWorldUI(const CWorld* World, RHI::FCmdListH CmdList)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized || State.Renderer == nullptr || World == nullptr)
        {
            return;
        }
        FWorldUIContext* UI = World->GetUIContext();
        if (UI == nullptr || UI->Context == nullptr)
        {
            return;
        }

        const FWorldTarget Tgt = GetWorldTarget(World);
        if (!RHI::IsValid(Tgt.Image))
        {
            return;
        }

        const FUIntVector2 LayoutSize = (UI->DisplaySize.x > 0 && UI->DisplaySize.y > 0) ? UI->DisplaySize : Tgt.Size;
        State.Renderer->BeginFrame(CmdList, Tgt.Image, Tgt.Size, LayoutSize);
        UI->Context->Render();
        State.Renderer->EndFrame();
    }

    void TickWorldWidgets(CWorld* World)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized || World == nullptr)
        {
            return;
        }
        FWorldUIContext* UI = World->GetUIContext();
        if (UI == nullptr)
        {
            return;
        }

        UI->WidgetJobs.clear();

        FEntityRegistry& Registry = World->GetEntityRegistry();
        Registry.view<SWidgetComponent>().each([&](entt::entity Entity, SWidgetComponent& Comp)
        {
            FWidgetRuntime& R = Comp.Runtime;

            if (!R.bVisible && R.Context != nullptr)
            {
                return;
            }

            const uint32 Width  = (uint32)Math::Max(1, Comp.DrawWidth);
            const uint32 Height = (uint32)Math::Max(1, Comp.DrawHeight);

            if (R.Context == nullptr || R.BuiltSize != FUIntVector2(Width, Height))
            {
                EnsureWidgetResources(R, World, Entity, Width, Height);
            }
            if (R.Context == nullptr)
            {
                return;
            }

            // Resolve the rename-safe ref to its current virtual path (GUID-first).
            const FStringView DocView = Comp.DocumentPath.ResolvePath();
            const FString     DocPath(DocView.data(), DocView.size());

            const int64 CurrentWriteTime = GetDocumentWriteTime(DocPath);
            const bool  bPathChanged     = (R.LoadedPath != DocPath);
            const bool  bFileChanged     = (R.Document != nullptr && CurrentWriteTime != 0 && CurrentWriteTime != R.DocWriteTime);

            if (bPathChanged || bFileChanged)
            {
                // On-disk edit: drop RmlUi's cached stylesheets/templates so the re-parse picks
                // up .rcss changes too, not just the .rml body.
                if (bFileChanged)
                {
                    Rml::Factory::ClearStyleSheetCache();
                    Rml::Factory::ClearTemplateCache();
                }

                R.Context->UnloadAllDocuments();
                R.Document   = nullptr;
                R.LoadedPath = DocPath;
                if (!DocPath.empty())
                {
                    R.Document = R.Context->LoadDocument(Rml::String(DocPath.c_str()));
                    if (R.Document != nullptr)
                    {
                        R.Document->SetProperty("width", "100%");
                        R.Document->SetProperty("height", "100%");
                        R.Document->Show();
                    }
                    else
                    {
                        LOG_WARN("[RmlUi] Widget failed to load document '{}'.", DocPath.c_str());
                    }
                }
                R.DocWriteTime = CurrentWriteTime;
            }
            
            const int32 DormancyFrames = CVarWidgetDormancyFrames.GetValue();
            if (DormancyFrames > 0 && R.bRmlIdle && !bPathChanged && !bFileChanged && R.Target.IsValid() && State.Renderer != nullptr)
            {
                if (State.Renderer->GetTargetStableFrames(R.Target.Texture) >= (uint32)DormancyFrames)
                {
                    return;   // settled: no Update, no job; keep last RT
                }
            }

            R.Context->SetDimensions(Rml::Vector2i(int(Width), int(Height)));
            R.Context->SetDensityIndependentPixelRatio(std::max(0.1f, float(Height) / 1080.0f));
            R.Context->Update();

            // RmlUi sets the next-update timeout to infinity at the start of Update and lowers it when
            // an animation/transition (incl. delayed) is pending. Huge => idle => dormancy-eligible.
            R.bRmlIdle = (R.Context->GetNextUpdateDelay() > 1.0e6);

            // Queue for the render thread (R.ResourceID is read by the scene gather directly).
            if (R.Document != nullptr && R.Target.IsValid())
            {
                UI->WidgetJobs.push_back(FWidgetRenderJob{ R.Context, R.Target.Texture, R.BuiltSize });
            }
        });
    }

    void RenderWorldWidgets(const CWorld* World, RHI::FCmdListH CmdList)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized || State.Renderer == nullptr || World == nullptr)
        {
            return;
        }
        const FWorldUIContext* UI = World->GetUIContext();
        if (UI == nullptr)
        {
            return;
        }

        const size_t JobCount = UI->WidgetJobs.size();
        if (JobCount == 0)
        {
            return;
        }

        const int32 Budget = Math::Max(0, CVarWidgetMaxRendersPerFrame.GetValue());
        int32 Rendered = 0;

        // Rotate the start each frame so the budget doesn't always favor the first widgets.
        for (size_t k = 0; k < JobCount; ++k)
        {
            const FWidgetRenderJob& Job = UI->WidgetJobs[(State.WidgetRenderCursor + k) % JobCount];
            if (Job.Context == nullptr || !RHI::IsValid(Job.Target))
            {
                continue;
            }

            State.Renderer->BeginFrame(CmdList, Job.Target, Job.Size);
            Job.Context->Render();
            const uint64 Hash = State.Renderer->PeekFrameHash();

            if (State.Renderer->IsTargetUpToDate(Job.Target, Hash))
            {
                State.Renderer->AbortFrame();
                State.Renderer->NoteTargetStable(Job.Target, true);   // unchanged -> closer to dormant
                continue;
            }

            if (Budget > 0 && Rendered >= Budget)
            {
                State.Renderer->AbortFrame();
                State.Renderer->NoteTargetStable(Job.Target, false);  // pending change -> keep awake
                continue;
            }

            // Renderer composites with LoadOp=Load, so clear the RT to transparent first.
            const float Transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            RHI::CmdBarrier(CmdList, RHI::EStageFlags::AllCommands, RHI::EStageFlags::Transfer);
            RHI::CmdClearTexture(CmdList, Job.Target, Transparent);
            RHI::CmdBarrier(CmdList, RHI::EStageFlags::Transfer, RHI::EStageFlags::AllCommands);

            State.Renderer->EndFrame();
            State.Renderer->NoteTargetStable(Job.Target, false);      // just changed -> reset
            ++Rendered;
        }

        if (Rendered > 0)
        {
            // Widget RT writes visible to the scene's widget pass sampling them later this frame.
            RHI::CmdBarrier(CmdList, RHI::EStageFlags::RasterColorOut, RHI::EStageFlags::PixelShader);
        }

        State.WidgetRenderCursor = (uint32)((State.WidgetRenderCursor + 1) % JobCount);
    }

    void ReleaseWidget(CWorld* World, SWidgetComponent& Component)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        
        if (World != nullptr)
        {
            if (FWorldUIContext* UI = World->GetUIContext())
            {
                Rml::Context* DyingContext = Component.Runtime.Context;
                for (auto It = UI->WidgetJobs.begin(); It != UI->WidgetJobs.end(); )
                {
                    It = (It->Context == DyingContext) ? UI->WidgetJobs.erase(It) : It + 1;
                }
            }
        }

        DestroyWidgetRuntime(Component.Runtime);
    }

    void TickEditorContexts()
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized)
        {
            return;
        }

        for (auto& E : State.EditorContexts)
        {
            if (E->Context == nullptr)
            {
                continue;
            }
            if (E->Size.x > 0 && E->Size.y > 0)
            {
                E->Context->SetDimensions(Rml::Vector2i(int(E->Size.x), int(E->Size.y)));
                // Editor contexts use caller-supplied DPI; the world heuristic is too small for previews <1080px.
                E->Context->SetDensityIndependentPixelRatio(std::max(0.1f, E->DpiScale));
            }
            E->Context->Update();
        }
    }

    void RenderEditorContexts(RHI::FCmdListH CmdList)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized || State.Renderer == nullptr)
        {
            return;
        }

        bool bAnyRendered = false;
        for (auto& E : State.EditorContexts)
        {
            if (E->Context == nullptr || !RHI::IsValid(E->Target))
            {
                continue;
            }
            if (E->Size.x == 0 || E->Size.y == 0)
            {
                continue;
            }
            // Renderer uses LoadOp=Load; clear here so editor can composite its own background under a transparent canvas.
            const float Clear[4] = { E->ClearColor.x, E->ClearColor.y, E->ClearColor.z, E->ClearColor.w };
            RHI::CmdBarrier(CmdList, RHI::EStageFlags::AllCommands, RHI::EStageFlags::Transfer);
            RHI::CmdClearTexture(CmdList, E->Target, Clear);
            RHI::CmdBarrier(CmdList, RHI::EStageFlags::Transfer, RHI::EStageFlags::AllCommands);

            State.Renderer->BeginFrame(CmdList, E->Target, E->Size);
            E->Context->Render();
            State.Renderer->EndFrame();
            bAnyRendered = true;
        }

        if (bAnyRendered)
        {
            // Preview RT writes visible to ImGui sampling them this frame.
            RHI::CmdBarrier(CmdList, RHI::EStageFlags::RasterColorOut, RHI::EStageFlags::PixelShader);
        }
    }

    // Renderer pointer is set once in Initialise() and only cleared in Shutdown() -- safe to read unlocked.
    FRmlUiRenderer* GetRenderer()      { return S().Renderer.get(); }

    Rml::Context* GetContextForWorld(CWorld* World)
    {
        if (World == nullptr)
        {
            return nullptr;
        }
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        FWorldUIContext* UI = World->GetUIContext();
        return UI ? UI->Context : nullptr;
    }

    bool WorldUIWantsMouse(const CWorld* World)
    {
        if (World == nullptr)
        {
            return false;
        }
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized)
        {
            return false;
        }
        const FWorldUIContext* UI = World->GetUIContext();
        return UI != nullptr && UI->Context != nullptr && UI->Context->IsMouseInteracting();
    }

    FLockedWorldContext::FLockedWorldContext(CWorld* World)
    {
        if (World == nullptr)
        {
            return;
        }
        FState& State = S();
        State.StateMutex.lock();
        bLocked = true;
        // Resolved INSIDE the locked scope so the Context* can't be torn down between
        // resolution and use; ~FLockedWorldContext releases.
        FWorldUIContext* UI = World->GetUIContext();
        Context = UI ? UI->Context : nullptr;
    }

    FLockedWorldContext::~FLockedWorldContext()
    {
        if (bLocked)
        {
            S().StateMutex.unlock();
        }
    }

    void SetWorldDisplaySize(CWorld* World, const FUIntVector2& Size)
    {
        if (World == nullptr)
        {
            return;
        }
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (FWorldUIContext* UI = World->GetUIContext())
        {
            UI->DisplaySize = Size;
        }
    }

    bool SetWorldInlineDocument(CWorld* World, FStringView Body, FStringView SourceUrl)
    {
        if (World == nullptr)
        {
            return false;
        }
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized)
        {
            return false;
        }
        FWorldUIContext* UI = World->GetUIContext();
        if (UI == nullptr || UI->Context == nullptr)
        {
            return false;
        }

        // Drop whatever was loaded; the preview owns the whole context.
        for (auto& KV : UI->Documents)
        {
            UI->Context->UnloadDocument(KV.second);
        }
        UI->Documents.clear();

        if (Body.empty())
        {
            return false;
        }

        const Rml::String BodyStr(Body.data(), Body.size());
        const Rml::String UrlStr(SourceUrl.data(), SourceUrl.size());
        Rml::ElementDocument* Doc = UI->Context->LoadDocumentFromMemory(BodyStr, UrlStr);
        if (Doc == nullptr)
        {
            LOG_ERROR("[RmlUi] SetWorldInlineDocument: failed to parse inline body.");
            return false;
        }
        Doc->Show();
        UI->Documents.emplace(FString("__inline__"), Doc);
        return true;
    }

    Rml::Context* CreateEditorContext(const char* Name, const FUIntVector2& InitialSize)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized || Name == nullptr)
        {
            return nullptr;
        }

        const Rml::Vector2i Size(
            int(InitialSize.x > 0 ? InitialSize.x : 1u),
            int(InitialSize.y > 0 ? InitialSize.y : 1u));

        Rml::Context* Ctx = Rml::CreateContext(Name, Size);
        if (Ctx == nullptr)
        {
            LOG_ERROR("[RmlUi] CreateEditorContext failed for name '{}'.", Name);
            return nullptr;
        }

        TUniquePtr<FEditorEntry> Entry = MakeUnique<FEditorEntry>();
        Entry->Context = Ctx;
        Entry->Size    = InitialSize;
        State.EditorContexts.push_back(Move(Entry));
        return Ctx;
    }

    void DestroyEditorContext(Rml::Context* Context)
    {
        if (Context == nullptr)
        {
            return;
        }
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        for (size_t i = 0; i < State.EditorContexts.size(); ++i)
        {
            FEditorEntry* E = State.EditorContexts[i].get();
            if (E->Context != Context)
            {
                continue;
            }

            if (State.DebuggerHost == E->Context)
            {
                Rml::Debugger::Shutdown();
                State.DebuggerHost = nullptr;
                State.bDebuggerVisible = false;
            }

            Rml::RemoveContext(E->Context->GetName());
            E->Context = nullptr;

            const size_t Last = State.EditorContexts.size() - 1;
            if (i != Last)
            {
                eastl::swap(State.EditorContexts[i], State.EditorContexts[Last]);
            }
            State.EditorContexts.pop_back();
            return;
        }
    }

    void SetEditorContextTarget(Rml::Context* Context, RHI::FTextureH Target, const FUIntVector2& Size)
    {
        if (Context == nullptr)
        {
            return;
        }
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        for (auto& E : State.EditorContexts)
        {
            if (E->Context == Context)
            {
                E->Target = Target;
                E->Size   = Size;
                return;
            }
        }
    }

    void SetEditorContextDpiScale(Rml::Context* Context, float Scale)
    {
        if (Context == nullptr)
        {
            return;
        }
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        for (auto& E : State.EditorContexts)
        {
            if (E->Context == Context)
            {
                E->DpiScale = Scale;
                return;
            }
        }
    }

    void SetEditorContextClearColor(Rml::Context* Context, const FVector4& Color)
    {
        if (Context == nullptr)
        {
            return;
        }
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        for (auto& E : State.EditorContexts)
        {
            if (E->Context == Context)
            {
                E->ClearColor = Color;
                return;
            }
        }
    }

    namespace
    {
        FEditorEntry* FindEditorEntry(Rml::Context* Context)
        {
            FState& State = S();
            for (auto& E : State.EditorContexts)
            {
                if (E->Context == Context)
                {
                    return E.get();
                }
            }
            return nullptr;
        }
    }

    bool ReplaceEditorContextDocument(Rml::Context* Context, FStringView Body, FStringView SourceUrl)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        FEditorEntry* Entry = FindEditorEntry(Context);
        if (Entry == nullptr || Entry->Context == nullptr)
        {
            return false;
        }

        if (Entry->Document != nullptr)
        {
            Entry->Context->UnloadDocument(Entry->Document);
            Entry->Document = nullptr;
        }

        if (Body.empty())
        {
            return false;
        }

        const Rml::String BodyStr(Body.data(), Body.size());
        const Rml::String UrlStr(SourceUrl.data(), SourceUrl.size());

        Entry->Document = Entry->Context->LoadDocumentFromMemory(BodyStr, UrlStr);
        if (Entry->Document == nullptr)
        {
            return false;
        }
        Entry->Document->Show();
        return true;
    }

    void ClearEditorContextDocument(Rml::Context* Context)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        FEditorEntry* Entry = FindEditorEntry(Context);
        if (Entry == nullptr || Entry->Context == nullptr)
        {
            return;
        }
        if (Entry->Document != nullptr)
        {
            Entry->Context->UnloadDocument(Entry->Document);
            Entry->Document = nullptr;
        }
    }

}
