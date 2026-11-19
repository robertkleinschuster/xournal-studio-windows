#pragma once

// Runs an action on the UI thread a while after it was asked for (a thread-pool timer posts it through UiQueue): the
// explorer's refresh 400 ms after the last change on disk, the recovery copy 5 s after a change. Restart() starts the
// wait again (debounce); Start() only when none is running (the first request counts). Nothing runs after destruction.

#include <windows.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace xournal::app
{
    class DelayedAction
    {
    public:
        DelayedAction(std::string name, std::chrono::milliseconds delay, std::function<void()> action);
        ~DelayedAction();
        DelayedAction(DelayedAction const&) = delete;
        DelayedAction& operator=(DelayedAction const&) = delete;

        /// Any thread: the action runs `delay` from now (an earlier wait is replaced).
        void Restart();
        /// Any thread: the action runs `delay` from now unless a wait is running already.
        void Start();
        /// The action will not run for the waits so far.
        void Cancel();

    private:
        static void CALLBACK Fire(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER);
        void Arm();

        struct Shared; // what the timer callback and posted work may still reach after this object is gone
        std::string name;
        std::chrono::milliseconds delay;
        std::shared_ptr<Shared> shared;
        PTP_TIMER timer = nullptr;
    };
}
