using Microsoft.UI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.Storage.Pickers;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// Datei → Als PDF exportieren: asks where (the document's name in its folder; never over its background PDF) and
/// writes the document as it is now, saved or not, with <see cref="PdfExport"/> off the UI thread.
/// </summary>
public sealed class PdfExportCommand(WindowId window, DocumentSession session, Messages messages, Func<bool> previewing)
{
    public async Task ExportAsync()
    {
        if (previewing())
        {
            messages.Show(InfoBarSeverity.Informational, "Nicht exportiert", "Erst loslassen, dann exportieren.");
            return;
        }
        var doc = session.Document;
        string? background = doc.PdfPath;
        string folder = Path.GetDirectoryName(doc.FilePath ?? doc.PdfPath) ?? Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
        string name = doc.SuggestedName;
        if (background != null && ProjectFiles.SamePath(Path.Combine(folder, name + ".pdf"), background))
        {
            name += " (mit Notizen)"; // the lecture PDF itself stays untouched
        }
        var picker = new FileSavePicker(window) { SuggestedFileName = name };
        if (Directory.Exists(folder))
        {
            picker.SuggestedFolder = folder;
        }
        picker.FileTypeChoices.Add("PDF-Dokument", [".pdf"]);
        string? target = (await picker.PickSaveFileAsync())?.Path;
        if (target == null)
        {
            return;
        }
        if (background != null && ProjectFiles.SamePath(target, background))
        {
            messages.Show(InfoBarSeverity.Warning, "Nicht exportiert",
                "Das ist die PDF, auf der das Dokument liegt; bitte einen anderen Namen wählen.");
            return;
        }
        try
        {
            var snapshot = doc.Snapshot(doc.ReferenceBase);
            var pictures = doc.Pages.Select(page => page.BackgroundImagePath).ToList();
            var result = await Task.Run(() => PdfExport.Write(snapshot, background, target,
                i => i < pictures.Count ? pictures[i] : null));
            string pages = result.Pages == 1 ? "1 Seite" : $"{result.Pages} Seiten";
            string text = $"{Path.GetFileName(target)} ({pages}).";
            if (result.MissingBackgrounds > 0)
            {
                text += $" {result.MissingBackgrounds} Seite(n) ohne ihren Hintergrund (PDF oder Bild nicht gefunden oder nicht lesbar).";
            }
            if (result.SkippedElements > 0)
            {
                text += $" {result.SkippedElements} Element(e), die diese App noch nicht darstellt, fehlen darin.";
            }
            bool complete = result.MissingBackgrounds + result.SkippedElements == 0;
            messages.Show(complete ? InfoBarSeverity.Success : InfoBarSeverity.Warning,
                complete ? "Als PDF exportiert" : "Als PDF exportiert – unvollständig", text);
        }
        catch (Exception e)
        {
            messages.Error("export PDF", "Export fehlgeschlagen", e, target);
        }
    }
}
