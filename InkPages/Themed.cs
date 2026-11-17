using Microsoft.UI.Xaml.Markup;
using Rect = Windows.Foundation.Rect;
using XamlCanvas = Microsoft.UI.Xaml.Controls.Canvas;
using FrameworkElement = Microsoft.UI.Xaml.FrameworkElement;

namespace InkPages;

/// <summary>
/// Elements whose brushes and styles are <c>{ThemeResource}</c>s, made from XAML markup: they follow the theme (an
/// element's RequestedTheme and theme changes) as elements declared in XAML do – a resource looked up in code would keep
/// the colors of the moment and ignore a window's own theme. Needs WinUI's XamlControlsResources merged by the app (as
/// every WinUI app does).
/// </summary>
internal static class Themed
{
    private const string Presentation = "xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'";

    /// <summary>An element from its tag and attributes, e.g. <c>Load&lt;Grid&gt;("Grid", "Background='{ThemeResource ...}'")</c>.</summary>
    public static T Load<T>(string element, string attributes) where T : class =>
        (T)XamlReader.Load($"<{element} {Presentation} {attributes} />");

    /// <summary>The tools' dashed accent outline (lasso, selection rectangle, selection frame).</summary>
    public static T AccentOutline<T>(string element) where T : class =>
        Load<T>(element, "Stroke='{ThemeResource AccentFillColorDefaultBrush}' StrokeThickness='1' StrokeDashArray='4,2'");
}

/// <summary>Placing feedback on a page's overlay (a Canvas in page units).</summary>
internal static class Overlay
{
    public static void Place(FrameworkElement element, Rect r)
    {
        XamlCanvas.SetLeft(element, r.X);
        XamlCanvas.SetTop(element, r.Y);
        element.Width = r.Width;
        element.Height = r.Height;
    }
}
