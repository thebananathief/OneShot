#pragma once

#include "oneshot_native/CaptureTypes.h"
#include "oneshot_native/MarkupEditorSettingsStore.h"
#include "oneshot_native/OutputService.h"

namespace oneshot
{
    class MarkupEditorWindow
    {
    public:
        using Tool = MarkupTool;

        MarkupEditorWindow(OutputService& outputService, MarkupEditorSettingsStore& settingsStore);
        ~MarkupEditorWindow();

        [[nodiscard]] std::optional<CapturedImage> Edit(HWND owner, const CapturedImage& source);

        struct State;

        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        static void ClearBitmapStack(std::vector<HBITMAP>& stack);
        static HBITMAP CloneBitmap(HBITMAP source);
        static void DrawArrow(HDC dc, POINT start, POINT end, COLORREF color, int thickness);

    private:
        OutputService& _outputService;
        MarkupEditorSettingsStore& _settingsStore;
    };
}
