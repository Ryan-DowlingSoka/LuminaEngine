#include "CSharpBindingEmitter.h"

#include <EASTL/algorithm.h>

#include <EASTL/unique_ptr.h>

#include "CodeWriter.h"
#include "Reflector/CodeGeneration/ReflectionNames.h"
#include "Reflector/ReflectionCore/ReflectedHeader.h"
#include "Reflector/ReflectionCore/ReflectedProject.h"
#include "Reflector/ReflectionCore/ReflectionDatabase.h"
#include "Reflector/Types/FieldInfo.h"
#include "Reflector/Types/Functions/ReflectedFunction.h"
#include "Reflector/Types/Properties/ReflectedArrayProperty.h"
#include "Reflector/Types/PropertyFlags.h"
#include "Reflector/Types/ReflectedType.h"
#include "StringHash.h"

namespace Lumina::Reflection
{
    namespace
    {
        // Excluded from the C# API: explicit opt-out (NoCSharp/NoLua) or a manually-registered stub
        // (ManualStub, e.g. the math types) whose real layout the Reflector doesn't see, those are
        // hand-written in LuminaSharp instead.
        bool IsExcludedFromCSharp(const FReflectedType& Type)
        {
            return eastl::any_of(Type.Metadata.begin(), Type.Metadata.end(),
                [](const FMetadataPair& Pair)
                {
                    return Pair.Key == "NoCSharp" || Pair.Key == "NoLua" || Pair.Key == "ManualStub";
                });
        }

        bool HasMetadata(const FReflectedType& Type, const char* Key)
        {
            return eastl::any_of(Type.Metadata.begin(), Type.Metadata.end(),
                [Key](const FMetadataPair& Pair) { return Pair.Key == Key; });
        }

        // The module a type is defined in ("Runtime", "Editor", "Sandbox", ...). Drives both the
        // native thunk's export macro (RUNTIME_API/EDITOR_API/...) and the C# P/Invoke library key,
        // so any module's reflected types can carry C# bindings, not just Runtime.
        eastl::string ModuleOf(const FReflectedType& Type)
        {
            if (Type.Header != nullptr && Type.Header->Project != nullptr)
            {
                return Type.Header->Project->Name;
            }
            return "Runtime";
        }

        // C++ "Lumina::FVector3" -> C# "Lumina.FVector3".
        eastl::string ToCSharpName(eastl::string Name)
        {
            for (size_t Pos = Name.find("::"); Pos != eastl::string::npos; Pos = Name.find("::"))
            {
                Name.replace(Pos, 2, ".");
            }
            return Name;
        }

        bool IsCSharpKeyword(const eastl::string& Word)
        {
            static const char* Keywords[] = {
                "abstract","as","base","bool","break","byte","case","catch","char","checked","class",
                "const","continue","decimal","default","delegate","do","double","else","enum","event",
                "explicit","extern","false","finally","fixed","float","for","foreach","goto","if",
                "implicit","in","int","interface","internal","is","lock","long","namespace","new","null",
                "object","operator","out","override","params","private","protected","public","readonly",
                "ref","return","sbyte","sealed","short","sizeof","stackalloc","static","string","struct",
                "switch","this","throw","true","try","typeof","uint","ulong","unchecked","unsafe","ushort",
                "using","virtual","void","volatile","while",
            };
            for (const char* K : Keywords)
            {
                if (Word == K)
                {
                    return true;
                }
            }
            return false;
        }

        eastl::string SafeIdentifier(const eastl::string& Name)
        {
            return IsCSharpKeyword(Name) ? ("@" + Name) : Name;
        }

        // Maps a property kind (FReflectedProperty::GetTypeName) to a C# primitive. Returns true with
        // the size/alignment for blittable primitives; false for non-primitive kinds.
        bool PrimitiveCSharp(const eastl::string& Kind, eastl::string& OutCSharp, int& OutSize, int& OutAlign)
        {
            struct FEntry { const char* Kind; const char* CSharp; int Size; int Align; };
            static const FEntry Table[] = {
                { "Int8",   "sbyte",  1, 1 }, { "UInt8",  "byte",   1, 1 }, { "Bool",   "bool",  1, 1 },
                { "Int16",  "short",  2, 2 }, { "UInt16", "ushort", 2, 2 },
                { "Int32",  "int",    4, 4 }, { "UInt32", "uint",   4, 4 }, { "Float",  "float", 4, 4 },
                { "Int64",  "long",   8, 8 }, { "UInt64", "ulong",  8, 8 }, { "Double", "double",8, 8 },
            };
            for (const FEntry& E : Table)
            {
                if (Kind == E.Kind)
                {
                    OutCSharp = E.CSharp;
                    OutSize = E.Size;
                    OutAlign = E.Align;
                    return true;
                }
            }
            return false;
        }

        int AlignUp(int Offset, int Align)
        {
            return (Offset + Align - 1) & ~(Align - 1);
        }

        // Computes the C# Sequential layout (== C++ layout) of a struct if every reflected field is
        // blittable (primitive / int-backed enum / nested blittable struct). Returns false otherwise.
        bool ComputeBlittableLayout(const FReflectedStruct& Struct, const FReflectionDatabase& Db, int& OutSize, int& OutAlign, int Depth)
        {
            if (Depth > 16)
            {
                return false;
            }

            int Offset = 0;
            int MaxAlign = 1;
            for (const auto& Prop : Struct.Props)
            {
                if (Prop->bInner)
                {
                    continue;
                }

                const char* KindPtr = Prop->GetTypeName();
                if (KindPtr == nullptr)
                {
                    return false; // Array / Optional report a null kind -> not blittable
                }
                const eastl::string Kind = KindPtr;
                int Size = 0;
                int Align = 0;
                eastl::string Unused;

                if (PrimitiveCSharp(Kind, Unused, Size, Align))
                {
                    // blittable primitive
                }
                else if (Kind == "Enum")
                {
                    // Assume int-backed; a non-int underlying type trips the native size assert -> excluded.
                    Size = 4;
                    Align = 4;
                }
                else if (Kind == "Struct")
                {
                    const FReflectedStruct* Nested = Db.GetReflectedType<FReflectedStruct>(FStringHash(Prop->TypeName));
                    int NestedSize = 0;
                    int NestedAlign = 0;
                    if (Nested == nullptr || !ComputeBlittableLayout(*Nested, Db, NestedSize, NestedAlign, Depth + 1))
                    {
                        return false;
                    }
                    Size = NestedSize;
                    Align = NestedAlign;
                }
                else
                {
                    return false; // String / Object / Array / etc. -> not blittable
                }

                Offset = AlignUp(Offset, Align) + Size;
                MaxAlign = (Align > MaxAlign) ? Align : MaxAlign;
            }

            OutAlign = MaxAlign;
            OutSize = AlignUp(Offset, MaxAlign);
            return true;
        }

