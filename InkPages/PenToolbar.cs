using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace InkPages;

/// <summary>A pen preset: one tap selects it; it keeps its own color and width (an InkToolbar pen button of its own).</summary>
/// <param name="ColorIndex">Index into the palette.</param>
public sealed record PenPreset(int ColorIndex, double Width);

/// <summary>A palette color with its name (the preset buttons are named after their color).</summary>
public sealed record NamedColor(Color Color, string Name);

/// <summary>
/// The writing surface's toolbar, built from WinUI's own controls in one dense, left-aligned row (pen-operated, Windows
/// defaults, no custom styling), in groups: undo/redo; then Windows' <see cref="InkToolbar"/> with the pen presets, the
/// highlighter presets (one tap each; tap again for color and width), the eraser, select, shapes (with shape
/// recognition in its options), the ruler and the eraser's mode (whole strokes or a circle: a toggle, which the pen's
/// eraser button follows too); then the selection's commands and the app's own buttons
/// (<see cref="Extensions"/>). The control raises nothing itself: it exposes its WinUI controls, and the app (or this
/// library's <see cref="InkWorkspace"/>) wires their events.
/// </summary>
public sealed class PenToolbar : UserControl
{
    private const double CommandWidth = 44; // dense: operated with the pen, not optimized for fingers
    private readonly StackPanel extensions = new() { Orientation = Orientation.Horizontal, Spacing = 2 };
    private readonly string presetTip;

