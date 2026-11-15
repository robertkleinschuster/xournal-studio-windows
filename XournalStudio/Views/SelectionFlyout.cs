using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Windows.Foundation;

namespace XournalStudio.Views;

/// <summary>
/// The commands of a selection right at it, as Windows shows them for selected text (a transient CommandBarFlyout:
/// takes no focus, closes when the page is used): Ausschneiden, Kopieren, Duplizieren, Löschen. A windowed popup, so
/// the ink does not draw over it (see CLAUDE.md, ink over XAML).
/// </summary>
public sealed class SelectionFlyout
{
    private readonly CommandBarFlyout flyout;

    public SelectionFlyout(EditCommands commands, UIElement passThrough)
    {
        flyout = new CommandBarFlyout
        {
            Placement = FlyoutPlacementMode.Top,
            ShouldConstrainToRootBounds = false,
            // Taps beside it reach the page (and close it), as with the text selection's flyout.
            OverlayInputPassThroughElement = passThrough,
        };
        flyout.PrimaryCommands.Add(Button("Ausschneiden", Symbol.Cut, async () => await commands.CutAsync()));
        flyout.PrimaryCommands.Add(Button("Kopieren", Symbol.Copy, async () => await commands.CopyAsync()));
        flyout.PrimaryCommands.Add(Button("Duplizieren", Symbol.Add, commands.Duplicate));
        flyout.PrimaryCommands.Add(Button("Löschen", Symbol.Delete, () => commands.Delete()));
    }

    /// <summary>Shows the commands above the selection (or hides them when there is none).</summary>
    public void ShowFor(Selection? selection)
    {
        if (selection == null)
        {
            flyout.Hide();
            return;
        }
        var b = selection.Bounds;
        flyout.ShowAt(selection.View.Surface, new FlyoutShowOptions
        {
            Position = new Point(b.X + b.Width / 2, Math.Max(0, b.Y - 6)),
            Placement = FlyoutPlacementMode.Top,
            ShowMode = FlyoutShowMode.Transient,
        });
    }

    public void Hide() => flyout.Hide();

    private static AppBarButton Button(string label, Symbol symbol, Action action)
    {
        var button = new AppBarButton { Label = label, Icon = new SymbolIcon(symbol), LabelPosition = CommandBarLabelPosition.Collapsed };
        ToolTipService.SetToolTip(button, label);
        button.Click += (_, _) => action();
        return button;
    }
}