        // The C# field type for a blittable struct member.
        eastl::string CSharpFieldType(FReflectedProperty& Prop)
        {
            const eastl::string Kind = Prop.GetTypeName();
            eastl::string CSharp;
            int Size = 0;
            int Align = 0;
            if (PrimitiveCSharp(Kind, CSharp, Size, Align))
            {
                return CSharp;
            }
            if (Kind == "Enum" || Kind == "Struct")
            {
                return ToCSharpName(Prop.TypeName);
            }
            return "int"; // unreachable for blittable structs
        }

        void OpenNamespace(FCodeWriter& Writer, const eastl::string& CppNamespace, bool& bOpened)
        {
            const eastl::string Ns = ToCSharpName(CppNamespace);
            bOpened = !Ns.empty();
            if (bOpened)
            {
                Writer.Linef("namespace %s", Ns.c_str());
                Writer.BeginBlock();
            }
        }

        // C# wrapper read-only, decoupled from the editor flags: ScriptReadOnly forces getter-only even on
        // an editor-editable property; ScriptWritable forces a setter even on an editor ReadOnly/Const one;
        // otherwise the editor ReadOnly/Const flags apply (the historical behavior).
        bool IsReadOnlyProp(const FReflectedProperty& Prop)
        {
            if (EnumHasAnyFlags(Prop.PropertyFlags, EPropertyFlags::ScriptReadOnly))
            {
                return true;
            }
            if (EnumHasAnyFlags(Prop.PropertyFlags, EPropertyFlags::ScriptWritable))
            {
                return false;
            }
            return EnumHasAnyFlags(Prop.PropertyFlags, EPropertyFlags::ReadOnly | EPropertyFlags::Const);
        }

        // A property explicitly hidden from the C# layer (PROPERTY(ScriptHidden) / NotScriptable): no
        // wrapper member is emitted for it at all.
        bool IsScriptHidden(const FReflectedProperty& Prop)
        {
            return EnumHasAnyFlags(Prop.PropertyFlags, EPropertyFlags::ScriptHidden);
        }
        
        void EmitThunkField(FCodeWriter& Writer, const eastl::string& Module, const eastl::string& EntryPoint,
            const eastl::string& Fn, const eastl::string& ParamTypesCsv, const eastl::string& RetType)
        {
            const eastl::string Sig = ParamTypesCsv.empty() ? RetType : (ParamTypesCsv + ", " + RetType);
            Writer.Linef("private static readonly delegate* unmanaged[Cdecl]<%s> %s =", Sig.c_str(), Fn.c_str());
            Writer.Linef("    (delegate* unmanaged[Cdecl]<%s>)global::LuminaSharp.NativeBindings.Resolve(\"%s\", \"%s\");",
                Sig.c_str(), Module.c_str(), EntryPoint.c_str());
        }

        // A reflected type that exists in the database and is allowed in the C# API.
        bool IsExposed(const FReflectionDatabase& Db, const eastl::string& Qualified)
        {
            const FReflectedType* T = Db.GetReflectedType<FReflectedType>(FStringHash(Qualified));
            return T != nullptr && !IsExcludedFromCSharp(*T);
        }

        // "global::" + C# form of a qualified C++ name ("Lumina::CStaticMesh" -> "global::Lumina.CStaticMesh").
        eastl::string GlobalCSharp(const eastl::string& Qualified)
        {
            return "global::" + ToCSharpName(Qualified);
        }

        // A reflected type that becomes an opaque handle wrapper (a C# class deriving NativeObject/
        // NativeStruct), i.e. a class, or a struct that is NOT a blittable value mirror. Such a type
        // is a valid C# *base class* for another wrapper.
        bool IsOpaqueWrapperType(const FReflectionDatabase& Db, const eastl::string& Qualified)
        {
            const FReflectedType* T = Db.GetReflectedType<FReflectedType>(FStringHash(Qualified));
            if (T == nullptr || IsExcludedFromCSharp(*T) || T->Type == FReflectedType::EType::Enum)
            {
                return false;
            }
            if (T->Type == FReflectedType::EType::Class)
            {
                return true; // classes are always opaque wrappers
            }
            const FReflectedStruct* S = static_cast<const FReflectedStruct*>(T);
            int Size = 0;
            int Align = 0;
            if (HasMetadata(*S, "ManualStub"))
            {
                return false; // hand-written value type
            }
            if (HasMetadata(*S, "ScriptStruct") && ComputeBlittableLayout(*S, Db, Size, Align, 0))
            {
                return false; // blittable value mirror
            }
            return true;
        }

        // The C# base for an opaque wrapper: the reflected parent's wrapper when it's an opaque type,
        // otherwise the given Native* root. Mirrors the C++ single-inheritance chain so e.g. a
        // CStaticMesh wrapper derives the CObject wrapper and inherits its bound members.
        eastl::string CSharpBase(const FReflectedStruct& Type, const FReflectionDatabase& Db, const char* Root)
        {
            const eastl::string& Parent = Type.Parent;
            if (!Parent.empty())
            {
                const bool bQualified = Parent.find("::") != eastl::string::npos;
                const eastl::string Qualified = (bQualified || Type.Namespace.empty())
                    ? Parent
                    : (Type.Namespace + "::" + Parent);
                if (IsOpaqueWrapperType(Db, Qualified))
                {
                    return GlobalCSharp(Qualified);
                }
            }
            return Root;
        }

        // How a property is surfaced to C#. Drives BOTH the managed member and the native thunk, so
        // the two stay in lock-step: classify once, emit on each side from the same result.
        enum class EBind { None, Number, Bool, Enum, Str, Object, StructValue, StructOpaque, Array };

        struct FBinding
        {
            EBind         Kind = EBind::None;
            eastl::string CSharp;     // C# property type (for Array: the element's C# type)
            eastl::string Cpp;        // by-value thunk C++ type for Number ("int32", "double", ...)
            eastl::string TargetCpp;  // qualified C++ type for Enum/Object/StructValue ("Lumina::CStaticMesh")
            bool          bIsName = false;   // Str: FName (vs FString)
            bool          bReadOnly = false;
            eastl::unique_ptr<FBinding> Elem; // Array: the element binding (one of the scalar kinds above)
        };

