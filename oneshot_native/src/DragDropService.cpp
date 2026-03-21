#include "oneshot_native/DragDropService.h"

#include <objidl.h>
#include <shellapi.h>

namespace
{
    struct DropFilesHeader
    {
        DWORD pFiles;
        POINT pt;
        BOOL fNC;
        BOOL fWide;
    };

    class FileDataObject final : public IDataObject
    {
    public:
        explicit FileDataObject(std::filesystem::path filePath)
            : _filePath(std::move(filePath))
        {
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

            if (formatEtc->cfFormat != CF_HDROP || !(formatEtc->tymed & TYMED_HGLOBAL))
            {
                return DV_E_FORMATETC;
            }

            const auto widePath = _filePath.wstring();
            const SIZE_T pathBytes = (widePath.size() + 1) * sizeof(wchar_t);
            const SIZE_T totalBytes = sizeof(DropFilesHeader) + pathBytes + sizeof(wchar_t);

            HGLOBAL global = GlobalAlloc(GHND, totalBytes);
            if (!global)
            {
                return E_OUTOFMEMORY;
            }

            auto* dropFiles = static_cast<DropFilesHeader*>(GlobalLock(global));
            dropFiles->pFiles = sizeof(DropFilesHeader);
            dropFiles->fWide = TRUE;

            auto* pathBuffer = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(dropFiles) + sizeof(DropFilesHeader));
            memcpy(pathBuffer, widePath.c_str(), pathBytes);
            pathBuffer[widePath.size() + 1] = L'\0';
            GlobalUnlock(global);

            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = global;
            medium->pUnkForRelease = nullptr;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* formatEtc) override
        {
            if (formatEtc && formatEtc->cfFormat == CF_HDROP && (formatEtc->tymed & TYMED_HGLOBAL))
            {
                return S_OK;
            }
            return DV_E_FORMATETC;
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
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

    private:
        ~FileDataObject() = default;

        LONG _refCount{1};
        std::filesystem::path _filePath;
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

            if (!(keyState & MK_LBUTTON))
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
    bool DragDropService::StartFileDrag(HWND sourceWindow, const std::filesystem::path& filePath) const
    {
        if (!std::filesystem::exists(filePath))
        {
            return false;
        }

        auto* dataObject = new FileDataObject(filePath);
        auto* dropSource = new DropSource();
        DWORD effect = DROPEFFECT_NONE;
        const HRESULT hr = DoDragDrop(dataObject, dropSource, DROPEFFECT_COPY, &effect);
        dataObject->Release();
        dropSource->Release();

        POINT point{};
        if (GetCursorPos(&point))
        {
            const HWND target = WindowFromPoint(point);
            if (target)
            {
                SetForegroundWindow(target);
            }
        }

        return hr == DRAGDROP_S_DROP && effect == DROPEFFECT_COPY;
    }
}
