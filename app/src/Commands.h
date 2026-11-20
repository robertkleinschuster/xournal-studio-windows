#pragma once

// The app's command ids: menu items, accelerators and the app's toolbar buttons share them.

namespace xournal::app
{
    enum Command : int
    {
        // Datei
        CmdNew = 100,
        CmdOpen,
        CmdSave,
        CmdSaveAs,
        CmdExportPdf,
        CmdImportPdf,
        CmdOpenFolder,
        CmdSync,
        CmdExit,
        // Bearbeiten
        CmdUndo = 200,
        CmdRedo,
        CmdCut,
        CmdCopy,
        CmdPaste,
        CmdDuplicate,
        CmdDelete,
        CmdSelectAll,
        CmdClearSelection,
        CmdFind,
        CmdFindNext,
        CmdFindPrevious,
        // Seite
        CmdInsertPage = 300,
        CmdInsertFromFile,
        CmdDeletePage,
        CmdMovePageUp,
        CmdMovePageDown,
        // Ansicht
        CmdTogglePane = 400,
        CmdFitWidth,
        CmdZoomIn,
        CmdZoomOut,
        CmdActualSize,
        CmdFitPage,
        CmdGoToPage,
        CmdBack,
        CmdForward,
        // Datei -> Zuletzt geöffnet: CmdRecentFirst + index (settings' recentFiles)
        CmdRecentFirst = 500,
        CmdRecentLast = CmdRecentFirst + 19,
    };
}