        bool NumericCpp(const eastl::string& Kind, eastl::string& OutCSharp, eastl::string& OutCpp)
        {
            struct FEntry { const char* Kind; const char* CSharp; const char* Cpp; };
            static const FEntry Table[] = {
                { "Int8","sbyte","int8" }, { "Int16","short","int16" }, { "Int32","int","int32" }, { "Int64","long","int64" },
                { "UInt8","byte","uint8" }, { "UInt16","ushort","uint16" }, { "UInt32","uint","uint32" }, { "UInt64","ulong","uint64" },
                { "Float","float","float" }, { "Double","double","double" },
            };
            for (const FEntry& E : Table)
            {
                if (Kind == E.Kind) { OutCSharp = E.CSharp; OutCpp = E.Cpp; return true; }
            }
            return false;
        }

        // Classifies a TVector element by its C++ type spelling (e.g. "int32", "FString", "FNavTileData").
        // Covers the same scalar kinds as Classify minus object elements (can't tell TObjectPtr<T> from a
        // by-value T from the name alone, and a wrong guess would not compile). Unknowns return false so
        // the whole array is skipped, never a broken binding.
        bool ClassifyElement(const eastl::string& RawElem, const eastl::string& Ns, const FReflectionDatabase& Db, FBinding& B)
        {
            eastl::string Bare = RawElem;
            if (Bare.find("Lumina::") == 0)
            {
                Bare = Bare.substr(8);
            }

            struct FEntry { const char* Name; const char* CSharp; const char* Cpp; };
            static const FEntry Numeric[] = {
                { "int8","sbyte","int8" }, { "int16","short","int16" }, { "int32","int","int32" }, { "int64","long","int64" },
                { "uint8","byte","uint8" }, { "uint16","ushort","uint16" }, { "uint32","uint","uint32" }, { "uint64","ulong","uint64" },
                { "float","float","float" }, { "double","double","double" },
            };
            for (const FEntry& E : Numeric)
            {
                if (Bare == E.Name) { B.Kind = EBind::Number; B.CSharp = E.CSharp; B.Cpp = E.Cpp; return true; }
            }
            if (Bare == "bool") { B.Kind = EBind::Bool; B.CSharp = "bool"; return true; }
            if (Bare == "FString" || Bare == "FName") { B.Kind = EBind::Str; B.CSharp = "string"; B.bIsName = (Bare == "FName"); return true; }

            eastl::string Q = RawElem;
            const FReflectedType* T = Db.GetReflectedType<FReflectedType>(FStringHash(Q));
            if (T == nullptr && Q.find("::") == eastl::string::npos && !Ns.empty())
            {
                Q = Ns + "::" + RawElem;
                T = Db.GetReflectedType<FReflectedType>(FStringHash(Q));
            }
            if (T == nullptr)
            {
                return false;
            }
            if (T->Type == FReflectedType::EType::Enum)
            {
                if (IsExcludedFromCSharp(*T))
                {
                    return false;
                }
                B.Kind = EBind::Enum;
                B.CSharp = GlobalCSharp(Q);
                B.TargetCpp = Q;
                return true;
            }
            if (T->Type == FReflectedType::EType::Structure)
            {
                const FReflectedStruct* S = static_cast<const FReflectedStruct*>(T);
                int Size = 0;
                int Align = 0;
                if (HasMetadata(*S, "ManualStub") || (HasMetadata(*S, "ScriptStruct") && ComputeBlittableLayout(*S, Db, Size, Align, 0)))
                {
                    B.Kind = EBind::StructValue;
                    B.CSharp = GlobalCSharp(Q);
                    B.TargetCpp = Q;
                    return true;
                }
                if (!IsExcludedFromCSharp(*S))
                {
                    B.Kind = EBind::StructOpaque;
                    B.CSharp = GlobalCSharp(Q);
                    B.TargetCpp = Q;
                    return true;
                }
            }
            return false; // classes (object elements) and anything else: skipped
        }

        // Classifies a single (non-inner) property into its C# binding. Returns false for kinds with no
        // binding (custom-accessor props, soft-objects, optionals here, unexposed targets).
        bool Classify(FReflectedProperty& Prop, const eastl::string& OwnerNs, const FReflectionDatabase& Db, FBinding& B)
        {
            if (EnumHasAnyFlags(Prop.PropertyFlags, EPropertyFlags::Private) ||
                EnumHasAnyFlags(Prop.PropertyFlags, EPropertyFlags::Protected))
            {
                return false; // a free-function thunk can't reach a non-public member
            }
            if (!Prop.GetterFunc.empty() || !Prop.SetterFunc.empty())
            {
                return false; // custom accessors -> route through FProperty later
            }

            if (auto* Arr = dynamic_cast<FReflectedArrayProperty*>(&Prop))
            {
                auto Elem = eastl::make_unique<FBinding>();
                if (!ClassifyElement(Arr->ElementTypeName, OwnerNs, Db, *Elem))
                {
                    return false; // element kind we don't bind -> skip the whole array
                }
                B.Kind = EBind::Array;
                B.CSharp = Elem->CSharp;  // element C# type; the property type is NativeReadOnlyList<this>
                B.bReadOnly = true;       // read-only view this pass (no add/remove/set)
                B.Elem = eastl::move(Elem);
                return true;
            }

            const char* KindPtr = Prop.GetTypeName();
            if (KindPtr == nullptr)
            {
                return false; // Optional (null kind) -> not bound yet
            }
            const eastl::string Kind = KindPtr;
            B.bReadOnly = IsReadOnlyProp(Prop);

            if (NumericCpp(Kind, B.CSharp, B.Cpp))
            {
                B.Kind = EBind::Number;
                return true;
            }
            if (Kind == "Bool")
            {
                B.Kind = EBind::Bool;
                B.CSharp = "bool";
                return true;
            }
            if (Kind == "Enum")
            {
                if (!IsExposed(Db, Prop.TypeName))
                {
                    return false;
                }
                B.Kind = EBind::Enum;
                B.CSharp = GlobalCSharp(Prop.TypeName);
                B.TargetCpp = Prop.TypeName;
                return true;
            }
            if (Kind == "FString" || Kind == "FName")
            {
                B.Kind = EBind::Str;
                B.CSharp = "string";
                B.bIsName = (Kind == "FName");
                return true;
            }
            if (Kind == "Object")
            {
                B.Kind = EBind::Object;
                B.TargetCpp = Prop.TypeName;
                B.CSharp = IsExposed(Db, Prop.TypeName) ? GlobalCSharp(Prop.TypeName) : "global::LuminaSharp.NativeObject";
                // Settable now via the type-erased LuminaSharp_SetObjectPtr helper (the element type need
                // not be complete here); respect only the property's real const/readonly flags.
                B.bReadOnly = IsReadOnlyProp(Prop);
                return true;
            }
            if (Kind == "Struct")
            {
                const FReflectedStruct* S = Db.GetReflectedType<FReflectedStruct>(FStringHash(Prop.TypeName));
                if (S == nullptr)
                {
                    return false;
                }
                B.TargetCpp = Prop.TypeName;
                int Size = 0;
                int Align = 0;
                const bool bValue = HasMetadata(*S, "ManualStub")   // hand-written blittable value type (math)
                    || (HasMetadata(*S, "ScriptStruct") && ComputeBlittableLayout(*S, Db, Size, Align, 0));
                if (bValue)
                {
                    B.Kind = EBind::StructValue;
                    B.CSharp = GlobalCSharp(Prop.TypeName);
                    return true;
                }
                if (!IsExcludedFromCSharp(*S))
                {
                    B.Kind = EBind::StructOpaque;   // wrapper viewing the embedded struct; read-only
                    B.CSharp = GlobalCSharp(Prop.TypeName);
                    B.bReadOnly = true;
                    return true;
                }
                return false;
            }
            return false; // SoftObject and anything else: not bound yet
        }

