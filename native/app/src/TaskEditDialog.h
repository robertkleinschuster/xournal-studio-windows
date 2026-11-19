#pragma once

// "Aufgabe bearbeiten": a modal dialog (an in-memory dialog template, comctl32 controls) with the task's text, its due
// date (date picker, "none" possible) and its page link (keep / remove, or link the page in view). It only asks;
// TasksPane writes the line.

#include "xournal/model/TodoTxt.h"

#include <windows.h>

#include <optional>
#include <string>

namespace xournal::app::TaskEditDialog
{
    struct Values
    {
        std::wstring text;              // the description without due: and doc: tags
        std::optional<Date> due;
        bool link = false;              // keep the link (or link the page in view when there is none)
    };

    /// `linkText`: what the task links to ("Blatt 3.xopp, Seite 2"), empty when it has no link; `canLink`: a page is in
    /// view that a task without link could get. Nothing when cancelled.
    std::optional<Values> Show(HWND owner, Values const& values, std::wstring const& linkText, bool canLink);
}
