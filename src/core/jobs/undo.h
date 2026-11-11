// Takes an undo entry back: its items in reverse, each by its inverse.
// What was created goes to the trash, what was trashed or replaced
// comes back from it, what was moved or renamed goes back, mode and
// owner are set to what they were. Nothing is deleted on its own:
// where there is no trash (a network share), taking a created item
// back means deleting it, and the job asks first. An item that
// changed since the operation (size or time, or mode and owner) is
// left alone and listed.
//
// The run records what it did like any job, and that record is the
// entry for the other direction: a redo is the undo of an undo.
#pragma once

#include "core/jobs/job.h"

namespace core {

class UndoJob : public Job {
    Q_OBJECT
public:
    UndoJob(UndoEntry entry, bool redo, QObject* parent = nullptr);
    ~UndoJob() override { stop(); }

    bool isRedo() const { return kind() == JobKind::Redo; }
    const QString& what() const { return entry_.what; }
    // Valid once finished: the items an abort left untouched, still in
    // the order they happened.
    UndoEntry remaining() const;

protected:
    void run() override;

private:
    struct Impl;
    friend struct Impl;

    UndoEntry entry_;
    size_t left_ = 0;  // items [0, left_) were not reached
};

// Where trashed items are, by the path they had: the home trash and
// the trash folders of the volume a path is on, read from their info
// files as the freedesktop trash specification lays them out. GIO can
// put a file in the trash but not say where it went.
class TrashIndex {
public:
    TrashIndex();
    ~TrashIndex();
    // The trashed copy of original that was trashed at `when`
    // (milliseconds, taken just after), and its info file. False when there is none. An entry
    // is handed out once.
    bool find(const Location& original, qint64 when, Location* file, Location* info);
    // Reads the trash folders that serve original, if not read yet. An
    // undo does this before it trashes anything itself, so that what
    // it puts there is not mistaken for what it is looking for.
    void prime(const Location& original);

private:
    struct Data;
    std::unique_ptr<Data> d_;
};

} // namespace core
