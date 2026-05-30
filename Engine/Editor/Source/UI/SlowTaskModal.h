#pragma once

namespace Lumina::SlowTaskModal
{
    // Blocking progress popup for live FScopedSlowTasks. Call only from
    // FEditorModalManager::DrawDialogue so it never becomes a competing sibling modal.
    void Render();
}
