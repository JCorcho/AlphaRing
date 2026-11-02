#include "Toast.h"

#include <Windows.h>
#include <imgui.h>

#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <deque>
#include <string>

#include "log/Log.h"

namespace AlphaRing::Toast {
    struct Message {
        std::string text;
        ULONGLONG shown;
    };

    static constexpr ULONGLONG k_lifetime = 4000; // ms
    static constexpr ULONGLONG k_fade = 500;
    static constexpr size_t k_maxShown = 4;

    static SRWLOCK s_lock = SRWLOCK_INIT;
    static std::deque<Message> s_messages;

    void Show(const char* format, ...) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        LOG_INFO("Toast: {}", buffer);

        AcquireSRWLockExclusive(&s_lock);
        s_messages.push_back({buffer, GetTickCount64()});
        while (s_messages.size() > k_maxShown)
            s_messages.pop_front();
        ReleaseSRWLockExclusive(&s_lock);
    }

    bool Active() {
        AcquireSRWLockShared(&s_lock);
        bool active = !s_messages.empty();
        ReleaseSRWLockShared(&s_lock);
        return active;
    }

    void Draw() {
        std::deque<Message> messages;
        ULONGLONG now = GetTickCount64();
        AcquireSRWLockExclusive(&s_lock);
        while (!s_messages.empty() && now - s_messages.front().shown > k_lifetime)
            s_messages.pop_front();
        messages = s_messages;
        ReleaseSRWLockExclusive(&s_lock);
        if (messages.empty()) return;

        auto* draw = ImGui::GetForegroundDrawList();
        auto* viewport = ImGui::GetMainViewport();
        float scale = (std::max)(1.0f, viewport->Size.y / 1080.0f);
        float fontSize = ImGui::GetFontSize() * 1.25f * scale;
        ImFont* font = ImGui::GetFont();
        float padX = 18.0f * scale, padY = 10.0f * scale;
        float y = viewport->Pos.y + 40.0f * scale;

        for (auto& message : messages) {
            ULONGLONG age = now - message.shown;
            float alpha = 1.0f;
            if (age > k_lifetime - k_fade)
                alpha = (float)(k_lifetime - age) / (float)k_fade;
            ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, message.text.c_str());
            float x = viewport->Pos.x + (viewport->Size.x - size.x) * 0.5f;
            ImVec2 min(x - padX, y), max(x + size.x + padX, y + size.y + padY * 2);
            draw->AddRectFilled(min, max, IM_COL32(16, 16, 16, (int)(215 * alpha)), 6.0f * scale);
            draw->AddRectFilled(ImVec2(min.x, min.y), ImVec2(min.x + 5.0f * scale, max.y), IM_COL32(16, 124, 16, (int)(255 * alpha)), 6.0f * scale, ImDrawFlags_RoundCornersLeft);
            draw->AddText(font, fontSize, ImVec2(x, y + padY), IM_COL32(255, 255, 255, (int)(255 * alpha)), message.text.c_str());
            y = max.y + 8.0f * scale;
        }
    }
}
