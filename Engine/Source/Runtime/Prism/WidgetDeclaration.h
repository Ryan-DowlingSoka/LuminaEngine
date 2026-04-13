#pragma once

#include "Widget.h"
#include "Memory/SmartPtr.h"

// ---------------------------------------------------------------------------
// Declarative construction for Prism widgets.
//
// Each widget declares a nested FArguments struct using PRISM_BEGIN_ARGS /
// PRISM_END_ARGS and a handful of per-field macros. The widget implements
// Construct(const FArguments&) to consume those arguments, and is then built
// via PNew / PAssignNew:
//
//   auto Label = PNew(STextBlock)
//       .Text("Hello")
//       .ColorAndOpacity(FPrismColor::White())
//       .FontSize(16.0f);
//
//   TSharedPtr<SBorder> BorderRef;
//   auto Root = PAssignNew(BorderRef, SBorder)
//       .Padding(FPrismMargin(8.0f))
//       [
//           PNew(SButton).Text("Click me")
//       ];
// ---------------------------------------------------------------------------

namespace Lumina::Prism
{
    template<typename WidgetT>
    struct TPrismMaker
    {
        TSharedPtr<WidgetT> Widget;

        TPrismMaker() : Widget(MakeShared<WidgetT>()) {}

        TSharedPtr<WidgetT> operator<<=(typename WidgetT::FArguments&& Args) const
        {
            Widget->Construct(Args);
            return Widget;
        }

        TSharedPtr<WidgetT> operator<<=(const typename WidgetT::FArguments& Args) const
        {
            Widget->Construct(Args);
            return Widget;
        }
    };
}

#define PRISM_BEGIN_ARGS(WidgetType)                                            \
    public:                                                                     \
        struct FArguments                                                       \
        {                                                                       \
            using WidgetArgsType = FArguments;                                  \
            FArguments() = default;

#define PRISM_END_ARGS() };

// Named value argument. Expands to a field `_Name` plus a setter.
#define PRISM_ARGUMENT(Type, Name)                                              \
        Type _##Name{};                                                         \
        WidgetArgsType& Name(Type InValue)                                      \
        {                                                                       \
            _##Name = eastl::move(InValue);                                     \
            return *this;                                                       \
        }

// Same as PRISM_ARGUMENT but with an explicit default expression.
#define PRISM_ARGUMENT_DEFAULT(Type, Name, Default)                             \
        Type _##Name = Default;                                                 \
        WidgetArgsType& Name(Type InValue)                                      \
        {                                                                       \
            _##Name = eastl::move(InValue);                                     \
            return *this;                                                       \
        }

// Event setter for a callable/delegate field.
#define PRISM_EVENT(DelegateType, Name)                                         \
        DelegateType _##Name{};                                                 \
        WidgetArgsType& Name(DelegateType InDelegate)                           \
        {                                                                       \
            _##Name = eastl::move(InDelegate);                                  \
            return *this;                                                       \
        }

// Single-child slot, injected via `[ Child ]` syntax.
#define PRISM_DEFAULT_SLOT(Name)                                                \
        ::Lumina::TSharedPtr<::Lumina::Prism::SWidget> _##Name;                 \
        WidgetArgsType& operator[](const ::Lumina::TSharedPtr<::Lumina::Prism::SWidget>& InChild) \
        {                                                                       \
            _##Name = InChild;                                                  \
            return *this;                                                       \
        }

// Named child slot; multiple per widget, set via `Name##Slot(Child)`.
#define PRISM_NAMED_SLOT(Name)                                                  \
        ::Lumina::TSharedPtr<::Lumina::Prism::SWidget> _##Name;                 \
        WidgetArgsType& Name##Slot(const ::Lumina::TSharedPtr<::Lumina::Prism::SWidget>& InChild) \
        {                                                                       \
            _##Name = InChild;                                                  \
            return *this;                                                       \
        }

// operator<<= is right-associative and has the same precedence as = and
// other compound assignments, so in `PNew(T).Setter(x)` the `.Setter` binds
// to `T::FArguments()` first and the whole FArguments is then consumed by
// TPrismMaker<T>, which calls Widget->Construct() and returns TSharedPtr<T>.
#define PNew(WidgetType)                                                        \
    ::Lumina::Prism::TPrismMaker<WidgetType>() <<= typename WidgetType::FArguments()

// Same as PNew but also assigns the constructed widget into OutPtr so you
// can keep a typed handle without breaking the chain.
#define PAssignNew(OutPtr, WidgetType)                                          \
    ((OutPtr) = (::Lumina::Prism::TPrismMaker<WidgetType>() <<= typename WidgetType::FArguments()))
