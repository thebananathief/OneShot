#include "oneshot_native/OutputService.h"

#include <wincodec.h>

namespace
{
    std::wstring HrMessage(const HRESULT hr)
    {
        std::wstringstream builder;
        builder << L"0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
        return builder.str();
    }
}

namespace oneshot
{
    OutputService::OutputService(AppPaths paths)
        : _paths(std::move(paths))
    {
    }

    bool OutputService::SavePng(const CapturedImage& image, const std::filesystem::path& destination, std::wstring& error) const
    {
        if (!image.IsValid())
        {
            error = L"capture is empty";
            return false;
        }

        if (!EnsureParentDirectory(destination, error))
        {
            return false;
        }

        return EncodeBitmapToPng(image.bitmap, destination, error);
    }

    bool OutputService::CopyToClipboard(HWND owner, const CapturedImage& image, std::wstring& error) const
    {
        if (!image.IsValid())
        {
            error = L"capture is empty";
            return false;
        }

        HBITMAP clipboardBitmap = static_cast<HBITMAP>(CopyImage(image.bitmap, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
        if (!clipboardBitmap)
        {
            error = L"CopyImage failed";
            return false;
        }

        if (!OpenClipboard(owner))
        {
            DeleteObject(clipboardBitmap);
            error = L"OpenClipboard failed";
            return false;
        }

        if (!EmptyClipboard())
        {
            CloseClipboard();
            DeleteObject(clipboardBitmap);
            error = L"EmptyClipboard failed";
            return false;
        }

        if (!SetClipboardData(CF_BITMAP, clipboardBitmap))
        {
            CloseClipboard();
            DeleteObject(clipboardBitmap);
            error = L"SetClipboardData failed";
            return false;
        }

        CloseClipboard();
        return true;
    }

    std::filesystem::path OutputService::BuildSavedScreenshotPath(const SYSTEMTIME& timestampUtc) const
    {
        SYSTEMTIME localTimestamp = timestampUtc;
        SystemTimeToTzSpecificLocalTime(nullptr, &timestampUtc, &localTimestamp);

        wchar_t filename[128]{};
        swprintf_s(
            filename,
            L"OneShot_%04d%02d%02d_%02d%02d%02d_%03d.png",
            localTimestamp.wYear,
            localTimestamp.wMonth,
            localTimestamp.wDay,
            localTimestamp.wHour,
            localTimestamp.wMinute,
            localTimestamp.wSecond,
            localTimestamp.wMilliseconds);
        return _paths.screenshotsDirectory / filename;
    }

    bool OutputService::EnsureParentDirectory(const std::filesystem::path& path, std::wstring& error) const
    {
        std::error_code filesystemError;
        std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError)
        {
            error = L"create_directories failed";
            return false;
        }

        return true;
    }

    bool OutputService::EncodeBitmapToPng(HBITMAP bitmap, const std::filesystem::path& destination, std::wstring& error) const
    {
        IWICImagingFactory* factory = nullptr;
        IWICBitmap* wicBitmap = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;

        const auto releaseAll = [&]()
        {
            if (frame)
            {
                frame->Release();
            }
            if (encoder)
            {
                encoder->Release();
            }
            if (stream)
            {
                stream->Release();
            }
            if (wicBitmap)
            {
                wicBitmap->Release();
            }
            if (factory)
            {
                factory->Release();
            }
        };

        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            error = L"CoCreateInstance(CLSID_WICImagingFactory) failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        hr = factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUseAlpha, &wicBitmap);
        if (FAILED(hr))
        {
            error = L"CreateBitmapFromHBITMAP failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        hr = factory->CreateStream(&stream);
        if (FAILED(hr))
        {
            error = L"CreateStream failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        hr = stream->InitializeFromFilename(destination.c_str(), GENERIC_WRITE);
        if (FAILED(hr))
        {
            error = L"InitializeFromFilename failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (FAILED(hr))
        {
            error = L"CreateEncoder failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (FAILED(hr))
        {
            error = L"Encoder Initialize failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        IPropertyBag2* properties = nullptr;
        hr = encoder->CreateNewFrame(&frame, &properties);
        if (properties)
        {
            properties->Release();
        }

        if (FAILED(hr))
        {
            error = L"CreateNewFrame failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        hr = frame->Initialize(nullptr);
        if (FAILED(hr))
        {
            error = L"Frame Initialize failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        hr = frame->WriteSource(wicBitmap, nullptr);
        if (FAILED(hr))
        {
            error = L"WriteSource failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        hr = frame->Commit();
        if (FAILED(hr))
        {
            error = L"Frame Commit failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        hr = encoder->Commit();
        if (FAILED(hr))
        {
            error = L"Encoder Commit failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        releaseAll();
        return true;
    }
}
