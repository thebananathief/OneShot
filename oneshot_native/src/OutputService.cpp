#include "oneshot_native/OutputService.h"

#include <objidl.h>
#include <wincodec.h>

namespace
{
    struct BitmapPixels
    {
        int width{0};
        int height{0};
        std::vector<BYTE> bytes;
    };

    struct BitfieldsBitmapInfo
    {
        BITMAPINFOHEADER header{};
        DWORD masks[3]{};
    };

    struct PackedDib
    {
        HGLOBAL handle{nullptr};
        void* data{nullptr};

        PackedDib() = default;
        PackedDib(const PackedDib&) = delete;
        PackedDib& operator=(const PackedDib&) = delete;

        ~PackedDib()
        {
            Reset();
        }

        void Reset()
        {
            if (data)
            {
                GlobalUnlock(handle);
                data = nullptr;
            }
            if (handle)
            {
                GlobalFree(handle);
                handle = nullptr;
            }
        }

        [[nodiscard]] HGLOBAL ReleaseHandle()
        {
            if (data)
            {
                GlobalUnlock(handle);
                data = nullptr;
            }

            HGLOBAL released = handle;
            handle = nullptr;
            return released;
        }
    };

    std::wstring HrMessage(const HRESULT hr)
    {
        std::wstringstream builder;
        builder << L"0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
        return builder.str();
    }

    BITMAPV5HEADER CreateBitmapV5Header(int width, int height)
    {
        BITMAPV5HEADER header{};
        header.bV5Size = sizeof(header);
        header.bV5Width = width;
        header.bV5Height = -height;
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = 0x00FF0000;
        header.bV5GreenMask = 0x0000FF00;
        header.bV5BlueMask = 0x000000FF;
        header.bV5AlphaMask = 0xFF000000;
        header.bV5CSType = LCS_sRGB;
        return header;
    }

    BitfieldsBitmapInfo CreateBitfieldsBitmapInfo(int width, int height)
    {
        const BITMAPV5HEADER header = CreateBitmapV5Header(width, height);

        BitfieldsBitmapInfo info{};
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = width;
        info.header.biHeight = -height;
        info.header.biPlanes = header.bV5Planes;
        info.header.biBitCount = header.bV5BitCount;
        info.header.biCompression = BI_BITFIELDS;
        info.header.biSizeImage = static_cast<DWORD>(static_cast<SIZE_T>(width) * static_cast<SIZE_T>(height) * 4);
        info.masks[0] = header.bV5RedMask;
        info.masks[1] = header.bV5GreenMask;
        info.masks[2] = header.bV5BlueMask;
        return info;
    }

    bool ExtractBitmapPixels(HBITMAP bitmap, BitmapPixels& pixels, std::wstring& error)
    {
        pixels = {};

        BITMAP bitmapInfo{};
        if (GetObjectW(bitmap, sizeof(bitmapInfo), &bitmapInfo) != sizeof(bitmapInfo))
        {
            error = L"GetObjectW failed";
            return false;
        }
        if (bitmapInfo.bmWidth <= 0 || bitmapInfo.bmHeight <= 0)
        {
            error = L"bitmap dimensions are invalid";
            return false;
        }

        pixels.width = bitmapInfo.bmWidth;
        pixels.height = bitmapInfo.bmHeight;
        pixels.bytes.resize(static_cast<size_t>(pixels.width) * static_cast<size_t>(pixels.height) * 4);
        BitfieldsBitmapInfo info = CreateBitfieldsBitmapInfo(pixels.width, pixels.height);

        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            error = L"GetDC failed";
            return false;
        }

        const int rows = GetDIBits(
            screenDc,
            bitmap,
            0,
            static_cast<UINT>(bitmapInfo.bmHeight),
            pixels.bytes.data(),
            reinterpret_cast<BITMAPINFO*>(&info),
            DIB_RGB_COLORS);
        ReleaseDC(nullptr, screenDc);
        if (rows == 0)
        {
            error = L"GetDIBits failed";
            return false;
        }

