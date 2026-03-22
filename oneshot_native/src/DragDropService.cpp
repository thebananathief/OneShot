#include "oneshot_native/DragDropService.h"

#include <ole2.h>
#include <objidl.h>
#include <shellapi.h>

namespace
{
    constexpr wchar_t kPreferredDropEffectFormat[] = L"Preferred DropEffect";

    struct DropFilesHeader
    {
        DWORD pFiles;
        POINT pt;
        BOOL fNC;
        BOOL fWide;
    };

    std::wstring HrMessage(const HRESULT hr)
    {
        std::wstringstream builder;
        builder << L"0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
        return builder.str();
    }

    std::wstring DescribeOleInitFailure(const HRESULT hr)
    {
        if (hr == RPC_E_CHANGED_MODE)
        {
            return L"OLE initialization failed because the UI thread is already using a different COM apartment mode.";
        }

        return L"OLE initialization failed: " + HrMessage(hr);
    }

    std::wstring DescribeDragDropFailure(const HRESULT hr)
    {
        if (hr == CO_E_NOTINITIALIZED)
        {
            return L"DoDragDrop failed because OLE is not initialized on the UI thread.";
        }

        if (hr == RPC_E_CHANGED_MODE)
        {
            return L"DoDragDrop failed because the UI thread is using a different COM apartment mode.";
        }

        return L"DoDragDrop failed: " + HrMessage(hr);
    }

    FORMATETC MakeFormat(CLIPFORMAT format)
    {
        FORMATETC value{};
        value.cfFormat = format;
        value.dwAspect = DVASPECT_CONTENT;
        value.lindex = -1;
        value.tymed = TYMED_HGLOBAL;
        return value;
    }

    bool MatchesFormat(const FORMATETC& candidate, const FORMATETC& requested)
    {
        return candidate.cfFormat == requested.cfFormat &&
            candidate.dwAspect == requested.dwAspect &&
            (requested.tymed & candidate.tymed) != 0;
    }

    bool CanFocusWindow(const HWND hwnd)
    {
        return hwnd && IsWindow(hwnd) && IsWindowEnabled(hwnd);
    }

    void RestoreFocusAtDropPoint()
    {
        POINT point{};
        if (!GetCursorPos(&point))
        {
            return;
        }

        const HWND targetWindow = WindowFromPoint(point);
        if (!CanFocusWindow(targetWindow))
        {
            return;
        }

        HWND rootWindow = GetAncestor(targetWindow, GA_ROOT);
        if (!CanFocusWindow(rootWindow))
        {
            rootWindow = targetWindow;
        }

        SetForegroundWindow(rootWindow);

        const DWORD currentThreadId = GetCurrentThreadId();
        const DWORD targetThreadId = GetWindowThreadProcessId(rootWindow, nullptr);
        if (targetThreadId == 0)
        {
            return;
        }

        const bool attached = currentThreadId != targetThreadId &&
            AttachThreadInput(currentThreadId, targetThreadId, TRUE) != FALSE;

        if (currentThreadId == targetThreadId || attached)
        {
            SetActiveWindow(rootWindow);
            SetFocus(targetWindow);
        }

        if (attached)
        {
            AttachThreadInput(currentThreadId, targetThreadId, FALSE);
        }
    }

