#ifndef __INC_DCOMP__
#define __INC_DCOMP__

#include <unknwn.h>
#include <dcomptypes_c.h>
#include <d2dbasetypes.h>
#ifndef D3DMATRIX_DEFINED
#include <d3d9types.h>
#endif
#include <dcompanimation_c.h>
#include <d2d1_1.h>

/* mingw-w64 header fixups */
typedef struct D2D_MATRIX_3X2_F D2D_MATRIX_3X2_F;
typedef struct D2D_RECT_F D2D_RECT_F;

typedef interface IDCompositionDevice IDCompositionDevice;
typedef interface IDCompositionTarget IDCompositionTarget;
typedef interface IDCompositionVisual IDCompositionVisual;
typedef interface IDCompositionTransform IDCompositionTransform;
typedef interface IDCompositionTransform3D IDCompositionTransform3D;
typedef interface IDCompositionTranslateTransform IDCompositionTranslateTransform;
typedef interface IDCompositionTranslateTransform3D IDCompositionTranslateTransform3D;
typedef interface IDCompositionScaleTransform IDCompositionScaleTransform;
typedef interface IDCompositionScaleTransform3D IDCompositionScaleTransform3D;
typedef interface IDCompositionRotateTransform IDCompositionRotateTransform;
typedef interface IDCompositionRotateTransform3D IDCompositionRotateTransform3D;
typedef interface IDCompositionSkewTransform IDCompositionSkewTransform;
typedef interface IDCompositionMatrixTransform IDCompositionMatrixTransform;
typedef interface IDCompositionMatrixTransform3D IDCompositionMatrixTransform3D;
typedef interface IDCompositionEffect IDCompositionEffect;
typedef interface IDCompositionEffectGroup IDCompositionEffectGroup;
typedef interface IDCompositionClip IDCompositionClip;
typedef interface IDCompositionRectangleClip IDCompositionRectangleClip;
typedef interface IDCompositionAnimation IDCompositionAnimation;
typedef interface IDCompositionSurface IDCompositionSurface;
typedef interface IDCompositionVirtualSurface IDCompositionVirtualSurface;

STDAPI DCompositionCreateDevice(IDXGIDevice *dxgiDevice, REFIID iid,
    void **dcompositionDevice);
STDAPI DCompositionCreateDevice2(IUnknown *renderingDevice, REFIID iid,
    void **dcompositionDevice);
STDAPI DCompositionCreateSurfaceHandle(DWORD desiredAccess,
    SECURITY_ATTRIBUTES *securityAttributes, HANDLE *surfaceHandle);