        return true;
    }

    bool CreatePackedDibFromPixels(const BitmapPixels& pixels, bool useV5Header, PackedDib& dib, std::wstring& error)
    {
        dib.Reset();

        if (pixels.width <= 0 || pixels.height <= 0 || pixels.bytes.empty())
        {
            error = L"bitmap pixels are invalid";
            return false;
        }

        const BITMAPV5HEADER header = CreateBitmapV5Header(pixels.width, pixels.height);
        const SIZE_T pixelBytes = pixels.bytes.size();
        const SIZE_T headerBytes = useV5Header ? sizeof(BITMAPV5HEADER) : sizeof(BITMAPINFOHEADER) + (sizeof(DWORD) * 3);

        dib.handle = GlobalAlloc(GHND, headerBytes + pixelBytes);
        if (!dib.handle)
        {
            error = L"GlobalAlloc failed";
            return false;
        }

        dib.data = GlobalLock(dib.handle);
        if (!dib.data)
        {
            error = L"GlobalLock failed";
            dib.Reset();
            return false;
        }

        BYTE* bytes = static_cast<BYTE*>(dib.data);
        BYTE* pixelData = bytes + headerBytes;
        if (useV5Header)
        {
            memcpy(bytes, &header, sizeof(header));
        }
        else
        {
            auto* infoHeader = reinterpret_cast<BITMAPINFOHEADER*>(bytes);
            infoHeader->biSize = sizeof(BITMAPINFOHEADER);
            infoHeader->biWidth = header.bV5Width;
            infoHeader->biHeight = header.bV5Height;
            infoHeader->biPlanes = header.bV5Planes;
            infoHeader->biBitCount = header.bV5BitCount;
            infoHeader->biCompression = header.bV5Compression;
            infoHeader->biSizeImage = static_cast<DWORD>(pixelBytes);

            auto* masks = reinterpret_cast<DWORD*>(bytes + sizeof(BITMAPINFOHEADER));
            masks[0] = header.bV5RedMask;
            masks[1] = header.bV5GreenMask;
            masks[2] = header.bV5BlueMask;
        }

        memcpy(pixelData, pixels.bytes.data(), pixelBytes);
        return true;
    }

    bool CreateBitmapFromPixels(const BitmapPixels& pixels, HBITMAP& bitmap, std::wstring& error)
    {
        bitmap = nullptr;
        if (pixels.width <= 0 || pixels.height <= 0 || pixels.bytes.empty())
        {
            error = L"bitmap pixels are invalid";
            return false;
        }

        BITMAPV5HEADER header = CreateBitmapV5Header(pixels.width, pixels.height);
        void* bits = nullptr;
        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            error = L"GetDC failed";
            return false;
        }

        bitmap = CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screenDc);
        if (!bitmap || !bits)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
                bitmap = nullptr;
            }
            error = L"CreateDIBSection failed";
            return false;
        }

        memcpy(bits, pixels.bytes.data(), pixels.bytes.size());
        return true;
    }

    bool EncodePixelsToPngStream(const BitmapPixels& pixels, IStream* stream, std::wstring& error)
    {
        if (!stream)
        {
            error = L"stream is null";
            return false;
        }
        if (pixels.width <= 0 || pixels.height <= 0 || pixels.bytes.empty())
        {
            error = L"bitmap pixels are invalid";
            return false;
        }

        IWICImagingFactory* factory = nullptr;
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

        hr = frame->SetSize(static_cast<UINT>(pixels.width), static_cast<UINT>(pixels.height));
        if (FAILED(hr))
        {
            error = L"SetSize failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&pixelFormat);
        if (FAILED(hr))
        {
            error = L"SetPixelFormat failed: " + HrMessage(hr);
            releaseAll();
            return false;
        }

        const UINT stride = static_cast<UINT>(pixels.width * 4);
        hr = frame->WritePixels(
            static_cast<UINT>(pixels.height),
            stride,
            static_cast<UINT>(pixels.bytes.size()),
            const_cast<BYTE*>(pixels.bytes.data()));
        if (FAILED(hr))
        {
            error = L"WritePixels failed: " + HrMessage(hr);
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

    bool EncodePixelsToPngBytes(const BitmapPixels& pixels, std::vector<BYTE>& bytes, std::wstring& error)
    {
        bytes.clear();

        IStream* stream = nullptr;
        HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
        if (FAILED(hr))
        {
            error = L"CreateStreamOnHGlobal failed: " + HrMessage(hr);
            return false;
        }

        const auto releaseStream = [&]()
        {
            if (stream)
            {
                stream->Release();
                stream = nullptr;
            }
        };

        if (!EncodePixelsToPngStream(pixels, stream, error))
        {
            releaseStream();
            return false;
        }

        HGLOBAL handle = nullptr;
        hr = GetHGlobalFromStream(stream, &handle);
        if (FAILED(hr))
        {
            error = L"GetHGlobalFromStream failed: " + HrMessage(hr);
            releaseStream();
            return false;
        }

        const SIZE_T size = static_cast<SIZE_T>(GlobalSize(handle));
        void* data = GlobalLock(handle);
        if (!data)
        {
            error = L"GlobalLock failed";
            releaseStream();
            return false;
        }

        bytes.resize(size);
        memcpy(bytes.data(), data, size);
        GlobalUnlock(handle);
        releaseStream();
        return true;
    }

    bool SetClipboardBlob(UINT format, HGLOBAL& handle, std::wstring& error)
    {
        if (!handle)
        {
            return false;
        }

        if (!SetClipboardData(format, handle))
        {
            error = L"SetClipboardData failed";
            return false;
        }

        handle = nullptr;
        return true;
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

        BitmapPixels pixels{};
        std::wstring pixelError;
        if (!ExtractBitmapPixels(image.bitmap, pixels, pixelError))
        {
            error = pixelError;
            return false;
        }

        HBITMAP clipboardBitmap = nullptr;
        PackedDib dib{};
        PackedDib dibV5{};
        std::vector<BYTE> pngBytes;

        std::wstring bitmapError;
        std::wstring dibError;
        std::wstring dibV5Error;
        std::wstring pngError;

        if (!CreateBitmapFromPixels(pixels, clipboardBitmap, bitmapError))
        {
            clipboardBitmap = nullptr;
        }
        if (!CreatePackedDibFromPixels(pixels, false, dib, dibError))
        {
            dib.Reset();
        }
        if (!CreatePackedDibFromPixels(pixels, true, dibV5, dibV5Error))
        {
            dibV5.Reset();
        }
        if (!EncodePixelsToPngBytes(pixels, pngBytes, pngError))
        {
            pngBytes.clear();
        }

        if (!clipboardBitmap && !dib.handle && !dibV5.handle && pngBytes.empty())
        {
            error = !pngError.empty() ? pngError :
                !dibV5Error.empty() ? dibV5Error :
                !dibError.empty() ? dibError :
                bitmapError;
            return false;
        }

        if (!OpenClipboard(owner))
        {
            if (clipboardBitmap)
            {
                DeleteObject(clipboardBitmap);
            }
            error = L"OpenClipboard failed";
            return false;
        }

        if (!EmptyClipboard())
        {
            CloseClipboard();
            if (clipboardBitmap)
            {
                DeleteObject(clipboardBitmap);
            }
            error = L"EmptyClipboard failed";
            return false;
        }

        bool copied = false;
        std::wstring setDataError;

        if (clipboardBitmap && SetClipboardData(CF_BITMAP, clipboardBitmap))
        {
            clipboardBitmap = nullptr;
            copied = true;
        }
        else if (clipboardBitmap)
        {
            setDataError = L"SetClipboardData failed";
        }

        HGLOBAL dibHandle = dib.ReleaseHandle();
        if (dibHandle && SetClipboardBlob(CF_DIB, dibHandle, setDataError))
        {
            copied = true;
        }
        else if (dibHandle)
        {
            GlobalFree(dibHandle);
        }

        HGLOBAL dibV5Handle = dibV5.ReleaseHandle();
        if (dibV5Handle && SetClipboardBlob(CF_DIBV5, dibV5Handle, setDataError))
        {
            copied = true;
        }
        else if (dibV5Handle)
        {
            GlobalFree(dibV5Handle);
        }

        if (!pngBytes.empty())
        {
            const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
            if (pngFormat != 0)
            {
                HGLOBAL pngHandle = GlobalAlloc(GHND, pngBytes.size());
                if (pngHandle)
                {
                    void* pngData = GlobalLock(pngHandle);
                    if (pngData)
                    {
                        memcpy(pngData, pngBytes.data(), pngBytes.size());
                        GlobalUnlock(pngHandle);
                        if (SetClipboardBlob(pngFormat, pngHandle, setDataError))
                        {
                            copied = true;
                        }
                    }
                    else
                    {
                        setDataError = L"GlobalLock failed";
                    }

                    if (pngHandle)
                    {
                        GlobalFree(pngHandle);
                    }
                }
                else
                {
                    setDataError = L"GlobalAlloc failed";
                }
            }
        }

        CloseClipboard();
        if (clipboardBitmap)
        {
            DeleteObject(clipboardBitmap);
        }

        if (!copied)
        {
            error = !setDataError.empty() ? setDataError :
                !pngError.empty() ? pngError :
                !dibV5Error.empty() ? dibV5Error :
                !dibError.empty() ? dibError :
                bitmapError;
            return false;
        }

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

    bool OutputService::EncodeBitmapToPngStream(HBITMAP bitmap, IStream* stream, std::wstring& error) const
    {
        BitmapPixels pixels{};
        if (!ExtractBitmapPixels(bitmap, pixels, error))
        {
            return false;
        }

        return EncodePixelsToPngStream(pixels, stream, error);
    }

    bool OutputService::EncodeBitmapToPngBytes(HBITMAP bitmap, std::vector<BYTE>& bytes, std::wstring& error) const
    {
        bytes.clear();

        IStream* stream = nullptr;
        HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
        if (FAILED(hr))
        {
            error = L"CreateStreamOnHGlobal failed: " + HrMessage(hr);
            return false;
        }

        const auto releaseStream = [&]()
        {
            if (stream)
            {
                stream->Release();
                stream = nullptr;
            }
        };

        if (!EncodeBitmapToPngStream(bitmap, stream, error))
        {
            releaseStream();
            return false;
        }

        HGLOBAL handle = nullptr;
        hr = GetHGlobalFromStream(stream, &handle);
        if (FAILED(hr))
        {
            error = L"GetHGlobalFromStream failed: " + HrMessage(hr);
            releaseStream();
            return false;
        }

        const SIZE_T size = static_cast<SIZE_T>(GlobalSize(handle));
        void* data = GlobalLock(handle);
        if (!data)
        {
            error = L"GlobalLock failed";
            releaseStream();
            return false;
        }

        bytes.resize(size);
        memcpy(bytes.data(), data, size);
        GlobalUnlock(handle);
        releaseStream();
        return true;
    }

    bool OutputService::EncodeBitmapToPng(HBITMAP bitmap, const std::filesystem::path& destination, std::wstring& error) const
    {
        IWICImagingFactory* factory = nullptr;
        IWICStream* stream = nullptr;

        const auto releaseAll = [&]()
        {
            if (stream)
            {
                stream->Release();
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

        const bool ok = EncodeBitmapToPngStream(bitmap, stream, error);
        releaseAll();
        return ok;
    }
}