    class FormatEtcEnumerator final : public IEnumFORMATETC
    {
    public:
        explicit FormatEtcEnumerator(std::vector<FORMATETC> formats, size_t index = 0)
            : _formats(std::move(formats))
            , _index(index)
        {
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
        {
            if (!ppvObject)
            {
                return E_POINTER;
            }

            if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC)
            {
                *ppvObject = static_cast<IEnumFORMATETC*>(this);
                AddRef();
                return S_OK;
            }

            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&_refCount));
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const auto count = static_cast<ULONG>(InterlockedDecrement(&_refCount));
            if (count == 0)
            {
                delete this;
            }
            return count;
        }

        HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC* elements, ULONG* fetched) override
        {
            if (count == 0)
            {
                return S_OK;
            }
            if (!elements)
            {
                return E_POINTER;
            }

            ULONG copied = 0;
            while (_index < _formats.size() && copied < count)
            {
                elements[copied] = _formats[_index];
                elements[copied].ptd = nullptr;
                ++_index;
                ++copied;
            }

            if (fetched)
            {
                *fetched = copied;
            }

            return copied == count ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE Skip(ULONG count) override
        {
            _index = std::min(_formats.size(), _index + count);
            return _index < _formats.size() ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE Reset() override
        {
            _index = 0;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** clone) override
        {
            if (!clone)
            {
                return E_POINTER;
            }

            *clone = new FormatEtcEnumerator(_formats, _index);
            return *clone ? S_OK : E_OUTOFMEMORY;
        }

    private:
        ~FormatEtcEnumerator() = default;

        LONG _refCount{1};
        std::vector<FORMATETC> _formats;
        size_t _index{0};
    };

    class FileDataObject final : public IDataObject
    {
    public:
        explicit FileDataObject(std::filesystem::path filePath)
            : _filePath(std::move(filePath))
            , _preferredDropEffectFormat(static_cast<CLIPFORMAT>(RegisterClipboardFormatW(kPreferredDropEffectFormat)))
        {
            _formats.push_back(MakeFormat(CF_HDROP));
            if (_preferredDropEffectFormat != 0)
            {
                _formats.push_back(MakeFormat(_preferredDropEffectFormat));
            }
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
        {
            if (!ppvObject)
            {
                return E_POINTER;
            }

            if (riid == IID_IUnknown || riid == IID_IDataObject)
            {
                *ppvObject = static_cast<IDataObject*>(this);
                AddRef();
                return S_OK;
            }

            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&_refCount));
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const auto count = static_cast<ULONG>(InterlockedDecrement(&_refCount));
            if (count == 0)
            {
                delete this;
            }
            return count;
        }

        HRESULT STDMETHODCALLTYPE GetData(FORMATETC* formatEtc, STGMEDIUM* medium) override
        {
            if (!formatEtc || !medium)
            {
                return E_POINTER;
            }

            const auto match = std::find_if(_formats.begin(), _formats.end(), [formatEtc](const auto& candidate)
            {
                return MatchesFormat(candidate, *formatEtc);
            });

            if (match == _formats.end())
            {
                return DV_E_FORMATETC;
            }

            ZeroMemory(medium, sizeof(*medium));

            HGLOBAL global = nullptr;
            if (formatEtc->cfFormat == CF_HDROP)
            {
                const auto widePath = _filePath.wstring();
                const SIZE_T pathBytes = (widePath.size() + 1) * sizeof(wchar_t);
                const SIZE_T totalBytes = sizeof(DropFilesHeader) + pathBytes + sizeof(wchar_t);

                global = GlobalAlloc(GHND, totalBytes);
                if (!global)
                {
                    return E_OUTOFMEMORY;
                }

                auto* dropFiles = static_cast<DropFilesHeader*>(GlobalLock(global));
                if (!dropFiles)
                {
                    GlobalFree(global);
                    return E_OUTOFMEMORY;
                }
                ZeroMemory(dropFiles, sizeof(*dropFiles));
                dropFiles->pFiles = sizeof(DropFilesHeader);
                dropFiles->fWide = TRUE;

                auto* pathBuffer = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(dropFiles) + sizeof(DropFilesHeader));
                memcpy(pathBuffer, widePath.c_str(), pathBytes);
                pathBuffer[widePath.size() + 1] = L'\0';
                GlobalUnlock(global);
            }
            else if (_preferredDropEffectFormat != 0 && formatEtc->cfFormat == _preferredDropEffectFormat)
            {
                global = GlobalAlloc(GHND, sizeof(DWORD));
                if (!global)
                {
                    return E_OUTOFMEMORY;
                }

                auto* effect = static_cast<DWORD*>(GlobalLock(global));
                if (!effect)
                {
                    GlobalFree(global);
                    return E_OUTOFMEMORY;
                }
                *effect = DROPEFFECT_COPY;
                GlobalUnlock(global);
            }
            else
            {
                return DV_E_FORMATETC;
            }

            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = global;
            medium->pUnkForRelease = nullptr;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }

        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* formatEtc) override
        {
            if (!formatEtc)
            {
                return E_POINTER;
            }

            const auto match = std::find_if(_formats.begin(), _formats.end(), [formatEtc](const auto& candidate)
            {
                return MatchesFormat(candidate, *formatEtc);
            });
            return match != _formats.end() ? S_OK : DV_E_FORMATETC;
        }

        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* formatEtcOut) override
        {
            if (formatEtcOut)
            {
                formatEtcOut->ptd = nullptr;
            }
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }

        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** enumerator) override
        {
            if (!enumerator)
            {
                return E_POINTER;
            }
            if (direction != DATADIR_GET)
            {
                *enumerator = nullptr;
                return E_NOTIMPL;
            }

            *enumerator = new FormatEtcEnumerator(_formats);
            return *enumerator ? S_OK : E_OUTOFMEMORY;
        }

        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

    private:
        ~FileDataObject() = default;

        LONG _refCount{1};
        std::filesystem::path _filePath;
        CLIPFORMAT _preferredDropEffectFormat{0};
        std::vector<FORMATETC> _formats;
    };

    class DropSource final : public IDropSource
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
        {
            if (!ppvObject)
            {
                return E_POINTER;
            }

            if (riid == IID_IUnknown || riid == IID_IDropSource)
            {
                *ppvObject = static_cast<IDropSource*>(this);
                AddRef();
                return S_OK;
            }

            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&_refCount));
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const auto count = static_cast<ULONG>(InterlockedDecrement(&_refCount));
            if (count == 0)
            {
                delete this;
            }
            return count;
        }

        HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override
        {
            if (escapePressed)
            {
                return DRAGDROP_S_CANCEL;
            }

            if ((keyState & MK_LBUTTON) == 0)
            {
                return DRAGDROP_S_DROP;
            }

            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
        {
            return DRAGDROP_S_USEDEFAULTCURSORS;
        }

    private:
        ~DropSource() = default;

        LONG _refCount{1};
    };
}

