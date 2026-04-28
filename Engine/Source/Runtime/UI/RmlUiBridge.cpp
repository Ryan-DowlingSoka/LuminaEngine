#include "pch.h"
#include "RmlUiBridge.h"

#include "RmlUiFileInterface.h"
#include "RmlUiRenderer.h"

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
#include "Renderer/RenderResource.h"
#include "Scripting/Lua/Reference.h"
#include "World/World.h"
#include "World/Scene/RenderScene/RenderScene.h"

namespace Lumina::RmlUi
{
    namespace
    {
        // Bridges Rml clock + log onto Lumina's logger / std::chrono.
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
                // Stack-local copy keeps a lua ref alive even if the callback
                // destroys this listener (e.g. UI.UnloadDocument from inside
                // the click handler) — FRef::Invoke would otherwise read a
                // nulled State after pcall returns.
                Lua::FRef Local = Cb;
                Local();
            }

            // RmlUi calls OnDetach when the element is destroyed (during
            // ReleaseUnloadedDocuments) or when the listener is explicitly
            // removed. Self-deleting here matches RmlUi's view of the
            // listener exactly — Context::UnloadDocument is deferred, so any
            // external owner that erased earlier would leave RmlUi walking
            // dangling pointers in DetachAllEvents.
            void OnDetach(Rml::Element* /*element*/) override
            {
                delete this;
            }

