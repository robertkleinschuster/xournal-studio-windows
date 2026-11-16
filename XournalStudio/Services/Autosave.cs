using System.Xml.Linq;
using Microsoft.UI.Dispatching;

namespace XournalStudio.Services;

/// <summary>
/// Keeps the recovery copy (<see cref="Recovery"/>) of the open document up to date: a few seconds after a change, or
/// at once when asked (the window lost focus). Writes and deletes run in order on a queue, so a delete never
/// overtakes a write still running.
/// </summary>
public sealed class Autosave
{
    private static readonly TimeSpan Delay = TimeSpan.FromSeconds(5);

    private readonly Recovery recovery;
    private readonly Func<(XDocument Snapshot, RecoveryInfo Info)> snapshot;
    private readonly DispatcherQueueTimer timer;
    private string id = Recovery.NewId();
    private bool pending;
    private Task queue = Task.CompletedTask;

    /// <param name="snapshot">The document as it is now (taken on the UI thread, written on another).</param>
    public Autosave(Recovery recovery, DispatcherQueue dispatcher, Func<(XDocument, RecoveryInfo)> snapshot)
    {
        this.recovery = recovery;
        this.snapshot = snapshot;
        timer = dispatcher.CreateTimer();
        timer.Interval = Delay;
        timer.IsRepeating = false;
        timer.Tick += (_, _) => WriteNow();
    }

    /// <summary>A copy could not be written or deleted (the exception is logged).</summary>
    public event EventHandler<Exception>? Failed;

    /// <summary>Completes when all queued writes and deletes are done.</summary>
    public Task Idle => queue;

    /// <summary>The document has changes the copy does not have yet.</summary>
    public void Changed()
    {
        pending = true;
        if (!timer.IsRunning)
        {
            timer.Start();
        }
    }

    /// <summary>Writes pending changes now.</summary>
    public void WriteNow()
    {
        timer.Stop();
        if (!pending)
        {
            return;
        }
        pending = false;
        string current = id;
        (XDocument, RecoveryInfo) copy;
        try
        {
            copy = snapshot();
        }
        catch (Exception e)
        {
            DebugLog.Write($"snapshot for recovery copy {current}", e);
            pending = true; // tried again with the next change
            Failed?.Invoke(this, e);
            return;
        }
        var (xml, info) = copy;
        Enqueue($"write recovery copy {current}", () => recovery.WriteAsync(current, xml, info));
    }

    /// <summary>
    /// Writes the document now and then deletes <paramref name="oldId"/> (a restored copy) – only if the write
    /// succeeded, so there is always one complete copy.
    /// </summary>
    public void WriteNowReplacing(string oldId)
    {
        timer.Stop();
        pending = false;
        string current = id;
        (XDocument, RecoveryInfo) copy;
        try
        {
            copy = snapshot();
        }
        catch (Exception e)
        {
            DebugLog.Write($"snapshot for recovery copy {current} (the restored copy {oldId} stays)", e);
            pending = true;
            Failed?.Invoke(this, e);
            return;
        }
        var (xml, info) = copy;
        Enqueue($"write recovery copy {current} replacing {oldId}", async () =>
        {
            await recovery.WriteAsync(current, xml, info);
            recovery.Delete(oldId);
        });
    }

    /// <summary>Nothing to recover any more (saved, or discarded by the user): the copy goes.</summary>
    public void Discard()
    {
        timer.Stop();
        pending = false;
        Delete(id);
    }

    /// <summary>Another document is shown: the previous one's copy goes, the new one gets its own.</summary>
    public void StartNewDocument()
    {
        Discard();
        id = Recovery.NewId();
    }

    private void Delete(string copyId) => Enqueue($"delete recovery copy {copyId}", () =>
    {
        recovery.Delete(copyId);
        return Task.CompletedTask;
    });

    private void Enqueue(string what, Func<Task> work) => queue = RunAfter(queue, what, work);

    private async Task RunAfter(Task previous, string what, Func<Task> work)
    {
        await previous; // never faults: failures are reported below
        try
        {
            await work();
        }
        catch (Exception e)
        {
            DebugLog.Write(what, e);
            Failed?.Invoke(this, e);
        }
    }
}
