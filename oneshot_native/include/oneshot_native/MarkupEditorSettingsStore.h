#pragma once

#include "oneshot_native/Common.h"
#include "oneshot_native/PathService.h"

namespace oneshot
{
    enum class MarkupTool
    {
        Pen,
        Line,
        Arrow,
        Rectangle,
        Ellipse,
        Polygon,
        Text,
        CutMove
    };

    enum class MarkupCutMoveMode
    {
        Cut,
        Move
    };

    struct MarkupStrokePreferences
    {
        COLORREF color{RGB(255, 0, 0)};
        int thickness{3};
    };

    struct MarkupShapePreferences
    {
        MarkupStrokePreferences stroke{};
        COLORREF fillColor{RGB(255, 224, 224)};
        bool fillEnabled{true};
    };

    struct MarkupTextPreferences
    {
        COLORREF color{RGB(255, 0, 0)};
        int fontSize{22};
        std::wstring fontFace{L"Segoe UI"};
    };

    struct MarkupEditorPreferences
    {
        MarkupTool activeTool{MarkupTool::Pen};
        MarkupCutMoveMode cutMoveMode{MarkupCutMoveMode::Cut};
        MarkupStrokePreferences pen{};
        MarkupStrokePreferences line{};
        MarkupStrokePreferences arrow{};
        MarkupShapePreferences rectangle{};
        MarkupShapePreferences ellipse{};
        MarkupShapePreferences polygon{};
        MarkupTextPreferences text{};
    };

    class MarkupEditorSettingsStore
    {
    public:
        explicit MarkupEditorSettingsStore(const AppPaths& paths);

        [[nodiscard]] const MarkupEditorPreferences& GetPreferences() const noexcept;
        void Save(const MarkupEditorPreferences& preferences) noexcept;

    private:
        [[nodiscard]] MarkupEditorPreferences Load() const noexcept;
        void PersistToDisk(const MarkupEditorPreferences& preferences) const noexcept;

        std::filesystem::path _settingsPath;
        MarkupEditorPreferences _preferences;
    };
}
