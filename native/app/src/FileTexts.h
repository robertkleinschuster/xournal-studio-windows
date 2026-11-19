#pragma once

// The file's texts for InkPages' Text tool (TextHost): a pen tap edits the text under it or starts a new one, the library
// edits it and hands the result back here, written into the file as upstream writes texts (XoppText: font, size, color,
// x, y; only what was edited changes). Ids, removal and the "being edited" mark are FilePictures', so selecting, moving,
// scaling and deleting texts go through the same host as pictures. Fonts: the Windows family the library shows, "Sans"
// in the file for Arial (Xournal++'s default). UI thread; the document's lock is taken around every change.

#include "FilePictures.h"

#include "inkpages/Texts.h"
#include "inkpages/WritingSurface.h"

#include "xournal/model/XoppDocument.h"

#include <functional>
#include <memory>
#include <mutex>

namespace xournal::app
{
    class FileTexts : public inkpages::TextHost
    {
    public:
        FileTexts(std::shared_ptr<XoppDocument> doc, std::mutex& documentLock, FilePictures& objects,
            std::function<void(int page)> invalidate);

        std::optional<inkpages::TextId> TextAt(inkpages::PointF document) override;
        inkpages::Text Get(inkpages::TextId text) override;
        void SetEditing(std::optional<inkpages::TextId> text) override;
        inkpages::TextId Add(inkpages::Text const& text) override;
        void Change(inkpages::TextId id, inkpages::Text const& text) override;
        uint64_t Remove(inkpages::TextId id) override;
        void Restore(uint64_t token) override;

    private:
        TextEdit ToEdit(inkpages::Text const& text) const; // page units of text.page

        std::shared_ptr<XoppDocument> doc;
        std::mutex& documentLock;
        FilePictures& objects;
        std::function<void(int)> invalidate;
        int editingPage = -1;
    };
}
