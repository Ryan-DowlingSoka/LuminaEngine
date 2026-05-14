#pragma once

namespace Lumina::SlowTaskModal
{
    /**
     * Draws a dedicated, centered, blocking progress popup for every live FScopedSlowTask.
     * Call only from FEditorModalManager::DrawDialogue: it must be issued from the correct
     * ImGui scope (nested inside an active blocking modal, or at root) so it never becomes
     * a competing sibling modal — two sibling modals close each other every frame.
     */
    void Render();
}
