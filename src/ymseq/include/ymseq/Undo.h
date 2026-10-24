// STYM2149 — ymseq: undo history (Undo / Redo over 50 steps)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Rules of the prototype (undoStack, redoStack, sameEntry, doUndo, doRedo):
//   - a step is the state before the change; it is not pushed if it is identical to the last step;
//   - beyond 50 steps, the oldest one is lost; any new step empties the redo stack;
//   - Undo pops until it finds a step different from the current state of the same object (identical steps are
//     skipped), pushes the current state on the Redo side then returns the step to apply; Redo does the reverse.
// The step type E (pattern, Song, channel, kit, mixer) is defined by the caller: it must be comparable (==).
#pragma once
#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

namespace stym::ymseq {

// Used on the message thread only: the editor's history is pluginui::EditorSession::undo, which outlives the editor
// window but is not saved with the project.
template <class E>
class UndoHistory {
public:
    static constexpr size_t kMax = 50;

    // e: the state of an object just before the caller changes it.
    void push(E e) {
        if (!undo_.empty() && undo_.back() == e) return;
        undo_.push_back(std::move(e));
        if (undo_.size() > kMax) undo_.pop_front();
        redo_.clear();
    }

    // current(e): current state of the object that step e describes (curEntryLike).
    template <class Current>
    std::optional<E> undo(Current&& current) {
        while (!undo_.empty()) {
            E e = std::move(undo_.back());
            undo_.pop_back();
            E now = current(e);
            if (e == now) continue;  // identical state: skip
            redo_.push_back(std::move(now));
            return e;
        }
        return std::nullopt;
    }

    // The reverse of undo: the current state goes back on the Undo side (the oldest step lost beyond kMax).
    template <class Current>
    std::optional<E> redo(Current&& current) {
        while (!redo_.empty()) {
            E e = std::move(redo_.back());
            redo_.pop_back();
            E now = current(e);
            if (e == now) continue;
            undo_.push_back(std::move(now));
            if (undo_.size() > kMax) undo_.pop_front();
            return e;
        }
        return std::nullopt;
    }

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    size_t undoSize() const noexcept { return undo_.size(); }
    size_t redoSize() const noexcept { return redo_.size(); }
    void clear() noexcept {
        undo_.clear();
        redo_.clear();
    }

private:
    std::deque<E> undo_, redo_;
};

}  // namespace stym::ymseq
