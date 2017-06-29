/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <windows.h>
#include <objidlbase.h>
#include <mmdeviceapi.h>
#include <mmdeviceapi-extra.h>

#include "osdep/atomic.h"
#include "mpv_talloc.h"
#include "ao_wasapi.h"

struct activatehandler {
    IActivateAudioInterfaceCompletionHandler iface;
    atomic_int ref_cnt;
    void (*cb)(void *ctx, IActivateAudioInterfaceAsyncOperation *op);
    void *ctx;
};

static STDMETHODIMP ActivateHandler_QueryInterface(
    IActivateAudioInterfaceCompletionHandler *self, REFIID riid,
    void **ppvObject)
{
    // The IAgileObject interface is used to mark this object as free threaded.
    // Return the base interface because IAgileObject doesn't have methods.
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IAgileObject) ||
        IsEqualIID(riid, &IID_IActivateAudioInterfaceCompletionHandler))
    {
        *ppvObject = self;
        IActivateAudioInterfaceCompletionHandler_AddRef(self);
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static STDMETHODIMP_(ULONG) ActivateHandler_AddRef(
    IActivateAudioInterfaceCompletionHandler *self)
{
    struct activatehandler *t = (struct activatehandler *)self;
    return atomic_fetch_add(&t->ref_cnt, 1) + 1;
}

static STDMETHODIMP_(ULONG) ActivateHandler_Release(
    IActivateAudioInterfaceCompletionHandler *self)
{
    struct activatehandler *t = (struct activatehandler *)self;

    ULONG ref_cnt = atomic_fetch_add(&t->ref_cnt, -1) - 1;
    if (ref_cnt == 0)
        talloc_free(self);
    return ref_cnt;
}

static STDMETHODIMP ActivateHandler_ActivateCompleted(
    IActivateAudioInterfaceCompletionHandler *self,
    IActivateAudioInterfaceAsyncOperation *activateOperation)
{
    struct activatehandler *t = (struct activatehandler *)self;
    t->cb(t->ctx, activateOperation);
    return S_OK;
}

static IActivateAudioInterfaceCompletionHandlerVtbl iactivate_vtbl = {
    .QueryInterface = ActivateHandler_QueryInterface,
    .AddRef = ActivateHandler_AddRef,
    .Release = ActivateHandler_Release,
    .ActivateCompleted = ActivateHandler_ActivateCompleted,
};

static IActivateAudioInterfaceCompletionHandler *ActivateHandler_Create(
    void (*cb)(void *ctx, IActivateAudioInterfaceAsyncOperation *op),
    void *ctx)
{
    struct activatehandler *ia = talloc(NULL, struct activatehandler);
    *ia = (struct activatehandler) {
        .iface = { .lpVtbl = &iactivate_vtbl },
        .cb = cb,
        .ctx = ctx,
    };
    atomic_store(&ia->ref_cnt, 1);

    return &ia->iface;
}

static void evt_cb(void *ctx, IActivateAudioInterfaceAsyncOperation *op)
{
    SetEvent(ctx);
}

HRESULT wasapi_activate_audio_interface(const wchar_t *device,
                                        PROPVARIANT *params,
                                        IAudioClient **aclient)
{
    HRESULT hr;
    HANDLE cevt = NULL;
    IActivateAudioInterfaceCompletionHandler *chandler = NULL;
    IActivateAudioInterfaceAsyncOperation *actop = NULL;
    IUnknown *aclient_punk = NULL;

#if !HAVE_UWP
    HMODULE mmdevapi = LoadLibraryW(L"mmdevapi.dll");
    HRESULT (*ActivateAudioInterfaceAsync)(LPCWSTR, REFIID, PROPVARIANT *,
        IActivateAudioInterfaceCompletionHandler *,
        IActivateAudioInterfaceAsyncOperation **) = (void*)
        GetProcAddress(mmdevapi, "ActivateAudioInterfaceAsync");
    if (!ActivateAudioInterfaceAsync) {
        hr = E_FAIL;
        goto exit_label;
    }
#endif

    cevt = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!cevt) {
        hr = E_FAIL;
        goto exit_label;
    }
    chandler = ActivateHandler_Create(evt_cb, cevt);

    // Apparently call should be on the main thread in a UWP app so it can show
    // a permissions UI. This probably doesn't matter for the renderer device.
    hr = ActivateAudioInterfaceAsync(device, &IID_IAudioClient, params,
                                     chandler, &actop);
    EXIT_ON_ERROR(hr);

    // Wait for audio client activation to complete asynchronously
    WaitForSingleObject(cevt, INFINITE);

    HRESULT actres;
    hr = IActivateAudioInterfaceAsyncOperation_GetActivateResult(actop,
        &actres, &aclient_punk);
    EXIT_ON_ERROR(hr);
    // Check asynchronous result code
    hr = actres;
    EXIT_ON_ERROR(hr);

    hr = IUnknown_QueryInterface(aclient_punk, &IID_IAudioClient,
                                 (void **)aclient);
    EXIT_ON_ERROR(hr);

    hr = S_OK;
exit_label:
    SAFE_RELEASE(aclient_punk);
    SAFE_RELEASE(actop);
    SAFE_RELEASE(chandler);
    if (cevt)
        CloseHandle(cevt);
    return hr;
}