#undef INTERFACE
#define INTERFACE IDCompositionDevice
DECLARE_INTERFACE_(IDCompositionDevice, IUnknown) {
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    STDMETHOD(Commit)(THIS) PURE;
    STDMETHOD(WaitForCommitCompletion)(THIS) PURE;
    STDMETHOD(GetFrameStatistics)(THIS_
        DCOMPOSITION_FRAME_STATISTICS *statistics) PURE;
    STDMETHOD(CreateTargetForHwnd)(THIS_
        HWND hwnd,
        BOOL topmost,
        IDCompositionTarget **target) PURE;
    STDMETHOD(CreateVisual)(THIS_
        IDCompositionVisual **visual) PURE;
    STDMETHOD(CreateSurface)(THIS_
        UINT width,
        UINT height,
        DXGI_FORMAT pixelFormat,
        DXGI_ALPHA_MODE alphaMode,
        IDCompositionSurface **surface) PURE;
    STDMETHOD(CreateVirtualSurface)(THIS_
        UINT initialWidth,
        UINT initialHeight,
        DXGI_FORMAT pixelFormat,
        DXGI_ALPHA_MODE alphaMode,
        IDCompositionVirtualSurface **virtualSurface) PURE;
    STDMETHOD(CreateSurfaceFromHandle)(THIS_
        HANDLE handle,
        IUnknown **surface) PURE;
    STDMETHOD(CreateSurfaceFromHwnd)(THIS_
        HWND hwnd,
        IUnknown **surface) PURE;
    STDMETHOD(CreateTranslateTransform)(THIS_
        IDCompositionTranslateTransform **translateTransform) PURE;
    STDMETHOD(CreateScaleTransform)(THIS_
        IDCompositionScaleTransform **scaleTransform) PURE;
    STDMETHOD(CreateRotateTransform)(THIS_
        IDCompositionRotateTransform **rotateTransform) PURE;
    STDMETHOD(CreateSkewTransform)(THIS_
        IDCompositionSkewTransform **skewTransform) PURE;
    STDMETHOD(CreateMatrixTransform)(THIS_
        IDCompositionMatrixTransform **matrixTransform) PURE;
    STDMETHOD(CreateTransformGroup)(THIS_
        IDCompositionTransform **transforms,
        UINT elements,
        IDCompositionTransform **transformGroup) PURE;
    STDMETHOD(CreateTranslateTransform3D)(THIS_
        IDCompositionTranslateTransform3D **translateTransform3D) PURE;
    STDMETHOD(CreateScaleTransform3D)(THIS_
        IDCompositionScaleTransform3D **scaleTransform3D) PURE;
    STDMETHOD(CreateRotateTransform3D)(THIS_
        IDCompositionRotateTransform3D **rotateTransform3D) PURE;
    STDMETHOD(CreateMatrixTransform3D)(THIS_
        IDCompositionMatrixTransform3D **matrixTransform3D) PURE;
    STDMETHOD(CreateTransform3DGroup)(THIS_
        IDCompositionTransform3D **transforms3D,
        UINT elements,
        IDCompositionTransform3D **transform3DGroup) PURE;
    STDMETHOD(CreateEffectGroup)(THIS_
        IDCompositionEffectGroup **effectGroup) PURE;
    STDMETHOD(CreateRectangleClip)(THIS_
        IDCompositionRectangleClip **clip) PURE;
    STDMETHOD(CreateAnimation)(THIS_
        IDCompositionAnimation **animation) PURE;
    STDMETHOD(CheckDeviceState)(THIS_
        BOOL *pfValid) PURE;
};

