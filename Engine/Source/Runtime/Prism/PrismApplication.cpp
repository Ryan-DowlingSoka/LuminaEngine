#include "PCH.h"
#include "PrismApplication.h"

namespace Lumina::Prism
{
    FPrismApplication::FPrismApplication()
        : Renderer(MakeUnique<FPrismRenderer>())
    {
    }

    void FPrismApplication::Shutdown()
    {
        Renderer.reset();
    }

    void FPrismApplication::SetRootWidget(const FWidgetPtr& InRoot)
    {
        Root           = InRoot;
        HoveredWidget  = nullptr;
        CapturedWidget = nullptr;
        FocusedWidget  = nullptr;
    }

    void FPrismApplication::SetWindowSize(const glm::vec2& InSize)
    {
        WindowSize = InSize;
    }

    void FPrismApplication::CacheDesiredSizes(const FWidgetPtr& Node)
    {
        if (!Node || !Node->IsVisible())
        {
            return;
        }
        const size_t ChildCount = Node->GetChildCount();
        for (size_t i = 0; i < ChildCount; ++i)
        {
            CacheDesiredSizes(Node->GetChildAt(i));
        }
        Node->CacheDesiredSize();
    }

    void FPrismApplication::Tick(float /*DeltaTime*/)
    {
        if (!Root || WindowSize.x <= 0.0f || WindowSize.y <= 0.0f)
        {
            return;
        }

        // Phase 1: bubble desired sizes up the tree. Bottom-up so each
        // parent sees fresh child sizes before computing its own.
        CacheDesiredSizes(Root);

        // Phase 2: assign the root a full-window geometry and let the
        // tree arrange itself inside it.
        const FPrismGeometry RootGeom(glm::vec2(0.0f), WindowSize, 1.0f);
        Root->SetGeometry(RootGeom);
        Root->ArrangeChildren(RootGeom);

        // Phase 3: paint into a fresh draw list and submit to the backend.
        DrawList.Reset();
        const FPrismRect ScreenRect(glm::vec2(0.0f), WindowSize);
        const FPrismPaintContext PaintCtx(DrawList, ScreenRect, 0);
        if (Root->IsDrawn())
        {
            (void)Root->OnPaint(PaintCtx);
        }

        Renderer->BeginFrame(WindowSize);
        Renderer->Submit(DrawList);
        Renderer->EndFrame();
    }

    FWidgetPtr FPrismApplication::HitTest(const FWidgetPtr& Node, const glm::vec2& Point)
    {
        if (!Node || !Node->HitTest(Point))
        {
            return nullptr;
        }

        // Children are painted on top of the parent, so iterate in reverse
        // to prefer topmost widgets.
        const size_t ChildCount = Node->GetChildCount();
        for (size_t i = ChildCount; i-- > 0;)
        {
            FWidgetPtr Hit = HitTest(Node->GetChildAt(i), Point);
            if (Hit)
            {
                return Hit;
            }
        }
        return Node;
    }

    void FPrismApplication::ApplyReply(const FPrismReply& Reply)
    {
        if (const auto& Capture = Reply.GetMouseCapture())
        {
            CapturedWidget = Capture;
        }
        if (Reply.WantsReleaseCapture())
        {
            CapturedWidget = nullptr;
        }
        if (const auto& Focus = Reply.GetFocusTarget())
        {
            FocusedWidget = Focus;
        }
        if (Reply.HasCursor())
        {
            CurrentCursor = Reply.GetCursor();
        }
    }

    bool FPrismApplication::DispatchMouseMove(const FPrismPointerEvent& E)
    {
        if (!Root)
        {
            return false;
        }

        // Captured widget steals all mouse events until it releases.
        if (CapturedWidget)
        {
            const FPrismReply R = CapturedWidget->OnMouseMove(E);
            ApplyReply(R);
            return R.IsHandled();
        }

        FWidgetPtr Hit = HitTest(Root, E.ScreenPosition);
        if (Hit != HoveredWidget)
        {
            if (HoveredWidget)
            {
                ApplyReply(HoveredWidget->OnMouseLeave(E));
            }
            HoveredWidget = Hit;
            if (HoveredWidget)
            {
                ApplyReply(HoveredWidget->OnMouseEnter(E));
            }
            else
            {
                CurrentCursor = EPrismCursor::Default;
            }
        }
        if (HoveredWidget)
        {
            const FPrismReply R = HoveredWidget->OnMouseMove(E);
            ApplyReply(R);
            return R.IsHandled();
        }
        return false;
    }

    bool FPrismApplication::DispatchMouseButtonDown(const FPrismPointerEvent& E)
    {
        if (!Root)
        {
            return false;
        }
        FWidgetPtr Target = CapturedWidget ? CapturedWidget : HitTest(Root, E.ScreenPosition);
        if (!Target)
        {
            return false;
        }
        const FPrismReply R = Target->OnMouseButtonDown(E);
        ApplyReply(R);
        return R.IsHandled();
    }

    bool FPrismApplication::DispatchMouseButtonUp(const FPrismPointerEvent& E)
    {
        if (!Root)
        {
            return false;
        }
        FWidgetPtr Target = CapturedWidget ? CapturedWidget : HitTest(Root, E.ScreenPosition);
        if (!Target)
        {
            return false;
        }
        const FPrismReply R = Target->OnMouseButtonUp(E);
        ApplyReply(R);
        return R.IsHandled();
    }

    bool FPrismApplication::DispatchMouseWheel(const FPrismPointerEvent& E)
    {
        if (!Root)
        {
            return false;
        }
        FWidgetPtr Target = CapturedWidget ? CapturedWidget : HitTest(Root, E.ScreenPosition);
        if (!Target)
        {
            return false;
        }
        const FPrismReply R = Target->OnMouseWheel(E);
        ApplyReply(R);
        return R.IsHandled();
    }

    bool FPrismApplication::DispatchKeyDown(const FPrismKeyEvent& E)
    {
        if (!FocusedWidget)
        {
            return false;
        }
        const FPrismReply R = FocusedWidget->OnKeyDown(E);
        ApplyReply(R);
        return R.IsHandled();
    }

    bool FPrismApplication::DispatchKeyUp(const FPrismKeyEvent& E)
    {
        if (!FocusedWidget)
        {
            return false;
        }
        const FPrismReply R = FocusedWidget->OnKeyUp(E);
        ApplyReply(R);
        return R.IsHandled();
    }

    bool FPrismApplication::DispatchChar(const FPrismKeyEvent& E)
    {
        if (!FocusedWidget)
        {
            return false;
        }
        const FPrismReply R = FocusedWidget->OnChar(E);
        ApplyReply(R);
        return R.IsHandled();
    }
}
