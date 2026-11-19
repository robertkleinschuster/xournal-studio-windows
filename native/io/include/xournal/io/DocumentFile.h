#pragma once

// .xopp files: gzip-compressed XML (plain XML is read too). Loading resolves the document's references from the file's
// place; saving writes "<file>~" first and replaces the file only once it is complete. Save As takes the attachments
// along, as upstream does.
//
// Threads: Load/LoadCopy and Write run on any thread. Save, SaveAs and SaveCopy take the snapshot (which reads the
// pages' ink) - call them on the ink's thread, or take the snapshot there (TakeSnapshot) and Write elsewhere.

#include "xournal/model/XoppDocument.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace xournal::io::DocumentFile
{
    using std::filesystem::path;

    /// The XML of a .xopp file (gzip or plain).
    XmlElementPtr ReadXml(path const& file);

    std::unique_ptr<XoppDocument> Load(path const& file);

    /// Loads a copy written from a snapshot (crash recovery): references are resolved as from `referenceBase`, and the
    /// document's file is `documentPath` (nothing: never saved).
    std::unique_ptr<XoppDocument> LoadCopy(path const& copy, std::optional<path> const& documentPath, path const& referenceBase);

    /// Writes a snapshot gzip-compressed to "<file>~", then replaces the file.
    void Write(XmlElement const& snapshot, path const& file);

    /// Ctrl+S: the snapshot written to `file`, nothing else; the document's file is `file` from then on.
    void Save(XoppDocument& doc, path const& file);

    /// Save As: the attachments go along (copied next to `file` as "<file>.<name>", never over an existing file), the
    /// file keeps "attach", and the document refers to its own attachments from then on. If writing fails, the copies
    /// just made are removed again.
    void SaveAs(XoppDocument& doc, path const& file);

    /// A copy of the document at `file` with its own attachments (the document itself stays where it is).
    void SaveCopy(XoppDocument& doc, path const& file);
}
