#pragma once

// The render thread: work that takes long (PDF pages, PDF pictures) runs here one at a time, the waiting job nearest to
// the page in view first (of one page, the lowest rank first). A job waits under a key: a newer job with the same key replaces it (another zoom), and jobs
// can be dropped before they start (the page left the view, another document). A job that runs is finished.
// Exceptions of a job are logged. The thread owns the PDF renderer (one page at a time, as the interop docs say).

#include "PdfPageRenderer.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace xournal::app
{
    class RenderQueue
    {
    public:
        using Job = std::function<void(PdfPageRenderer&)>;
        static constexpr int ThumbnailRank = 0x40000000;

        RenderQueue();
        ~RenderQueue();
        RenderQueue(RenderQueue const&) = delete;
        RenderQueue& operator=(RenderQueue const&) = delete;

        /// Queues `job` for page `page` under `key` (replacing a waiting job with that key); `rank` orders the jobs of
        /// one page (lower first).
        void Submit(std::uint64_t key, int page, std::string name, Job job, int rank = 0);
        /// Drops waiting jobs whose key matches.
        void Drop(std::function<bool(std::uint64_t key)> const& which);
        /// The page in view: jobs nearest to it run first.
        void SetFocus(int page);
        void SetThumbnailFocus(int page);

        /// Ends the thread after the running job and releases the renderer's device (before the app exits).
        void Stop();

    private:
        struct Waiting
        {
            int page = 0;
            int rank = 0;
            std::string name;
            Job job;
        };

        void Run();

        std::mutex mutex;
        std::condition_variable wake;
        std::map<std::uint64_t, Waiting> waiting;
        int focus = 0;
        int thumbnailFocus = 0;
        bool stopping = false;
        PdfPageRenderer renderer; // used on the render thread only
        std::thread thread;
    };
}
