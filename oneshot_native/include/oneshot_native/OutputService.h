#pragma once

#include "oneshot_native/CaptureTypes.h"
#include "oneshot_native/PathService.h"

namespace oneshot
{
    class OutputService
    {
    public:
        explicit OutputService(AppPaths paths);

        [[nodiscard]] bool SavePng(const CapturedImage& image, const std::filesystem::path& destination, std::wstring& error) const;
        [[nodiscard]] bool CopyToClipboard(HWND owner, const CapturedImage& image, std::wstring& error) const;
        [[nodiscard]] std::filesystem::path BuildSavedScreenshotPath(const SYSTEMTIME& timestampUtc) const;

    private:
        [[nodiscard]] bool EnsureParentDirectory(const std::filesystem::path& path, std::wstring& error) const;
        [[nodiscard]] bool EncodeBitmapToPng(HBITMAP bitmap, const std::filesystem::path& destination, std::wstring& error) const;

        AppPaths _paths;
    };
}