#ifdef COBJMACROS
#define IDCompositionDevice_QueryInterface(self, riid, ppvObject) (self)->lpVtbl->QueryInterface(self, riid, ppvObject)
#define IDCompositionDevice_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IDCompositionDevice_Release(self) (self)->lpVtbl->Release(self)
#define IDCompositionDevice_Commit(self) (self)->lpVtbl->Commit(self)
#define IDCompositionDevice_WaitForCommitCompletion(self) (self)->lpVtbl->WaitForCommitCompletion(self)
#define IDCompositionDevice_GetFrameStatistics(self, statistics) (self)->lpVtbl->GetFrameStatistics(self, statistics)
#define IDCompositionDevice_CreateTargetForHwnd(self, hwnd, topmost, target) (self)->lpVtbl->CreateTargetForHwnd(self, hwnd, topmost, target)
#define IDCompositionDevice_CreateVisual(self, visual) (self)->lpVtbl->CreateVisual(self, visual)
#define IDCompositionDevice_CreateSurface(self, width, height, pixelFormat, alphaMode, surface) (self)->lpVtbl->CreateSurface(self, width, height, pixelFormat, alphaMode, surface)
#define IDCompositionDevice_CreateVirtualSurface(self, initialWidth, initialHeight, pixelFormat, alphaMode, virtualSurface) (self)->lpVtbl->CreateVirtualSurface(self, initialWidth, initialHeight, pixelFormat, alphaMode, virtualSurface)
#define IDCompositionDevice_CreateSurfaceFromHandle(self, handle, surface) (self)->lpVtbl->CreateSurfaceFromHandle(self, handle, surface)
#define IDCompositionDevice_CreateSurfaceFromHwnd(self, hwnd, surface) (self)->lpVtbl->CreateSurfaceFromHwnd(self, hwnd, surface)
#define IDCompositionDevice_CreateTranslateTransform(self, translateTransform) (self)->lpVtbl->CreateTranslateTransform(self, translateTransform)
#define IDCompositionDevice_CreateScaleTransform(self, scaleTransform) (self)->lpVtbl->CreateScaleTransform(self, scaleTransform)
#define IDCompositionDevice_CreateRotateTransform(self, rotateTransform) (self)->lpVtbl->CreateRotateTransform(self, rotateTransform)
#define IDCompositionDevice_CreateSkewTransform(self, skewTransform) (self)->lpVtbl->CreateSkewTransform(self, skewTransform)
#define IDCompositionDevice_CreateMatrixTransform(self, matrixTransform) (self)->lpVtbl->CreateMatrixTransform(self, matrixTransform)
#define IDCompositionDevice_CreateTransformGroup(self, transforms, elements, transformGroup) (self)->lpVtbl->CreateTransformGroup(self, transforms, elements, transformGroup)
#define IDCompositionDevice_CreateTranslateTransform3D(self, translateTransform3D) (self)->lpVtbl->CreateTranslateTransform3D(self, translateTransform3D)
#define IDCompositionDevice_CreateScaleTransform3D(self, scaleTransform3D) (self)->lpVtbl->CreateScaleTransform3D(self, scaleTransform3D)
#define IDCompositionDevice_CreateRotateTransform3D(self, rotateTransform3D) (self)->lpVtbl->CreateRotateTransform3D(self, rotateTransform3D)
#define IDCompositionDevice_CreateMatrixTransform3D(self, matrixTransform3D) (self)->lpVtbl->CreateMatrixTransform3D(self, matrixTransform3D)
#define IDCompositionDevice_CreateTransform3DGroup(self, transforms3D, elements, transform3DGroup) (self)->lpVtbl->CreateTransform3DGroup(self, transforms3D, elements, transform3DGroup)
#define IDCompositionDevice_CreateEffectGroup(self, effectGroup) (self)->lpVtbl->CreateEffectGroup(self, effectGroup)
#define IDCompositionDevice_CreateRectangleClip(self, clip) (self)->lpVtbl->CreateRectangleClip(self, clip)
#define IDCompositionDevice_CreateAnimation(self, animation) (self)->lpVtbl->CreateAnimation(self, animation)
#define IDCompositionDevice_CheckDeviceState(self, pfValid) (self)->lpVtbl->CheckDeviceState(self, pfValid)
#endif

#undef INTERFACE
#define INTERFACE IDCompositionTarget
DECLARE_INTERFACE_(IDCompositionTarget, IUnknown) {
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    STDMETHOD(SetRoot)(THIS_
        IDCompositionVisual* visual) PURE;
};

#ifdef COBJMACROS
#define IDCompositionTarget_QueryInterface(self, riid, ppvObject) (self)->lpVtbl->QueryInterface(self, riid, ppvObject)
#define IDCompositionTarget_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IDCompositionTarget_Release(self) (self)->lpVtbl->Release(self)
#define IDCompositionTarget_SetRoot(self, visual) (self)->lpVtbl->SetRoot(self, visual)
#endif

