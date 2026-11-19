#pragma once

// The selection on the Windows clipboard (InkPages' copyToClipboard/pasteFromClipboard):
//   - copy: the app's own format "Xournal Studio Clip" - the selected strokes, pictures and texts as upstream .xopp
//     elements in a <clip> (XoppClip, written as XML) - and, a moment later, the clip as a picture for other apps (PNG and
//     CF_DIB on white), drawn by the PDF export and rendered by Windows.Data.Pdf in the background; it is added only while
//     the clipboard still holds this copy.
//   - paste: the own format (between documents and windows of this app), else a picture of another app (PNG, else a
//     device-independent bitmap) as a new <image>, else nothing - then InkPages pastes Windows ink (ink of other apps).
// What is pasted lands with its top left at the point InkPages gives (the view's center). UI thread.

#include "DocumentView.h"

#include "inkpages/WritingSurface.h"

#include <windows.h>

#include <memory>
#include <optional>

namespace xournal::app
{
    class SelectionClipboard
    {
    public:
        SelectionClipboard(HWND owner, inkpages::WritingSurface& surface, DocumentView& view);

        void Copy(inkpages::Clip const& clip);
        std::optional<inkpages::Clip> Paste(inkpages::PointF at);

    private:
        std::optional<inkpages::Clip> PasteClip(inkpages::PointF at);
        std::optional<inkpages::Clip> PastePicture(inkpages::PointF at);
        void AddPicturesLater(XmlElementPtr clip, DWORD sequence);

        HWND owner;
        inkpages::WritingSurface& surface;
        DocumentView& view;
        UINT clipFormat = 0;
        UINT pngFormat = 0;
        std::shared_ptr<bool> alive = std::make_shared<bool>(true); // background renders check it before they post back
    };
}