        // A read-only TVector<T> view: NativeReadOnlyList<element> backed by count + per-index thunks.
        void EmitCSharpArray(FCodeWriter& Writer, FReflectedProperty& Prop, const FBinding& B,
            const eastl::string& Friendly, const eastl::string& Module)
        {
            const eastl::string& Member = Prop.Name;
            const eastl::string PropName = SafeIdentifier(Member);
            const eastl::string CountFn = "__count_" + Member;
            const eastl::string GetAtFn = "__getat_" + Member;
            const eastl::string CountThunk = "LuminaSharp_Count_" + Friendly + "_" + Member;
            const eastl::string GetAtThunk = "LuminaSharp_GetAt_" + Friendly + "_" + Member;
            const eastl::string& ECS = B.Elem->CSharp;

            eastl::string Lambda;
            eastl::string GetAtRet;
            bool bStr = false;
            switch (B.Elem->Kind)
            {
                case EBind::Number:
                case EBind::StructValue:
                    Lambda = "__i => " + GetAtFn + "(Handle, __i)";
                    GetAtRet = ECS;
                    break;
                case EBind::Bool:
                    Lambda = "__i => " + GetAtFn + "(Handle, __i) != 0";
                    GetAtRet = "byte";
                    break;
                case EBind::Enum:
                    Lambda = "__i => (" + ECS + ")" + GetAtFn + "(Handle, __i)";
                    GetAtRet = "int";
                    break;
                case EBind::StructOpaque:
                    Lambda = "__i => new " + ECS + "(" + GetAtFn + "(Handle, __i))";
                    GetAtRet = "System.IntPtr";
                    break;
                case EBind::Str:
                    bStr = true;
                    Lambda = "__i => { int __n = " + GetAtFn + "(Handle, __i, (byte*)null, 0); if (__n <= 0) return string.Empty;"
                        " byte[] __b = new byte[__n]; fixed (byte* __p = __b) { " + GetAtFn + "(Handle, __i, __p, __n); }"
                        " return global::System.Text.Encoding.UTF8.GetString(__b); }";
                    GetAtRet = "int";
                    break;
                default:
                    return;
            }

            Writer.Linef("public global::LuminaSharp.NativeReadOnlyList<%s> %s =>", ECS.c_str(), PropName.c_str());
            Writer.Linef("    new global::LuminaSharp.NativeReadOnlyList<%s>(%s(Handle), %s);", ECS.c_str(), CountFn.c_str(), Lambda.c_str());

            EmitThunkField(Writer, Module, CountThunk, CountFn, "System.IntPtr", "int");
            if (bStr)
            {
                EmitThunkField(Writer, Module, GetAtThunk, GetAtFn, "System.IntPtr, int, byte*, int", "int");
            }
            else
            {
                EmitThunkField(Writer, Module, GetAtThunk, GetAtFn, "System.IntPtr, int", GetAtRet);
            }
        }

        void EmitNativeArray(FCodeWriter& Writer, FReflectedProperty& Prop, const FBinding& B,
            const eastl::string& Friendly, const char* Qualified, const char* Api)
        {
            const eastl::string& Member = Prop.Name;
            const char* M = Member.c_str();
            const eastl::string CountThunk = "LuminaSharp_Count_" + Friendly + "_" + Member;
            const eastl::string GetAtThunk = "LuminaSharp_GetAt_" + Friendly + "_" + Member;
            const char* G = GetAtThunk.c_str();

            Writer.Linef("extern \"C\" %s int %s(%s* Self) { return (int)Self->%s.size(); }",
                Api, CountThunk.c_str(), Qualified, M);

            switch (B.Elem->Kind)
            {
                case EBind::Number:
                    Writer.Linef("extern \"C\" %s %s %s(%s* Self, int Index) { if (Index < 0 || Index >= (int)Self->%s.size()) return 0; return Self->%s[(size_t)Index]; }",
                        Api, B.Elem->Cpp.c_str(), G, Qualified, M, M);
                    break;
                case EBind::Bool:
                    Writer.Linef("extern \"C\" %s unsigned char %s(%s* Self, int Index) { if (Index < 0 || Index >= (int)Self->%s.size()) return 0; return Self->%s[(size_t)Index] ? 1 : 0; }",
                        Api, G, Qualified, M, M);
                    break;
                case EBind::Enum:
                    Writer.Linef("extern \"C\" %s int %s(%s* Self, int Index) { if (Index < 0 || Index >= (int)Self->%s.size()) return 0; return (int)Self->%s[(size_t)Index]; }",
                        Api, G, Qualified, M, M);
                    break;
                case EBind::StructValue:
                    Writer.Linef("extern \"C\" %s %s %s(%s* Self, int Index) { if (Index < 0 || Index >= (int)Self->%s.size()) return %s{}; return Self->%s[(size_t)Index]; }",
                        Api, B.Elem->TargetCpp.c_str(), G, Qualified, M, B.Elem->TargetCpp.c_str(), M);
                    break;
                case EBind::StructOpaque:
                    Writer.Linef("extern \"C\" %s void* %s(%s* Self, int Index) { if (Index < 0 || Index >= (int)Self->%s.size()) return nullptr; return (void*)&Self->%s[(size_t)Index]; }",
                        Api, G, Qualified, M, M);
                    break;
                case EBind::Str:
                    Writer.Linef("extern \"C\" %s int %s(%s* Self, int Index, char* Buffer, int Capacity) "
                        "{ if (Index < 0 || Index >= (int)Self->%s.size()) return 0; const char* S = Self->%s[(size_t)Index].c_str(); "
                        "int L = S ? (int)Self->%s[(size_t)Index].length() : 0; "
                        "if (S && Buffer && Capacity > 0) { int N = L < Capacity ? L : Capacity; for (int i = 0; i < N; ++i) Buffer[i] = S[i]; } return L; }",
                        Api, G, Qualified, M, M, M);
                    break;
                default:
                    break;
            }
        }

