using System.Numerics;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using Windows.System;
using Windows.UI;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace InkPages.Demo;

/// <summary>
/// The demo's window: the library's toolbar above three graph pages in a ScrollView, wired by
/// <see cref="InkWorkspace"/>; delete, duplicate, copy/paste (Windows ink's clipboard) and adding pages are the demo's
/// own small commands, as an app adds its own. Built in code.
/// </summary>
public sealed class DemoWindow : Window
{
    private const float Step = 14.17f;

    private static readonly NamedColor[] Palette =
    [
        new(Color.FromArgb(255, 0x00, 0x00, 0x00), "Black"),
        new(Color.FromArgb(255, 0x33, 0x33, 0xcc), "Blue"),
        new(Color.FromArgb(255, 0xff, 0x00, 0x00), "Red"),
        new(Color.FromArgb(255, 0x00, 0x80, 0x00), "Green"),
        new(Color.FromArgb(255, 0x80, 0x80, 0x80), "Gray"),
        new(Color.FromArgb(255, 0xff, 0x80, 0x00), "Orange"),
    ];

    private static readonly NamedColor[] HighlighterPalette =
    [
        new(Color.FromArgb(255, 0xff, 0xff, 0x00), "Yellow"),
        new(Color.FromArgb(255, 0x00, 0xff, 0x00), "Green"),
        new(Color.FromArgb(255, 0x00, 0xc0, 0xff), "Light blue"),
        new(Color.FromArgb(255, 0xff, 0x00, 0xff), "Magenta"),
    ];

    private readonly PenToolbar bar;
    private readonly ScrollView scroller;
    private readonly StackPanel panel = new() { Spacing = 16, Padding = new Thickness(16), HorizontalAlignment = HorizontalAlignment.Center };
    private readonly InkWorkspace ink;

