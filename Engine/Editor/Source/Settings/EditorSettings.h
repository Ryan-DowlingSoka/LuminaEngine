#pragma once

#include "Core/Object/ObjectMacros.h"
#include "Config/DeveloperSettings.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Math/Math.h"
#include "Input/Key.h"
#include "EditorSettings.generated.h"

namespace Lumina
{
    // All editor settings persist into a single grouped file, one JSON section per class.

    // Transform-gizmo snapping defaults for the world editor.
    REFLECT(MinimalAPI, ConfigFile = "/Editor/Config/EditorPreferences.json", DisplayName = "World Tool", Category = "Editor")
    class CWorldEditorSettings : public CDeveloperSettings
    {
        GENERATED_BODY()
    public:

        /** Whether transform gizmo snapping is enabled by default. */
        PROPERTY(Editable, Category = "Snapping")
        bool bGizmoSnapEnabled = false;

        /** Snap step (units) for the translate gizmo. */
        PROPERTY(Editable, Category = "Snapping", ClampMin = 0.001f, ClampMax = 100.0f)
        float GizmoSnapTranslate = 0.1f;

        /** Snap step (degrees) for the rotate gizmo. */
        PROPERTY(Editable, Category = "Snapping", ClampMin = 0.1f, ClampMax = 90.0f)
        float GizmoSnapRotate = 5.0f;

        /** Snap step (scale factor) for the scale gizmo. */
        PROPERTY(Editable, Category = "Snapping", ClampMin = 0.001f, ClampMax = 10.0f)
        float GizmoSnapScale = 0.1f;
    };

    // Transform-gizmo snapping defaults for the prefab editor (kept distinct from the world editor).
    REFLECT(MinimalAPI, ConfigFile = "/Editor/Config/EditorPreferences.json", DisplayName = "Prefab Tool", Category = "Editor")
    class CPrefabEditorSettings : public CDeveloperSettings
    {
        GENERATED_BODY()
    public:

        /** Whether transform gizmo snapping is enabled by default. */
        PROPERTY(Editable, Category = "Snapping")
        bool bGizmoSnapEnabled = false;

        /** Snap step (units) for the translate gizmo. */
        PROPERTY(Editable, Category = "Snapping", ClampMin = 0.001f, ClampMax = 100.0f)
        float GizmoSnapTranslate = 0.1f;

        /** Snap step (degrees) for the rotate gizmo. */
        PROPERTY(Editable, Category = "Snapping", ClampMin = 0.1f, ClampMax = 90.0f)
        float GizmoSnapRotate = 5.0f;

        /** Snap step (scale factor) for the scale gizmo. */
        PROPERTY(Editable, Category = "Snapping", ClampMin = 0.001f, ClampMax = 10.0f)
        float GizmoSnapScale = 0.1f;
    };

    // Content browser preferences.
    REFLECT(MinimalAPI, ConfigFile = "/Editor/Config/EditorPreferences.json", DisplayName = "Content Browser", Category = "Editor")
    class CContentBrowserSettings : public CDeveloperSettings
    {
        GENERATED_BODY()
    public:

        /** Pixel size of asset tiles in the content browser. */
        PROPERTY(Editable, Category = "Layout", ClampMin = 32.0f, ClampMax = 256.0f)
        float TileSize = 86.0f;
    };

    // In-engine Lua code editor preferences.
    REFLECT(MinimalAPI, ConfigFile = "/Editor/Config/EditorPreferences.json", DisplayName = "Lua Editor", Category = "Editor")
    class CLuaEditorSettings : public CDeveloperSettings
    {
        GENERATED_BODY()
    public:

        /** Open .lua/.luau files with the OS default editor instead of the in-engine Lua editor. */
        PROPERTY(Editable, Category = "General")
        bool bUsePlatformEditor = false;

        /** Font scale multiplier for the in-engine Lua editor. */
        PROPERTY(Editable, Category = "Appearance", ClampMin = 0.75f, ClampMax = 3.0f)
        float FontScale = 1.25f;

        /** Tab size in spaces. */
        PROPERTY(Editable, Category = "Editing", ClampMin = 1, ClampMax = 8)
        int32 TabSize = 4;

