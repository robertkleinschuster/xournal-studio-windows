#include "FileTexts.h"

#include "xournal/base/Log.h"
#include "xournal/model/XoppText.h"

#include <format>

namespace xournal::app
{
    FileTexts::FileTexts(std::shared_ptr<XoppDocument> doc, std::mutex& documentLock, FilePictures& objects,
        std::function<void(int)> invalidate)
        : doc(std::move(doc)), documentLock(documentLock), objects(objects), invalidate(std::move(invalidate))
    {
    }

    std::optional<inkpages::TextId> FileTexts::TextAt(inkpages::PointF document)
    {
        std::scoped_lock guard(documentLock);
        return objects.TextAtLocked(document);
    }

    inkpages::Text FileTexts::Get(inkpages::TextId id)
    {
        std::scoped_lock guard(documentLock);
        auto const& element = objects.ElementOf(id);
        int page = objects.PageOf(element.get());
        auto edit = XoppText::Read(*element).ToEdit();
        // Texts.h: the library's text places its top left in page coordinates, as the file does.
        return { page, { static_cast<float>(edit.at.x), static_cast<float>(edit.at.y) }, edit.content,
            { edit.windowsFamily, static_cast<float>(edit.size), { edit.color.a, edit.color.r, edit.color.g, edit.color.b } } };
    }

    void FileTexts::SetEditing(std::optional<inkpages::TextId> id)
    {
        int before = editingPage;
        {
            std::scoped_lock guard(documentLock);
            auto element = id ? objects.ElementOf(*id).get() : nullptr;
            objects.SetEditing(element);
            editingPage = element ? objects.PageOf(element) : -1;
        }
        if (before >= 0) invalidate(before);
        if (editingPage >= 0 && editingPage != before) invalidate(editingPage);
    }

    TextEdit FileTexts::ToEdit(inkpages::Text const& text) const
    {
        auto const& c = text.style.color;
        return { text.content, text.style.font, text.style.size, Color{ c.a, c.r, c.g, c.b }, { text.at.x, text.at.y } };
    }

    inkpages::TextId FileTexts::Add(inkpages::Text const& text)
    {
        inkpages::TextId id = 0;
        {
            std::scoped_lock guard(documentLock);
            if (text.page < 0 || text.page >= static_cast<int>(doc->Pages().size()))
            {
                throw std::out_of_range(std::format("texts: a new text on page {} of {}", text.page + 1, doc->Pages().size()));
            }
            auto element = XoppText::Create(ToEdit(text));
            doc->Pages()[text.page]->Images().Add({ element }); // the top layer, like strokes and pictures
            id = objects.IdOf(element);
        }
        invalidate(text.page);
        return id;
    }

    void FileTexts::Change(inkpages::TextId id, inkpages::Text const& text)
    {
        int page = -1;
        {
            std::scoped_lock guard(documentLock);
            auto const& element = objects.ElementOf(id);
            page = objects.PageOf(element.get());
            if (page < 0)
            {
                LogError(std::format("texts: change of text {} that is on no page", id));
                return;
            }
            if (page != text.page)
            {
                LogError(std::format("texts: text {} is on page {}, the change names page {} (written on its page)", id,
                    page + 1, text.page + 1));
            }
            auto edit = ToEdit(text);
            Point shift = objects.Origin(text.page);
            Point own = objects.Origin(page);
            edit.at = { edit.at.x + shift.x - own.x, edit.at.y + shift.y - own.y };
            XoppText::Apply(*element, edit);
        }
        invalidate(page);
    }

    uint64_t FileTexts::Remove(inkpages::TextId id)
    {
        return objects.Remove({ id });
    }

    void FileTexts::Restore(uint64_t token)
    {
        objects.Restore(token);
    }
}