        // The blittable-field offset static: resolved ONCE per property from live reflection (no native
        // export per property). C# then reads/writes native memory directly at (Handle + offset).
        void EmitOffsetField(FCodeWriter& Writer, const eastl::string& OffName, const eastl::string& TypeName, const eastl::string& Member)
        {
            Writer.Linef("private static readonly nint %s = (nint)global::LuminaSharp.NativeBindings.PropertyOffset(\"%s\", \"%s\");",
                OffName.c_str(), TypeName.c_str(), Member.c_str());
        }

        // The non-blittable-field token static: the FProperty* resolved ONCE per property, reused by the
        // generic per-type exporters (PropGet/SetString/Name/Object).
        void EmitTokenField(FCodeWriter& Writer, const eastl::string& PropFieldName, const eastl::string& TypeName, const eastl::string& Member)
        {
            Writer.Linef("private static readonly System.IntPtr %s = global::LuminaSharp.NativeBindings.FindProperty(\"%s\", \"%s\");",
                PropFieldName.c_str(), TypeName.c_str(), Member.c_str());
        }

        // The managed property on an opaque wrapper. Blittable kinds (Number/Bool/Enum/StructValue/
        // StructOpaque) read/write native memory directly at the property's runtime-resolved offset (zero
        // boundary crossing, zero per-property export). Non-blittable kinds (Str/Object) route through the
        // fixed generic per-type exporters with the property's FProperty* token resolved once. Array still
        // uses per-property thunks (the follow-up). TypeName is the reflected type's simple name, which the
        // native FindProperty/PropertyOffset resolve via FindObject (mirrors the component-ops lookup).
        void EmitCSharpMember(FCodeWriter& Writer, FReflectedProperty& Prop, const FBinding& B,
            const eastl::string& Friendly, const eastl::string& Module, const eastl::string& TypeName)
        {
            const eastl::string& Member = Prop.Name;
            const eastl::string PropName = SafeIdentifier(Member);
            const eastl::string OffName = "__off_" + Member;
            const eastl::string PropFieldName = "__prop_" + Member;
            const bool bRO = B.bReadOnly;
            const char* CS = B.CSharp.c_str();

            switch (B.Kind)
            {
                case EBind::Number:
                case EBind::Bool:
                case EBind::Enum:
                case EBind::StructValue:
                {
                    // For Bool the C# type is bool (1 byte); for Enum it's the enum mirror (its underlying
                    // width matches native); both are blittable so Unsafe.Read/Write at the offset is exact.
                    const char* T = (B.Kind == EBind::Bool) ? "bool" : CS;
                    Writer.Linef("public %s %s", T, PropName.c_str());
                    Writer.BeginBlock();
                    Writer.Linef("get => global::System.Runtime.CompilerServices.Unsafe.ReadUnaligned<%s>((void*)((nint)Handle + %s));",
                        T, OffName.c_str());
                    if (!bRO)
                    {
                        Writer.Linef("set => global::System.Runtime.CompilerServices.Unsafe.WriteUnaligned((void*)((nint)Handle + %s), value);",
                            OffName.c_str());
                    }
                    Writer.EndBlock();
                    EmitOffsetField(Writer, OffName, TypeName, Member);
                    break;
                }
                case EBind::Str:
                {
                    const char* Get = B.bIsName ? "PropGetName" : "PropGetString";
                    const char* Set = B.bIsName ? "PropSetName" : "PropSetString";
                    Writer.Linef("public string %s", PropName.c_str());
                    Writer.BeginBlock();
                    Writer.Linef("get => global::LuminaSharp.Native.%s(Handle, %s);", Get, PropFieldName.c_str());
                    if (!bRO)
                    {
                        Writer.Linef("set => global::LuminaSharp.Native.%s(Handle, %s, value);", Set, PropFieldName.c_str());
                    }
                    Writer.EndBlock();
                    EmitTokenField(Writer, PropFieldName, TypeName, Member);
                    break;
                }
                case EBind::Object:
                {
                    Writer.Linef("public %s %s", CS, PropName.c_str());
                    Writer.BeginBlock();
                    Writer.Line("get");
                    Writer.BeginBlock();
                    Writer.Linef("System.IntPtr __h = global::LuminaSharp.Native.PropGetObject(Handle, %s);", PropFieldName.c_str());
                    Writer.Linef("return __h == System.IntPtr.Zero ? null : new %s(__h);", CS);
                    Writer.EndBlock();
                    if (!bRO)
                    {
                        Writer.Linef("set => global::LuminaSharp.Native.PropSetObject(Handle, %s, value is null ? System.IntPtr.Zero : value.Handle);",
                            PropFieldName.c_str());
                    }
                    Writer.EndBlock();
                    EmitTokenField(Writer, PropFieldName, TypeName, Member);
                    break;
                }
                case EBind::StructOpaque:
                {
                    // A wrapper viewing the embedded struct in place at the offset (read-only). No export.
                    Writer.Linef("public %s %s => new %s((nint)Handle + %s);", CS, PropName.c_str(), CS, OffName.c_str());
                    EmitOffsetField(Writer, OffName, TypeName, Member);
                    break;
                }
                case EBind::Array:
                    EmitCSharpArray(Writer, Prop, B, Friendly, Module);
                    break;
                case EBind::None:
                    break;
            }
        }

        // The native `extern "C" <API>` thunk(s) backing one bound property. Only Array still needs them:
        // the blittable kinds are read/written directly at the offset from C#, and the non-blittable kinds
        // (Str/Object) route through the fixed generic per-type exporters in DotNetProperty.cpp. So the
        // per-property native-export count is now O(array properties), not O(properties).
        void EmitNativeThunk(FCodeWriter& Writer, FReflectedProperty& Prop, const FBinding& B,
            const eastl::string& Friendly, const char* Qualified, const char* Api)
        {
            if (B.Kind == EBind::Array)
            {
                EmitNativeArray(Writer, Prop, B, Friendly, Qualified, Api);
            }
        }