namespace oneshot
{
    bool DragDropService::StartFileDrag(HWND sourceWindow, const std::filesystem::path& filePath, std::wstring& error) const
    {
        error.clear();

        if (!std::filesystem::exists(filePath))
        {
            error = L"Drag image is missing: " + filePath.wstring();
            return false;
        }

        const HRESULT oleHr = OleInitialize(nullptr);
        if (oleHr != S_OK && oleHr != S_FALSE)
        {
            error = DescribeOleInitFailure(oleHr);
            return false;
        }

        if (sourceWindow)
        {
            SetForegroundWindow(sourceWindow);
        }

        auto* dataObject = new FileDataObject(filePath);
        auto* dropSource = new DropSource();
        DWORD effect = DROPEFFECT_NONE;
        const HRESULT hr = DoDragDrop(dataObject, dropSource, DROPEFFECT_COPY, &effect);
        dataObject->Release();
        dropSource->Release();

        if (hr == DRAGDROP_S_DROP && (effect & DROPEFFECT_COPY) != 0)
        {
            RestoreFocusAtDropPoint();
            OleUninitialize();
            return true;
        }

        if (hr == DRAGDROP_S_CANCEL)
        {
            OleUninitialize();
            return false;
        }

        if (FAILED(hr))
        {
            error = DescribeDragDropFailure(hr);
            OleUninitialize();
            return false;
        }

        if (effect == DROPEFFECT_NONE)
        {
            error = L"Drop target did not accept the image file.";
        }
        else
        {
            error = L"Drop completed without a copy effect.";
        }

        OleUninitialize();
        return false;
    }
}
