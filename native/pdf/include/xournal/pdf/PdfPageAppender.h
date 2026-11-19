#pragma once

// Inserting pages of another PDF the way upstream Xournal++ stores a document: it has one background PDF and pages refer
// to its pages by number. So the other PDF's pages are appended to the end of the document's PDF (appending keeps the
// numbers of the pages already there - another document may use the same PDF), and the document's new pages refer to
// them. What the other PDF's annotations show (e.g. signatures added on the iPad) is drawn into those pages first, since
// annotations do not come along as page content. Port of the C# PdfPageAppender on qpdf.

#include <filesystem>
#include <vector>

namespace xournal::pdf
{
    /// Appends `pages` (0-based, in this order) of `source` to `target`; returns the index of the first appended page.
    /// `source` stays as it is; `target` is written as "<target>~" and then replaced (io::Files). The appended pages
    /// carry no annotations (what they showed is in their content now). Throws xournal::Error (e.g. a page index out of
    /// range, a file qpdf cannot read).
    int AppendPages(std::filesystem::path const& target, std::filesystem::path const& source, std::vector<int> const& pages);
}
