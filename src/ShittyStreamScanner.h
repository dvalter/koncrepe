/*
    SPDX-FileCopyrightText: 2026 Koncrepe Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SHITTYSTREAMSCANNER_H
#define SHITTYSTREAMSCANNER_H

#include <QByteArray>

#include "koncrepeprivate_export.h"

namespace Koncrepe
{
/**
 * Watches the byte stream the shitty engine interprets for the few
 * sequences whose meaning lives outside a terminal engine: the working
 * directory, notifications, progress, tab titles, profile changes and
 * the like, which the engine parses but has no one to hand to.
 *
 * The scanner only tracks sequence boundaries; it never alters the
 * stream. It reports each finished sequence of interest together with
 * the offset just past its last byte, so the caller can let the engine
 * consume the stream up to that point before acting on it. It also knows
 * whether the stream currently rests between sequences, which is the only
 * time bytes of our own may be spliced into it.
 */
class KONCREPEPRIVATE_EXPORT ShittyStreamScanner
{
public:
    enum class EventType {
        None,
        /** An OSC string: number() and payload() describe it. */
        Osc,
        /** CSI 3 J: the application erased the scrollback. */
        EraseScrollback,
        /** ESC c: full reset. */
        FullReset,
        /** CSI ! p: soft reset. */
        SoftReset,
        /** A kitty keyboard protocol request: CSI > u, CSI < u, CSI = u, CSI ? u. */
        KittyKeyboard,
    };

    struct Event {
        EventType type = EventType::None;
        /** OSC number, -1 when the OSC does not start with one. */
        int number = -1;
        /** OSC text after the first ';' (raw bytes, usually UTF-8). */
        QByteArray payload;
        /** Offset just past the sequence's final byte. */
        int end = 0;
        /**
         * Offset of the sequence's ESC, or -1 when the sequence started in an
         * earlier buffer.
         */
        int start = -1;
    };

    /**
     * Scans @p data from @p offset and stops after the first sequence of
     * interest, which it returns. Returns an event of type None once the
     * whole buffer was consumed without one.
     */
    Event scan(const char *data, int length, int offset);

    /** True when the stream is between sequences. */
    bool atGround() const
    {
        return _state == State::Ground;
    }

    /** True inside an unfinished escape or control sequence (not a string). */
    bool inControlSequence() const
    {
        return _state == State::Escape || _state == State::EscapeIntermediate || _state == State::Csi;
    }

    /** True inside an unfinished OSC string that is, or may yet be, OSC @p number. */
    bool inOsc(int number) const;

    /**
     * Offset of the unfinished sequence's ESC in the buffer last scanned, or
     * -1 when it started in an earlier buffer.
     */
    int sequenceStart() const
    {
        return _sequenceStart;
    }

    void reset();

private:
    enum class State {
        Ground,
        Escape,
        EscapeIntermediate,
        Csi,
        Osc,
        OscEscape,
        String, // DCS, SOS, PM, APC: skipped up to ST
        StringEscape,
    };

    // Processes the byte after an ESC; returns an event when it completes one.
    EventType escapeFinal(unsigned char c);
    Event finishOsc(int end);

    State _state = State::Ground;
    int _sequenceStart = -1;
    QByteArray _buffer;
    bool _overflow = false;

    // OSC strings longer than this are passed through untouched but not
    // reported: none of the ones we act on is anywhere near this long.
    static constexpr int MaxOscLength = 64 * 1024;
    static constexpr int MaxCsiLength = 32;
};

}

#endif
