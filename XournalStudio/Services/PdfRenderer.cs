using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.DXGI;
using Windows.Data.Pdf;
using Windows.Foundation;
using Windows.Graphics.Imaging;

namespace XournalStudio.Services;

/// <summary>
/// Renders PDF pages with Windows.Data.Pdf through its DirectX interop (<c>PdfCreateRenderer</c> /
/// <c>IPdfRendererNative::RenderPageToSurface</c>, windows.data.pdf.interop.h) on a Direct3D device the app owns.
/// <c>PdfPage.RenderToStreamAsync</c> creates a device of its own in a static that is only released when the process
/// exits (DLL detach, other threads already gone); the Intel graphics driver hung the exiting process in that release
/// (device 2026-11-16, see CLAUDE.md). This device is released while the app still runs (<see cref="Shutdown"/>).
/// One instance, calls serialized; PdfDocument/PdfPage are agile, so rendering runs on the thread pool.
/// </summary>
public sealed class PdfRenderer
{
    public static PdfRenderer Shared { get; } = new();

    private readonly SemaphoreSlim gate = new(1, 1);
    private ID3D11Device? device;
    private ID3D11DeviceContext? context;
    private IntPtr renderer; // IPdfRendererNative*
    private bool shutDown;

    private PdfRenderer()
    {
    }

    /// <summary>
    /// Renders <paramref name="sourceDips"/> of the page (its whole area when null; DIPs like <see cref="PdfPage.Size"/>)
    /// into a bitmap of <paramref name="width"/> × <paramref name="height"/> pixels on white (BGRA8, premultiplied).
    /// </summary>
    public async Task<SoftwareBitmap> RenderAsync(PdfPage page, Rect? sourceDips, uint width, uint height)
    {
        await gate.WaitAsync();
        try
        {
            ObjectDisposedException.ThrowIf(shutDown, this);
            return await Task.Run(() => RenderWithRetry(page, sourceDips, width, height));
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

    private SoftwareBitmap RenderWithRetry(PdfPage page, Rect? sourceDips, uint width, uint height)
    {
        try
        {
            return Render(page, sourceDips, width, height);
        }
        catch (Exception e) when (e.HResult is DxgiDeviceRemoved or DxgiDeviceReset) // COMException or Vortice's SharpGenException
        {
            // The GPU was reset or removed (driver update, sleep): a new device, once.
            DebugLog.Write("pdf render: device lost, recreating", e);
            ReleaseDevice();
            return Render(page, sourceDips, width, height);
        }
    }

    private const int DxgiDeviceRemoved = unchecked((int)0x887A0005), DxgiDeviceReset = unchecked((int)0x887A0007);

    private SoftwareBitmap Render(PdfPage page, Rect? sourceDips, uint width, uint height)
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
            R = 1, G = 1, B = 1, A = 1,
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

        // Read the pixels back through a CPU-readable copy.
        var readable = target with { Usage = ResourceUsage.Staging, BindFlags = BindFlags.None, CPUAccessFlags = CpuAccessFlags.Read };
        using var staging = device.CreateTexture2D(readable);
        context!.CopyResource(staging, texture);
        var mapped = context.Map(staging, 0, MapMode.Read);
        var pixels = new byte[width * height * 4];
        try
        {
            for (int y = 0; y < height; y++)
            {
                Marshal.Copy(mapped.DataPointer + (nint)(y * mapped.RowPitch), pixels, (int)(y * width * 4), (int)(width * 4));
            }
        }
        finally
        {
            context.Unmap(staging, 0);
        }
        return SoftwareBitmap.CreateCopyFromBuffer(pixels.AsBuffer(), BitmapPixelFormat.Bgra8, (int)width, (int)height,
            BitmapAlphaMode.Premultiplied);
    }

    private void EnsureDevice()
    {
        if (device != null)
        {
            return;
        }
        D3D11.D3D11CreateDevice(null, DriverType.Hardware, DeviceCreationFlags.BgraSupport,
            [FeatureLevel.Level_11_1, FeatureLevel.Level_11_0, FeatureLevel.Level_10_1, FeatureLevel.Level_10_0],
            out device, out context).CheckError();
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
        context?.Dispose();
        context = null;
        device?.Dispose();
        device = null;
    }

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
