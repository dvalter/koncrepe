/*
    SPDX-FileCopyrightText: 2026 Koncrepe Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ShittyStreamScanner.h"

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
    for (int i = offset; i < length; ++i) {
        const auto c = static_cast<unsigned char>(data[i]);
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
                }
                _buffer.clear();
                if (type != EventType::None) {
                    Event event;
                    event.type = type;
                    event.end = i + 1;
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
                const EventType type = escapeFinal(c);
                if (type != EventType::None) {
                    Event event;
                    event.type = type;
                    event.end = i + 1;
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
                const EventType type = escapeFinal(c);
                if (type != EventType::None) {
                    Event event;
                    event.type = type;
                    event.end = i + 1;
                    return event;
                }
            }
            break;
        }
    }
    return {};
}