        // Emits the managed members for every bindable property on an opaque wrapper.
        void EmitProperties(FCodeWriter& Writer, const FReflectedStruct& Type, const FReflectionDatabase& Db)
        {
            const eastl::string Friendly = Names::FriendlyFromQualified(Type.QualifiedName);
            const eastl::string Module = ModuleOf(Type);
            for (const auto& Prop : Type.Props)
            {
                if (Prop->bInner || IsScriptHidden(*Prop))
                {
                    continue;
                }
                FBinding B;
                if (Classify(*Prop, Type.Namespace, Db, B))
                {
                    EmitCSharpMember(Writer, *Prop, B, Friendly, Module, Type.DisplayName);
                }
            }
        }

        // "a" / 3 -> "a3". Used to name generated function parameters.
        eastl::string ArgIndexName(char Prefix, size_t I)
        {
            eastl::string Out;
            Out += Prefix;
            char Buf[12];
            int P = 11;
            Buf[P] = 0;
            if (I == 0)
            {
                Out += '0';
                return Out;
            }
            while (I > 0)
            {
                Buf[--P] = static_cast<char>('0' + (I % 10));
                I /= 10;
            }
            Out += (Buf + P);
            return Out;
        }

        // One function parameter / return classification (by-value-safe kinds only).
        struct FArg
        {
            EBind         Kind = EBind::None;
            eastl::string CSharp;
            eastl::string Cpp;
            eastl::string TargetCpp;
            bool          bEntity = false; // entt::entity: ABI-marshalled as uint32, surfaced as C# Entity.
            bool          bIsName = false; // EBind::Str arg: FName (true) vs FString (false), for the native ctor.
        };

        // Type spelling with const / & / * / whitespace removed: "const FName &" -> "FName".
        eastl::string StripQualifiers(const eastl::string& T)
        {
            eastl::string Out;
            for (char C : T)
            {
                if (C != ' ' && C != '\t' && C != '&' && C != '*')
                {
                    Out += C;
                }
            }
            for (size_t P; (P = Out.find("const")) != eastl::string::npos; )
            {
                Out.erase(P, 5);
            }
            return Out;
        }

        // Classifies a function parameter / return by its reflected type flag. Conservative: only
        // by-value-safe kinds (numeric / bool / enum / blittable value struct). Pointers are rejected
        // (ambiguous marshaling); a mutable-reference *arg* is rejected (a by-value can't bind it),
        // but a const-ref arg and a (copied) ref return are fine. For primitive kinds the raw spelling
        // must actually BE that primitive, so strong types that classify as an int (entt::entity, scoped
        // enums typedef'd to ints) are skipped rather than passed an incompatible int.
        bool ClassifyField(const FFieldInfo& F, const FReflectionDatabase& Db, bool bIsArg, FArg& B)
        {
            if (F.RawFieldType.find('*') != eastl::string::npos)
            {
                return false; // pointer in/out -> ambiguous marshaling
            }
            if (bIsArg && F.RawFieldType.find('&') != eastl::string::npos
                && F.RawFieldType.find("const") == eastl::string::npos)
            {
                return false; // mutable-reference arg can't take a by-value
            }

            const eastl::string Bare = StripQualifiers(F.RawFieldType);

            // entt::entity (a scoped enum over uint32) marshals as the C# Entity handle. Checked before the
            // numeric/enum classification below, where it would otherwise be skipped (it classifies as an
            // int but spells "entt::entity"). The thunk passes it as a raw uint32; bEntity drives the C#
            // Entity wrapper + the native static_cast back to entt::entity.
            if (Bare == "entt::entity")
            {
                B.Kind = EBind::Number;
                B.CSharp = "global::LuminaSharp.Entity";
                B.Cpp = "uint32";
                B.bEntity = true;
                return true;
            }

            struct FNum { EPropertyTypeFlags Flag; const char* CSharp; const char* Cpp; };
            static const FNum Numerics[] = {
                { EPropertyTypeFlags::Int8,   "sbyte",  "int8" },   { EPropertyTypeFlags::Int16,  "short",  "int16" },
                { EPropertyTypeFlags::Int32,  "int",    "int32" },  { EPropertyTypeFlags::Int64,  "long",   "int64" },
                { EPropertyTypeFlags::UInt8,  "byte",   "uint8" },  { EPropertyTypeFlags::UInt16, "ushort", "uint16" },
                { EPropertyTypeFlags::UInt32, "uint",   "uint32" }, { EPropertyTypeFlags::UInt64, "ulong",  "uint64" },
                { EPropertyTypeFlags::Float,  "float",  "float" },  { EPropertyTypeFlags::Double, "double", "double" },
            };
            for (const FNum& N : Numerics)
            {
                if (F.Flags == N.Flag)
                {
                    if (Bare != N.Cpp)
                    {
                        return false; // strong type spelled differently than its int -> skip
                    }
                    B.Kind = EBind::Number; B.CSharp = N.CSharp; B.Cpp = N.Cpp;
                    return true;
                }
            }

            switch (F.Flags)
            {
                case EPropertyTypeFlags::Bool:
                    if (Bare != "bool" && Bare != "_Bool")
                    {
                        return false;
                    }
                    B.Kind = EBind::Bool; B.CSharp = "bool"; return true;
                case EPropertyTypeFlags::Enum:
                    if (!IsExposed(Db, F.TypeName))
                    {
                        return false;
                    }
                    B.Kind = EBind::Enum; B.CSharp = GlobalCSharp(F.TypeName); B.TargetCpp = F.TypeName; return true;
                case EPropertyTypeFlags::Struct:
                {
                    const FReflectedStruct* S = Db.GetReflectedType<FReflectedStruct>(FStringHash(F.TypeName));
                    if (S == nullptr)
                    {
                        return false;
                    }
                    int Size = 0;
                    int Align = 0;
                    if (HasMetadata(*S, "ManualStub") || (HasMetadata(*S, "ScriptStruct") && ComputeBlittableLayout(*S, Db, Size, Align, 0)))
                    {
                        B.Kind = EBind::StructValue; B.CSharp = GlobalCSharp(F.TypeName); B.TargetCpp = F.TypeName; return true;
                    }
                    return false; // opaque struct by value isn't supported as a function param/return yet
                }
                case EPropertyTypeFlags::Name:
                case EPropertyTypeFlags::String:
                    // FName/FString as an ARG marshal as a C# string (UTF-8 byte* + len at the thunk). A
                    // string RETURN needs the 2-pass caller-buffer protocol, not wired for functions yet.
                    if (!bIsArg)
                    {
                        return false;
                    }
                    B.Kind = EBind::Str; B.CSharp = "string"; B.bIsName = (F.Flags == EPropertyTypeFlags::Name);
                    return true;
                default:
                    return false; // Object / Vector / SoftObject / Optional / etc.: not in functions yet
            }
        }