#undef INTERFACE
#define INTERFACE IDCompositionVisual
DECLARE_INTERFACE_(IDCompositionVisual, IUnknown) {
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    STDMETHOD(SetOffsetX2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetOffsetX)(THIS_
        float offsetX) PURE;
    STDMETHOD(SetOffsetY2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetOffsetY)(THIS_
        float offsetY) PURE;
    STDMETHOD(SetTransform2)(THIS_
        IDCompositionTransform *transform) PURE;
    STDMETHOD(SetTransform)(THIS_
        const D2D_MATRIX_3X2_F *matrix) PURE;
    STDMETHOD(SetTransformParent)(THIS_
        IDCompositionVisual *visual) PURE;
    STDMETHOD(SetEffect)(THIS_
        IDCompositionEffect *effect) PURE;
    STDMETHOD(SetBitmapInterpolationMode)(THIS_
        DCOMPOSITION_BITMAP_INTERPOLATION_MODE interpolationMode) PURE;
    STDMETHOD(SetBorderMode)(THIS_
        DCOMPOSITION_BORDER_MODE borderMode) PURE;
    STDMETHOD(SetClip2)(THIS_
        IDCompositionClip *clip) PURE;
    STDMETHOD(SetClip)(THIS_
        const D2D_RECT_F *rect) PURE;
    STDMETHOD(SetContent)(THIS_
        IUnknown *content) PURE;
    STDMETHOD(AddVisual)(THIS_
        IDCompositionVisual *visual,
        BOOL insertAbove,
        IDCompositionVisual *referenceVisual) PURE;
    STDMETHOD(RemoveVisual)(THIS_
        IDCompositionVisual *visual) PURE;
    STDMETHOD(RemoveAllVisuals)(THIS) PURE;
    STDMETHOD(SetCompositeMode)(THIS_
        DCOMPOSITION_COMPOSITE_MODE compositeMode) PURE;
};

#ifdef COBJMACROS
#define IDCompositionVisual_QueryInterface(self, riid, ppvObject) (self)->lpVtbl->QueryInterface(self, riid, ppvObject)
#define IDCompositionVisual_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IDCompositionVisual_Release(self) (self)->lpVtbl->Release(self)
#define IDCompositionVisual_SetOffsetX(self, offsetX) (self)->lpVtbl->SetOffsetX(self, offsetX)
#define IDCompositionVisual_SetOffsetX2(self, animation) (self)->lpVtbl->SetOffsetX2(self, animation)
#define IDCompositionVisual_SetOffsetY(self, offsetY) (self)->lpVtbl->SetOffsetY(self, offsetY)
#define IDCompositionVisual_SetOffsetY2(self, animation) (self)->lpVtbl->SetOffsetY2(self, animation)
#define IDCompositionVisual_SetTransform(self, matrix) (self)->lpVtbl->SetTransform(self, matrix)
#define IDCompositionVisual_SetTransform2(self, transform) (self)->lpVtbl->SetTransform2(self, transform)
#define IDCompositionVisual_SetTransformParent(self, visual) (self)->lpVtbl->SetTransformParent(self, visual)
#define IDCompositionVisual_SetEffect(self, effect) (self)->lpVtbl->SetEffect(self, effect)
#define IDCompositionVisual_SetBitmapInterpolationMode(self, interpolationMode) (self)->lpVtbl->SetBitmapInterpolationMode(self, interpolationMode)
#define IDCompositionVisual_SetBorderMode(self, borderMode) (self)->lpVtbl->SetBorderMode(self, borderMode)
#define IDCompositionVisual_SetClip(self, rect) (self)->lpVtbl->SetClip(self, rect)
#define IDCompositionVisual_SetClip2(self, clip) (self)->lpVtbl->SetClip2(self, clip)
#define IDCompositionVisual_SetContent(self, content) (self)->lpVtbl->SetContent(self, content)
#define IDCompositionVisual_AddVisual(self, visual, insertAbove, referenceVisual) (self)->lpVtbl->AddVisual(self, visual, insertAbove, referenceVisual)
#define IDCompositionVisual_RemoveVisual(self, visual) (self)->lpVtbl->RemoveVisual(self, visual)
#define IDCompositionVisual_RemoveAllVisuals(self) (self)->lpVtbl->RemoveAllVisuals(self)
#define IDCompositionVisual_SetCompositeMode(self, compositeMode) (self)->lpVtbl->SetCompositeMode(self, compositeMode)
#endif

#undef INTERFACE
#define INTERFACE IDCompositionEffect
DECLARE_INTERFACE_(IDCompositionEffect, IUnknown) {
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
};

#ifdef COBJMACROS
#define IDCompositionEffect_QueryInterface(self, riid, ppvObject) (self)->lpVtbl->QueryInterface(self, riid, ppvObject)
#define IDCompositionEffect_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IDCompositionEffect_Release(self) (self)->lpVtbl->Release(self)
#endif