    public DemoWindow(ElementTheme theme)
    {
        Title = "InkPages Demo";
        SystemBackdrop = new MicaBackdrop();
        if (AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.Maximize();
        }

        bar = new PenToolbar(new PenToolbarTexts(), Palette, [new(0, 1.41), new(1, 1.41), new(2, 1.41)],
            HighlighterPalette, [new(0, 8.5), new(1, 8.5)]);
        scroller = new ScrollView
        {
            // The pen never pans (it would capture the pen); touch and touchpad scroll and zoom.
            IgnoredInputKinds = ScrollingInputKinds.Pen,
            ContentOrientation = ScrollingContentOrientation.Vertical,
            ZoomMode = ScrollingZoomMode.Enabled,
            HorizontalScrollBarVisibility = ScrollingScrollBarVisibility.Hidden,
            VerticalScrollBarVisibility = ScrollingScrollBarVisibility.Hidden,
            Content = panel,
        };
        ink = new InkWorkspace(bar, scroller);
        ink.TransferToPage = TransferToPage;

        AddCommands();
        for (int i = 0; i < 3; i++)
        {
            AddPage();
        }
        ((DemoPage)panel.Children[0]).AddPictures([new DemoPicture(new Rect(60, 420, 160, 100), Colors.SteelBlue)]);
        scroller.ViewChanged += (_, _) => ink.SetCurrentPage(CurrentPage);

        // The accelerators' tooltip would open wherever the pen hovers over the window - a windowed popup, and while one
        // is open a selection drag loses the pen (sandbox 2026-11-17). Hidden: no tooltip over the pages.
        var root = new Grid { RequestedTheme = theme, KeyboardAcceleratorPlacementMode = KeyboardAcceleratorPlacementMode.Hidden };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition());
        bar.Margin = new Thickness(8, 4, 8, 4);
        root.Children.Add(bar);
        Grid.SetRow(scroller, 1);
        root.Children.Add(scroller);
        root.KeyboardAccelerators.Add(Accelerator(VirtualKey.Z, VirtualKeyModifiers.Control, () => ink.UndoStep()));
        root.KeyboardAccelerators.Add(Accelerator(VirtualKey.Y, VirtualKeyModifiers.Control, () => ink.RedoStep()));
        root.KeyboardAccelerators.Add(Accelerator(VirtualKey.Delete, VirtualKeyModifiers.None, Delete));
        root.KeyboardAccelerators.Add(Accelerator(VirtualKey.D, VirtualKeyModifiers.Control, Duplicate));
        Content = root;
        // The page has the keyboard focus at start (not the first toolbar button, which would show a focus rectangle).
        root.Loaded += (_, _) => scroller.Focus(FocusState.Programmatic);
    }

    private IEnumerable<DemoPage> Pages => panel.Children.OfType<DemoPage>();

    // The page in the middle of the view.
    private DemoPage? CurrentPage => Pages.LastOrDefault(p =>
        p.TransformToVisual(scroller).TransformPoint(default).Y <= scroller.ViewportHeight / 2) ?? Pages.FirstOrDefault();

    private void AddCommands()
    {
        bar.Undo.Click += (_, _) => ink.UndoStep();
        bar.Redo.Click += (_, _) => ink.RedoStep();
        bar.DeleteSelection.Click += (_, _) => Delete();
        bar.Duplicate.Click += (_, _) => Duplicate();
        bar.Copy.Click += (_, _) => Copy();
        bar.Cut.Click += (_, _) =>
        {
            // The demo's clipboard holds ink only: a selection with pictures is not cut (they would be lost).
            if (ink.Selection.Current is { Pictures.Count: 0 })
            {
                Copy();
                Delete();
            }
        };
        var paste = PenToolbar.Command(new SymbolIcon(Symbol.Paste), "Paste ink", "PasteButton");
        paste.Click += (_, _) => Paste();
        var addPage = PenToolbar.Command(new SymbolIcon(Symbol.Add), "Add page", "InsertPageButton");
        addPage.Click += (_, _) => AddPage();
        bar.Extensions.Add(paste);
        bar.Extensions.Add(new AppBarSeparator());
        bar.Extensions.Add(addPage);
    }

    private static KeyboardAccelerator Accelerator(VirtualKey key, VirtualKeyModifiers modifiers, Action action)
    {
        var accelerator = new KeyboardAccelerator { Key = key, Modifiers = modifiers };
        accelerator.Invoked += (_, e) =>
        {
            action();
            e.Handled = true;
        };
        return accelerator;
    }

    private void AddPage()
    {
        var page = new DemoPage(595, 842); // A4 in points
        ink.Attach(page);
        panel.Children.Add(page);
        ink.SetPages(Pages);
        ink.SetCurrentPage(CurrentPage);
    }

    // ---------------------------------------------------------------- the demo's commands

    private void Delete()
    {
        if (ink.Selection.Current is not { HasContent: true } current || ink.Selection.IsDragging)
        {
            return;
        }
        var edit = PageEdit.RemoveSelected(current);
        ink.Selection.Clear();
        ink.Undo.Push(edit);
    }

    private void Duplicate()
    {
        if (ink.Selection.Current is not { HasContent: true } current || ink.Selection.IsDragging)
        {
            return;
        }
        var page = current.Page;
        var (strokes, pictures) = Copies(current, page, Matrix3x2.CreateTranslation(Step, Step));
        var edit = new PageEdit(page);
        edit.AddedStrokes.AddRange(strokes);
        edit.AddedPictures.AddRange(pictures);
        ink.Undo.Push(edit);
        ink.Selection.Select(page, strokes, pictures);
    }

    // Strokes through Windows ink's clipboard (pictures are not copied by the demo).
    private void Copy()
    {
        if (ink.Selection.Current is not { } current || current.Strokes.Count == 0 || ink.Selection.IsDragging)
        {
            return;
        }
        PageEdit.SelectOnly(current.Page, current.Strokes);
        current.Page.Ink.StrokeContainer.CopySelectedToClipboard();
        foreach (var stroke in current.Strokes)
        {
            stroke.Selected = false;
        }
    }

    private void Paste()
    {
        if (CurrentPage is not { } page || ink.Selection.IsDragging || !page.Ink.StrokeContainer.CanPasteFromClipboard())
        {
            return;
        }
        var container = page.Ink.StrokeContainer;
        var before = container.GetStrokes().Select(s => s.Id).ToHashSet();
        container.PasteFromClipboard(new Point(Step, Step));
        var added = container.GetStrokes().Where(s => !before.Contains(s.Id)).ToList();
        added.ForEach(s => s.Selected = false);
        var edit = new PageEdit(page);
        edit.AddedStrokes.AddRange(added);
        ink.Undo.Push(edit);
        ink.Selection.Select(page, added, []);
    }

    // Copies of the selection's strokes and pictures added to a page, transformed by m.
    private static (List<InkStroke> Strokes, List<object> Pictures) Copies(Selection selection, InkPage target, Matrix3x2 m)
    {
        var strokes = new List<InkStroke>();
        foreach (var stroke in selection.Page.CurrentStrokes(selection.Strokes).OfType<InkStroke>())
        {
            var copy = stroke.Clone();
            copy.PointTransform = stroke.PointTransform * m;
            target.Ink.StrokeContainer.AddStroke(copy);
            strokes.Add(copy);
        }
        var pictures = selection.Pictures.Cast<DemoPicture>()
            .Select(p => (object)new DemoPicture(Selection.TransformBounds(p.Bounds, m), p.Color)).ToList();
        if (pictures.Count > 0)
        {
            target.AddPictures(pictures);
        }
        return (strokes, pictures);
    }

    // ---------------------------------------------------------------- moving across pages

    // The selection's content moves to another page during a drag: copies there, the originals taken away.
    private (List<InkStroke> Strokes, List<object> Pictures, IPageEdit Edit)? TransferToPage(Selection current, InkPage target, Matrix3x2 m)
    {
        try
        {
            current.Revert(); // m maps the content as it was before the drag
            var (strokes, pictures) = Copies(current, target, m);
            var adding = new PageEdit(target);
            adding.AddedStrokes.AddRange(strokes);
            adding.AddedPictures.AddRange(pictures);
            var removal = PageEdit.RemoveSelected(current);
            return (strokes, pictures, new CombinedEdit([removal, adding]));
        }
        catch (Exception e)
        {
            InkLog.Write("demo: move to another page (the content stays on its page)", e);
            return null;
        }
    }
}
