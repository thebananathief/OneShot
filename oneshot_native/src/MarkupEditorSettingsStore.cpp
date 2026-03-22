#include "oneshot_native/MarkupEditorSettingsStore.h"

#include <cwctype>

namespace
{
    constexpr wchar_t kSettingsFileName[] = L"markup-tools.ini";
    constexpr wchar_t kMetaSection[] = L"meta";
    constexpr wchar_t kEditorSection[] = L"editor";
    constexpr wchar_t kPenSection[] = L"pen";
    constexpr wchar_t kLineSection[] = L"line";
    constexpr wchar_t kArrowSection[] = L"arrow";
    constexpr wchar_t kRectangleSection[] = L"rectangle";
    constexpr wchar_t kEllipseSection[] = L"ellipse";
    constexpr wchar_t kPolygonSection[] = L"polygon";
    constexpr wchar_t kTextSection[] = L"text";

    void TraceSettingsMessage(const std::wstring& message)
    {
        OutputDebugStringW((message + L"\n").c_str());
    }

    std::wstring Trim(std::wstring value)
    {
        const auto isSpace = [](wchar_t ch)
        {
            return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
        };

        while (!value.empty() && isSpace(value.front()))
        {
            value.erase(value.begin());
        }

        while (!value.empty() && isSpace(value.back()))
        {
            value.pop_back();
        }

        return value;
    }