        /** Line spacing multiplier. */
        PROPERTY(Editable, Category = "Appearance", ClampMin = 1.0f, ClampMax = 2.0f)
        float LineSpacing = 1.0f;

        /** Render whitespace glyphs (spaces and tabs). */
        PROPERTY(Editable, Category = "Appearance")
        bool bShowWhitespace = false;

        /** Show line numbers in the gutter. */
        PROPERTY(Editable, Category = "Appearance")
        bool bShowLineNumbers = true;

        /** Auto-indent new lines based on surrounding scope. */
        PROPERTY(Editable, Category = "Editing")
        bool bAutoIndent = true;

        /** Highlight matching brackets at the cursor. */
        PROPERTY(Editable, Category = "Editing")
        bool bMatchBrackets = true;

        /** Auto-close paired glyphs (parentheses, brackets, quotes). */
        PROPERTY(Editable, Category = "Editing")
        bool bCompletePairs = true;

        /** Color palette for the Lua editor ("Dark" or "Light"). */
        PROPERTY(Editable, Category = "Appearance")
        FString Palette = "Dark";

        /** Show the scrollbar mini-map. */
        PROPERTY(Editable, Category = "Appearance")
        bool bShowMiniMap = true;

        /** Insert spaces when the user presses Tab instead of a tab character. */
        PROPERTY(Editable, Category = "Editing")
        bool bInsertSpacesOnTabs = false;

        /** Strip trailing whitespace from every line on save. */
        PROPERTY(Editable, Category = "Editing")
        bool bTrimTrailingOnSave = false;

        /** Open the autocomplete popup automatically while typing identifiers. */
        PROPERTY(Editable, Category = "Completion")
        bool bAutoTriggerCompletion = true;

        /** Delay (ms) between the last keystroke and the autocomplete popup. */
        PROPERTY(Editable, Category = "Completion", ClampMin = 0, ClampMax = 1000)
        int32 AutoTriggerDelayMs = 100;

        // RGB token colors layered over the Dark/Light base. Defaults match the dark palette; the two
        // annotation entries drive the --@export / --@rpc directive coloring.

        /** Keywords (local, function, if, ...). */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 KeywordColor = FVector3(0.773f, 0.525f, 0.753f);

        /** Numeric literals. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 NumberColor = FVector3(0.710f, 0.808f, 0.659f);

        /** String literals. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 StringColor = FVector3(0.808f, 0.569f, 0.471f);

        /** Comments. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 CommentColor = FVector3(0.416f, 0.600f, 0.333f);

        /** Plain identifiers. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 IdentifierColor = FVector3(0.612f, 0.863f, 0.996f);

        /** Known identifiers (engine globals, built-in types). */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 KnownIdentifierColor = FVector3(0.310f, 0.757f, 1.0f);

        /** Punctuation. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 PunctuationColor = FVector3(1.0f, 1.0f, 0.6f);

        /** Annotation directives: the --@export / --@rpc / --@replicated marker and name. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 AnnotationColor = FVector3(0.898f, 0.753f, 0.310f);

        /** Annotation argument list: the (...) following a directive. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 AnnotationArgumentColor = FVector3(0.878f, 0.878f, 0.878f);

        // ---- Hotkeys -------------------------------------------------------------------------------
        // Rebindable editor shortcuts. Click a key field and press the chord to change it.

        /** Pretty-print / format the document. */
        PROPERTY(Editable, Category = "Hotkeys")
        SKey FormatDocumentKey = SKey(EKey::I, true, true);   // Ctrl+Shift+I

        /** Open the Go To Line prompt. */
        PROPERTY(Editable, Category = "Hotkeys")
        SKey GoToLineKey = SKey(EKey::G, true);               // Ctrl+G

        /** Toggle the document outline panel. */
        PROPERTY(Editable, Category = "Hotkeys")
        SKey ToggleOutlineKey = SKey(EKey::Backslash, true);  // Ctrl+\

        /** Go to the definition of the symbol under the cursor. */
        PROPERTY(Editable, Category = "Hotkeys")
        SKey GoToDefinitionKey = SKey(EKey::F12);             // F12

