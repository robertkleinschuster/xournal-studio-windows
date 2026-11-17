using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace InkPages;

/// <summary>A pen preset: one tap selects it; it keeps its own color and width (an InkToolbar pen button of its own).</summary>
public sealed record PenPreset(string Name, int ColorIndex, double Width);

/// <summary>
/// The writing surface's toolbar, built from WinUI's own controls in one dense, left-aligned row (pen-operated, Windows
/// defaults, no custom styling): pen presets and the highlighter, the eraser with its partial mode, the custom tools
/// (select, shapes), the ruler and shape recognition – all in Windows' <see cref="InkToolbar"/> – then undo/redo, the
/// selection's commands, and the app's own buttons (<see cref="Extensions"/>).
/// The control raises nothing itself: it exposes its WinUI controls, and the app (or this library's controllers) wires
/// their events.
/// </summary>
public sealed class PenToolbar : UserControl
{
    private const double CommandWidth = 44; // dense: operated with the pen, not optimized for fingers
    private readonly StackPanel extensions = new() { Orientation = Orientation.Horizontal, Spacing = 2 };

    public PenToolbar(PenToolbarTexts texts, IReadOnlyList<Color> palette, IReadOnlyList<PenPreset> pens)
    {
        Ink = new InkToolbar { InitialControls = InkToolbarInitialControls.None, VerticalAlignment = VerticalAlignment.Center };
        var brushes = palette.Select(c => (Brush)new SolidColorBrush(c)).ToList();
        // Pen presets: InkToolbar takes one built-in ballpoint button only; any number of custom pen buttons, each with
        // its own palette index and width and Windows' own pen flyout (InkToolbarPenConfigurationControl).
        Pens = pens.Select((preset, i) =>
        {
            var button = new InkToolbarCustomPenButton
            {
                CustomPen = new BallpointPen(),
                Palette = brushes,
                SelectedBrushIndex = Math.Clamp(preset.ColorIndex, 0, brushes.Count - 1),
                MinStrokeWidth = 0.5,
                MaxStrokeWidth = 6,
                ConfigurationContent = new InkToolbarPenConfigurationControl(),
            };
            button.SelectedStrokeWidth = Math.Clamp(preset.Width, button.MinStrokeWidth, button.MaxStrokeWidth);
            button.Content = PenIcon(button, "\uE76D", "\uE88F");
            Describe(button, preset.Name, preset.Name, $"PenButton{i + 1}");
            Ink.Children.Add(button);
            return button;
        }).ToList();

        Highlighter = new InkToolbarCustomPenButton
        {
            CustomPen = new HighlighterPen(),
            Palette = HighlighterColors.Select(c => (Brush)new SolidColorBrush(c)).ToList(),
            SelectedBrushIndex = 0,
            MinStrokeWidth = 2,
            MaxStrokeWidth = 30,
            ConfigurationContent = new InkToolbarPenConfigurationControl(),
        };
        Highlighter.Content = PenIcon(Highlighter, "\uE7E6", "\uE891");
        Describe(Highlighter, texts.Highlighter, texts.Highlighter, "HighlighterButton");
        Ink.Children.Add(Highlighter);

        Eraser = new InkToolbarEraserButton { IsClearAllVisible = false };
        Describe(Eraser, texts.Eraser, texts.Eraser);
        Ink.Children.Add(Eraser);
        // How the eraser (tool and the pen's eraser end) erases: whole strokes (Windows) or exactly the circle at the
        // pen. A toggle, so the active tool stays; its radius in the context menu.
        EraserRadius = new Slider { Header = texts.EraserRadius, Minimum = 1, Maximum = 20, StepFrequency = 0.5, Width = 220 };
        PartialEraser = new InkToolbarCustomToggleButton
        {
            Content = new FontIcon { Glyph = "" },
            ContextFlyout = new Flyout { Content = EraserRadius },
        };
        Describe(PartialEraser, texts.PartialEraser, texts.PartialEraserTip, "PartialEraserButton");
        Ink.Children.Add(PartialEraser);

        SelectMode = new RadioButtons { Header = texts.SelectWith, ItemsSource = new[] { texts.Lasso, texts.Rectangle } };
        Select = new InkToolbarCustomToolButton { Content = new FontIcon { Glyph = "" }, ConfigurationContent = SelectMode };
        Describe(Select, texts.Select, texts.SelectTip, "SelectButton");
        Ink.Children.Add(Select);

        ShapeKind = new RadioButtons { Header = texts.Shape, ItemsSource = texts.ShapeKinds };
        Shapes = new InkToolbarCustomToolButton
        {
            // Line, rectangle, circle as outlines (PathIcon fills its geometry, so even-odd rings).
            Content = new PathIcon
            {
                Data = (Geometry)XamlBindingHelper.ConvertValue(typeof(Geometry),
                    "F0 M1,17.9 L7.9,11 L9,12.1 L2.1,19 Z M10,1 H19 V10 H10 Z M11.5,2.5 V8.5 H17.5 V2.5 Z " +
                    "M10,15 A4.5,4.5 0 1 0 19,15 A4.5,4.5 0 1 0 10,15 Z M11.5,15 A3,3 0 1 1 17.5,15 A3,3 0 1 1 11.5,15 Z"),
            },
            ConfigurationContent = ShapeKind,
        };
        Describe(Shapes, texts.Shapes, texts.ShapesTip, "ShapeButton");
        Ink.Children.Add(Shapes);

        // Windows' own ruler and protractor: draw along the edge for straight lines; move/turn with fingers.
        Ruler = new InkToolbarStencilButton();
        Describe(Ruler, texts.Ruler, texts.Ruler, "StencilButton");
        Ink.Children.Add(Ruler);

        RecognizeShapes = new InkToolbarCustomToggleButton { Content = new FontIcon { Glyph = "" } };
        Describe(RecognizeShapes, texts.RecognizeShapes, texts.RecognizeShapes, "RecognizeButton");
        Ink.Children.Add(RecognizeShapes);

        Undo = Command(new SymbolIcon(Symbol.Undo), texts.Undo, "UndoButton");
        Redo = Command(new SymbolIcon(Symbol.Redo), texts.Redo, "RedoButton");
        // The selection's commands: enabled while something is selected; they take no keyboard focus.
        Cut = Command(new SymbolIcon(Symbol.Cut), texts.Cut, "CutSelectionButton");
        Copy = Command(new SymbolIcon(Symbol.Copy), texts.Copy, "CopySelectionButton");
        Duplicate = Command(new FontIcon { Glyph = "" }, texts.Duplicate, "DuplicateSelectionButton");
        DeleteSelection = Command(new FontIcon { Glyph = "" }, texts.DeleteSelection, "DeleteSelectionButton");
        foreach (var button in new[] { Cut, Copy, Duplicate, DeleteSelection })
        {
            button.IsEnabled = false;
        }
        Undo.IsEnabled = Redo.IsEnabled = false;

        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2 };
        row.Children.Add(Ink);
        row.Children.Add(new AppBarSeparator());
        row.Children.Add(Undo);
        row.Children.Add(Redo);
        row.Children.Add(new AppBarSeparator());
        row.Children.Add(Cut);
        row.Children.Add(Copy);
        row.Children.Add(Duplicate);
        row.Children.Add(DeleteSelection);
        row.Children.Add(new AppBarSeparator());
        row.Children.Add(extensions);
        Content = row;
    }

    /// <summary>Windows' ink toolbar holding the pens, eraser and tools (target canvas, active tool, attributes).</summary>
    public InkToolbar Ink { get; }

    public IReadOnlyList<InkToolbarCustomPenButton> Pens { get; }
    public InkToolbarCustomPenButton Highlighter { get; }

    /// <summary>The highlighter's colors (Xournal++'s and Windows' usual ones: yellow first).</summary>
    public static readonly Color[] HighlighterColors =
    [
        Color.FromArgb(255, 0xff, 0xff, 0x00), // yellow
        Color.FromArgb(255, 0x00, 0xff, 0x00), // green
        Color.FromArgb(255, 0x00, 0xc0, 0xff), // light blue
        Color.FromArgb(255, 0xff, 0x00, 0xff), // magenta
        Color.FromArgb(255, 0xff, 0x80, 0x00), // orange
    ];
    public InkToolbarEraserButton Eraser { get; }
    public InkToolbarCustomToggleButton PartialEraser { get; }
    public Slider EraserRadius { get; }
    public InkToolbarCustomToolButton Select { get; }
    /// <summary>The select tool's mode (its configuration flyout): 0 lasso, 1 rectangle.</summary>
    public RadioButtons SelectMode { get; }
    public InkToolbarCustomToolButton Shapes { get; }
    /// <summary>The shape tool's shape (its configuration flyout), in the order of <see cref="PenToolbarTexts.ShapeKinds"/>.</summary>
    public RadioButtons ShapeKind { get; }
    public InkToolbarStencilButton Ruler { get; }
    public InkToolbarCustomToggleButton RecognizeShapes { get; }
    public AppBarButton Undo { get; }
    public AppBarButton Redo { get; }
    public AppBarButton Cut { get; }
    public AppBarButton Copy { get; }
    public AppBarButton Duplicate { get; }
    public AppBarButton DeleteSelection { get; }

    /// <summary>The app's own buttons, at the end of the row.</summary>
    public UIElementCollection Extensions => extensions.Children;

    /// <summary>The selection commands (cut, copy, duplicate, delete), for enabling them with a selection.</summary>
    public IReadOnlyList<AppBarButton> SelectionCommands => new[] { Cut, Copy, Duplicate, DeleteSelection };

    // Windows' own ballpoint icon: the pen's outline over a fill in the pen's color (as InkToolbar's template draws it).
    private static Grid PenIcon(InkToolbarPenButton button, string outline, string fillGlyph)
    {
        var fill = new FontIcon { Glyph = fillGlyph, Foreground = button.SelectedBrush };
        button.RegisterPropertyChangedCallback(InkToolbarPenButton.SelectedBrushProperty, (_, _) => fill.Foreground = button.SelectedBrush);
        var icon = new Grid();
        icon.Children.Add(fill);
        icon.Children.Add(new FontIcon { Glyph = outline });
        return icon;
    }

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
