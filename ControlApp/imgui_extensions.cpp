#include <imgui/imgui_internal.h>

namespace ImGui {
    static bool GridSquare(bool Selected, bool PreviousSelected, bool *Hovered) {
        ImGuiWindow* Window = GetCurrentWindow();
        if(Window->SkipItems) {
            return false;
        }

        ImGuiContext& G = *GImGui;
        const ImGuiStyle& Style = G.Style;

        ImGuiID Id = Window->GetID("");
        ImVec2 Size(5, 10);
        ImVec2 Pos = Window->DC.CursorPos;
        Pos.y += Window->DC.CurrentLineTextBaseOffset;
        ImRect Bb(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y));
        ItemSize(Bb);

        float SpacingL = (float)(int)(Style.ItemSpacing.x * 0.5f);
        float SpacingU = (float)(int)(Style.ItemSpacing.y * 0.5f);
        float SpacingR = Style.ItemSpacing.x - SpacingL;
        float SpacingD = Style.ItemSpacing.y - SpacingU;
        Bb.Min.x -= SpacingL;
        Bb.Min.y -= SpacingU;
        Bb.Max.x += SpacingR;
        Bb.Max.y += SpacingD;
        if(!ItemAdd(Bb, Id)) {
            return false;
        }

        bool Ignored1, Ignored2;
        ButtonBehavior(Bb, Id, &Ignored1, &Ignored2, 0);

        *Hovered = IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        bool Pressed = *Hovered && IsMouseDown(0);
        if(Pressed || *Hovered) {
            if (!G.NavDisableMouseHover && G.NavWindow == Window && G.NavLayer == Window->DC.NavLayerCurrent) {
                G.NavDisableHighlight = true;
                SetNavID(Id, Window->DC.NavLayerCurrent);
            }
        }
        if(Pressed) {
            SetActiveID(Id, Window);
            SetFocusID(Id, Window);
            FocusWindow(Window);
            MarkItemEdited(Id);
        }

        // Render
        if(*Hovered || Selected) {
            const ImU32 Col = GetColorU32(Pressed ? ImGuiCol_HeaderActive : *Hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
            RenderFrame(Bb.Min, Bb.Max, Col, false, 0.0f);
            RenderNavHighlight(Bb, Id, ImGuiNavHighlightFlags_TypeThin | ImGuiNavHighlightFlags_NoRounding);
        } else if(PreviousSelected) {
            const ImU32 Col = GetColorU32(IM_COL32(52, 80, 99, 70));
            RenderFrame(Bb.Min, Bb.Max, Col, false, 0.0f);
            RenderNavHighlight(Bb, Id, ImGuiNavHighlightFlags_TypeThin | ImGuiNavHighlightFlags_NoRounding);
        }

        return Pressed;
    }
}
