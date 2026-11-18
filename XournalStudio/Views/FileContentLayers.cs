using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using Windows.UI;
using XournalStudio.Model;
using Matrix = Microsoft.UI.Xaml.Media.Matrix;

namespace XournalStudio.Views;

/// <summary>
/// What a page shows from its file below the ink and does not edit: texts (and links) and the strokes Windows ink cannot
/// draw (filled, styled or damaged ones), as XAML elements in a layer of the page view; the ruling (<see cref="BackgroundPattern"/>).
/// </summary>
internal static class FileContentLayers
{
    /// <summary>
    /// The strokes shown from the file (filled, styled or damaged ones, <see cref="StrokeConverter.NeedsOwnDrawing"/>):
    /// their <see cref="StrokeShape.Parts"/>, as the export draws them.
    /// </summary>
    public static void ShowDrawnStrokes(XoppPage page, Microsoft.UI.Xaml.Controls.Canvas layer)
    {
        foreach (var element in page.DrawnStrokes)
        {
            if (StrokeConverter.ReadShape(element) is not { } shape)
            {
                continue;
            }
            // A filled highlighter upstream is one translucent shape (filling and line drawn opaque into a mask, blended
            // with the filling's alpha). XAML applies opacity per shape (a Canvas' Opacity and BitmapCache did not group
            // them, sandbox 2026-11-16): both get that alpha, the line over the filling comes out a little stronger.
            var color = shape.Highlighter && shape.Fill >= 0
                ? Color.FromArgb((byte)Math.Clamp(shape.Fill, 0, 255), shape.Color.R, shape.Color.G, shape.Color.B)
                : shape.Color;
            var cap = shape.Cap switch
            {
                StrokeCap.Butt => PenLineCap.Flat,
                StrokeCap.Square => PenLineCap.Square,
                _ => PenLineCap.Round,
            };
            foreach (var part in shape.Parts())
            {
                layer.Children.Add(part switch
                {
                    StrokeFill fill => new Microsoft.UI.Xaml.Shapes.Polygon
                    {
                        Points = PointsOf(fill.Points),
                        Fill = new SolidColorBrush(Color.FromArgb((byte)Math.Clamp(shape.Fill, 0, 255), color.R, color.G, color.B)),
                        FillRule = FillRule.Nonzero,
                    },
                    StrokeDot dot => Dot(dot, color),
                    StrokeLine line => Line(shape, line, color, cap),
                    _ => throw new InvalidOperationException(part.GetType().Name),
                });
            }
        }
    }

    // A PointCollection belongs to one shape: each part gets its own.
    private static PointCollection PointsOf(IEnumerable<Point> points)
    {
        var collection = new PointCollection();
        foreach (var p in points)
        {
            collection.Add(p);
        }
        return collection;
    }

    private static Microsoft.UI.Xaml.Shapes.Shape Dot(StrokeDot dot, Color color)
    {
        Microsoft.UI.Xaml.Shapes.Shape shape = dot.Square
            ? new Microsoft.UI.Xaml.Shapes.Rectangle()
            : new Microsoft.UI.Xaml.Shapes.Ellipse();
        shape.Width = shape.Height = dot.Size;
        shape.Fill = new SolidColorBrush(color);
        Microsoft.UI.Xaml.Controls.Canvas.SetLeft(shape, dot.Center.X - dot.Size / 2);
        Microsoft.UI.Xaml.Controls.Canvas.SetTop(shape, dot.Center.Y - dot.Size / 2);
        return shape;
    }

    // XAML dash lengths and offset are in multiples of the thickness (StrokeShape.DashesFor).
    private static Microsoft.UI.Xaml.Shapes.Shape Line(StrokeShape shape, StrokeLine line, Color color, PenLineCap cap)
    {
        DoubleCollection? dash = null;
        if (shape.DashesFor(line.Width) is { } dashes)
        {
            dash = new DoubleCollection();
            foreach (var d in dashes)
            {
                dash.Add(d);
            }
        }
        return new Microsoft.UI.Xaml.Shapes.Polyline
        {
            Points = PointsOf(line.Points),
            Stroke = new SolidColorBrush(color),
            StrokeThickness = line.Width,
            StrokeStartLineCap = cap,
            StrokeEndLineCap = cap,
            StrokeDashCap = cap,
            StrokeLineJoin = PenLineJoin.Round,
            StrokeDashArray = dash,
            StrokeDashOffset = dash != null ? line.DashOffset / line.Width : 0,
        };
    }

    /// <summary>The file's texts (and links) as TextBlocks: font, size, color, wrap width, alignment and placement as stored.</summary>
    public static void ShowTexts(XoppPage page, Microsoft.UI.Xaml.Controls.Canvas layer)
    {
        foreach (var element in page.Texts)
        {
            var text = XoppText.Read(element);
            var block = new TextBlock
            {
                Text = text.Content,
                FontFamily = new FontFamily(text.WindowsFamily),
                FontSize = text.Size, // points, as the page's units
                FontWeight = text.Bold ? Microsoft.UI.Text.FontWeights.Bold : Microsoft.UI.Text.FontWeights.Normal,
                FontStyle = text.Italic ? Windows.UI.Text.FontStyle.Italic : Windows.UI.Text.FontStyle.Normal,
                Foreground = new SolidColorBrush(text.Color),
                IsTextScaleFactorEnabled = false, // the page's size, not the system's text size
                TextAlignment = text.Justify ? TextAlignment.Justify : text.Alignment switch
                {
                    XoppTextAlignment.Center => TextAlignment.Center,
                    XoppTextAlignment.Right => TextAlignment.Right,
                    _ => TextAlignment.Left,
                },
            };
            if (text.Wrap is { } wrap)
            {
                block.Width = wrap;
                block.TextWrapping = TextWrapping.Wrap;
            }
            var m = text.Transform;
            block.RenderTransform = new MatrixTransform
            {
                Matrix = new Matrix { M11 = m.M11, M12 = m.M12, M21 = m.M21, M22 = m.M22, OffsetX = m.M31, OffsetY = m.M32 },
            };
            layer.Children.Add(block);
        }
    }
}

/// <summary>A page's ruling lines of one color and width (<see cref="RulingLines"/>) as a XAML Path.</summary>
internal static class BackgroundPattern
{
    public static Microsoft.UI.Xaml.Shapes.Path Create(RulingLines ruling)
    {
        var group = new GeometryGroup();
        foreach (var (from, to) in ruling.Lines)
        {
            group.Children.Add(new LineGeometry { StartPoint = from, EndPoint = to });
        }
        double r = ruling.Width / 2;
        foreach (var dot in ruling.Dots)
        {
            group.Children.Add(new EllipseGeometry { Center = dot, RadiusX = r, RadiusY = r });
        }
        var brush = new SolidColorBrush(ruling.Color);
        return new Microsoft.UI.Xaml.Shapes.Path
        {
            Data = group,
            Stroke = ruling.Lines.Count > 0 ? brush : null,
            StrokeThickness = ruling.Width,
            Fill = ruling.Dots.Count > 0 ? brush : null,
        };
    }
}