        struct FFnBinding
        {
            bool                bVoid = true;
            FArg                Ret;
            eastl::vector<FArg> Args;
        };

        // Binds a reflected function only when the name is unique in the type (no overloads -> no C#
        // method-name collision) and every arg + the return is a by-value-safe kind.
        bool ClassifyFunction(const FReflectedFunction& Fn, const FReflectedStruct& Type, const FReflectionDatabase& Db, FFnBinding& Out)
        {
            int NameCount = 0;
            for (const auto& Other : Type.Functions)
            {
                if (Other->Name == Fn.Name)
                {
                    ++NameCount;
                }
            }
            if (NameCount != 1)
            {
                return false; // overloaded -> skip
            }
            if (Fn.bHasOmittedArgs)
            {
                return false; // an unsupported arg was dropped (LRT1005); the real signature has more args
            }

            if (Fn.Return.has_value())
            {
                if (!ClassifyField(*Fn.Return, Db, false, Out.Ret))
                {
                    return false;
                }
                Out.bVoid = false;
            }
            for (const FFieldInfo& Arg : Fn.Arguments)
            {
                FArg A;
                if (!ClassifyField(Arg, Db, true, A))
                {
                    return false;
                }
                Out.Args.push_back(A);
            }
            return true;
        }
        
        void EmitCSharpFunction(FCodeWriter& Writer, const FReflectedFunction& Fn, const FFnBinding& FB,
            const eastl::string& Friendly, const eastl::string& Module)
        {
            const eastl::string& Name = Fn.Name;
            const eastl::string CallThunk = "LuminaSharp_Call_" + Friendly + "_" + Name;

            eastl::string SigParams;
            for (size_t i = 0; i < FB.Args.size(); ++i)
            {
                if (i != 0) { SigParams += ", "; }
                SigParams += FB.Args[i].CSharp + " " + ArgIndexName('a', i);
            }

            const eastl::string RetCS = FB.bVoid ? eastl::string("void") : FB.Ret.CSharp;

            Writer.Linef("[global::LuminaSharp.NativeCall(Module = \"%s\", EntryPoint = \"%s\")]",
                Module.c_str(), CallThunk.c_str());
            Writer.Linef("public partial %s %s(%s);", RetCS.c_str(), Name.c_str(), SigParams.c_str());
        }

        // The native `extern "C"` thunk that performs the instance call.
        void EmitNativeFunction(FCodeWriter& Writer, const FReflectedFunction& Fn, const FFnBinding& FB,
            const eastl::string& Friendly, const char* Qualified, const char* Api)
        {
            const eastl::string& Name = Fn.Name;
            const eastl::string CallThunk = "LuminaSharp_Call_" + Friendly + "_" + Name;

            eastl::string Params;
            eastl::string CallArgs;
            for (size_t i = 0; i < FB.Args.size(); ++i)
            {
                const FArg& A = FB.Args[i];
                const eastl::string An = ArgIndexName('A', i);
                if (i != 0)
                {
                    Params += ", "; CallArgs += ", ";
                }
                
                if (A.bEntity)
                {
                    Params += "uint32 " + An; CallArgs += "static_cast<entt::entity>(" + An + ")";
                }
                else if (A.Kind == EBind::Str)
                {
                    // (const char* utf8, int len) -> a temporary FName/FString bound to the (const ref) param.
                    Params += "const char* " + An + ", int " + An + "Len";
                    const eastl::string Ctor = A.bIsName ? eastl::string("Lumina::FName") : eastl::string("Lumina::FString");
                    CallArgs += "((" + An + "Len > 0) ? " + Ctor + "(" + An + ", (size_t)" + An + "Len) : " + Ctor + "())";
                }
                else
                {
                    switch (A.Kind)
                    {
                    case EBind::Number:      Params += A.Cpp + " " + An;            CallArgs += An;                            break;
                    case EBind::Bool:        Params += "unsigned char " + An;       CallArgs += "(" + An + " != 0)";           break;
                    case EBind::Enum:        Params += "int " + An;                 CallArgs += "(" + A.TargetCpp + ")" + An;  break;
                    case EBind::StructValue: Params += A.TargetCpp + " " + An;      CallArgs += An;                            break;
                    default: break;
                    }
                }
            }

            const eastl::string CallExpr = "Self->" + Name + "(" + CallArgs + ")";
            eastl::string RetCpp = "void";
            eastl::string Body = CallExpr + ";";
            if (!FB.bVoid)
            {
                if (FB.Ret.bEntity)
                {
                    RetCpp = "uint32"; Body = "return (uint32)(" + CallExpr + ");";
                }
                else
                {
                    switch (FB.Ret.Kind)
                    {
                    case EBind::Number:      RetCpp = FB.Ret.Cpp;       Body = "return " + CallExpr + ";";          break;
                    case EBind::Bool:        RetCpp = "unsigned char";  Body = "return " + CallExpr + " ? 1 : 0;";  break;
                    case EBind::Enum:        RetCpp = "int";            Body = "return (int)" + CallExpr + ";";      break;
                    case EBind::StructValue: RetCpp = FB.Ret.TargetCpp; Body = "return " + CallExpr + ";";          break;
                    default: break;
                    }
                }
            }

            const eastl::string ParamSig = Params.empty() ? eastl::string() : (", " + Params);
            Writer.Linef("extern \"C\" %s %s %s(%s* Self%s) { %s }",
                Api, RetCpp.c_str(), CallThunk.c_str(), Qualified, ParamSig.c_str(), Body.c_str());
        }

        void EmitFunctions(FCodeWriter& Writer, const FReflectedStruct& Type, const FReflectionDatabase& Db)
        {
            const eastl::string Friendly = Names::FriendlyFromQualified(Type.QualifiedName);
            const eastl::string Module = ModuleOf(Type);
            for (const auto& Fn : Type.Functions)
            {
                FFnBinding FB;
                if (ClassifyFunction(*Fn, Type, Db, FB))
                {
                    EmitCSharpFunction(Writer, *Fn, FB, Friendly, Module);
                }
            }
        }

        void EmitNativeFunctions(FCodeWriter& Writer, const FReflectedStruct& Type, const char* Qualified, const char* Api, const FReflectionDatabase& Db)
        {
            const eastl::string Friendly = Names::FriendlyFromQualified(Type.QualifiedName);
            for (const auto& Fn : Type.Functions)
            {
                FFnBinding FB;
                if (ClassifyFunction(*Fn, Type, Db, FB))
                {
                    EmitNativeFunction(Writer, *Fn, FB, Friendly, Qualified, Api);
                }
            }
        }
    }

