#include "pch.h"
#include "RmlUiBridge.h"

#include "RmlUiFileInterface.h"
#include "RmlUiRenderer.h"
#include "WorldUIContext.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Debugger.h>

#include "Log/Log.h"
#include "Memory/SmartPtr.h"
#include "Core/Threading/Thread.h"
#include "Renderer/CommandList.h"
#include "Renderer/RenderResource.h"
#include "Scripting/Lua/Reference.h"
#include "World/World.h"
#include "World/Scene/RenderScene/RenderScene.h"

namespace Lumina::RmlUi
{
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

        private:
            std::chrono::steady_clock::time_point StartTime;
        };

        class FLuaEventListener final : public Rml::EventListener
        {
        public:
            FLuaEventListener(Lua::FRef Callback) : Cb(Move(Callback)) {}

            void ProcessEvent(Rml::Event& /*Event*/) override
            {
                if (!Cb.IsInvokable())
                {
                    return;
                }
                // Stack-local copy: callback may destroy this listener (e.g. UI.UnloadDocument from a click handler).
                Lua::FRef Local = Cb;
                Local();
            }

            void OnDetach(Rml::Element* /*element*/) override
            {
                delete this;
            }

        private:
            Lua::FRef Cb;
        };

        // Editor preview context not bound to any world; tool owns the target image.
        struct FEditorEntry
        {
            Rml::Context*         Context = nullptr;
            FRHIImage*            Target = nullptr;
            glm::uvec2            Size{0, 0};
            Rml::ElementDocument* Document = nullptr;
            float                 DpiScale = 1.0f;
            glm::vec4             ClearColor{0.10f, 0.10f, 0.12f, 1.0f};
        };

        struct FState
        {
            TUniquePtr<FLuminaSystemInterface>  System;
            TUniquePtr<FRmlUiFileInterface>     Files;
            TUniquePtr<FRmlUiRenderer>          Renderer;
            TVector<TUniquePtr<FEditorEntry>>   EditorContexts;

            // World whose context the `UI.*` Lua module targets; the worlds themselves
            // own their FWorldUIContext (on CWorld). Null when no world is active.
            CWorld*                             ActiveWorld = nullptr;

            Rml::Context*                       DebuggerHost = nullptr;
            bool                                bDebuggerVisible = false;
            bool                                bInitialized = false;

            // Guards EditorContexts/ActiveWorld/Debugger* + serializes Rml::Context DOM
            // mutations (TickWorldUI/Update on game thread) against DOM traversal
            // (RenderWorldUI on render thread). Recursive because Update may fire event
            // callbacks that re-enter public bridge API on the same thread.
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

            if (State.DebuggerHost != Active)
            {
                if (State.DebuggerHost != nullptr)
                {
                    Rml::Debugger::Shutdown();
                    State.DebuggerHost = nullptr;
                }
                if (Active != nullptr)
                {
                    if (Rml::Debugger::Initialise(Active))
                    {
                        State.DebuggerHost = Active;
                    }
                    else
                    {
                        LOG_WARN("[RmlUi] Debugger failed to attach to context '{}'.", Active->GetName());
                    }
                }
            }

            Rml::Debugger::SetVisible(State.bDebuggerVisible);
        }

        Rml::ElementDocument* FindDoc(FStringView Path)
        {
            FWorldUIContext* UI = ActiveUI();
            if (UI == nullptr)
            {
                return nullptr;
            }
            auto It = UI->Documents.find(FString(Path.data(), Path.size()));
            return (It != UI->Documents.end()) ? It->second : nullptr;
        }

        struct FWorldTarget { FRHIImage* Image = nullptr; glm::uvec2 Size{0, 0}; };
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
            FRHIImage* Img = Scene->GetRenderTarget();
            if (Img == nullptr)
            {
                return {};
            }
            return { Img, glm::uvec2(Img->GetSizeX(), Img->GetSizeY()) };
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
        LOG_INFO("[RmlUi] Initialized. Per-world contexts are owned by their CWorld.");
        return true;
    }

    void Shutdown()
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized && !State.System) return;

        // Detach debugger before its host context dies; matters if Shutdown is followed by re-Init.
        if (State.DebuggerHost != nullptr)
        {
            Rml::Debugger::Shutdown();
            State.DebuggerHost = nullptr;
        }

        // Editor contexts are bridge-owned, so drop them here. World contexts belong to
        // their CWorld; once we flip bInitialized, any world torn down after this point
        // just nulls its (now Rml::Shutdown-destroyed) context pointer instead of double
        // removing it (see DestroyWorldUI).
        for (auto& E : State.EditorContexts)
        {
            if (E->Context != nullptr)
            {
                Rml::RemoveContext(E->Context->GetName());
                E->Context = nullptr;
            }
        }
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

        // Newest world becomes active so Lua calls land there.
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
        FWorldUIContext* UI = World->GetUIContext();
        if (UI == nullptr || UI->Context == nullptr)
        {
            return;
        }

        const FWorldTarget Tgt = GetWorldTarget(World);
        if (Tgt.Image != nullptr)
        {
            // DisplaySize override (set by editor each frame) lets the UI lay out at the
            // panel's aspect instead of the RT's. Falls back to RT size in standalone.
            const glm::uvec2 LayoutSize = (UI->DisplaySize.x > 0 && UI->DisplaySize.y > 0) ? UI->DisplaySize : Tgt.Size;

            constexpr float NominalHeight = 1080.0f;
            UI->Context->SetDimensions(Rml::Vector2i(int(LayoutSize.x), int(LayoutSize.y)));

            const float DpRatio = std::max(1.0f, float(LayoutSize.y) / NominalHeight);
            UI->Context->SetDensityIndependentPixelRatio(DpRatio);
        }
        UI->Context->Update();
    }

    void RenderWorldUI(const CWorld* World, ICommandList& CmdList)
    {
        FState& State = S();
        // Render thread holds the lock for the DOM walk. Game-thread mutators
        // (TickWorldUI, DestroyWorldUI) block until this completes, so the
        // Context* is stable for the duration of the render.
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
        if (Tgt.Image == nullptr)
        {
            return;
        }

        const glm::uvec2 LayoutSize = (UI->DisplaySize.x > 0 && UI->DisplaySize.y > 0) ? UI->DisplaySize : Tgt.Size;
        State.Renderer->BeginFrame(CmdList, Tgt.Image, Tgt.Size, LayoutSize);
        UI->Context->Render();
        State.Renderer->EndFrame();
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

    void RenderEditorContexts(ICommandList& CmdList)
    {
        FState& State = S();
        FRecursiveScopeLock Lock(State.StateMutex);
        if (!State.bInitialized || State.Renderer == nullptr)
        {
            return;
        }

        for (auto& E : State.EditorContexts)
        {
            if (E->Context == nullptr || E->Target == nullptr)
            {
                continue;
            }
            if (E->Size.x == 0 || E->Size.y == 0)
            {
                continue;
            }
            // Renderer uses LoadOp=Load; clear here so editor can composite its own background under a transparent canvas.
            CmdList.SetImageState(E->Target, AllSubresources, EResourceStates::CopyDest);
            CmdList.CommitBarriers();
            CmdList.ClearImageColor(E->Target, FColor(E->ClearColor.r, E->ClearColor.g, E->ClearColor.b, E->ClearColor.a));

            State.Renderer->BeginFrame(CmdList, E->Target, E->Size);
            E->Context->Render();
            State.Renderer->EndFrame();
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

    void SetWorldDisplaySize(CWorld* World, const glm::uvec2& Size)
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

    Rml::Context* CreateEditorContext(const char* Name, const glm::uvec2& InitialSize)
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

    void SetEditorContextTarget(Rml::Context* Context, FRHIImage* Target, const glm::uvec2& Size)
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

    void SetEditorContextClearColor(Rml::Context* Context, const glm::vec4& Color)
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

    // `UI.*` Lua module on the active world's context.
    namespace LuaApi
    {
        bool LoadDocument(FStringView Path)
        {
            FState& State = S();
            FRecursiveScopeLock Lock(State.StateMutex);
            FWorldUIContext* UI = ActiveUI();
            if (!State.bInitialized || UI == nullptr || UI->Context == nullptr)
            {
                LOG_WARN("[RmlUi] UI.LoadDocument('{}') has no active world context yet.", FString(Path.data(), Path.size()).c_str());
                return false;
            }

            const FString Key(Path.data(), Path.size());
            if (UI->Documents.find(Key) != UI->Documents.end())
            {
                UI->Documents[Key]->Show();
                LOG_INFO("[RmlUi] '{}' already loaded; re-shown.", Key.c_str());
                return true;
            }

            Rml::ElementDocument* Doc = UI->Context->LoadDocument(Rml::String(Path.data(), Path.size()));
            if (Doc == nullptr)
            {
                LOG_ERROR("[RmlUi] UI.LoadDocument: failed to load '{}'.", Key.c_str());
                return false;
            }
            Doc->Show();
            UI->Documents.emplace(Key, Doc);

            const Rml::Vector2f DocSize = Doc->GetBox().GetSize();
            const Rml::Vector2i CtxSize = UI->Context->GetDimensions();
            LOG_INFO("[RmlUi] Loaded '{}' (doc box {}x{}, context {}x{}).",
                     Key.c_str(), DocSize.x, DocSize.y, CtxSize.x, CtxSize.y);
            return true;
        }

        void UnloadDocument(FStringView Path)
        {
            FState& State = S();
            FRecursiveScopeLock Lock(State.StateMutex);
            FWorldUIContext* UI = ActiveUI();
            if (UI == nullptr || UI->Context == nullptr)
            {
                return;
            }

            const FString Key(Path.data(), Path.size());
            auto It = UI->Documents.find(Key);
            if (It == UI->Documents.end()) return;

            // RmlUi defers destruction until ReleaseUnloadedDocuments; OnDetach fires then.
            UI->Context->UnloadDocument(It->second);
            UI->Documents.erase(It);
        }

        void Show(FStringView Path)
        {
            FRecursiveScopeLock Lock(S().StateMutex);
            if (auto* D = FindDoc(Path)) D->Show();
        }
        void Hide(FStringView Path)
        {
            FRecursiveScopeLock Lock(S().StateMutex);
            if (auto* D = FindDoc(Path)) D->Hide();
        }
        bool IsLoaded(FStringView Path)
        {
            FRecursiveScopeLock Lock(S().StateMutex);
            return FindDoc(Path) != nullptr;
        }
        bool IsVisible(FStringView Path)
        {
            FRecursiveScopeLock Lock(S().StateMutex);
            auto* D = FindDoc(Path); return D && D->IsVisible();
        }

        void SetText(FStringView Path, FStringView ElementId, FStringView Text)
        {
            FRecursiveScopeLock Lock(S().StateMutex);
            auto* Doc = FindDoc(Path);
            if (Doc == nullptr) return;
            if (Rml::Element* El = Doc->GetElementById(Rml::String(ElementId.data(), ElementId.size())))
            {
                El->SetInnerRML(Rml::String(Text.data(), Text.size()));
            }
        }

        void SetInnerRml(FStringView Path, FStringView ElementId, FStringView Rml_)
        {
            FRecursiveScopeLock Lock(S().StateMutex);
            auto* Doc = FindDoc(Path);
            if (Doc == nullptr) return;
            if (Rml::Element* El = Doc->GetElementById(Rml::String(ElementId.data(), ElementId.size())))
            {
                El->SetInnerRML(Rml::String(Rml_.data(), Rml_.size()));
            }
        }

        // Toggle a single class on the element without disturbing other classes.
        // Pass true to add, false to remove.
        void SetClass(FStringView Path, FStringView ElementId, FStringView ClassName, bool bActive)
        {
            FRecursiveScopeLock Lock(S().StateMutex);
            auto* Doc = FindDoc(Path);
            if (Doc == nullptr) return;
            if (Rml::Element* El = Doc->GetElementById(Rml::String(ElementId.data(), ElementId.size())))
            {
                El->SetClass(Rml::String(ClassName.data(), ClassName.size()), bActive);
            }
        }

        void SetDebuggerVisible(bool bVisible)
        {
            FState& State = S();
            FRecursiveScopeLock Lock(State.StateMutex);
            // Persists across PIE: SyncDebuggerToActiveContext re-reads after host rebind.
            State.bDebuggerVisible = bVisible;
            SyncDebuggerToActiveContext();
            LOG_INFO("[RmlUi] Debugger {}.", bVisible ? "shown" : "hidden");
        }

        FString DescribeState()
        {
            FState& State = S();
            FRecursiveScopeLock Lock(State.StateMutex);
            if (!State.bInitialized) return FString("RmlUi not initialised.");

            FWorldUIContext* UI = ActiveUI();
            std::string Out;
            Out.reserve(256);
            if (UI == nullptr || UI->Context == nullptr)
            {
                Out += "RmlUi: no active world context.";
                return FString(Out.c_str(), Out.size());
            }

            const Rml::Vector2i Sz = UI->Context->GetDimensions();
            Out += "RmlUi active context: ";
            Out += UI->Context->GetName();
            Out += " size=";
            Out += std::to_string(Sz.x);
            Out += "x";
            Out += std::to_string(Sz.y);
            Out += " docs=";
            Out += std::to_string(UI->Documents.size());
            for (const auto& KV : UI->Documents)
            {
                Out += "\n    - ";
                Out += KV.first.c_str();
                if (KV.second != nullptr)
                {
                    const Rml::Vector2f Box = KV.second->GetBox().GetSize();
                    Out += " (";
                    Out += std::to_string(int(Box.x));
                    Out += "x";
                    Out += std::to_string(int(Box.y));
                    Out += KV.second->IsVisible() ? ", visible)" : ", hidden)";
                }
            }
            return FString(Out.c_str(), Out.size());
        }

        void OnEvent(FStringView Path, FStringView ElementId, FStringView EventName, Lua::FRef Callback)
        {
            FRecursiveScopeLock Lock(S().StateMutex);
            FWorldUIContext* UI = ActiveUI();
            if (UI == nullptr || UI->Context == nullptr) return;

            auto It = UI->Documents.find(FString(Path.data(), Path.size()));
            if (It == UI->Documents.end())
            {
                LOG_WARN("[RmlUi] UI.OnEvent: document '{}' not loaded.",
                         FString(Path.data(), Path.size()).c_str());
                return;
            }
            Rml::ElementDocument* Doc = It->second;

            const Rml::String EventStr(EventName.data(), EventName.size());
            Rml::Element* Target = ElementId.empty()
                ? static_cast<Rml::Element*>(Doc)
                : Doc->GetElementById(Rml::String(ElementId.data(), ElementId.size()));
            if (Target == nullptr)
            {
                LOG_WARN("[RmlUi] UI.OnEvent: element '{}' not found in '{}'.",
                         FString(ElementId.data(), ElementId.size()).c_str(),
                         FString(Path.data(), Path.size()).c_str());
                return;
            }

            // Ownership transfers to RmlUi; listener self-deletes via OnDetach.
            Target->AddEventListener(EventStr, new FLuaEventListener(Move(Callback)));
        }
    }

    void RegisterLuaModule(Lua::FRef& GlobalsRef)
    {
        Lua::FRef UI = GlobalsRef.NewTable("UI");
        UI.SetFunction<&LuaApi::LoadDocument>("LoadDocument");
        UI.SetFunction<&LuaApi::UnloadDocument>("UnloadDocument");
        UI.SetFunction<&LuaApi::Show>("Show");
        UI.SetFunction<&LuaApi::Hide>("Hide");
        UI.SetFunction<&LuaApi::IsLoaded>("IsLoaded");
        UI.SetFunction<&LuaApi::IsVisible>("IsVisible");
        UI.SetFunction<&LuaApi::SetText>("SetText");
        UI.SetFunction<&LuaApi::SetInnerRml>("SetInnerRml");
        UI.SetFunction<&LuaApi::SetClass>("SetClass");
        UI.SetFunction<&LuaApi::OnEvent>("OnEvent");
        UI.SetFunction<&LuaApi::SetDebuggerVisible>("SetDebuggerVisible");
        UI.SetFunction<&LuaApi::DescribeState>("DescribeState");
    }
}