        private:
            Lua::FRef Cb;
        };

        // 1:1 with CWorld. Listener lifetime is owned by RmlUi (FLuaEventListener
        // self-deletes in OnDetach), so we only track documents here.
        struct FWorldEntry
        {
            CWorld*       World = nullptr;
            Rml::Context* Context = nullptr;
            THashMap<FString, Rml::ElementDocument*> Documents;
        };

        struct FState
        {
            TUniquePtr<FLuminaSystemInterface>  System;
            TUniquePtr<FRmlUiFileInterface>     Files;
            TUniquePtr<FRmlUiRenderer>          Renderer;
            TVector<TUniquePtr<FWorldEntry>>    Worlds;
            Rml::Context*                       ActiveContext = nullptr;

            Rml::Context*                       DebuggerHost = nullptr;
            bool                                bDebuggerVisible = false;
            bool                                Initialised = false;
        };

        FState& S()
        {
            static FState State;
            return State;
        }

        FWorldEntry* FindEntryByWorld(CWorld* W)
        {
            for (auto& E : S().Worlds)
            {
                if (E->World == W)
                {
                    return E.get();
                }
            }
            return nullptr;
        }

        FWorldEntry* GetActiveEntry()
        {
            FState& State = S();
            for (auto& E : State.Worlds)
            {
                if (E->Context == State.ActiveContext)
                {
                    return E.get();
                }
            }
            // ActiveContext stale or unset; fall through to the first world.
            return State.Worlds.empty() ? nullptr : State.Worlds.front().get();
        }

        void SyncDebuggerToActiveContext()
        {
            FState& State = S();
            Rml::Context* Active = State.ActiveContext;

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
            FWorldEntry* E = GetActiveEntry();
            if (E == nullptr)
            {
                return nullptr;
            }
            auto It = E->Documents.find(FString(Path.data(), Path.size()));
            return (It != E->Documents.end()) ? It->second : nullptr;
        }

        // {nullptr, 0} if the world has no renderer yet.
        struct FWorldTarget { FRHIImage* Image = nullptr; glm::uvec2 Size{0, 0}; };
        FWorldTarget GetWorldTarget(CWorld* World)
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

    bool Initialise()
    {
        FState& State = S();
        if (State.Initialised) return true;

        State.System   = MakeUnique<FLuminaSystemInterface>();
        State.Files    = MakeUnique<FRmlUiFileInterface>();
        State.Renderer = MakeUnique<FRmlUiRenderer>();

        Rml::SetSystemInterface(State.System.get());
        Rml::SetFileInterface(State.Files.get());
        Rml::SetRenderInterface(State.Renderer.get());

        if (!Rml::Initialise())
        {
            LOG_ERROR("[RmlUi] Rml::Initialise failed.");
            Rml::SetRenderInterface(nullptr);
            Rml::SetFileInterface(nullptr);
            Rml::SetSystemInterface(nullptr);
            State = FState{};
            return false;
        }

        if (!State.Renderer->Initialize())
        {
            LOG_ERROR("[RmlUi] FRmlUiRenderer initialisation failed; tearing down.");
            Rml::Shutdown();
            Rml::SetRenderInterface(nullptr);
            Rml::SetFileInterface(nullptr);
            Rml::SetSystemInterface(nullptr);
            State = FState{};
            return false;
        }

        if (!Rml::LoadFontFace("/Engine/Resources/UI/Fonts/LatoLatin-Regular.ttf", true /*fallback_face*/))
        {
            LOG_WARN("[RmlUi] Default font LatoLatin-Regular.ttf failed to load; text may not render.");
        }

        State.Initialised = true;
        LOG_INFO("[RmlUi] Initialised. Per-world contexts will be created as worlds come up.");
        return true;
    }

    void Shutdown()
    {
        FState& State = S();
        if (!State.Initialised && !State.System) return;

        // Detach the debugger before its host context dies. Keeping our
        // DebuggerHost tracking honest matters if Shutdown is followed by a
        // re-Init.
        if (State.DebuggerHost != nullptr)
        {
            Rml::Debugger::Shutdown();
            State.DebuggerHost = nullptr;
        }

        // Drop every context first: Rml walks element trees and calls OnDetach
        // on each listener. Our listener wrappers must outlive that walk.
        for (auto& E : State.Worlds)
        {
            if (E->Context != nullptr)
            {
                Rml::RemoveContext(E->Context->GetName());
                E->Context = nullptr;
            }
        }
        Rml::Shutdown();

        State.Worlds.clear();
        State.ActiveContext = nullptr;

        if (State.Renderer) State.Renderer->Shutdown();

        Rml::SetRenderInterface(nullptr);
        Rml::SetFileInterface(nullptr);
        Rml::SetSystemInterface(nullptr);

        State = FState{};
    }

    void OnWorldInitialized(CWorld* World)
    {
        FState& State = S();
        if (!State.Initialised || World == nullptr)
        {
            return;
        }
        if (FindEntryByWorld(World) != nullptr)
        {
            return;
        }

        // TickAll resizes from the real RT every frame; this initial value
        // is just to let layout run until the first RenderAll.
        const FWorldTarget Tgt = GetWorldTarget(World);
        const Rml::Vector2i InitialSize = (Tgt.Size.x > 0 && Tgt.Size.y > 0)
            ? Rml::Vector2i(int(Tgt.Size.x), int(Tgt.Size.y))
            : Rml::Vector2i(1280, 720);

        // Pointer-based context name: unique while the world lives.
        char NameBuf[64];
        std::snprintf(NameBuf, sizeof(NameBuf), "world_%p", static_cast<void*>(World));

        Rml::Context* Ctx = Rml::CreateContext(NameBuf, InitialSize);
        if (Ctx == nullptr)
        {
            LOG_ERROR("[RmlUi] CreateContext failed for world {}.", static_cast<void*>(World));
            return;
        }

        TUniquePtr<FWorldEntry> Entry = MakeUnique<FWorldEntry>();
        Entry->World   = World;
        Entry->Context = Ctx;
        State.Worlds.push_back(Move(Entry));

        // Most-recent-world becomes active so Lua calls land on it.
        State.ActiveContext = Ctx;
        SyncDebuggerToActiveContext();

        LOG_INFO("[RmlUi] Created context '{}' for world {} (initial {}x{}).", NameBuf, static_cast<void*>(World), InitialSize.x, InitialSize.y);
    }

    void OnWorldTornDown(CWorld* World)
    {
        FState& State = S();
        if (World == nullptr)
        {
            return;
        }

        for (size_t i = 0; i < State.Worlds.size(); ++i)
        {
            FWorldEntry* E = State.Worlds[i].get();
            if (E->World != World)
            {
                continue;
            }

            // Detach the debugger before removing its host; otherwise
            // Debugger::Shutdown would walk a freed element tree.
            if (State.DebuggerHost == E->Context)
            {
                Rml::Debugger::Shutdown();
                State.DebuggerHost = nullptr;
            }

            // Cache the pointer for the ActiveContext compare after we null it.
            Rml::Context* DyingContext = E->Context;

            // RmlUi tears down first (walks elements, calls listener OnDetach);
            // then we drop our wrappers.
            if (E->Context != nullptr)
            {
                Rml::RemoveContext(E->Context->GetName());
                E->Context = nullptr;
            }
            if (State.ActiveContext == DyingContext)
            {
                State.ActiveContext = nullptr;
            }

            const size_t Last = State.Worlds.size() - 1;
            if (i != Last)
            {
                eastl::swap(State.Worlds[i], State.Worlds[Last]);
            }
            State.Worlds.pop_back();

            if (State.ActiveContext == nullptr && !State.Worlds.empty())
            {
                State.ActiveContext = State.Worlds.front()->Context;
            }

            SyncDebuggerToActiveContext();
            return;
        }
    }

    void TickAll()
    {
        FState& State = S();
        if (!State.Initialised)
        {
            return;
        }

        constexpr float NominalHeight = 1080.0f;

        for (auto& E : State.Worlds)
        {
            if (E->Context == nullptr)
            {
                continue;
            }
            const FWorldTarget Tgt = GetWorldTarget(E->World);
            if (Tgt.Image != nullptr)
            {
                E->Context->SetDimensions(Rml::Vector2i(int(Tgt.Size.x), int(Tgt.Size.y)));

                const float DpRatio = std::max(1.0f, float(Tgt.Size.y) / NominalHeight);
                E->Context->SetDensityIndependentPixelRatio(DpRatio);
            }
            E->Context->Update();
        }
    }

    void RenderAll(ICommandList& CmdList)
    {
        FState& State = S();
        if (!State.Initialised || State.Renderer == nullptr) return;

        for (auto& E : State.Worlds)
        {
            if (E->Context == nullptr)
            {
                continue;
            }
            const FWorldTarget Tgt = GetWorldTarget(E->World);
            if (Tgt.Image == nullptr)
            {
                continue;
            }

            State.Renderer->BeginFrame(CmdList, Tgt.Image, Tgt.Size);
            E->Context->Render();
            State.Renderer->EndFrame();
        }
    }

    Rml::Context*   GetActiveContext() { return S().ActiveContext; }
    FRmlUiRenderer* GetRenderer()      { return S().Renderer.get(); }

    Rml::Context* GetContextForWorld(CWorld* World)
    {
        if (World == nullptr)
        {
            return nullptr;
        }
        FWorldEntry* E = FindEntryByWorld(World);
        return E ? E->Context : nullptr;
    }

    // `UI.*` Lua module. Operates on the active world's context.
    namespace LuaApi
    {
        bool LoadDocument(FStringView Path)
        {
            FState& State = S();
            FWorldEntry* Entry = GetActiveEntry();
            if (!State.Initialised || Entry == nullptr || Entry->Context == nullptr)
            {
                LOG_WARN("[RmlUi] UI.LoadDocument('{}') has no active world context yet.", FString(Path.data(), Path.size()).c_str());
                return false;
            }

            const FString Key(Path.data(), Path.size());
            if (Entry->Documents.find(Key) != Entry->Documents.end())
            {
                Entry->Documents[Key]->Show();
                LOG_INFO("[RmlUi] '{}' already loaded; re-shown.", Key.c_str());
                return true;
            }

            Rml::ElementDocument* Doc = Entry->Context->LoadDocument(Rml::String(Path.data(), Path.size()));
            if (Doc == nullptr)
            {
                LOG_ERROR("[RmlUi] UI.LoadDocument: failed to load '{}'.", Key.c_str());
                return false;
            }
            Doc->Show();
            Entry->Documents.emplace(Key, Doc);

            const Rml::Vector2f DocSize = Doc->GetBox().GetSize();
            const Rml::Vector2i CtxSize = Entry->Context->GetDimensions();
            LOG_INFO("[RmlUi] Loaded '{}' (doc box {}x{}, context {}x{}).",
                     Key.c_str(), DocSize.x, DocSize.y, CtxSize.x, CtxSize.y);
            return true;
        }

        void UnloadDocument(FStringView Path)
        {
            FWorldEntry* Entry = GetActiveEntry();
            if (Entry == nullptr || Entry->Context == nullptr)
            {
                return;
            }

            const FString Key(Path.data(), Path.size());
            auto It = Entry->Documents.find(Key);
            if (It == Entry->Documents.end()) return;

            // RmlUi defers actual destruction until ReleaseUnloadedDocuments;
            // OnDetach (and FLuaEventListener::delete this) fires then.
            Entry->Context->UnloadDocument(It->second);
            Entry->Documents.erase(It);
        }

        void Show(FStringView Path)        { if (auto* D = FindDoc(Path)) D->Show(); }
        void Hide(FStringView Path)        { if (auto* D = FindDoc(Path)) D->Hide(); }
        bool IsLoaded(FStringView Path)    { return FindDoc(Path) != nullptr; }
        bool IsVisible(FStringView Path)   { auto* D = FindDoc(Path); return D && D->IsVisible(); }

        void SetText(FStringView Path, FStringView ElementId, FStringView Text)
        {
            auto* Doc = FindDoc(Path);
            if (Doc == nullptr) return;
            if (Rml::Element* El = Doc->GetElementById(Rml::String(ElementId.data(), ElementId.size())))
            {
                El->SetInnerRML(Rml::String(Text.data(), Text.size()));
            }
        }

        void SetInnerRml(FStringView Path, FStringView ElementId, FStringView Rml_)
        {
            auto* Doc = FindDoc(Path);
            if (Doc == nullptr) return;
            if (Rml::Element* El = Doc->GetElementById(Rml::String(ElementId.data(), ElementId.size())))
            {
                El->SetInnerRML(Rml::String(Rml_.data(), Rml_.size()));
            }
        }

        void SetDebuggerVisible(bool bVisible)
        {
            // Sticks across PIE: SyncDebuggerToActiveContext reads bDebuggerVisible
            // after rebinding the host.
            S().bDebuggerVisible = bVisible;
            SyncDebuggerToActiveContext();
            LOG_INFO("[RmlUi] Debugger {}.", bVisible ? "shown" : "hidden");
        }

        FString DescribeState()
        {
            FState& State = S();
            if (!State.Initialised) return FString("RmlUi not initialised.");

            std::string Out;
            Out.reserve(256);
            Out += "RmlUi worlds: ";
            Out += std::to_string(State.Worlds.size());
            for (auto& E : State.Worlds)
            {
                if (E->Context == nullptr) continue;
                const Rml::Vector2i Sz = E->Context->GetDimensions();
                Out += "\n  ";
                Out += (E->Context == State.ActiveContext) ? "* " : "  ";
                Out += E->Context->GetName();
                Out += " size=";
                Out += std::to_string(Sz.x);
                Out += "x";
                Out += std::to_string(Sz.y);
                Out += " docs=";
                Out += std::to_string(E->Documents.size());
                for (const auto& KV : E->Documents)
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
            }
            return FString(Out.c_str(), Out.size());
        }

        void OnEvent(FStringView Path, FStringView ElementId, FStringView EventName, Lua::FRef Callback)
        {
            FWorldEntry* Entry = GetActiveEntry();
            if (Entry == nullptr || Entry->Context == nullptr) return;

            auto It = Entry->Documents.find(FString(Path.data(), Path.size()));
            if (It == Entry->Documents.end())
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

            // Ownership transfers to RmlUi; the listener self-deletes in
            // OnDetach when the element/document is destroyed.
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
        UI.SetFunction<&LuaApi::OnEvent>("OnEvent");
        UI.SetFunction<&LuaApi::SetDebuggerVisible>("SetDebuggerVisible");
        UI.SetFunction<&LuaApi::DescribeState>("DescribeState");
    }
}
