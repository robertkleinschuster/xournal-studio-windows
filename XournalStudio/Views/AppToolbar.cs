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

    // A page with a plus and a page with a minus (user, 2026-11-17: both in the row; deleting always asks first).
    public const string InsertPageIcon = "F0 M1,1 H11 V19 H1 Z M2.5,2.5 V17.5 H9.5 V2.5 Z " +
        "M15.25,9.5 H16.75 V13.25 H19.5 V14.75 H16.75 V18.5 H15.25 V14.75 H12.5 V13.25 H15.25 Z";
    public const string DeletePageIcon = "F0 M1,1 H11 V19 H1 Z M2.5,2.5 V17.5 H9.5 V2.5 Z M12.5,13.25 H19.5 V14.75 H12.5 Z";

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