#undef INTERFACE
#define INTERFACE IDCompositionTransform3D
DECLARE_INTERFACE_(IDCompositionTransform3D, IDCompositionEffect) {
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
};

#ifdef COBJMACROS
#define IDCompositionTransform3D_QueryInterface(self, riid, ppvObject) (self)->lpVtbl->QueryInterface(self, riid, ppvObject)
#define IDCompositionTransform3D_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IDCompositionTransform3D_Release(self) (self)->lpVtbl->Release(self)
#endif

#undef INTERFACE
#define INTERFACE IDCompositionTransform
DECLARE_INTERFACE_(IDCompositionTransform, IDCompositionTransform3D) {
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
};

#ifdef COBJMACROS
#define IDCompositionTransform_QueryInterface(self, riid, ppvObject) (self)->lpVtbl->QueryInterface(self, riid, ppvObject)
#define IDCompositionTransform_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IDCompositionTransform_Release(self) (self)->lpVtbl->Release(self)
#endif

#undef INTERFACE
#define INTERFACE IDCompositionTranslateTransform
DECLARE_INTERFACE_(IDCompositionTranslateTransform, IDCompositionTransform) {
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    STDMETHOD(SetOffsetX2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetOffsetX)(THIS_
        float offsetX) PURE;
    STDMETHOD(SetOffsetY2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetOffsetY)(THIS_
        float offsetY) PURE;
};

#ifdef COBJMACROS
#define IDCompositionTranslateTransform_QueryInterface(self, riid, ppvObject) (self)->lpVtbl->QueryInterface(self, riid, ppvObject)
#define IDCompositionTranslateTransform_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IDCompositionTranslateTransform_Release(self) (self)->lpVtbl->Release(self)
#define IDCompositionTranslateTransform_SetOffsetX(self, offsetX) (self)->lpVtbl->SetOffsetX(self, offsetX)
#define IDCompositionTranslateTransform_SetOffsetX2(self, animation) (self)->lpVtbl->SetOffsetX2(self, animation)
#define IDCompositionTranslateTransform_SetOffsetY(self, offsetY) (self)->lpVtbl->SetOffsetY(self, offsetY)
#define IDCompositionTranslateTransform_SetOffsetY2(self, animation) (self)->lpVtbl->SetOffsetY2(self, animation)
#endif

#undef INTERFACE
#define INTERFACE IDCompositionScaleTransform
DECLARE_INTERFACE_(IDCompositionScaleTransform, IDCompositionTransform) {
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    STDMETHOD(SetScaleX2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetScaleX)(THIS_
        float scaleX) PURE;
    STDMETHOD(SetScaleY2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetScaleY)(THIS_
        float scaleY) PURE;
    STDMETHOD(SetCenterX2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetCenterX)(THIS_
        float centerX) PURE;
    STDMETHOD(SetCenterY2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetCenterY)(THIS_
        float centerY) PURE;
};

#ifdef COBJMACROS
#define IDCompositionScaleTransform_QueryInterface(self, riid, ppvObject) (self)->lpVtbl->QueryInterface(self, riid, ppvObject)
#define IDCompositionScaleTransform_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IDCompositionScaleTransform_Release(self) (self)->lpVtbl->Release(self)
#define IDCompositionScaleTransform_SetScaleX(self, scaleX) (self)->lpVtbl->SetScaleX(self, scaleX)
#define IDCompositionScaleTransform_SetScaleX2(self, animation) (self)->lpVtbl->SetScaleX2(self, animation)
#define IDCompositionScaleTransform_SetScaleY(self, scaleY) (self)->lpVtbl->SetScaleY(self, scaleY)
#define IDCompositionScaleTransform_SetScaleY2(self, animation) (self)->lpVtbl->SetScaleY2(self, animation)
#define IDCompositionScaleTransform_SetCenterX(self, centerX) (self)->lpVtbl->SetCenterX(self, centerX)
#define IDCompositionScaleTransform_SetCenterX2(self, animation) (self)->lpVtbl->SetCenterX2(self, animation)
#define IDCompositionScaleTransform_SetCenterY(self, centerY) (self)->lpVtbl->SetCenterY(self, centerY)
#define IDCompositionScaleTransform_SetCenterY2(self, animation) (self)->lpVtbl->SetCenterY2(self, animation)
#endif

