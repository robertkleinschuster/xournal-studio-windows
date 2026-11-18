using System.Runtime.InteropServices;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.DXGI;
using Windows.Data.Pdf;
using Windows.Foundation;
using Windows.Graphics.DirectX.Direct3D11;
using Windows.Graphics.Imaging;

namespace XournalStudio.Services;

/// <summary>
/// Renders PDF pages with Windows.Data.Pdf through its DirectX interop (<c>PdfCreateRenderer</c> /
/// <c>IPdfRendererNative::RenderPageToSurface</c>, windows.data.pdf.interop.h) on a Direct3D device the app owns.
/// <c>PdfPage.RenderToStreamAsync</c> creates a device of its own in a static that is only released when the process
/// exits (DLL detach, other threads already gone); the Intel graphics driver hung the exiting process in that release
/// (device 2026-11-16, see CLAUDE.md). This device is released while the app still runs (<see cref="ShutdownAsync"/>).
/// One instance, calls serialized (the interop docs: one page at a time); PdfDocument/PdfPage are agile, so rendering
/// runs on the thread pool. A request that is cancelled while it waits for its turn is not rendered.
/// </summary>
public sealed class PdfRenderer
{
    public static PdfRenderer Shared { get; } = new();

    private readonly SemaphoreSlim gate = new(1, 1);
    private ID3D11Device? device;
    private IntPtr renderer; // IPdfRendererNative*
    private bool shutDown;

    private PdfRenderer()
    {
    }

    /// <summary>
    /// Renders <paramref name="sourceDips"/> of the page (its whole area when null; DIPs like <see cref="PdfPage.Size"/>)
    /// into a bitmap of <paramref name="width"/> × <paramref name="height"/> pixels on white, or on nothing when
    /// <paramref name="transparent"/> (BGRA8, premultiplied).
    /// Throws <see cref="OperationCanceledException"/> when <paramref name="cancellation"/> is cancelled before the
    /// rendering began (a render that runs is finished).
    /// </summary>
    public async Task<SoftwareBitmap> RenderAsync(PdfPage page, Rect? sourceDips, uint width, uint height,
        CancellationToken cancellation = default, bool transparent = false)
    {
        await gate.WaitAsync(cancellation);
        try
        {
            ObjectDisposedException.ThrowIf(shutDown, this);
            cancellation.ThrowIfCancellationRequested();
            return await Task.Run(() => RenderWithRetryAsync(page, sourceDips, width, height, transparent));
        }
        finally
        {
            gate.Release();
        }
    }

    /// <summary>Releases the device before the process ends (no rendering afterwards).</summary>
    public async Task ShutdownAsync()
    {
        await gate.WaitAsync();
        try
        {
            shutDown = true;
            ReleaseDevice();
        }
        finally
        {
            gate.Release();
        }
    }

    private async Task<SoftwareBitmap> RenderWithRetryAsync(PdfPage page, Rect? sourceDips, uint width, uint height, bool transparent)
    {
        try
        {
            return await RenderOnceAsync(page, sourceDips, width, height, transparent);
        }
        catch (Exception e) when (e.HResult is DxgiDeviceRemoved or DxgiDeviceReset) // COMException or Vortice's SharpGenException
        {
            // The GPU was reset or removed (driver update, sleep): a new device, once.
            DebugLog.Write("pdf render: device lost, recreating", e);
            ReleaseDevice();
            return await RenderOnceAsync(page, sourceDips, width, height, transparent);
        }
    }

    private const int DxgiDeviceRemoved = unchecked((int)0x887A0005), DxgiDeviceReset = unchecked((int)0x887A0007);