        /** Run the script up to the cursor line (debugger). */
        PROPERTY(Editable, Category = "Hotkeys")
        SKey RunToCursorKey = SKey(EKey::F10, true);          // Ctrl+F10
    };

    // In-engine RmlUi (.rml/.rcss) code editor preferences.
    REFLECT(MinimalAPI, ConfigFile = "/Editor/Config/EditorPreferences.json", DisplayName = "RmlUi Editor", Category = "Editor")
    class CRmlUiEditorSettings : public CDeveloperSettings
    {
        GENERATED_BODY()
    public:

        /** Font scale multiplier for the in-engine RmlUi editor. */
        PROPERTY(Editable, Category = "Appearance", ClampMin = 0.75f, ClampMax = 3.0f)
        float FontScale = 1.25f;

        /** Tab size in spaces. */
        PROPERTY(Editable, Category = "Editing", ClampMin = 1, ClampMax = 8)
        int32 TabSize = 4;

        /** Line spacing multiplier. */
        PROPERTY(Editable, Category = "Appearance", ClampMin = 1.0f, ClampMax = 2.0f)
        float LineSpacing = 1.0f;

        /** Render whitespace glyphs (spaces and tabs). */
        PROPERTY(Editable, Category = "Appearance")
        bool bShowWhitespace = false;

        /** Show line numbers in the gutter. */
        PROPERTY(Editable, Category = "Appearance")
        bool bShowLineNumbers = true;

        /** Show the scrollbar mini-map. */
        PROPERTY(Editable, Category = "Appearance")
        bool bShowMiniMap = true;

        /** Auto-indent new lines based on surrounding scope. */
        PROPERTY(Editable, Category = "Editing")
        bool bAutoIndent = true;

        /** Highlight matching brackets at the cursor. */
        PROPERTY(Editable, Category = "Editing")
        bool bMatchBrackets = true;

        /** Auto-close paired glyphs (parentheses, brackets, quotes). */
        PROPERTY(Editable, Category = "Editing")
        bool bCompletePairs = true;

        /** Insert spaces when the user presses Tab instead of a tab character. */
        PROPERTY(Editable, Category = "Editing")
        bool bInsertSpacesOnTabs = false;

        /** Strip trailing whitespace from every line on save. */
        PROPERTY(Editable, Category = "Editing")
        bool bTrimTrailingOnSave = false;

        /** Re-parse the buffer into the preview ~250ms after each edit. */
        PROPERTY(Editable, Category = "General")
        bool bAutoReload = true;

        /** Color palette for the RmlUi editor ("Dark" or "Light"). */
        PROPERTY(Editable, Category = "Appearance")
        FString Palette = "Dark";

        // ---- Syntax colors -------------------------------------------------------------------------
        // RGB token colors layered over the Dark/Light base. Defaults match the dark palette.

        /** Element tags (div, span, button, ...). */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 TagColor = FVector3(0.773f, 0.525f, 0.753f);

        /** Attribute names (id, class, style, ...). */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 AttributeColor = FVector3(0.353f, 0.702f, 0.608f);

        /** RCSS property names (display, color, margin, ...). */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 PropertyColor = FVector3(0.310f, 0.757f, 1.0f);

        /** Plain identifiers. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 IdentifierColor = FVector3(0.612f, 0.863f, 0.996f);

        /** Numeric literals and dimensions. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 NumberColor = FVector3(0.710f, 0.808f, 0.659f);

        /** String / attribute values. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 StringColor = FVector3(0.808f, 0.569f, 0.471f);

        /** Comments. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 CommentColor = FVector3(0.416f, 0.600f, 0.333f);

        /** Punctuation. */
        PROPERTY(Editable, Color, Category = "Syntax Colors")
        FVector3 PunctuationColor = FVector3(1.0f, 1.0f, 0.6f);

        // ---- Hotkeys -------------------------------------------------------------------------------

        /** Open the Go To Line prompt. */
        PROPERTY(Editable, Category = "Hotkeys")
        SKey GoToLineKey = SKey(EKey::G, true);   // Ctrl+G
    };
}