#undef INTERFACE
#define INTERFACE IDCompositionRotateTransform
DECLARE_INTERFACE_(IDCompositionRotateTransform, IDCompositionTransform) {
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    STDMETHOD(SetAngle2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetAngle)(THIS_
        float angle) PURE;
    STDMETHOD(SetCenterX2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetCenterX)(THIS_
        float centerX) PURE;
    STDMETHOD(SetCenterY2)(THIS_
        IDCompositionAnimation *animation) PURE;
    STDMETHOD(SetCenterY)(THIS_
        float centerY) PURE;
};

#ifdef COBJMACROS
#define IDCompositionRotateTransform_QueryInterface(self, riid, ppvObject) (self)->lpVtbl->QueryInterface(self, riid, ppvObject)
#define IDCompositionRotateTransform_AddRef(self) (self)->lpVtbl->AddRef(self)
#define IDCompositionRotateTransform_Release(self) (self)->lpVtbl->Release(self)
#define IDCompositionRotateTransform_SetAngle(self, angle) (self)->lpVtbl->SetAngle(self, angle)
#define IDCompositionRotateTransform_SetAngle2(self, animation) (self)->lpVtbl->SetAngle2(self, animation)
#define IDCompositionRotateTransform_SetCenterX(self, centerX) (self)->lpVtbl->SetCenterX(self, centerX)
#define IDCompositionRotateTransform_SetCenterX2(self, animation) (self)->lpVtbl->SetCenterX2(self, animation)
#define IDCompositionRotateTransform_SetCenterY(self, centerY) (self)->lpVtbl->SetCenterY(self, centerY)
#define IDCompositionRotateTransform_SetCenterY2(self, animation) (self)->lpVtbl->SetCenterY2(self, animation)
#endif

