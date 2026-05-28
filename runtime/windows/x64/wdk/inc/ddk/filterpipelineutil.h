//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (c) Microsoft Corporation. All rights reserved.
//
//--------------------------------------------------------------------------

#ifndef _PRINT_FILTER_UTIL_813b22ee_62f7_4200_
#define _PRINT_FILTER_UTIL_813b22ee_62f7_4200_

#if defined(__cplusplus)

//
// print filter pipeline
//
namespace pfp
{

//
// Helpful when you want to use a print read interface with XML SAX
// which needs an ISequentialStream
//
class PrintReadStreamToSeqStream : public ISequentialStream
{
public:

    PrintReadStreamToSeqStream(
        __in    IPrintReadStream    *pReadStream
        ) : m_cRef(1),
            m_pStream(pReadStream)
    {
        m_pStream->AddRef();
    }

    ~PrintReadStreamToSeqStream()
    {
        m_pStream->Release();
    }

    STDMETHODIMP_(ULONG)
    AddRef(
        VOID
        )
    {
        return InterlockedIncrement(&m_cRef);
    }

    STDMETHODIMP_(ULONG)
    Release(
        VOID
        )
    {
        ULONG cRefCount = InterlockedDecrement(&m_cRef);

        if (cRefCount)
        {
            return cRefCount;
        }

        delete this;

        return 0;
    }

    STDMETHODIMP
    QueryInterface(
        __in      REFIID      riid,
        __out     VOID        **ppv
        )
    {
        HRESULT hRes = E_POINTER;

        if (ppv)
        {
            hRes = E_NOINTERFACE;

            *ppv = NULL;

            if (riid == IID_ISequentialStream)
            {
                *ppv = static_cast<ISequentialStream *>(this);
            }
            else if (riid == IID_IUnknown)
            {
                *ppv = static_cast<IUnknown *>(this);
            }

            if (*ppv)
            {
                AddRef();

                hRes = S_OK;
            }
        }

        return hRes;
    }

    STDMETHODIMP
    Read(
        __out_bcount(cb)   void*    pv,
        __in               ULONG    cb,
        __out              ULONG    *pcbRead
        )
    {
        BOOL bEof;

        return m_pStream->ReadBytes(pv, cb, pcbRead, &bEof);
    }

    STDMETHODIMP
    Write(
        __in_bcount(cb)    void const*    pv,
        __in               ULONG          cb,
        __out              ULONG          *pcbWritten
        )
    {
        UNREFERENCED_PARAMETER(pv);
        UNREFERENCED_PARAMETER(cb);
        UNREFERENCED_PARAMETER(pcbWritten);
        return E_NOTIMPL;
    }

private:

    LONG               m_cRef;
    IPrintReadStream  *m_pStream;
};

//
// Helpful when you want to use a print write interface with XML SAX
// which needs an ISequentialStream
//
class PrintWriteStreamToSeqStream : public ISequentialStream
{
public:

    PrintWriteStreamToSeqStream(
        __in    IPrintWriteStream    *pWriteStream
        ) : m_cRef(1),
            m_pStream(pWriteStream)
    {
        m_pStream->AddRef();
    }

    ~PrintWriteStreamToSeqStream()
    {
        m_pStream->Close();

        m_pStream->Release();
    }

    STDMETHODIMP_(ULONG)
    AddRef(
        VOID
        )
    {
        return InterlockedIncrement(&m_cRef);
    }

    STDMETHODIMP_(ULONG)
    Release(
        VOID
        )
    {
        ULONG cRefCount = InterlockedDecrement(&m_cRef);

        if (cRefCount)
        {
            return cRefCount;
        }

        delete this;

        return 0;
    }

    STDMETHODIMP
    QueryInterface(
        __in      REFIID      riid,
        __out     VOID        **ppv
        )
    {
        HRESULT hRes = E_POINTER;

        if (ppv)
        {
            hRes = E_NOINTERFACE;

            *ppv = NULL;

            if (riid == IID_ISequentialStream)
            {
                *ppv = static_cast<ISequentialStream *>(this);
            }
            else if (riid == IID_IUnknown)
            {
                *ppv = static_cast<IUnknown *>(this);
            }

            if (*ppv)
            {
                AddRef();

                hRes = S_OK;
            }
        }

        return hRes;
    }

    STDMETHODIMP
    Read(
        __out_bcount(cb)   void*    pv,
        __in               ULONG    cb,
        __out              ULONG    *pcbRead
        )
    {
        UNREFERENCED_PARAMETER(pv);
        UNREFERENCED_PARAMETER(cb);
        UNREFERENCED_PARAMETER(pcbRead);
        return E_NOTIMPL;
    }

    STDMETHODIMP
    Write(
        __in_bcount(cb)    void const*    pv,
        __in               ULONG          cb,
        __out              ULONG          *pcbWritten
        )
    {
        return m_pStream->WriteBytes(pv, cb, pcbWritten);
    }

private:

    LONG               m_cRef;
    IPrintWriteStream  *m_pStream;
};

}; // namespace pfp

#endif // if defined(__cplusplus)

#endif // #ifndef _PRINT_FILTER_UTIL_813b22ee-62f7-4200-9c85-73d139eaa579_

