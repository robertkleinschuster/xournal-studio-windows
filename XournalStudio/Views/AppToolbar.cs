using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// The app's set-up of the library's toolbar: the library's German texts and Xournal++'s palette
/// (<see cref="XournalPalette"/>) with the user's tool choices from the settings (pen presets: black, blue, red by
/// default). The window adds the app's own buttons at the end.
/// </summary>
public static class AppToolbar
{
    // The selection's tip also tells what a rectangle on a PDF page does here.
    public static readonly PenToolbarTexts Texts = PenToolbarTexts.German with
    {
        SelectTip = "Auswählen: Lasso oder Rechteck (nochmal tippen zum Wählen); ziehen verschiebt, Ecken skalieren; " +
            "ein Rechteck auf einer PDF-Seite kopiert diesen Bereich. Mit dem Stift geht das Lasso mit jedem Werkzeug: mit gehaltener Stifttaste ziehen",
    };

    public static PenToolbar Create(AppSettings settings) => new(Texts, settings.ToolChoices());

    // A sheet with a folded corner and a plus / minus badge, in Segoe Fluent's style (user, 2026-11-19: it must read as a
    // sheet; 2026-11-17: both in the row, deleting always asks first). Segoe Fluent Icons documents no "page add/remove"
    // glyph (the font has an undocumented one at EA1A – not used). Nonzero fill: outline, fold and badge clockwise, the
    // plus/minus counter-clockwise (a hole in the badge).
    private const string Sheet = "F1 M5,9.3 V3.5 L5.6,2.1 L7,1.5 H12 L17,6.5 V17 L16.4,18.4 L15,19 H10.7 V17.6 H15 " +
        "L15.4,17.4 L15.6,17 V7.1 L11.4,2.9 H7 L6.6,3.1 L6.4,3.5 V9.3 Z M10.9,2.2 H12.3 V5.5 H16.3 V6.9 H10.9 Z " +
        "M5,10.5 A4.5,4.5 0 1 1 5,19.5 A4.5,4.5 0 1 1 5,10.5 Z ";
    public const string InsertPageIcon = Sheet +
        "M4.3,12.25 V14.3 H2.25 V15.7 H4.3 V17.75 H5.7 V15.7 H7.75 V14.3 H5.7 V12.25 Z";
    public const string DeletePageIcon = Sheet + "M2.75,14.3 V15.7 H7.25 V14.3 Z";

    /// <summary>The app's page buttons at the end of the row (insert page, delete page); the window wires their clicks.</summary>
    public static (Microsoft.UI.Xaml.Controls.AppBarButton InsertPage, Microsoft.UI.Xaml.Controls.AppBarButton DeletePage)
        AddButtons(PenToolbar bar)
    {
        var insertPage = PenToolbar.Command(PenToolbar.Path(InsertPageIcon), "Seite einfügen (nach der Seite im Blick)", "InsertPageButton");
        var deletePage = PenToolbar.Command(PenToolbar.Path(DeletePageIcon),
            "Seite löschen (die Seite im Blick, mit Rückfrage)", "DeletePageButton");
        bar.Extensions.Add(insertPage);
        bar.Extensions.Add(deletePage);
        return (insertPage, deletePage);
    }
}