DEFINE_GUID(IID_IDCompositionDevice, 0xc37ea93a, 0xe7aa, 0x450d, 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3);
DEFINE_GUID(IID_IDCompositionTarget, 0xeacdd04c, 0x117e, 0x4e17, 0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89);
DEFINE_GUID(IID_IDCompositionVisual, 0x4d93059d, 0x097b, 0x4651, 0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3);
DEFINE_GUID(IID_IDCompositionEffect, 0xec81b08f, 0xbfcb, 0x4e8d, 0xb1, 0x93, 0xa9, 0x15, 0x58, 0x79, 0x99, 0xe8);
DEFINE_GUID(IID_IDCompositionTransform3D, 0x71185722, 0x246b, 0x41f2, 0xaa, 0xd1, 0x04, 0x43, 0xf7, 0xf4, 0xbf, 0xc2);
DEFINE_GUID(IID_IDCompositionTransform, 0xfd55faa7, 0x37e0, 0x4c20, 0x95, 0xd2, 0x9b, 0xe4, 0x5b, 0xc3, 0x3f, 0x55);
DEFINE_GUID(IID_IDCompositionTranslateTransform, 0x06791122, 0xc6f0, 0x417d, 0x83, 0x23, 0x26, 0x9e, 0x98, 0x7f, 0x59, 0x54);
DEFINE_GUID(IID_IDCompositionScaleTransform, 0x71fde914, 0x40ef, 0x45ef, 0xbd, 0x51, 0x68, 0xb0, 0x37, 0xc3, 0x39, 0xf9);
DEFINE_GUID(IID_IDCompositionRotateTransform, 0x641ed83c, 0xae96, 0x46c5, 0x90, 0xdc, 0x32, 0x77, 0x4c, 0xc5, 0xc6, 0xd5);
DEFINE_GUID(IID_IDCompositionSkewTransform, 0xe57aa735, 0xdcdb, 0x4c72, 0x9c, 0x61, 0x05, 0x91, 0xf5, 0x88, 0x89, 0xee);
DEFINE_GUID(IID_IDCompositionMatrixTransform, 0x16cdff07, 0xc503, 0x419c, 0x83, 0xf2, 0x09, 0x65, 0xc7, 0xaf, 0x1f, 0xa6);
DEFINE_GUID(IID_IDCompositionEffectGroup, 0xa7929a74, 0xe6b2, 0x4bd6, 0x8b, 0x95, 0x40, 0x40, 0x11, 0x9c, 0xa3, 0x4d);
DEFINE_GUID(IID_IDCompositionTranslateTransform3D, 0x91636d4b, 0x9ba1, 0x4532, 0xaa, 0xf7, 0xe3, 0x34, 0x49, 0x94, 0xd7, 0x88);
DEFINE_GUID(IID_IDCompositionScaleTransform3D, 0x2a9e9ead, 0x364b, 0x4b15, 0xa7, 0xc4, 0xa1, 0x99, 0x7f, 0x78, 0xb3, 0x89);
DEFINE_GUID(IID_IDCompositionRotateTransform3D, 0xd8f5b23f, 0xd429, 0x4a91, 0xb5, 0x5a, 0xd2, 0xf4, 0x5f, 0xd7, 0x5b, 0x18);
DEFINE_GUID(IID_IDCompositionMatrixTransform3D, 0x4b3363f0, 0x643b, 0x41b7, 0xb6, 0xe0, 0xcc, 0xf2, 0x2d, 0x34, 0x46, 0x7c);
DEFINE_GUID(IID_IDCompositionClip, 0x64ac3703, 0x9d3f, 0x45ec, 0xa1, 0x09, 0x7c, 0xac, 0x0e, 0x7a, 0x13, 0xa7);
DEFINE_GUID(IID_IDCompositionRectangleClip, 0x9842ad7d, 0xd9cf, 0x4908, 0xae, 0xd7, 0x48, 0xb5, 0x1d, 0xa5, 0xe7, 0xc2);
DEFINE_GUID(IID_IDCompositionSurface, 0xbb8a4953, 0x2c99, 0x4f5a, 0x96, 0xf5, 0x48, 0x19, 0x02, 0x7f, 0xa3, 0xac);
DEFINE_GUID(IID_IDCompositionVirtualSurface, 0xae471c51, 0x5f53, 0x4a24, 0x8d, 0x3e, 0xd0, 0xc3, 0x9c, 0x30, 0xb3, 0xf0);
DEFINE_GUID(IID_IDCompositionDevice2, 0x75f6468d, 0x1b8e, 0x447c, 0x9b, 0xc6, 0x75, 0xfe, 0xa8, 0x0b, 0x5b, 0x25);
DEFINE_GUID(IID_IDCompositionDesktopDevice, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
DEFINE_GUID(IID_IDCompositionDeviceDebug, 0xa1a3c64a, 0x224f, 0x4a81, 0x97, 0x73, 0x4f, 0x03, 0xa8, 0x9d, 0x3c, 0x6c);
DEFINE_GUID(IID_IDCompositionSurfaceFactory, 0xe334bc12, 0x3937, 0x4e02, 0x85, 0xeb, 0xfc, 0xf4, 0xeb, 0x30, 0xd2, 0xc8);
DEFINE_GUID(IID_IDCompositionVisual2, 0xe8de1639, 0x4331, 0x4b26, 0xbc, 0x5f, 0x6a, 0x32, 0x1d, 0x34, 0x7a, 0x85);
DEFINE_GUID(IID_IDCompositionVisualDebug, 0xfed2b808, 0x5eb4, 0x43a0, 0xae, 0xa3, 0x35, 0xf6, 0x52, 0x80, 0xf9, 0x1b);

#endif
