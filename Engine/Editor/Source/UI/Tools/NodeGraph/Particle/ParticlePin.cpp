#include "ParticlePin.h"
#include <imgui.h>

namespace Lumina
{
    float CParticleInput::DrawPin()
    {
        if (!ShouldDrawEditor())
        {
            ImGui::Dummy(ImVec2(1.5f, 1.5f));
            return 1.5f;
        }

        switch (PinType)
        {
        case EParticlePinType::Float:
            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat("##V", &DefaultFloat, 0.01f);
            return 100.0f;

        case EParticlePinType::Float3:
            ImGui::SetNextItemWidth(160.0f);
            ImGui::DragFloat3("##V", &DefaultFloat3.x, 0.01f);
            return 160.0f;

        case EParticlePinType::Float4:
            ImGui::SetNextItemWidth(200.0f);
            ImGui::ColorEdit4("##V", &DefaultFloat4.x);
            return 200.0f;
        }

        return 1.5f;
    }

    float CParticleOutput::DrawPin()
    {
        ImGui::Dummy(ImVec2(1.5f, 1.5f));
        return 1.5f;
    }
}