    private async Task<SoftwareBitmap> RenderOnceAsync(PdfPage page, Rect? sourceDips, uint width, uint height, bool transparent)
    {
        EnsureDevice();
        var target = new Texture2DDescription
        {
            Width = width,
            Height = height,
            MipLevels = 1,
            ArraySize = 1,
            Format = Format.B8G8R8A8_UNorm,
            SampleDescription = new SampleDescription(1, 0),
            Usage = ResourceUsage.Default,
            BindFlags = BindFlags.RenderTarget | BindFlags.ShaderResource,
        };
        using var texture = device!.CreateTexture2D(target);
        using var surface = texture.QueryInterface<IDXGISurface>();

        var area = sourceDips ?? default; // all zero: the whole page
        var parameters = new PdfRenderParams
        {
            Left = (float)area.X,
            Top = (float)area.Y,
            Right = (float)(area.X + area.Width),
            Bottom = (float)(area.Y + area.Height),
            DestinationWidth = width,
            DestinationHeight = height,
            R = transparent ? 0 : 1, G = transparent ? 0 : 1, B = transparent ? 0 : 1, A = transparent ? 0 : 1,
            IgnoreHighContrast = 1,
        };
        IntPtr pageUnknown = WinRT.MarshalInspectable<object>.FromManaged(page);
        try
        {
            var render = Marshal.GetDelegateForFunctionPointer<RenderPageToSurfaceFn>(
                Marshal.ReadIntPtr(Marshal.ReadIntPtr(renderer), 3 * IntPtr.Size)); // IUnknown (3), then RenderPageToSurface
            Marshal.ThrowExceptionForHR(render(renderer, pageUnknown, surface.NativePointer, default, ref parameters));
        }
        finally
        {
            Marshal.Release(pageUnknown);
        }

        // The pixels into a SoftwareBitmap by Windows itself (on this device; no managed pixel buffer: copying through a
        // byte[] left 11–55 MB of pinned garbage per page, measured 2026-11-17).
        using var d3dSurface = WrapSurface(surface);
        return await SoftwareBitmap.CreateCopyFromSurfaceAsync(d3dSurface, BitmapAlphaMode.Premultiplied);
    }

    // CreateDirect3D11SurfaceFromDXGISurface (windows.graphics.directx.direct3d11.interop.h).
    private static IDirect3DSurface WrapSurface(IDXGISurface surface)
    {
        Marshal.ThrowExceptionForHR(CreateDirect3D11SurfaceFromDXGISurface(surface.NativePointer, out IntPtr inspectable));
        try
        {
            return WinRT.MarshalInterface<IDirect3DSurface>.FromAbi(inspectable);
        }
        finally
        {
            Marshal.Release(inspectable);
        }
    }

    private void EnsureDevice()
    {
        if (device != null)
        {
            return;
        }
        D3D11.D3D11CreateDevice(null, DriverType.Hardware, DeviceCreationFlags.BgraSupport,
            [FeatureLevel.Level_11_1, FeatureLevel.Level_11_0, FeatureLevel.Level_10_1, FeatureLevel.Level_10_0],
            out device, out ID3D11DeviceContext? context).CheckError();
        context?.Dispose(); // not used by the app (the copy into a SoftwareBitmap is Windows')
        using var dxgi = device!.QueryInterface<IDXGIDevice>();
        Marshal.ThrowExceptionForHR(PdfCreateRenderer(dxgi.NativePointer, out renderer));
    }

    private void ReleaseDevice()
    {
        if (renderer != IntPtr.Zero)
        {
            Marshal.Release(renderer);
            renderer = IntPtr.Zero;
        }
        device?.Dispose();
        device = null;
    }

    [DllImport("d3d11.dll", ExactSpelling = true)]
    private static extern int CreateDirect3D11SurfaceFromDXGISurface(IntPtr dxgiSurface, out IntPtr graphicsSurface);

    // windows.data.pdf.interop.h
    [DllImport("Windows.Data.Pdf.dll", ExactSpelling = true)]
    private static extern int PdfCreateRenderer(IntPtr dxgiDevice, out IntPtr renderer);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate int RenderPageToSurfaceFn(IntPtr self, IntPtr pdfPage, IntPtr surface, Point32 offset,
        ref PdfRenderParams parameters);

    [StructLayout(LayoutKind.Sequential)]
    private struct Point32
    {
        public int X, Y;
    }

    // PDF_RENDER_PARAMS: D2D_RECT_F SourceRect, UINT32 DestinationWidth/Height, D2D_COLOR_F BackgroundColor, BOOLEAN.
    [StructLayout(LayoutKind.Sequential)]
    private struct PdfRenderParams
    {
        public float Left, Top, Right, Bottom;
        public uint DestinationWidth, DestinationHeight;
        public float R, G, B, A;
        public byte IgnoreHighContrast;
    }
}