    std::wstring ToLower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }

    std::wstring ToolToString(oneshot::MarkupTool tool)
    {
        switch (tool)
        {
        case oneshot::MarkupTool::Pen:
            return L"pen";
        case oneshot::MarkupTool::Line:
            return L"line";
        case oneshot::MarkupTool::Arrow:
            return L"arrow";
        case oneshot::MarkupTool::Rectangle:
            return L"rectangle";
        case oneshot::MarkupTool::Ellipse:
            return L"ellipse";
        case oneshot::MarkupTool::Polygon:
            return L"polygon";
        case oneshot::MarkupTool::Text:
            return L"text";
        default:
            return L"pen";
        }
    }

    std::optional<oneshot::MarkupTool> ParseTool(const std::wstring& value)
    {
        const std::wstring normalized = ToLower(Trim(value));
        if (normalized == L"pen")
        {
            return oneshot::MarkupTool::Pen;
        }
        if (normalized == L"line")
        {
            return oneshot::MarkupTool::Line;
        }
        if (normalized == L"arrow")
        {
            return oneshot::MarkupTool::Arrow;
        }
        if (normalized == L"rectangle")
        {
            return oneshot::MarkupTool::Rectangle;
        }
        if (normalized == L"ellipse")
        {
            return oneshot::MarkupTool::Ellipse;
        }
        if (normalized == L"polygon")
        {
            return oneshot::MarkupTool::Polygon;
        }
        if (normalized == L"text")
        {
            return oneshot::MarkupTool::Text;
        }

        return std::nullopt;
    }

    std::wstring FormatColor(COLORREF color)
    {
        wchar_t buffer[16]{};
        swprintf_s(buffer, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
        return buffer;
    }

    std::optional<COLORREF> ParseColor(const std::wstring& value)
    {
        const std::wstring trimmed = Trim(value);
        if (trimmed.size() != 7 || trimmed.front() != L'#')
        {
            return std::nullopt;
        }

        try
        {
            const int red = std::stoi(trimmed.substr(1, 2), nullptr, 16);
            const int green = std::stoi(trimmed.substr(3, 2), nullptr, 16);
            const int blue = std::stoi(trimmed.substr(5, 2), nullptr, 16);
            return RGB(red, green, blue);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::wstring FormatBool(bool value)
    {
        return value ? L"1" : L"0";
    }

    std::optional<bool> ParseBool(const std::wstring& value)
    {
        const std::wstring normalized = ToLower(Trim(value));
        if (normalized == L"1" || normalized == L"true" || normalized == L"yes")
        {
            return true;
        }
        if (normalized == L"0" || normalized == L"false" || normalized == L"no")
        {
            return false;
        }

        return std::nullopt;
    }

    std::wstring FormatInt(int value)
    {
        return std::to_wstring(value);
    }

    std::optional<int> ParseInt(const std::wstring& value)
    {
        const std::wstring trimmed = Trim(value);
        if (trimmed.empty())
        {
            return std::nullopt;
        }

        try
        {
            size_t consumed = 0;
            const int parsed = std::stoi(trimmed, &consumed, 10);
            if (consumed != trimmed.size())
            {
                return std::nullopt;
            }
            return parsed;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::wstring ReadIniString(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key)
    {
        std::wstring buffer(256, L'\0');
        for (;;)
        {
            const DWORD copied = GetPrivateProfileStringW(section, key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
            if (copied < (buffer.size() - 1))
            {
                buffer.resize(copied);
                return buffer;
            }

            buffer.resize(buffer.size() * 2);
        }
    }

    bool WriteIniValue(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key, const std::wstring& value)
    {
        if (WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()) != FALSE)
        {
            return true;
        }

        const DWORD error = GetLastError();
        std::wstring message = L"OneShot markup settings: failed to write [";
        message += section;
        message += L"] ";
        message += key;
        message += L" (error=";
        message += std::to_wstring(error);
        message += L")";
        TraceSettingsMessage(message);
        return false;
    }

    template <typename Loader>
    void LoadStroke(oneshot::MarkupStrokePreferences& stroke, Loader&& loader)
    {
        if (const auto color = ParseColor(loader(L"color")))
        {
            stroke.color = *color;
        }

        if (const auto thickness = ParseInt(loader(L"thickness")); thickness.has_value() && *thickness >= 1 && *thickness <= 16)
        {
            stroke.thickness = *thickness;
        }
    }

    template <typename Loader>
    void LoadShape(oneshot::MarkupShapePreferences& shape, Loader&& loader)
    {
        if (const auto color = ParseColor(loader(L"stroke_color")))
        {
            shape.stroke.color = *color;
        }

        if (const auto thickness = ParseInt(loader(L"thickness")); thickness.has_value() && *thickness >= 1 && *thickness <= 16)
        {
            shape.stroke.thickness = *thickness;
        }

        if (const auto fillColor = ParseColor(loader(L"fill_color")))
        {
            shape.fillColor = *fillColor;
        }

        if (const auto fillEnabled = ParseBool(loader(L"fill_enabled")))
        {
            shape.fillEnabled = *fillEnabled;
        }
    }

    template <typename Loader>
    void LoadText(oneshot::MarkupTextPreferences& text, Loader&& loader)
    {
        if (const auto color = ParseColor(loader(L"color")))
        {
            text.color = *color;
        }

        if (const auto fontSize = ParseInt(loader(L"font_size")); fontSize.has_value() && *fontSize >= 8 && *fontSize <= 96)
        {
            text.fontSize = *fontSize;
        }

        const std::wstring fontFace = Trim(loader(L"font_face"));
        if (!fontFace.empty())
        {
            text.fontFace = fontFace;
        }
    }
}

namespace oneshot
{
    MarkupEditorSettingsStore::MarkupEditorSettingsStore(const AppPaths& paths)
        : _settingsPath(paths.localAppDataRoot / kSettingsFileName)
        , _preferences(Load())
    {
    }

    const MarkupEditorPreferences& MarkupEditorSettingsStore::GetPreferences() const noexcept
    {
        return _preferences;
    }

    void MarkupEditorSettingsStore::Save(const MarkupEditorPreferences& preferences) noexcept
    {
        _preferences = preferences;
        PersistToDisk(_preferences);
    }

    MarkupEditorPreferences MarkupEditorSettingsStore::Load() const noexcept
    {
        MarkupEditorPreferences preferences{};

        std::error_code existsError;
        const bool fileExists = std::filesystem::exists(_settingsPath, existsError);
        if (existsError || !fileExists)
        {
            return preferences;
        }

        if (const auto activeTool = ParseTool(ReadIniString(_settingsPath, kEditorSection, L"active_tool")))
        {
            preferences.activeTool = *activeTool;
        }

        LoadStroke(preferences.pen, [this](const wchar_t* key) { return ReadIniString(_settingsPath, kPenSection, key); });
        LoadStroke(preferences.line, [this](const wchar_t* key) { return ReadIniString(_settingsPath, kLineSection, key); });
        LoadStroke(preferences.arrow, [this](const wchar_t* key) { return ReadIniString(_settingsPath, kArrowSection, key); });
        LoadShape(preferences.rectangle, [this](const wchar_t* key) { return ReadIniString(_settingsPath, kRectangleSection, key); });
        LoadShape(preferences.ellipse, [this](const wchar_t* key) { return ReadIniString(_settingsPath, kEllipseSection, key); });
        LoadShape(preferences.polygon, [this](const wchar_t* key) { return ReadIniString(_settingsPath, kPolygonSection, key); });
        LoadText(preferences.text, [this](const wchar_t* key) { return ReadIniString(_settingsPath, kTextSection, key); });

        return preferences;
    }

    void MarkupEditorSettingsStore::PersistToDisk(const MarkupEditorPreferences& preferences) const noexcept
    {
        std::error_code directoryError;
        std::filesystem::create_directories(_settingsPath.parent_path(), directoryError);
        if (directoryError)
        {
            std::wstring message = L"OneShot markup settings: failed to create settings directory (error=";
            message += std::to_wstring(directoryError.value());
            message += L")";
            TraceSettingsMessage(message);
            return;
        }

        if (DeleteFileW(_settingsPath.c_str()) == FALSE)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND)
            {
                std::wstring message = L"OneShot markup settings: failed to replace existing settings file (error=";
                message += std::to_wstring(error);
                message += L")";
                TraceSettingsMessage(message);
            }
        }

        bool ok = true;
        ok = WriteIniValue(_settingsPath, kMetaSection, L"version", L"1") && ok;
        ok = WriteIniValue(_settingsPath, kEditorSection, L"active_tool", ToolToString(preferences.activeTool)) && ok;

        ok = WriteIniValue(_settingsPath, kPenSection, L"color", FormatColor(preferences.pen.color)) && ok;
        ok = WriteIniValue(_settingsPath, kPenSection, L"thickness", FormatInt(preferences.pen.thickness)) && ok;

        ok = WriteIniValue(_settingsPath, kLineSection, L"color", FormatColor(preferences.line.color)) && ok;
        ok = WriteIniValue(_settingsPath, kLineSection, L"thickness", FormatInt(preferences.line.thickness)) && ok;

        ok = WriteIniValue(_settingsPath, kArrowSection, L"color", FormatColor(preferences.arrow.color)) && ok;
        ok = WriteIniValue(_settingsPath, kArrowSection, L"thickness", FormatInt(preferences.arrow.thickness)) && ok;

        ok = WriteIniValue(_settingsPath, kRectangleSection, L"stroke_color", FormatColor(preferences.rectangle.stroke.color)) && ok;
        ok = WriteIniValue(_settingsPath, kRectangleSection, L"thickness", FormatInt(preferences.rectangle.stroke.thickness)) && ok;
        ok = WriteIniValue(_settingsPath, kRectangleSection, L"fill_color", FormatColor(preferences.rectangle.fillColor)) && ok;
        ok = WriteIniValue(_settingsPath, kRectangleSection, L"fill_enabled", FormatBool(preferences.rectangle.fillEnabled)) && ok;

        ok = WriteIniValue(_settingsPath, kEllipseSection, L"stroke_color", FormatColor(preferences.ellipse.stroke.color)) && ok;
        ok = WriteIniValue(_settingsPath, kEllipseSection, L"thickness", FormatInt(preferences.ellipse.stroke.thickness)) && ok;
        ok = WriteIniValue(_settingsPath, kEllipseSection, L"fill_color", FormatColor(preferences.ellipse.fillColor)) && ok;
        ok = WriteIniValue(_settingsPath, kEllipseSection, L"fill_enabled", FormatBool(preferences.ellipse.fillEnabled)) && ok;

        ok = WriteIniValue(_settingsPath, kPolygonSection, L"stroke_color", FormatColor(preferences.polygon.stroke.color)) && ok;
        ok = WriteIniValue(_settingsPath, kPolygonSection, L"thickness", FormatInt(preferences.polygon.stroke.thickness)) && ok;
        ok = WriteIniValue(_settingsPath, kPolygonSection, L"fill_color", FormatColor(preferences.polygon.fillColor)) && ok;
        ok = WriteIniValue(_settingsPath, kPolygonSection, L"fill_enabled", FormatBool(preferences.polygon.fillEnabled)) && ok;

        ok = WriteIniValue(_settingsPath, kTextSection, L"color", FormatColor(preferences.text.color)) && ok;
        ok = WriteIniValue(_settingsPath, kTextSection, L"font_size", FormatInt(preferences.text.fontSize)) && ok;
        ok = WriteIniValue(_settingsPath, kTextSection, L"font_face", preferences.text.fontFace) && ok;

        if (WritePrivateProfileStringW(nullptr, nullptr, nullptr, _settingsPath.c_str()) == FALSE)
        {
            const DWORD error = GetLastError();
            std::wstring message = L"OneShot markup settings: failed to flush settings file (error=";
            message += std::to_wstring(error);
            message += L")";
            TraceSettingsMessage(message);
            return;
        }

        if (!ok)
        {
            TraceSettingsMessage(L"OneShot markup settings: preferences updated in memory but disk persistence was incomplete.");
        }
    }
}
