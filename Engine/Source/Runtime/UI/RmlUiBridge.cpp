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

#include "Core/Application/Application.h"
#include "Events/Event.h"
#include "Events/EventProcessor.h"
#include "Events/KeyCodes.h"
#include "Events/MouseCodes.h"
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

        Rml::Input::KeyIdentifier ToRmlKey(EKey Key)
        {
            using K = Rml::Input::KeyIdentifier;
            switch (Key)
            {
            case EKey::A: return K::KI_A;  case EKey::B: return K::KI_B;
            case EKey::C: return K::KI_C;  case EKey::D: return K::KI_D;
            case EKey::E: return K::KI_E;  case EKey::F: return K::KI_F;
            case EKey::G: return K::KI_G;  case EKey::H: return K::KI_H;
            case EKey::I: return K::KI_I;  case EKey::J: return K::KI_J;
            case EKey::K: return K::KI_K;  case EKey::L: return K::KI_L;
            case EKey::M: return K::KI_M;  case EKey::N: return K::KI_N;
            case EKey::O: return K::KI_O;  case EKey::P: return K::KI_P;
            case EKey::Q: return K::KI_Q;  case EKey::R: return K::KI_R;
            case EKey::S: return K::KI_S;  case EKey::T: return K::KI_T;
            case EKey::U: return K::KI_U;  case EKey::V: return K::KI_V;
            case EKey::W: return K::KI_W;  case EKey::X: return K::KI_X;
            case EKey::Y: return K::KI_Y;  case EKey::Z: return K::KI_Z;
            case EKey::D0: return K::KI_0; case EKey::D1: return K::KI_1;
            case EKey::D2: return K::KI_2; case EKey::D3: return K::KI_3;
            case EKey::D4: return K::KI_4; case EKey::D5: return K::KI_5;
            case EKey::D6: return K::KI_6; case EKey::D7: return K::KI_7;
            case EKey::D8: return K::KI_8; case EKey::D9: return K::KI_9;
            case EKey::F1:  return K::KI_F1;  case EKey::F2:  return K::KI_F2;
            case EKey::F3:  return K::KI_F3;  case EKey::F4:  return K::KI_F4;
            case EKey::F5:  return K::KI_F5;  case EKey::F6:  return K::KI_F6;
            case EKey::F7:  return K::KI_F7;  case EKey::F8:  return K::KI_F8;
            case EKey::F9:  return K::KI_F9;  case EKey::F10: return K::KI_F10;
            case EKey::F11: return K::KI_F11; case EKey::F12: return K::KI_F12;
            case EKey::Space:     return K::KI_SPACE;
            case EKey::Enter:     return K::KI_RETURN;
            case EKey::Tab:       return K::KI_TAB;
            case EKey::Backspace: return K::KI_BACK;
            case EKey::Escape:    return K::KI_ESCAPE;
            case EKey::Delete:    return K::KI_DELETE;
            case EKey::Insert:    return K::KI_INSERT;
            case EKey::Home:      return K::KI_HOME;
            case EKey::End:       return K::KI_END;
            case EKey::PageUp:    return K::KI_PRIOR;
            case EKey::PageDown:  return K::KI_NEXT;
            case EKey::Up:        return K::KI_UP;
            case EKey::Down:      return K::KI_DOWN;
            case EKey::Left:      return K::KI_LEFT;
            case EKey::Right:     return K::KI_RIGHT;
            case EKey::LeftShift:    return K::KI_LSHIFT;
            case EKey::RightShift:   return K::KI_RSHIFT;
            case EKey::LeftControl:  return K::KI_LCONTROL;
            case EKey::RightControl: return K::KI_RCONTROL;
            case EKey::LeftAlt:      return K::KI_LMENU;
            case EKey::RightAlt:     return K::KI_RMENU;
            case EKey::Comma:        return K::KI_OEM_COMMA;
            case EKey::Period:       return K::KI_OEM_PERIOD;
            case EKey::Minus:        return K::KI_OEM_MINUS;
            case EKey::Equal:        return K::KI_OEM_PLUS;
            case EKey::Slash:        return K::KI_OEM_2;
            case EKey::Backslash:    return K::KI_OEM_5;
            case EKey::Apostrophe:   return K::KI_OEM_7;
            case EKey::Semicolon:    return K::KI_OEM_1;
            case EKey::LeftBracket:  return K::KI_OEM_4;
            case EKey::RightBracket: return K::KI_OEM_6;
            case EKey::GraveAccent:  return K::KI_OEM_3;
            default:
                return K::KI_UNKNOWN;
            }
        }

        int ToRmlMouseButton(EMouseKey Button)
        {
            switch (Button)
            {
            case EMouseKey::ButtonLeft:   return 0;
            case EMouseKey::ButtonRight:  return 1;
            case EMouseKey::ButtonMiddle: return 2;
            default:                      return int(Button);
            }
        }

        Rml::Context* ResolveActiveContext();

        // Forwards window events to the active context (non-editor path).
        class FRmlUiInputHandler final : public IEventHandler
        {
        public:
            int  CachedModifierState = 0;
            int  LastMouseX = 0;
            int  LastMouseY = 0;

            EInputCategory GetInputCategory() const override { return EInputCategory::UI; }

            void UpdateModifiersFromKeyEvent(const FKeyEvent& KeyEvent)
            {
                using namespace Rml::Input;
                CachedModifierState = 0;
                if (KeyEvent.IsCtrlDown())  CachedModifierState |= KM_CTRL;
                if (KeyEvent.IsShiftDown()) CachedModifierState |= KM_SHIFT;
                if (KeyEvent.IsAltDown())   CachedModifierState |= KM_ALT;
                if (KeyEvent.IsSuperDown()) CachedModifierState |= KM_META;
            }

            bool OnEvent(FEvent& Event) override;
        };

        // Wraps a Lua::FRef so Rml events can fire script callbacks.
        class FLuaEventListener final : public Rml::EventListener
        {
        public:
            FLuaEventListener(Lua::FRef Callback) : Cb(Move(Callback)) {}

            void ProcessEvent(Rml::Event& /*Event*/) override
            {
                if (Cb.IsInvokable())
                {
                    Cb();
                }
            }

        private:
            Lua::FRef Cb;
        };

        // 1:1 with CWorld. Documents and listeners hang off the entry so
        // unloading a world's context cleans up its UI state in one shot.
        struct FWorldEntry
        {
            CWorld*       World = nullptr;
            Rml::Context* Context = nullptr;
            THashMap<FString, Rml::ElementDocument*>                   Documents;
            THashMap<FString, TVector<TUniquePtr<FLuaEventListener>>>  Listeners;
        };

        struct FState
        {
            TUniquePtr<FLuminaSystemInterface>  System;
            TUniquePtr<FRmlUiFileInterface>     Files;
            TUniquePtr<FRmlUiRenderer>          Renderer;
            TUniquePtr<FRmlUiInputHandler>      Input;
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

        Rml::Context* ResolveActiveContext()
        {
            FWorldEntry* E = GetActiveEntry();
            return E ? E->Context : nullptr;
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

    bool FRmlUiInputHandler::OnEvent(FEvent& Event)
    {
        Rml::Context* Ctx = ResolveActiveContext();
        if (Ctx == nullptr)
        {
            return false;
        }

#if WITH_EDITOR
        // Mouse events are forwarded explicitly from EditorTool::DrawViewport
        // because the viewport panel coords don't match window pixels. Keys
        // still flow through here.
        if (Event.IsA<FMouseMovedEvent>() || Event.IsA<FMouseButtonPressedEvent>()
            || Event.IsA<FMouseButtonReleasedEvent>() || Event.IsA<FMouseScrolledEvent>())
        {
            return false;
        }
#endif

        if (Event.IsA<FMouseMovedEvent>())
        {
            FMouseMovedEvent& E = Event.As<FMouseMovedEvent>();
            LastMouseX = int(E.GetX());
            LastMouseY = int(E.GetY());
            const bool NotConsumed = Ctx->ProcessMouseMove(LastMouseX, LastMouseY, CachedModifierState);
            return !NotConsumed;
        }
        if (Event.IsA<FMouseButtonPressedEvent>())
        {
            FMouseButtonEvent& E = Event.As<FMouseButtonPressedEvent>();
            const bool NotConsumed = Ctx->ProcessMouseButtonDown(ToRmlMouseButton(E.GetButton()), CachedModifierState);
            return !NotConsumed;
        }
        if (Event.IsA<FMouseButtonReleasedEvent>())
        {
            FMouseButtonEvent& E = Event.As<FMouseButtonReleasedEvent>();
            const bool NotConsumed = Ctx->ProcessMouseButtonUp(ToRmlMouseButton(E.GetButton()), CachedModifierState);
            return !NotConsumed;
        }
        if (Event.IsA<FMouseScrolledEvent>())
        {
            FMouseScrolledEvent& E = Event.As<FMouseScrolledEvent>();
            const bool NotConsumed = Ctx->ProcessMouseWheel(-E.GetOffset(), CachedModifierState);
            return !NotConsumed;
        }
        if (Event.IsA<FKeyPressedEvent>())
        {
            FKeyPressedEvent& E = Event.As<FKeyPressedEvent>();
            UpdateModifiersFromKeyEvent(E);
            const Rml::Input::KeyIdentifier RmlKey = ToRmlKey(E.GetKeyCode());
            if (RmlKey == Rml::Input::KI_UNKNOWN) return false;
            const bool NotConsumed = Ctx->ProcessKeyDown(RmlKey, CachedModifierState);
            return !NotConsumed;
        }
        if (Event.IsA<FKeyReleasedEvent>())
        {
            FKeyReleasedEvent& E = Event.As<FKeyReleasedEvent>();
            UpdateModifiersFromKeyEvent(E);
            const Rml::Input::KeyIdentifier RmlKey = ToRmlKey(E.GetKeyCode());
            if (RmlKey == Rml::Input::KI_UNKNOWN) return false;
            const bool NotConsumed = Ctx->ProcessKeyUp(RmlKey, CachedModifierState);
            return !NotConsumed;
        }
        if (Event.IsA<FCharInputEvent>())
        {
            FCharInputEvent& E = Event.As<FCharInputEvent>();
            const bool NotConsumed = Ctx->ProcessTextInput(Rml::Character(E.GetCodepoint()));
            return !NotConsumed;
        }

        return false;
    }

    bool Initialise()
    {
        FState& State = S();
        if (State.Initialised) return true;

        State.System   = MakeUnique<FLuminaSystemInterface>();
        State.Files    = MakeUnique<FRmlUiFileInterface>();
        State.Renderer = MakeUnique<FRmlUiRenderer>();
        State.Input    = MakeUnique<FRmlUiInputHandler>();

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

        if (GApp != nullptr)
        {
            GApp->GetEventProcessor().RegisterEventHandler(State.Input.get());
        }

        State.Initialised = true;
        LOG_INFO("[RmlUi] Initialised. Per-world contexts will be created as worlds come up.");
        return true;
    }

    void Shutdown()
    {
        FState& State = S();
        if (!State.Initialised && !State.System) return;

        if (GApp != nullptr && State.Input)
        {
            GApp->GetEventProcessor().UnregisterEventHandler(State.Input.get());
        }

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
        if (!State.Initialised || World == nullptr) return;
        if (FindEntryByWorld(World) != nullptr) return;

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

        LOG_INFO("[RmlUi] Created context '{}' for world {} (initial {}x{}).",
                 NameBuf, static_cast<void*>(World), InitialSize.x, InitialSize.y);
    }

    void OnWorldTornDown(CWorld* World)
    {
        FState& State = S();
        if (World == nullptr) return;

        for (size_t i = 0; i < State.Worlds.size(); ++i)
        {
            FWorldEntry* E = State.Worlds[i].get();
            if (E->World != World) continue;

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
            if (State.ActiveContext == DyingContext) State.ActiveContext = nullptr;

            const size_t Last = State.Worlds.size() - 1;
            if (i != Last) eastl::swap(State.Worlds[i], State.Worlds[Last]);
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
        if (!State.Initialised) return;

        // 1080p baseline: dp ratio scales with RT height so a UI written
        // against a 1080 panel looks the same on higher-res / super-sampled RTs.
        constexpr float NominalHeight = 1080.0f;

        for (auto& E : State.Worlds)
        {
            if (E->Context == nullptr) continue;
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
            if (E->Context == nullptr) continue;
            const FWorldTarget Tgt = GetWorldTarget(E->World);
            if (Tgt.Image == nullptr) continue;

            State.Renderer->BeginFrame(CmdList, Tgt.Image, Tgt.Size);
            E->Context->Render();
            State.Renderer->EndFrame();
        }
    }

    Rml::Context*   GetActiveContext() { return S().ActiveContext; }
    FRmlUiRenderer* GetRenderer()      { return S().Renderer.get(); }

    // Editor mouse forwarding bypasses the FInputProcessor chain so ImGui
    // can't eat events before they reach the right context.
    void ForwardMouseMove(CWorld* World, int X, int Y, int KeyModifierState)
    {
        FWorldEntry* E = FindEntryByWorld(World);
        if (E == nullptr || E->Context == nullptr) return;
        E->Context->ProcessMouseMove(X, Y, KeyModifierState);
    }

    void ForwardMouseButton(CWorld* World, int Button, bool bPressed, int KeyModifierState)
    {
        FWorldEntry* E = FindEntryByWorld(World);
        if (E == nullptr || E->Context == nullptr) return;
        if (bPressed) E->Context->ProcessMouseButtonDown(Button, KeyModifierState);
        else          E->Context->ProcessMouseButtonUp(Button, KeyModifierState);
    }

    void ForwardMouseWheel(CWorld* World, float Delta, int KeyModifierState)
    {
        FWorldEntry* E = FindEntryByWorld(World);
        if (E == nullptr || E->Context == nullptr) return;
        E->Context->ProcessMouseWheel(Delta, KeyModifierState);
    }

    void ForwardMouseLeave(CWorld* World)
    {
        FWorldEntry* E = FindEntryByWorld(World);
        if (E == nullptr || E->Context == nullptr) return;
        E->Context->ProcessMouseLeave();
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
                LOG_WARN("[RmlUi] UI.LoadDocument('{}') has no active world context yet.",
                         FString(Path.data(), Path.size()).c_str());
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
            if (Entry == nullptr || Entry->Context == nullptr) return;

            const FString Key(Path.data(), Path.size());
            auto It = Entry->Documents.find(Key);
            if (It == Entry->Documents.end()) return;

            // RmlUi detaches listeners before we delete the wrappers.
            Entry->Context->UnloadDocument(It->second);
            Entry->Documents.erase(It);
            Entry->Listeners.erase(Key);
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

            const FString Key(Path.data(), Path.size());
            TUniquePtr<FLuaEventListener> Listener = MakeUnique<FLuaEventListener>(Move(Callback));
            Target->AddEventListener(EventStr, Listener.get());
            Entry->Listeners[Key].push_back(Move(Listener));
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
