/*
    SPDX-FileCopyrightText: 2026 Koncrepe Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ShittyStreamScanner.h"

#include <QList>

#include <cstring>

using namespace Koncrepe;

namespace
{
constexpr unsigned char ESC = 0x1b;
constexpr unsigned char BEL = 0x07;
constexpr unsigned char CAN = 0x18;
constexpr unsigned char SUB = 0x1a;
}

void ShittyStreamScanner::reset()
{
    _state = State::Ground;
    _sequenceStart = -1;
    _buffer.clear();
    _overflow = false;
}

ShittyStreamScanner::EventType ShittyStreamScanner::escapeFinal(unsigned char c)
{
    _buffer.clear();
    _overflow = false;
    switch (c) {
    case '[':
        _state = State::Csi;
        return EventType::None;
    case ']':
        _state = State::Osc;
        return EventType::None;
    case 'P': // DCS
    case 'X': // SOS
    case '^': // PM
    case '_': // APC
        _state = State::String;
        return EventType::None;
    case ESC:
        _state = State::Escape;
        return EventType::None;
    case CAN:
    case SUB:
        _state = State::Ground;
        return EventType::None;
    case 'c':
        _state = State::Ground;
        return EventType::FullReset;
    default:
        if (c >= 0x20 && c <= 0x2f) {
            _state = State::EscapeIntermediate;
        } else if (c < 0x20) {
            // C0 controls execute inside an escape sequence without ending it
            _state = State::Escape;
        } else {
            _state = State::Ground;
        }
        return EventType::None;
    }
}

ShittyStreamScanner::Event ShittyStreamScanner::finishOsc(int end)
{
    Event event;
    _state = State::Ground;
    if (_overflow) {
        _buffer.clear();
        _overflow = false;
        return event;
    }
    event.type = EventType::Osc;
    event.end = end;
    event.start = _sequenceStart;
    int i = 0;
    int number = 0;
    bool digits = false;
    while (i < _buffer.size() && _buffer[i] >= '0' && _buffer[i] <= '9' && number < 100000) {
        number = number * 10 + (_buffer[i] - '0');
        digits = true;
        ++i;
    }
    event.number = digits ? number : -1;
    if (i < _buffer.size() && _buffer[i] == ';') {
        event.payload = _buffer.mid(i + 1);
    }
    _buffer.clear();
    return event;
}

ShittyStreamScanner::Event ShittyStreamScanner::scan(const char *data, int length, int offset)
{
    if (offset == 0 && !atGround()) {
        _sequenceStart = -1; // continues from an earlier buffer
    }
    for (int i = offset; i < length; ++i) {
        if (_state == State::Ground) {
            // Text is most of any output, and only an ESC ends it here:
            // let memchr's vector loop find the next one.
            const void *escape = memchr(data + i, ESC, size_t(length - i));
            if (escape == nullptr) {
                break;
            }
            i = int(static_cast<const char *>(escape) - data);
        }
        const auto c = static_cast<unsigned char>(data[i]);
        // An ESC starts a sequence, except the one opening a string terminator
        if (c == ESC && _state != State::Osc && _state != State::OscEscape && _state != State::String && _state != State::StringEscape) {
            _sequenceStart = i;
        }
        switch (_state) {
        case State::Ground:
            if (c == ESC) {
                _state = State::Escape;
            }
            break;

        case State::Escape: {
            const EventType type = escapeFinal(c);
            if (type != EventType::None) {
                Event event;
                event.type = type;
                event.end = i + 1;
                return event;
            }
            break;
        }

        case State::EscapeIntermediate:
            if (c == ESC) {
                _state = State::Escape;
            } else if (c == CAN || c == SUB || c >= 0x30) {
                _state = State::Ground;
            }
            break;

        case State::Csi:
            if (c == ESC) {
                _state = State::Escape;
            } else if (c == CAN || c == SUB) {
                _state = State::Ground;
            } else if (c >= 0x40 && c <= 0x7e) {
                _state = State::Ground;
                EventType type = EventType::None;
                if (c == 'J' && _buffer == "3") {
                    type = EventType::EraseScrollback;
                } else if (c == 'p' && _buffer == "!") {
                    type = EventType::SoftReset;
                } else if (c == 'u' && !_buffer.isEmpty() && (_buffer[0] == '>' || _buffer[0] == '<' || _buffer[0] == '=' || _buffer[0] == '?')) {
                    type = EventType::KittyKeyboard;
                } else if ((c == 'h' || c == 'l') && _buffer.startsWith('?')) {
                    for (const QByteArray &mode : _buffer.mid(1).split(';')) {
                        if (mode == "47" || mode == "1047" || mode == "1049") {
                            type = EventType::ScreenSwitch;
                            break;
                        }
                    }
                }
                _buffer.clear();
                if (type != EventType::None) {
                    Event event;
                    event.type = type;
                    event.end = i + 1;
                    event.start = _sequenceStart;
                    return event;
                }
            } else if (c >= 0x20) {
                if (_buffer.size() < MaxCsiLength) {
                    _buffer.append(char(c));
                }
            }
            // other C0 controls execute without ending the sequence
            break;

        case State::Osc:
            if (c == BEL) {
                Event event = finishOsc(i + 1);
                if (event.type != EventType::None) {
                    return event;
                }
            } else if (c == ESC) {
                _state = State::OscEscape;
            } else if (c == CAN || c == SUB) {
                reset();
            } else if (!_overflow) {
                if (_buffer.size() < MaxOscLength) {
                    _buffer.append(char(c));
                } else {
                    _overflow = true;
                    _buffer.clear();
                }
            }
            break;

        case State::OscEscape:
            if (c == '\\') {
                Event event = finishOsc(i + 1);
                if (event.type != EventType::None) {
                    return event;
                }
            } else {
                // ESC anything-but-backslash abandons the string and starts
                // a new escape sequence with this byte
                _sequenceStart = i > 0 ? i - 1 : -1;
                const EventType type = escapeFinal(c);
                if (type != EventType::None) {
                    Event event;
                    event.type = type;
                    event.end = i + 1;
                    event.start = _sequenceStart;
                    return event;
                }
            }
            break;

        case State::String:
            if (c == ESC) {
                _state = State::StringEscape;
            } else if (c == CAN || c == SUB) {
                _state = State::Ground;
            }
            break;

        case State::StringEscape:
            if (c == '\\') {
                _state = State::Ground;
            } else {
                _sequenceStart = i > 0 ? i - 1 : -1;
                const EventType type = escapeFinal(c);
                if (type != EventType::None) {
                    Event event;
                    event.type = type;
                    event.end = i + 1;
                    event.start = _sequenceStart;
                    return event;
                }
            }
            break;
        }
    }
    return {};
}

bool ShittyStreamScanner::inOsc(int number) const
{
    if (_state != State::Osc && _state != State::OscEscape) {
        return false;
    }
    // what arrived so far could still be this number, or already is
    QByteArray prefix = QByteArray::number(number);
    prefix.append(';');
    return _buffer.size() < prefix.size() ? prefix.startsWith(_buffer) : _buffer.startsWith(prefix);
}
