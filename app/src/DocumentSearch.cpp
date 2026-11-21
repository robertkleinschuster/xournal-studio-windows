#include "DocumentSearch.h"

#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"

#include <chrono>
#include <format>
#include <mutex>

namespace xournal::app
{
    // Shared with the background work: the handwriting cache and the PDF's text (read once per PDF file).
    struct DocumentSearch::Shared
    {
        search::HandwritingCache cache;
        std::shared_ptr<search::VisualTextCache> visualText = search::CreateVisualTextCache();
        std::mutex lock;
        std::optional<std::filesystem::path> pdfPath;
        std::shared_ptr<pdftext::TextDocument const> pdfText;
        bool pdfTried = false;

        // The PDF's text document (any thread; nothing without PDF or PDFium - logged once).
        std::shared_ptr<pdftext::TextDocument const> PdfText()
        {
            std::scoped_lock guard(lock);
            if (!pdfPath || pdfTried) return pdfText;
            pdfTried = true;
            if (!pdftext::Available())
            {
                LogError(std::format("search: the PDF's text is not searched ({})", pdftext::UnavailableReason()));
                return nullptr;
            }
            try
            {
                auto data = io::Files::ReadAll(*pdfPath);
                pdfText = std::make_shared<pdftext::TextDocument const>(data);
            }
            catch (...)
            {
                LogException(std::format("search: reading the text of {}", ToUtf8(pdfPath->native())));
            }
            return pdfText;
        }
    };

    DocumentSearch::DocumentSearch(DocumentView& view) : view(view), shared(std::make_shared<Shared>())
    {
    }

    DocumentSearch::~DocumentSearch()
    {
        stop.request_stop();
        *alive = false;
    }

    void DocumentSearch::Cancel()
    {
        stop.request_stop();
        stop = std::stop_source();
        ++generation;
        running = false;
    }

    void DocumentSearch::Clear()
    {
        Cancel();
        hits.clear();
        current.reset();
        view.Highlight(-1, {});
        if (changed) changed();
    }

    void DocumentSearch::DocumentShown()
    {
        Clear();
        query.clear();
        shared = std::make_shared<Shared>(); // the old document's handwriting and PDF text go
        if (auto doc = view.Document()) shared->pdfPath = doc->PdfPath();
        RecognizeAhead();
    }

    void DocumentSearch::PagesChanged()
    {
        if (!hits.empty() || running) Clear();
        auto doc = view.Document();
        if (!doc) return;
        std::scoped_lock guard(shared->lock);
        // Pages of another PDF are appended to the document's PDF: its text is read anew.
        shared->pdfPath = doc->PdfPath();
        shared->pdfText.reset();
        shared->pdfTried = false;
    }

    void DocumentSearch::RecognizeAhead()
    {
        auto doc = view.Document();
        if (!doc) return;
        search::SearchDocument snapshot;
        try
        {
            view.CaptureInk([&] { snapshot = search::PrepareSearch(*doc, nullptr); });
        }
        catch (...)
        {
            LogException("search: snapshot for recognising the handwriting ahead");
            return;
        }
        UiQueue::RunInBackground("search: recognise handwriting", [snapshot, keep = shared, token = stop.get_token()]
        {
            auto start = std::chrono::steady_clock::now();
            bool done = search::RecognizeAll(snapshot, keep->cache, token);
            Log(std::format("search: handwriting {} in {} ms ({} pages known)", done ? "recognised" : "stopped",
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(),
                keep->cache.Size()));
        });
    }

    void DocumentSearch::Start(std::wstring const& text)
    {
        Clear();
        query = text;
        auto doc = view.Document();
        if (!doc || Trim(query).empty()) return;
        search::SearchDocument snapshot;
        try
        {
            view.CaptureInk([&] { snapshot = search::PrepareSearch(*doc, nullptr); });
        }
        catch (...)
        {
            LogException("search: snapshot of the document");
            return;
        }
        running = true;
        if (changed) changed();
        std::uint64_t mine = generation;
        Log(std::format("search: \"{}\"", ToUtf8(query)));
        auto post = [this, alive = alive, mine](std::string name, std::function<void()> work)
        {
            UiQueue::Post(std::move(name), [this, alive, mine, work = std::move(work)]
            {
                if (*alive && mine == generation) work();
            });
        };
        // The work's results run on the UI thread only while this is alive and their search the current one, so
        // these lambdas may use `this` there (never on the pool thread).
        auto addHits = [this](std::vector<search::SearchHit> found)
        {
            hits.insert(hits.end(), found.begin(), found.end());
            if (!current) Show(0); // the first hit is shown at once
            else if (changed) changed();
        };
        auto finished = [this](bool completed)
        {
            running = false;
            Log(std::format("search: {} hits{}", hits.size(), completed ? "" : " (stopped)"));
            if (changed) changed();
        };
        UiQueue::RunInBackground("search", [post, addHits, finished, snapshot, text, keep = shared, token = stop.get_token()]() mutable
        {
            snapshot.pdf = keep->PdfText();
            snapshot.visualText = keep->visualText;
            bool completed = false;
            try
            {
                completed = search::Search(snapshot, text, {}, keep->cache, [&](int, std::vector<search::SearchHit> found)
                {
                    if (!found.empty()) post("search: hits", [addHits, found = std::move(found)] { addHits(found); });
                }, token);
            }
            catch (...)
            {
                LogException(std::format("search: \"{}\"", ToUtf8(text)));
            }
            post("search: done", [finished, completed] { finished(completed); });
        });
    }

    void DocumentSearch::Show(size_t index)
    {
        if (index >= hits.size()) return;
        current = index;
        auto const& hit = hits[index];
        view.Highlight(hit.page, hit.rects);
        if (shown) shown(hit);
        if (changed) changed();
    }

    void DocumentSearch::Next()
    {
        if (hits.empty()) return;
        Show(current ? (*current + 1) % hits.size() : 0);
    }

    void DocumentSearch::Previous()
    {
        if (hits.empty()) return;
        Show(current ? (*current + hits.size() - 1) % hits.size() : hits.size() - 1);
    }
}