    /// <param name="palette">The pens' colors (a preset's color is an index into it).</param>
    /// <param name="highlighterPalette">The highlighters' colors.</param>
    public PenToolbar(PenToolbarTexts texts, IReadOnlyList<NamedColor> palette, IReadOnlyList<PenPreset> pens,
        IReadOnlyList<NamedColor> highlighterPalette, IReadOnlyList<PenPreset> highlighters)
    {
        if (pens.Count == 0 || palette.Count == 0)
        {
            throw new ArgumentException("The toolbar needs at least one pen preset and one color.", nameof(pens));
        }
        if (highlighters.Count > 0 && highlighterPalette.Count == 0)
        {
            throw new ArgumentException("Highlighter presets need colors.", nameof(highlighterPalette));
        }
        if (texts.ShapeKinds.Count != Enum.GetValues<ShapeKind>().Length)
        {
            throw new ArgumentException($"{nameof(PenToolbarTexts.ShapeKinds)} names each {nameof(InkPages.ShapeKind)} in its order.", nameof(texts));
        }
        presetTip = texts.PresetTip;
        Ink = new InkToolbar { InitialControls = InkToolbarInitialControls.None, VerticalAlignment = VerticalAlignment.Center };
        // Presets: InkToolbar takes one built-in ballpoint and highlighter button only; any number of custom pen buttons,
        // each with its own palette index and width and Windows' own pen flyout (InkToolbarPenConfigurationControl).
        Pens = pens.Select((preset, i) =>
            Preset(new BallpointPen(), texts.Pen, palette, preset, 0.5, 6, "\uE76D", "\uE88F", $"PenButton{i + 1}")).ToList();
        Highlighters = highlighters.Select((preset, i) =>
            Preset(new HighlighterPen(), texts.Highlighter, highlighterPalette, preset, 2, 30, "\uE7E6", "\uE891", $"HighlighterButton{i + 1}")).ToList();

        SelectMode = new RadioButtons { ItemsSource = new[] { texts.Lasso, texts.Rectangle }, SelectedIndex = 0 };
        Select = new InkToolbarCustomToolButton
        {
            Content = Glyph("\uF1BE"),
            ConfigurationContent = Options(true, (texts.SelectWith, SelectMode)),
        };
        Describe(Select, texts.Select, texts.SelectTip, "SelectButton");
        Ink.Children.Add(Select);

        ShapeKind = new RadioButtons { ItemsSource = texts.ShapeKinds, SelectedIndex = 0 };
        // Recognition changes what the pen draws freehand into shapes: it belongs with the shapes.
        RecognizeShapes = new CheckBox { Content = texts.RecognizeShapes };
        Describe(RecognizeShapes, texts.RecognizeShapes, texts.RecognizeShapesTip, "RecognizeButton");
        Shapes = new InkToolbarCustomToolButton
        {
            // Line, rectangle, circle as outlines (PathIcon fills its geometry, so even-odd rings).
            Content = Sized(Path(
                "F0 M1,17.9 L7.9,11 L9,12.1 L2.1,19 Z M10,1 H19 V10 H10 Z M11.5,2.5 V8.5 H17.5 V2.5 Z " +
                "M10,15 A4.5,4.5 0 1 0 19,15 A4.5,4.5 0 1 0 10,15 Z M11.5,15 A3,3 0 1 1 17.5,15 A3,3 0 1 1 11.5,15 Z")),
            ConfigurationContent = Options(true, (texts.Shape, ShapeKind), (texts.Freehand, RecognizeShapes)),
        };
        Describe(Shapes, texts.Shapes, texts.ShapesTip, "ShapeButton");
        Ink.Children.Add(Shapes);

        // One eraser tool (Windows ink's) and the eraser's mode as a toggle beside it: whole strokes or exactly the circle
        // at the pen. (InkToolbar orders its buttons itself – built-in pens, custom pens, eraser, custom tools, stencil,
        // toggles – so the mode toggle stands at the end of the ink group, after the ruler.) The mode applies to the tool and to the pen's eraser (button or
        // end), and switching it leaves the active tool alone – a pen with an eraser button erases without ever choosing
        // the eraser tool (the tool stays for mouse users and long erasing). The circle's size is the toggle's context
        // flyout (right-click; pen: tap with the barrel button held): a toggle has no configuration flyout of its own.
        Eraser = new InkToolbarEraserButton { IsClearAllVisible = false };
        Describe(Eraser, texts.Eraser, texts.EraserTip);
        Ink.Children.Add(Eraser);
        EraserRadius = new Slider { Minimum = 1, Maximum = 20, StepFrequency = 0.5, Value = 4 };
        PartialErase = new InkToolbarCustomToggleButton
        {
            Content = Glyph("\uED61"), // Windows' PointErase glyph (the eraser button's is StrokeErase, ED60)
            ContextFlyout = new Flyout
            {
                Placement = Microsoft.UI.Xaml.Controls.Primitives.FlyoutPlacementMode.BottomEdgeAlignedLeft, // below the row, not over it
                Content = Options(false, (texts.EraserRadius, RadiusPreview(EraserRadius)), (null, EraserRadius)),
            },
        };
        Describe(PartialErase, texts.PartialErase, texts.PartialEraseTip, "PartialEraseButton");
        Ink.Children.Add(PartialErase);

        // Windows' own ruler and protractor: draw along the edge for straight lines; move/turn with fingers.
        Ruler = new InkToolbarStencilButton();
        Describe(Ruler, texts.Ruler, texts.Ruler, "StencilButton");
        Ink.Children.Add(Ruler);

        Undo = Command(new SymbolIcon(Symbol.Undo), texts.Undo, "UndoButton");
        Redo = Command(new SymbolIcon(Symbol.Redo), texts.Redo, "RedoButton");
        // The selection's commands: in their place always (nothing jumps), enabled while something is selected.
        Cut = Command(new SymbolIcon(Symbol.Cut), texts.Cut, "CutSelectionButton");
        Copy = Command(new SymbolIcon(Symbol.Copy), texts.Copy, "CopySelectionButton");
        // Duplicate: a copy (two sheets) with a plus; delete: Windows' trash can.
        Duplicate = Command(Path(DuplicateIcon), texts.Duplicate, "DuplicateSelectionButton");
        DeleteSelection = Command(new SymbolIcon(Symbol.Delete), texts.DeleteSelection, "DeleteSelectionButton");
        foreach (var button in new[] { Cut, Copy, Duplicate, DeleteSelection })
        {
            button.IsEnabled = false;
        }
        Undo.IsEnabled = Redo.IsEnabled = false;

        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2 };
        row.Children.Add(Undo);
        row.Children.Add(Redo);
        row.Children.Add(new AppBarSeparator());
        row.Children.Add(Ink);
        row.Children.Add(new AppBarSeparator());
        row.Children.Add(Cut);
        row.Children.Add(Copy);
        row.Children.Add(Duplicate);
        row.Children.Add(DeleteSelection);
        row.Children.Add(new AppBarSeparator());
        row.Children.Add(extensions);
        Content = row;
    }

    /// <summary>The duplicate icon (two sheets, a plus on the front one), 20×20; also for an app's menu.</summary>
    public const string DuplicateIcon =
        "F0 M1,1 H13 V4.5 H11.5 V2.5 H2.5 V11.5 H4.5 V13 H1 Z M6,6 H19 V19 H6 Z M7.5,7.5 V17.5 H17.5 V7.5 Z " +
        "M11.75,9.5 H13.25 V11.75 H15.5 V13.25 H13.25 V15.5 H11.75 V13.25 H9.5 V11.75 H11.75 Z";

    // A preset as a custom pen button: its color and width, Windows' pen flyout, Windows' icon in its color, named
    // after its color ("Pen – Blue").
    private InkToolbarCustomPenButton Preset(InkToolbarCustomPen pen, string kind, IReadOnlyList<NamedColor> palette,
        PenPreset preset, double minWidth, double maxWidth, string outline, string fill, string automationId)
    {
        var brushes = palette.Select(c => (Brush)new SolidColorBrush(c.Color)).ToList();
        var button = new InkToolbarCustomPenButton
        {
            CustomPen = pen,
            Palette = brushes,
            SelectedBrushIndex = Math.Clamp(preset.ColorIndex, 0, brushes.Count - 1),
            MinStrokeWidth = minWidth,
            MaxStrokeWidth = maxWidth,
            ConfigurationContent = new InkToolbarPenConfigurationControl(),
        };
        button.SelectedStrokeWidth = Math.Clamp(preset.Width, minWidth, maxWidth);
        button.Content = PenIcon(button, outline, fill);
        void Name()
        {
            int index = button.SelectedBrushIndex;
            string name = index >= 0 && index < palette.Count ? $"{kind} – {palette[index].Name}" : kind;
            Describe(button, name, $"{name} – {presetTip}", automationId);
        }
        Name();
        button.RegisterPropertyChangedCallback(InkToolbarPenButton.SelectedBrushIndexProperty, (_, _) => Name());
        Ink.Children.Add(button);
        return button;
    }

    /// <summary>Windows' ink toolbar holding the pens, eraser and tools (target canvas, active tool, attributes).</summary>
    public InkToolbar Ink { get; }

    public IReadOnlyList<InkToolbarCustomPenButton> Pens { get; }
    public IReadOnlyList<InkToolbarCustomPenButton> Highlighters { get; }

    /// <summary>Windows ink's eraser: whole strokes.</summary>
    public InkToolbarEraserButton Eraser { get; }
    /// <summary>
    /// The eraser's mode (see <see cref="PartialEraser"/>): checked, the eraser tool and the pen's eraser erase exactly the
    /// circle at the pen; otherwise whole strokes. Not a tool: switching it keeps the active tool.
    /// </summary>
    public InkToolbarCustomToggleButton PartialErase { get; }
    /// <summary>The partial eraser's radius (the toggle's context flyout).</summary>
    public Slider EraserRadius { get; }
    public InkToolbarCustomToolButton Select { get; }
    /// <summary>The select tool's mode (its configuration flyout): 0 lasso, 1 rectangle.</summary>
    public RadioButtons SelectMode { get; }
    public InkToolbarCustomToolButton Shapes { get; }
    /// <summary>The shape tool's shape (its configuration flyout), in the order of <see cref="PenToolbarTexts.ShapeKinds"/>.</summary>
    public RadioButtons ShapeKind { get; }
    public InkToolbarStencilButton Ruler { get; }
    /// <summary>Shapes drawn freehand become clean ones (in the shape tool's options).</summary>
    public CheckBox RecognizeShapes { get; }
    public AppBarButton Undo { get; }
    public AppBarButton Redo { get; }
    public AppBarButton Cut { get; }
    public AppBarButton Copy { get; }
    public AppBarButton Duplicate { get; }
    public AppBarButton DeleteSelection { get; }

    /// <summary>The app's own buttons, at the end of the row.</summary>
    public UIElementCollection Extensions => extensions.Children;

    // Windows' own ballpoint icon: the pen's outline over a fill in the pen's color (as InkToolbar's template draws it).
    private static Grid PenIcon(InkToolbarPenButton button, string outline, string fillGlyph)
    {
        var fill = new FontIcon { Glyph = fillGlyph, FontSize = GlyphSize, Foreground = button.SelectedBrush };
        button.RegisterPropertyChangedCallback(InkToolbarPenButton.SelectedBrushProperty, (_, _) => fill.Foreground = button.SelectedBrush);
        var icon = new Grid();
        icon.Children.Add(fill);
        icon.Children.Add(Glyph(outline));
        return icon;
    }

    // InkToolbar's own buttons draw their glyphs at 16 (InkToolbarButtonFontSize); FontIcon's default is 20, so custom
    // buttons' icons are set to 16 to match the built-in eraser and ruler (and the command row's AppBarButtons).
    private const double GlyphSize = 16;

    private static FontIcon Glyph(string glyph) => new() { Glyph = glyph, FontSize = GlyphSize };

    // A 20×20 path icon at the glyphs' size.
    private static Viewbox Sized(IconElement icon) => new() { Width = GlyphSize, Height = GlyphSize, Child = icon };

    // A tool's options as Windows' own pen flyout lays them out: padded, each section under a strong header. In a plain
    // Flyout (padded: false) its presenter pads them.
    private static StackPanel Options(bool padded, params (string? Header, UIElement Content)[] sections)
    {
        var panel = new StackPanel { Padding = padded ? new Thickness(16, 12, 16, 16) : new Thickness(0), MinWidth = 240 };
        foreach (var (header, content) in sections)
        {
            if (header != null)
            {
                var title = Themed.Load<TextBlock>("TextBlock", "Style='{ThemeResource BodyStrongTextBlockStyle}'");
                title.Text = header;
                title.Margin = new Thickness(0, panel.Children.Count == 0 ? 0 : 16, 0, 8);
                panel.Children.Add(title);
            }
            panel.Children.Add(content);
        }
        return panel;
    }

    // The eraser's circle at its size (page points shown 1:1), as Windows' pen flyout previews the pen's size.
    private static Grid RadiusPreview(Slider radius)
    {
        var circle = Themed.Load<Microsoft.UI.Xaml.Shapes.Ellipse>("Ellipse",
            "Stroke='{ThemeResource TextFillColorPrimaryBrush}' StrokeThickness='1' HorizontalAlignment='Center' VerticalAlignment='Center'");
        void Size() => circle.Width = circle.Height = 2 * radius.Value;
        Size();
        radius.ValueChanged += (_, _) => Size();
        return new Grid { Height = 2 * radius.Maximum + 4, Children = { circle } };
    }

    /// <summary>An outline icon from path markup (even-odd rings: PathIcon fills its geometry).</summary>
    public static PathIcon Path(string data) =>
        new() { Data = (Geometry)XamlBindingHelper.ConvertValue(typeof(Geometry), data) };

    /// <summary>A compact command button for the row (and for <see cref="Extensions"/>).</summary>
    public static AppBarButton Command(IconElement icon, string tip, string? automationId = null)
    {
        var button = new AppBarButton
        {
            Icon = icon,
            Label = tip,
            LabelPosition = CommandBarLabelPosition.Collapsed,
            Width = CommandWidth,
            AllowFocusOnInteraction = false,
            IsTabStop = false,
        };
        Describe(button, tip, tip, automationId);
        return button;
    }

    private static void Describe(FrameworkElement element, string name, string tip, string? automationId = null)
    {
        AutomationProperties.SetName(element, name);
        ToolTipService.SetToolTip(element, tip);
        if (automationId != null)
        {
            AutomationProperties.SetAutomationId(element, automationId);
        }
    }
}