    bool CSharpBindingEmitter::EmitForEnum(FCodeWriter& Writer, const FReflectedEnum& Enum)
    {
        if (IsExcludedFromCSharp(Enum))
        {
            return false;
        }

        bool bNs = false;
        OpenNamespace(Writer, Enum.Namespace, bNs);

        Writer.Linef("public enum %s", Enum.DisplayName.c_str());
        Writer.BeginBlock();
        for (const FReflectedEnum::FConstant& Constant : Enum.Constants)
        {
            Writer.Linef("%s = %u,", SafeIdentifier(Constant.Label).c_str(), Constant.Value);
        }
        Writer.EndBlock();

        if (bNs)
        {
            Writer.EndBlock();
        }
        Writer.Line();
        return true;
    }

    bool CSharpBindingEmitter::EmitForStruct(FCodeWriter& Writer, const FReflectedStruct& Struct, const FReflectionDatabase& Database)
    {
        if (IsExcludedFromCSharp(Struct))
        {
            return false;
        }

        // A reflected struct becomes a blittable C# VALUE mirror only when it opts in via the
        // `ScriptStruct` metadata AND every field is blittable (a native size assert validates it).
        // Otherwise it's an opaque handle, most reflected structs hold FString/containers/smart-ptrs
        // and would corrupt if mirrored by value, and the Reflector can't see non-PROPERTY fields.
        int Size = 0;
        int Align = 0;
        const bool bBlittable = HasMetadata(Struct, "ScriptStruct")
            && ComputeBlittableLayout(Struct, Database, Size, Align, 0);

        bool bNs = false;
        OpenNamespace(Writer, Struct.Namespace, bNs);

        if (bBlittable)
        {
            Writer.Line("[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]");
            Writer.Linef("public struct %s", Struct.DisplayName.c_str());
            Writer.BeginBlock();
            for (const auto& Prop : Struct.Props)
            {
                if (Prop->bInner)
                {
                    continue;
                }
                Writer.Linef("public %s %s;", CSharpFieldType(*Prop).c_str(), SafeIdentifier(Prop->Name).c_str());
            }
            Writer.EndBlock();
        }
        else
        {
            // Non-blittable (FString/containers/smart-ptrs): an opaque handle, not a value mirror.
            const eastl::string Base = CSharpBase(Struct, Database, "global::LuminaSharp.NativeStruct");
            Writer.Linef("public unsafe partial class %s : %s", Struct.DisplayName.c_str(), Base.c_str());
            Writer.BeginBlock();
            Writer.Linef("internal %s(System.IntPtr handle) : base(handle) { }", Struct.DisplayName.c_str());
            // Components support detached construction (`new T()` -> engine-allocated, disposable) so
            // they can be built then Emplace(entity, value)'d. Only root components (NativeStruct base)
            // get it; the detached ctor chains to NativeStruct(string).
            if (HasMetadata(Struct, "Component") && Base == "global::LuminaSharp.NativeStruct")
            {
                Writer.Linef("public %s() : base(\"%s\") { }", Struct.DisplayName.c_str(), Struct.DisplayName.c_str());
            }
            EmitProperties(Writer, Struct, Database);
            EmitFunctions(Writer, Struct, Database);
            Writer.EndBlock();
        }

        if (bNs)
        {
            Writer.EndBlock();
        }
        Writer.Line();
        return true;
    }

    bool CSharpBindingEmitter::EmitForClass(FCodeWriter& Writer, const FReflectedClass& Class, const FReflectionDatabase& Database)
    {
        if (IsExcludedFromCSharp(Class))
        {
            return false;
        }

        bool bNs = false;
        OpenNamespace(Writer, Class.Namespace, bNs);

        // Opaque handle wrapper: derives its reflected base's wrapper (inheriting its members) and
        // adds its own bound properties.
        const eastl::string Base = CSharpBase(Class, Database, "global::LuminaSharp.NativeObject");
        Writer.Linef("public unsafe partial class %s : %s", Class.DisplayName.c_str(), Base.c_str());
        Writer.BeginBlock();
        Writer.Linef("internal %s(System.IntPtr handle) : base(handle) { }", Class.DisplayName.c_str());
        EmitProperties(Writer, Class, Database);
        EmitFunctions(Writer, Class, Database);
        Writer.EndBlock();

        if (bNs)
        {
            Writer.EndBlock();
        }
        Writer.Line();
        return true;
    }

    void CSharpBindingEmitter::EmitNativeLayoutCheck(FCodeWriter& Writer, const FReflectedStruct& Struct, const FReflectionDatabase& Database)
    {
        if (IsExcludedFromCSharp(Struct))
        {
            return;
        }

        if (!HasMetadata(Struct, "ScriptStruct"))
        {
            return; // only opted-in value mirrors need (and have) a layout to validate
        }

        int Size = 0;
        int Align = 0;
        if (!ComputeBlittableLayout(Struct, Database, Size, Align, 0))
        {
            return;
        }

        Writer.Linef("static_assert(sizeof(%s) == %d, \"LuminaSharp: blittable C# mirror of %s has a size mismatch (non-reflected fields, or a non-int enum).\");",
            Struct.QualifiedName.c_str(), Size, Struct.DisplayName.c_str());
    }

    void CSharpBindingEmitter::EmitNativeThunks(FCodeWriter& Writer, const FReflectedStruct& Type, const FReflectionDatabase& Database)
    {
        if (IsExcludedFromCSharp(Type))
        {
            return;
        }
        // A blittable value-mirror struct exposes its fields directly -> no thunks.
        if (Type.Type == FReflectedType::EType::Structure && HasMetadata(Type, "ScriptStruct"))
        {
            int Size = 0;
            int Align = 0;
            if (ComputeBlittableLayout(Type, Database, Size, Align, 0))
            {
                return;
            }
        }

        const eastl::string Friendly = Names::FriendlyFromQualified(Type.QualifiedName);
        const char* Qualified = Type.QualifiedName.c_str();
        // Export from the module that defines the type (RUNTIME_API/EDITOR_API/SANDBOX_API/...) so a
        // thunk landing in another module's TU is a dllexport there, not a forbidden dllimport define.
        const eastl::string Api = Names::ProjectApiMacro(ModuleOf(Type));
        const char* ApiMacro = Api.c_str();

        for (const auto& Prop : Type.Props)
        {
            if (Prop->bInner || IsScriptHidden(*Prop))
            {
                continue;
            }
            FBinding B;
            if (Classify(*Prop, Type.Namespace, Database, B))
            {
                EmitNativeThunk(Writer, *Prop, B, Friendly, Qualified, ApiMacro);
            }
        }

        EmitNativeFunctions(Writer, Type, Qualified, ApiMacro, Database);
    }
}
