/*
    SPDX-FileCopyrightText: 2026 Koncrepe Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ShittyEmulation.h"

// Qt
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QVarLengthArray>

// KDE
#include <KLocalizedString>
#include <KNotification>

// Koncrepe
#include "EscapeSequenceUrlExtractor.h"
#include "KoncrepeSettings.h"
#include "Screen.h"
#include "ScreenWindow.h"
#include "history/HistoryType.h"
#include "keyboardtranslator/KeyboardTranslator.h"
#include "keyboardtranslator/KeyboardTranslatorReader.h"
#include "terminalDisplay/TerminalColor.h"
#include "terminalDisplay/TerminalDisplay.h"

#ifdef HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#endif

#include <shitty_vt.h>

#include <array>
#include <cstring>
#include <utility>

using namespace Koncrepe;

namespace
{
// Lines the engine keeps in its own scrollback. It only has to hold what
// scrolls off between two drains; everything older lives in Koncrepe's
// history, whichever kind the profile picked.
constexpr uint16_t EngineSaveLines = 2048;
// A byte scrolls at most one line, so feeding no more bytes than the
// engine's scrollback has room for can never overflow it. Lines are only
// copied out when that room runs short or the screen is about to be shown:
// output that scrolls faster than the views repaint is copied in batches,
// and a batch longer than the history keeps is mostly never copied at all.
constexpr uint32_t EngineHistoryRoom = EngineSaveLines - 1; // the parked view takes one row
// Below this much room, copy the waiting lines out rather than feed in
// small slices.
constexpr uint32_t MinFeedSlice = 256;

QByteArray colorSpec(const QColor &color)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "rgb:%02x/%02x/%02x", color.red(), color.green(), color.blue());
    return QByteArray(buffer);
}

uint32_t packColor(const QColor &color)
{
    return uint32_t(color.red()) | (uint32_t(color.green()) << 8) | (uint32_t(color.blue()) << 16);
}

// The xterm 256-color cube and gray ramp, which is also what Koncrepe's
// CharacterColor computes for entries 16 and up.
uint32_t xtermColor(int index)
{
    if (index < 232) {
        const int u = index - 16;
        const auto level = [](int v) {
            return v ? 40 * v + 55 : 0;
        };
        return uint32_t(level((u / 36) % 6)) | (uint32_t(level((u / 6) % 6)) << 8) | (uint32_t(level(u % 6)) << 16);
    }
    const int gray = (index - 232) * 10 + 8;
    return uint32_t(gray) | (uint32_t(gray) << 8) | (uint32_t(gray) << 16);
}

// An xterm color specification ("rgb:r/g/b" in 1-4 hex digits per channel,
// or anything QColor understands) as a QColor.
QColor parseColorSpec(const QByteArray &spec)
{
    if (spec.startsWith("rgb:")) {
        const QList<QByteArray> parts = spec.mid(4).split('/');
        if (parts.size() != 3) {
            return {};
        }
        int channels[3];
        for (int i = 0; i < 3; ++i) {
            bool ok = false;
            const int digits = parts[i].size();
            const int value = parts[i].toInt(&ok, 16);
            if (!ok || digits < 1 || digits > 4) {
                return {};
            }
            const int max = (1 << (4 * digits)) - 1;
            channels[i] = value * 255 / max;
        }
        return QColor(channels[0], channels[1], channels[2]);
    }
    return QColor::fromString(QString::fromUtf8(spec));
}

uint16_t engineModifiers(Qt::KeyboardModifiers modifiers)
{
    uint16_t result = 0;
    if (modifiers & Qt::ShiftModifier) {
        result |= SHITTY_VT_MOD_SHIFT;
    }
    if (modifiers & Qt::ControlModifier) {
        result |= SHITTY_VT_MOD_CONTROL;
    }
    if (modifiers & Qt::AltModifier) {
        result |= SHITTY_VT_MOD_ALT;
    }
    if (modifiers & Qt::MetaModifier) {
        result |= SHITTY_VT_MOD_SUPER;
    }
    if (modifiers & Qt::GroupSwitchModifier) {
        result |= SHITTY_VT_MOD_ALT_GRAPH;
    }
    return result;
}

// X11/Wayland scan codes (evdev + 8) that tell the right-hand modifiers apart.
constexpr quint32 ScanRightShift = 62;
constexpr quint32 ScanRightControl = 105;
constexpr quint32 ScanRightAlt = 108;
constexpr quint32 ScanRightSuper = 134;

// Maps a Qt key to the engine's named keys. Printable keys return
// SHITTY_VT_KEY_UNKNOWN; the caller identifies them by codepoint.
// *numLock is set for keypad keys that can only come from NumLock.
uint16_t namedKey(const QKeyEvent *event, bool *numLock)
{
    const bool keypad = event->modifiers() & Qt::KeypadModifier;
    const quint32 scan = event->nativeScanCode();
    *numLock = false;

    if (keypad) {
        switch (event->key()) {
        case Qt::Key_0:
        case Qt::Key_1:
        case Qt::Key_2:
        case Qt::Key_3:
        case Qt::Key_4:
        case Qt::Key_5:
        case Qt::Key_6:
        case Qt::Key_7:
        case Qt::Key_8:
        case Qt::Key_9:
            *numLock = true;
            return SHITTY_VT_KEY_KEYPAD_0 + (event->key() - Qt::Key_0);
        case Qt::Key_Period:
            *numLock = true;
            return SHITTY_VT_KEY_KEYPAD_DECIMAL;
        case Qt::Key_Comma:
            return SHITTY_VT_KEY_KEYPAD_SEPARATOR;
        case Qt::Key_Slash:
            return SHITTY_VT_KEY_KEYPAD_DIVIDE;
        case Qt::Key_Asterisk:
            return SHITTY_VT_KEY_KEYPAD_MULTIPLY;
        case Qt::Key_Minus:
            return SHITTY_VT_KEY_KEYPAD_SUBTRACT;
        case Qt::Key_Plus:
            return SHITTY_VT_KEY_KEYPAD_ADD;
        case Qt::Key_Enter:
        case Qt::Key_Return:
            return SHITTY_VT_KEY_KEYPAD_ENTER;
        case Qt::Key_Equal:
            return SHITTY_VT_KEY_KEYPAD_EQUAL;
        case Qt::Key_Insert:
            return SHITTY_VT_KEY_KEYPAD_INSERT;
        case Qt::Key_Delete:
            return SHITTY_VT_KEY_KEYPAD_DELETE;
        case Qt::Key_Up:
            return SHITTY_VT_KEY_KEYPAD_UP;
        case Qt::Key_Down:
            return SHITTY_VT_KEY_KEYPAD_DOWN;
        case Qt::Key_Left:
            return SHITTY_VT_KEY_KEYPAD_LEFT;
        case Qt::Key_Right:
            return SHITTY_VT_KEY_KEYPAD_RIGHT;
        case Qt::Key_Home:
            return SHITTY_VT_KEY_KEYPAD_HOME;
        case Qt::Key_End:
            return SHITTY_VT_KEY_KEYPAD_END;
        case Qt::Key_PageUp:
            return SHITTY_VT_KEY_KEYPAD_PAGE_UP;
        case Qt::Key_PageDown:
            return SHITTY_VT_KEY_KEYPAD_PAGE_DOWN;
        case Qt::Key_Clear:
            return SHITTY_VT_KEY_KEYPAD_BEGIN;
        case Qt::Key_Space:
            return SHITTY_VT_KEY_KEYPAD_SPACE;
        case Qt::Key_Tab:
            return SHITTY_VT_KEY_KEYPAD_TAB;
        default:
            break;
        }
    }

    switch (event->key()) {
    case Qt::Key_Space:
        return SHITTY_VT_KEY_SPACE;
    case Qt::Key_Escape:
        return SHITTY_VT_KEY_ESCAPE;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return SHITTY_VT_KEY_ENTER;
    case Qt::Key_Backspace:
        return SHITTY_VT_KEY_BACKSPACE;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return SHITTY_VT_KEY_TAB;
    case Qt::Key_Insert:
        return SHITTY_VT_KEY_INSERT;
    case Qt::Key_Delete:
        return SHITTY_VT_KEY_DELETE;
    case Qt::Key_Home:
        return SHITTY_VT_KEY_HOME;
    case Qt::Key_End:
        return SHITTY_VT_KEY_END;
    case Qt::Key_Up:
        return SHITTY_VT_KEY_UP;
    case Qt::Key_Down:
        return SHITTY_VT_KEY_DOWN;
    case Qt::Key_Left:
        return SHITTY_VT_KEY_LEFT;
    case Qt::Key_Right:
        return SHITTY_VT_KEY_RIGHT;
    case Qt::Key_PageUp:
        return SHITTY_VT_KEY_PAGE_UP;
    case Qt::Key_PageDown:
        return SHITTY_VT_KEY_PAGE_DOWN;
    case Qt::Key_Clear:
        return SHITTY_VT_KEY_CLEAR;
    case Qt::Key_CapsLock:
        return SHITTY_VT_KEY_CAPS_LOCK;
    case Qt::Key_ScrollLock:
        return SHITTY_VT_KEY_SCROLL_LOCK;
    case Qt::Key_NumLock:
        return SHITTY_VT_KEY_NUM_LOCK;
    case Qt::Key_Print:
        return SHITTY_VT_KEY_PRINT_SCREEN;
    case Qt::Key_Pause:
        return SHITTY_VT_KEY_PAUSE;
    case Qt::Key_Menu:
        return SHITTY_VT_KEY_MENU;
    case Qt::Key_Shift:
        return scan == ScanRightShift ? SHITTY_VT_KEY_RIGHT_SHIFT : SHITTY_VT_KEY_LEFT_SHIFT;
    case Qt::Key_Control:
        return scan == ScanRightControl ? SHITTY_VT_KEY_RIGHT_CONTROL : SHITTY_VT_KEY_LEFT_CONTROL;
    case Qt::Key_Alt:
        return scan == ScanRightAlt ? SHITTY_VT_KEY_RIGHT_ALT : SHITTY_VT_KEY_LEFT_ALT;
    case Qt::Key_AltGr:
        return SHITTY_VT_KEY_RIGHT_ALT;
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
        return (scan == ScanRightSuper || event->key() == Qt::Key_Super_R) ? SHITTY_VT_KEY_RIGHT_SUPER : SHITTY_VT_KEY_LEFT_SUPER;
    case Qt::Key_MediaPlay:
        return SHITTY_VT_KEY_MEDIA_PLAY;
    case Qt::Key_MediaPause:
        return SHITTY_VT_KEY_MEDIA_PAUSE;
    case Qt::Key_MediaTogglePlayPause:
        return SHITTY_VT_KEY_MEDIA_PLAY_PAUSE;
    case Qt::Key_MediaStop:
        return SHITTY_VT_KEY_MEDIA_STOP;
    case Qt::Key_AudioForward:
        return SHITTY_VT_KEY_MEDIA_FAST_FORWARD;
    case Qt::Key_AudioRewind:
        return SHITTY_VT_KEY_MEDIA_REWIND;
    case Qt::Key_MediaNext:
        return SHITTY_VT_KEY_MEDIA_TRACK_NEXT;
    case Qt::Key_MediaPrevious:
        return SHITTY_VT_KEY_MEDIA_TRACK_PREVIOUS;
    case Qt::Key_MediaRecord:
        return SHITTY_VT_KEY_MEDIA_RECORD;
    case Qt::Key_VolumeDown:
        return SHITTY_VT_KEY_VOLUME_DOWN;
    case Qt::Key_VolumeUp:
        return SHITTY_VT_KEY_VOLUME_UP;
    case Qt::Key_VolumeMute:
        return SHITTY_VT_KEY_VOLUME_MUTE;
    default:
        break;
    }

    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F35) {
        return SHITTY_VT_KEY_F1 + (event->key() - Qt::Key_F1);
    }
    return SHITTY_VT_KEY_UNKNOWN;
}

#ifdef HAVE_XKBCOMMON
// The key's identity in the base (US) layout, which layout-independent
// chords and the kitty protocol's base-layout key are defined against.
class BaseLayout
{
public:
    BaseLayout()
    {
        const QString platform = QGuiApplication::platformName();
        if (platform != QLatin1String("xcb") && platform != QLatin1String("wayland")) {
            return; // scan codes are only xkb keycodes on these
        }
        _context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (_context == nullptr) {
            return;
        }
        const struct xkb_rule_names names = {nullptr, nullptr, "us", nullptr, nullptr};
        _keymap = xkb_keymap_new_from_names(_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }

    ~BaseLayout()
    {
        if (_keymap != nullptr) {
            xkb_keymap_unref(_keymap);
        }
        if (_context != nullptr) {
            xkb_context_unref(_context);
        }
    }

    uint32_t codepoint(quint32 scanCode) const
    {
        if (_keymap == nullptr || scanCode == 0) {
            return 0;
        }
        const xkb_keysym_t *symbols = nullptr;
        if (xkb_keymap_key_get_syms_by_level(_keymap, scanCode, 0, 0, &symbols) <= 0) {
            return 0;
        }
        return xkb_keysym_to_utf32(symbols[0]);
    }

private:
    struct xkb_context *_context = nullptr;
    struct xkb_keymap *_keymap = nullptr;
};

uint32_t baseCodepoint(quint32 scanCode)
{
    static const BaseLayout layout;
    return layout.codepoint(scanCode);
}
#else
uint32_t baseCodepoint(quint32)
{
    return 0;
}
#endif

// The stock key bindings. A key whose binding in the session's keytab is
// exactly the stock one is left for the engine to encode, so the encoding
// the application negotiated (modifyOtherKeys, the kitty protocol) applies.
// A binding that differs is a user's (or a non-default keytab's) choice and
// is honoured as written.
const KeyboardTranslator *stockTranslator()
{
    static const KeyboardTranslator *translator = []() -> const KeyboardTranslator * {
        QFile source(QStringLiteral(":/koncrepe/keyboard-layouts/default.keytab"));
        if (!source.open(QIODevice::ReadOnly)) {
            return nullptr;
        }
        auto result = new KeyboardTranslator(QStringLiteral("default"));
        KeyboardTranslatorReader reader(&source);
        while (reader.hasNextEntry()) {
            result->addEntry(reader.nextEntry());
        }
        return result;
    }();
    return translator;
}

// Screen::wordFlags() of an ASCII character, from a table.
ExtraFlags asciiWordFlags(char32_t c)
{
    static const auto table = [] {
        std::array<ExtraFlags, 0x80> flags{};
        for (char32_t i = 0; i < 0x80; ++i) {
            flags[i] = Screen::wordFlags(i);
        }
        return flags;
    }();
    return table[c];
}

// A cell color as Koncrepe stores it: the request the application made,
// so the color scheme draws it. @p palette is what the engine resolves the
// 256 palette entries to, when Koncrepe's theme is in it.
inline CharacterColor convertColor(uint16_t source, uint32_t resolved, const uint32_t *palette)
{
    const auto rgb = [resolved] {
        return CharacterColor(COLOR_SPACE_RGB, int(((resolved & 0xff) << 16) | (resolved & 0xff00) | ((resolved >> 16) & 0xff)));
    };
    switch (SHITTY_VT_COLOR_KIND(source)) {
    case SHITTY_VT_COLOR_DEFAULT_FOREGROUND:
        return CharacterColor(COLOR_SPACE_DEFAULT, DEFAULT_FORE_COLOR);
    case SHITTY_VT_COLOR_DEFAULT_BACKGROUND:
        return CharacterColor(COLOR_SPACE_DEFAULT, DEFAULT_BACK_COLOR);
    case SHITTY_VT_COLOR_INDEXED: {
        const int index = SHITTY_VT_COLOR_INDEX(source);
        // An application may have redefined the entry (OSC 4): then the
        // color it asked for is no longer the scheme's, draw what it set.
        if (palette != nullptr && resolved != palette[index]) {
            return rgb();
        }
        if (index < 16) {
            return CharacterColor(COLOR_SPACE_SYSTEM, index);
        }
        return CharacterColor(COLOR_SPACE_256, index);
    }
    default:
        return rgb();
    }
}
}

ShittyEmulation::ShittyEmulation()
{
    static const shitty_vt_callbacks callbacks = [] {
        shitty_vt_callbacks c{};
        c.title_changed = &ShittyEmulation::onTitleChanged;
        c.bell = &ShittyEmulation::onBell;
        c.clipboard_set = &ShittyEmulation::onClipboardSet;
        c.resize_request = &ShittyEmulation::onResizeRequest;
        return c;
    }();
    // The engine keeps the callbacks pointer, not a copy, and hands back
    // callbacks.user: give every emulation its own struct.
    auto *ownCallbacks = new shitty_vt_callbacks(callbacks);
    ownCallbacks->user = this;
    _callbacks = ownCallbacks;
    _vt = shitty_vt_new(uint16_t(_columns), uint16_t(_lines), EngineSaveLines, _callbacks);
    Q_ASSERT(_vt != nullptr);

    for (int i = 0; i < 256; ++i) {
        _enginePalette[i] = i < 16 ? 0 : xtermColor(i);
    }

    _sessionAttributesUpdateTimer = new QTimer(this);
    _sessionAttributesUpdateTimer->setSingleShot(true);
    connect(_sessionAttributesUpdateTimer, &QTimer::timeout, this, &ShittyEmulation::updateSessionAttributes);

    setCodec(QByteArrayLiteral("UTF-8"));
    pushEngineSetup();
}

ShittyEmulation::~ShittyEmulation()
{
    shitty_vt_free(_vt);
    delete _callbacks;
}

bool ShittyEmulation::engineIsUtf8() const
{
    return utf8();
}

// ---------------------------------------------------------------------------
// Engine callbacks

void ShittyEmulation::onTitleChanged(void *user, const uint8_t *title, size_t len)
{
    auto *self = static_cast<ShittyEmulation *>(user);
    self->_pendingTitle = QByteArray(reinterpret_cast<const char *>(title), int(len));
    self->_titlePending = true;
}

void ShittyEmulation::onBell(void *user)
{
    static_cast<ShittyEmulation *>(user)->_pendingBells++;
}

void ShittyEmulation::onClipboardSet(void *user, int clipboard, const uint8_t *bytes, size_t len)
{
    auto *self = static_cast<ShittyEmulation *>(user);
    // The engine also reports its own mouse selections this way. Koncrepe
    // does its own selection, so only application requests (OSC 52) count.
    if (self->_inPointerEvent) {
        return;
    }
    self->_pendingClipboard.append({clipboard, QByteArray(reinterpret_cast<const char *>(bytes), int(len))});
}

void ShittyEmulation::onResizeRequest(void *user, uint16_t, uint16_t)
{
    // The window size belongs to the user; the engine resized itself
    // regardless, so it is put back once it returns.
    static_cast<ShittyEmulation *>(user)->_resizeRequested = true;
}

void ShittyEmulation::dispatchEngineEvents()
{
    if (_resizeRequested) {
        _resizeRequested = false;
        shitty_vt_resize(_vt, uint16_t(_columns), uint16_t(_lines));
        resetHistoryTracking();
    }
    if (_titlePending) {
        _titlePending = false;
        _pendingSessionAttributesUpdates[0] = QString::fromUtf8(_pendingTitle);
        _sessionAttributesUpdateTimer->start(20);
    }
    while (_pendingBells > 0) {
        _pendingBells--;
        Q_EMIT bell();
    }
    const auto clipboards = std::exchange(_pendingClipboard, {});
    for (const auto &[which, bytes] : clipboards) {
        const QClipboard::Mode mode = which == 0 ? QClipboard::Selection : QClipboard::Clipboard;
        if (mode == QClipboard::Selection && !QApplication::clipboard()->supportsSelection()) {
            continue;
        }
        if (bytes.isEmpty()) {
            QApplication::clipboard()->clear(mode);
        } else {
            QApplication::clipboard()->setText(QString::fromUtf8(bytes), mode);
        }
    }
}

void ShittyEmulation::updateSessionAttributes()
{
    const auto updates = std::exchange(_pendingSessionAttributesUpdates, {});
    for (auto it = updates.cbegin(); it != updates.cend(); ++it) {
        Q_EMIT sessionAttributeChanged(it.key(), it.value());
    }
}

// ---------------------------------------------------------------------------
// Output from the child

void ShittyEmulation::receiveData(const char *text, int length)
{
    if (length <= 0) {
        return;
    }
    bufferedUpdate();

    if (engineIsUtf8()) {
        feed(text, length);
    } else {
        // The engine speaks UTF-8 only; honour the profile's encoding by
        // transcoding on the way in.
        const QString decoded = _decoder.decode(QByteArrayView(text, length));
        const QByteArray utf8 = decoded.toUtf8();
        feed(utf8.constData(), utf8.size());
    }

    if (KoncrepeSettings::listenForZModemTerminalCodes()) {
        const auto *found = static_cast<const char *>(memchr(text, '\030', length));
        if (found != nullptr) {
            for (int i = int(found - text); i < length - 4; i++) {
                if (text[i] == '\030') {
                    if (qstrncmp(text + i + 1, "B00", 3) == 0) {
                        Q_EMIT zmodemDownloadDetected();
                    } else if (qstrncmp(text + i + 1, "B01", 3) == 0) {
                        Q_EMIT zmodemUploadDetected();
                    }
                }
            }
        }
    }
}

void ShittyEmulation::feed(const char *bytes, int length)
{
    QByteArray joined;
    if (!_heldBack.isEmpty()) {
        joined = std::exchange(_heldBack, {}) + QByteArray(bytes, length);
        bytes = joined.constData();
        length = int(joined.size());
    }

    int start = 0;
    while (start < length) {
        const ShittyStreamScanner::Event event = _scanner.scan(bytes, length, start);
        const bool none = event.type == ShittyStreamScanner::EventType::None;
        int end = none ? length : event.end;
        int engineEnd = end;

        if (none && _scanner.sequenceStart() >= start && ((!_kittyKeyboardEnabled && _scanner.inControlSequence()) || _scanner.inOsc(8))) {
            // A sequence the engine may not get is arriving in pieces: hold
            // it back until it is whole.
            _heldBack = QByteArray(bytes + _scanner.sequenceStart(), length - _scanner.sequenceStart());
            engineEnd = _scanner.sequenceStart();
            _scanner.reset();
        }
        if (event.type == ShittyStreamScanner::EventType::KittyKeyboard && !_kittyKeyboardEnabled && event.start >= start) {
            // The profile turned the kitty keyboard protocol off: the engine
            // never sees the application asking for it, and the application
            // gets no answer to its query, which is how "unsupported" looks.
            engineEnd = event.start;
        }
        const bool screenSwitch = event.type == ShittyStreamScanner::EventType::ScreenSwitch && event.start >= start;
        if (screenSwitch) {
            // Fed apart, below: the history is counted on one screen at a time.
            engineEnd = event.start;
        }
        if (event.type == ShittyStreamScanner::EventType::Osc && event.number == 8 && event.start >= start) {
            // Hyperlinks are Koncrepe's (see handleHyperlink()). The engine
            // would keep them in cell extras, which its embedding API reports
            // without the characters they are attached to.
            engineEnd = event.start;
        }

        feedEngine(bytes + start, engineEnd - start);
        start = end;

        switch (event.type) {
        case ShittyStreamScanner::EventType::None:
        case ShittyStreamScanner::EventType::KittyKeyboard:
            break;
        case ShittyStreamScanner::EventType::ScreenSwitch:
            // Copy out what scrolled off the primary screen before leaving
            // it, and re-base the count (see resetHistoryTracking()) as soon
            // as it is back, before anything else scrolls off uncounted.
            drainHistory();
            if (screenSwitch) {
                feedEngine(bytes + event.start, event.end - event.start);
            }
            drainHistory();
            break;
        case ShittyStreamScanner::EventType::Osc:
            handleOsc(event);
            break;
        case ShittyStreamScanner::EventType::EraseScrollback:
            // The engine forgot its scrollback; forget ours to match. Lines
            // that scrolled off before the erase go first, like they would.
            drainHistory();
            Emulation::clearHistory();
            resetHistoryTracking();
            break;
        case ShittyStreamScanner::EventType::FullReset:
            // RIS restores the engine's built-in palette and input defaults
            resetHistoryTracking();
            resetSemantics();
            _setupPending = true;
            Q_EMIT resetCursorStyleRequest();
            _cursorStyle = -1;
            break;
        case ShittyStreamScanner::EventType::SoftReset:
            _setupPending = true;
            break;
        }

        if (_setupPending && _scanner.atGround()) {
            pushEngineSetup();
        }
    }
    _screenDirty = true;
}

void ShittyEmulation::feedEngine(const char *bytes, int length)
{
    while (length > 0) {
        uint32_t room = EngineHistoryRoom - qMin(pendingHistoryRows(), EngineHistoryRoom);
        if (room < MinFeedSlice) {
            drainHistory();
            room = EngineHistoryRoom - qMin(pendingHistoryRows(), EngineHistoryRoom);
        }
        const int slice = int(qMin(uint32_t(length), qMax(room, MinFeedSlice)));
        shitty_vt_feed(_vt, reinterpret_cast<const uint8_t *>(bytes), size_t(slice));
        bytes += slice;
        length -= slice;
    }
    flushReplies();
    dispatchEngineEvents();
}

void ShittyEmulation::handleOsc(const ShittyStreamScanner::Event &event)
{
    const QByteArray &payload = event.payload;
    const bool query = payload == "?";
    TerminalDisplay *view = _currentScreen->currentTerminalDisplay();

    switch (event.number) {
    case 1: // icon name; the engine only reports the window title
    case 7: // current directory
    case 30: // session (tab) name
    case 32: // session icon
    case 34: // session (tab) color
    case 50: // profile change
        _pendingSessionAttributesUpdates[event.number] = QString::fromUtf8(payload);
        _sessionAttributesUpdateTimer->start(20);
        break;

    case 10: // default foreground/background: the view draws them
    case 11:
        if (!query) {
            const QColor color = parseColorSpec(payload.split(';').first());
            if (color.isValid()) {
                _pendingSessionAttributesUpdates[event.number] = color.name();
                _sessionAttributesUpdateTimer->start(20);
            }
        }
        break;

    case 110:
    case 111:
        _pendingSessionAttributesUpdates[event.number] = QString();
        _sessionAttributesUpdateTimer->start(20);
        _setupPending = true;
        break;

    case 12:
        if (!query && view != nullptr) {
            const QColor color = parseColorSpec(payload.split(';').first());
            if (color.isValid()) {
                view->terminalColor()->setCursorColor(color);
            }
        }
        break;

    case 112:
        if (view != nullptr) {
            view->terminalColor()->setCursorColor(QColor());
        }
        _setupPending = true;
        break;

    case 104: // palette reset: the engine falls back to its built-in one
        _setupPending = true;
        break;

    case 9: { // ConEmu: progress
        const QList<QByteArray> list = payload.split(';');
        if (list.size() >= 2 && list.at(0) == "4") {
            bool ok = false;
            const int state = list.at(1).toInt(&ok);
            if (!ok) {
                break;
            }
            if (state == 1 && list.size() >= 3) {
                const int progress = list.at(2).toInt(&ok);
                if (ok && progress >= 0 && progress <= 100) {
                    Q_EMIT progressChanged(progress);
                }
            } else if (state == 0 || state == 3) {
                Q_EMIT progressHidden();
            }
        }
        break;
    }

    case 777: {
        const QStringList params = QString::fromUtf8(payload).split(QLatin1Char(';'));
        Q_EMIT osc777Received(params);
        if (params.isEmpty() || params[0] != QLatin1String("notify") || params.length() < 2 || view == nullptr) {
            break;
        }
        const QString eventId = view->hasFocus() ? QStringLiteral("ProcessNotification") : QStringLiteral("ProcessNotificationHidden");
        KNotification *notification =
            params.length() >= 3 ? KNotification::event(eventId, params[1], params[2].toHtmlEscaped()) : KNotification::event(eventId, params[1]);
        auto *action = notification->addDefaultAction(i18n("Show session"));
        QPointer<TerminalDisplay> target = view;
        connect(action, &KNotificationAction::activated, this, [target, notification]() {
            if (target) {
                target->notificationClicked(notification->xdgActivationToken());
            }
        });
        break;
    }

    case 22: { // pointer shape
        if (view == nullptr) {
            break;
        }
        static const QHash<QByteArray, Qt::CursorShape> shapes = {
            {"default", Qt::ArrowCursor},
            {"left_ptr", Qt::ArrowCursor},
            {"help", Qt::WhatsThisCursor},
            {"pointer", Qt::PointingHandCursor},
            {"pointing_hand", Qt::PointingHandCursor},
            {"progress", Qt::BusyCursor},
            {"left_ptr_watch", Qt::BusyCursor},
            {"wait", Qt::WaitCursor},
            {"crosshair", Qt::CrossCursor},
            {"cross", Qt::CrossCursor},
            {"text", Qt::IBeamCursor},
            {"ibeam", Qt::IBeamCursor},
            {"vertical-text", Qt::IBeamCursor},
            {"alias", Qt::DragLinkCursor},
            {"copy", Qt::DragCopyCursor},
            {"move", Qt::DragMoveCursor},
            {"no-drop", Qt::ForbiddenCursor},
            {"not-allowed", Qt::ForbiddenCursor},
            {"forbidden", Qt::ForbiddenCursor},
            {"grab", Qt::OpenHandCursor},
            {"openhand", Qt::OpenHandCursor},
            {"grabbing", Qt::ClosedHandCursor},
            {"closedhand", Qt::ClosedHandCursor},
            {"col-resize", Qt::SplitHCursor},
            {"row-resize", Qt::SplitVCursor},
            {"ew-resize", Qt::SizeHorCursor},
            {"ns-resize", Qt::SizeVerCursor},
            {"nesw-resize", Qt::SizeBDiagCursor},
            {"nwse-resize", Qt::SizeFDiagCursor},
        };
        const auto it = shapes.constFind(payload);
        if (it == shapes.cend()) {
            view->resetCursor();
        } else {
            view->setPointerShape(it.value());
        }
        break;
    }

    case 8:
        handleHyperlink(payload);
        break;

    case 133:
        handleSemanticPrompt(payload);
        break;

    default:
        break;
    }
}

void ShittyEmulation::handleHyperlink(const QByteArray &payload)
{
    // Bring the model up to the point the link starts or ends at: the
    // extractor records positions from the screen's cursor and history.
    syncCursor();
    Screen *screen = _currentScreen;
    EscapeSequenceUrlExtractor *extractor = screen->urlExtractor();
    if (extractor == nullptr) {
        return; // the profile does not allow links in escape sequences
    }

    // '\e]8;<id>;<uri>\e\\' opens a link, '\e]8;;\e\\' closes it
    const int separator = payload.indexOf(';');
    const QByteArray uri = separator >= 0 ? payload.mid(separator + 1) : QByteArray();
    const int column = screen->getCursorX();
    const int line = screen->getCursorY();

    extractor->toggleUrlInput();
    if (extractor->reading()) {
        if (uri.isEmpty()) {
            extractor->abortUrlInput();
            return;
        }
        extractor->setUrl(QString::fromUtf8(uri));
        if (extractor->reading()) {
            // The link starts at the cursor. The extractor takes the start
            // from the first character printed, one column left of the
            // cursor then; the engine prints the text, so place it there.
            screen->setEngineCursor(column + 1, line);
            extractor->appendUrlText(U' ');
            screen->setEngineCursor(column, line);
        }
    }
}

void ShittyEmulation::handleSemanticPrompt(const QByteArray &payload)
{
    if (payload.isEmpty() || (shitty_vt_modes(_vt) & SHITTY_VT_MODE_ALT_SCREEN)) {
        return; // shell integration lives on the primary screen
    }
    syncCursor();
    Screen *screen = _screen[0];
    const int column = screen->getCursorX();
    const int line = screen->getCursorY();

    int mode = -1;
    switch (payload.at(0)) {
    case 'A':
    case 'N':
    case 'P':
        mode = REPL_PROMPT;
        break;
    case 'B':
        mode = REPL_INPUT;
        break;
    case 'C':
        mode = REPL_OUTPUT;
        break;
    case 'D':
        mode = REPL_None;
        break;
    case 'L':
        // fresh line: start the prompt on a line of its own
        if (column > 0) {
            static const char newLine[] = "\r\n";
            feedEngine(newLine, 2);
            _screenDirty = true;
        }
        return;
    default:
        return;
    }

    // Screen implements what the marks mean to the rest of Koncrepe:
    // prompt notifications, the command counter, exit code error bars
    // (which reach back into the history).
    screen->setReplMode(mode);
    if (payload.at(0) == 'D') {
        const QList<QByteArray> parameters = payload.split(';');
        if (parameters.size() > 1) {
            const int exitCode = parameters.at(1).toInt();
            if (exitCode != 0) {
                screen->setExitCode(exitCode);
            }
        }
    }

    // Keep the line flags it set for the lines still on screen, since the
    // next refresh from the engine rewrites them...
    const int history = screen->getHistLines();
    const QVector<LineProperty> properties = screen->getLineProperties(history, history + screen->getLines() - 1);
    constexpr quint16 semanticFlags = 0x1f0; // prompt/input/output starts, output, error
    for (int y = 0; y < properties.size(); ++y) {
        const qint64 absolute = _scrolledLines + y;
        const quint16 flags = properties[y].flags.all & semanticFlags;
        if (flags != 0 || properties[y].counter != 0) {
            _lineSemantics.insert(absolute, LineProperty(flags, 0, properties[y].counter));
        } else {
            _lineSemantics.remove(absolute);
        }
    }

    // ...and where the mode changed, for the characters printed from here.
    const SemanticMark mark{_scrolledLines + line, column, mode};
    while (!_semanticMarks.empty() && std::make_pair(_semanticMarks.back().line, _semanticMarks.back().column) >= std::make_pair(mark.line, mark.column)) {
        _semanticMarks.pop_back();
    }
    _semanticMarks.push_back(mark);
    _screenDirty = true;
}

LineProperty ShittyEmulation::lineSemantics(qint64 line) const
{
    return _lineSemantics.value(line);
}

void ShittyEmulation::resetSemantics()
{
    // Positions on screen are about to mean something else (a reset, a
    // reflow): keep the current mode, forget where it began.
    if (!_semanticMarks.empty()) {
        _semanticModeBefore = _semanticMarks.back().mode;
    }
    _semanticMarks.clear();
    _lineSemantics.clear();
}

void ShittyEmulation::setKittyKeyboardEnabled(bool enabled)
{
    _kittyKeyboardEnabled = enabled;
    if (enabled && !_heldBack.isEmpty()) {
        feed("", 0);
    }
}

void ShittyEmulation::flushReplies()
{
    uint8_t buffer[4096];
    QByteArray replies;
    for (;;) {
        const size_t count = shitty_vt_take_replies(_vt, buffer, sizeof(buffer));
        if (count == 0) {
            break;
        }
        replies.append(reinterpret_cast<const char *>(buffer), qsizetype(count));
    }
    if (!replies.isEmpty()) {
        Q_EMIT sendData(replies);
    }
}

// ---------------------------------------------------------------------------
// History and screen model

void ShittyEmulation::resetHistoryTracking()
{
    const uint32_t modes = shitty_vt_modes(_vt);
    if (modes & (SHITTY_VT_MODE_ALT_SCREEN | SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT)) {
        // The primary screen's scrollback cannot be read now: count afresh
        // once it can (see drainHistory()).
        _historyStale = true;
        return;
    }
    _historyStale = false;
    _knownHistoryRows = shitty_vt_history_rows(_vt);
    parkEngineView();
}

void ShittyEmulation::parkEngineView()
{
    if (_inputDepth > 0) {
        return;
    }
    _parkedOffset = shitty_vt_scroll_to(_vt, 1);
}

uint32_t ShittyEmulation::scrolledRows(uint32_t historyRows, uint32_t offset) const
{
    uint32_t scrolled = 0;
    if (_parkedOffset > 0 && offset >= _parkedOffset) {
        scrolled = offset - _parkedOffset;
    } else if (historyRows > _knownHistoryRows) {
        // The view was never parked (the engine had no history yet) or was
        // moved (resize, reset): the history is not full, so count rows.
        scrolled = historyRows - _knownHistoryRows;
    }
    return qMin(scrolled, historyRows);
}

uint32_t ShittyEmulation::pendingHistoryRows() const
{
    // Nothing reaches the primary screen's history while these hold (see
    // drainHistory()).
    if (shitty_vt_modes(_vt) & (SHITTY_VT_MODE_ALT_SCREEN | SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT)) {
        return 0;
    }
    // Back on the primary screen with a stale count: report it full, so
    // that it is re-based before more lines scroll off uncounted.
    if (_historyStale) {
        return EngineHistoryRoom;
    }
    return scrolledRows(shitty_vt_history_rows(_vt), shitty_vt_scroll_offset(_vt));
}

void ShittyEmulation::drainHistory()
{
    const uint32_t modes = shitty_vt_modes(_vt);
    // The alternate screen keeps no history, and during synchronized output
    // the engine holds its frames back; the rows wait for us either way.
    if (modes & (SHITTY_VT_MODE_ALT_SCREEN | SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT)) {
        return;
    }
    if (_historyStale) {
        resetHistoryTracking();
        return;
    }

    const uint32_t historyRows = shitty_vt_history_rows(_vt);
    const uint32_t offset = shitty_vt_scroll_offset(_vt);
    const uint32_t scrolled = scrolledRows(historyRows, offset);

    if (scrolled > 0) {
        Screen *primary = _screen[0];
        // A history that keeps the last N lines drops all but the last N of
        // these as they go in: those go in empty, without being read.
        uint32_t unread = 0;
        if (!primary->hasScroll()) {
            unread = scrolled;
        } else if (const int kept = primary->getScroll().maximumLineCount(); kept >= 0 && scrolled > uint32_t(kept) + 1) {
            // (a compact history can hold one line over its maximum)
            unread = scrolled - uint32_t(kept) - 1;
        }
        for (uint32_t k = 0; k < scrolled; ++k) {
            // the rows leave in order, each being the screen's top row then
            const uint32_t index = historyRows - scrolled + k;
            const qint64 absolute = _scrolledLines + k;
            LineProperty property = lineSemantics(absolute);
            if (k < unread) {
                primary->appendEngineHistoryLine(QVector<Character>(), property);
                continue;
            }
            int length = 0;
            QVector<Character> line = readLine(primary, index, &length, absolute);
            property.length = qint16(qMin(length, 0x7fff));
            property.flags.f.wrapped = shitty_vt_row_wrap_length(_vt, index) != 0;
            primary->appendEngineHistoryLine(std::move(line), property);
        }
        _scrolledLines += scrolled;

        // What left the screen is the history's now, marks included: keep
        // the mode in effect and forget the rest.
        _lineSemantics.removeIf([this](const auto &it) {
            return it.key() < _scrolledLines;
        });
        auto firstOnScreen = std::lower_bound(_semanticMarks.begin(), _semanticMarks.end(), _scrolledLines, [](const SemanticMark &mark, qint64 line) {
            return mark.line < line;
        });
        if (firstOnScreen != _semanticMarks.begin()) {
            _semanticModeBefore = std::prev(firstOnScreen)->mode;
            _semanticMarks.erase(_semanticMarks.begin(), firstOnScreen);
        }
        _screenDirty = true;
    }

    _knownHistoryRows = historyRows;
    if (scrolled > 0 || offset != _parkedOffset || _parkedOffset == 0) {
        parkEngineView();
    }
}

// Builds one screen line from the engine's cells, converted as the engine
// hands them out, left to right; the grapheme it points at is only valid
// during the callback, and nothing is copied twice.
class ShittyEmulation::LineBuilder
{
public:
    LineBuilder(const ShittyEmulation &emulation, Screen *screen)
        : _emulation(emulation)
        , _screen(screen)
        , _columns(emulation._columns)
        , _palette(emulation._haveTheme ? emulation._enginePalette : nullptr)
    {
    }

    /** Starts the line on absolute primary screen line @p absoluteLine, or -1. */
    void start(qint64 absoluteLine)
    {
        _line = QVector<Character>(_columns);
        _used = 0;
        _text = 0;
        _nextChange = 0;
        _replChanges.clear();
        _replMode = REPL_None;

        // Shell integration: the mode each column was printed in.
        const auto &marks = _emulation._semanticMarks;
        if (absoluteLine >= 0 && (!marks.empty() || _emulation._semanticModeBefore != REPL_None)) {
            auto it = std::lower_bound(marks.begin(), marks.end(), absoluteLine, [](const SemanticMark &mark, qint64 l) {
                return mark.line < l;
            });
            _replMode = it == marks.begin() ? _emulation._semanticModeBefore : std::prev(it)->mode;
            for (; it != marks.end() && it->line == absoluteLine; ++it) {
                _replChanges.append(*it);
            }
        }
    }

    void add(int column, const shitty_vt_cell &cell)
    {
        if (column >= _columns) {
            return;
        }
        while (_nextChange < _replChanges.size() && _replChanges[_nextChange].column <= column) {
            _replMode = _replChanges[_nextChange++].mode;
        }
        Character &ch = _line[column];

        // Most cells are blanks or plain ASCII in the default colors: take
        // them straight, with what the general path below would make of them.
        if (cell.attributes == 0 && cell.underline_style == 0 && cell.width == 1 && cell.grapheme_len <= 1
            && SHITTY_VT_COLOR_KIND(cell.foreground_source) == SHITTY_VT_COLOR_DEFAULT_FOREGROUND
            && SHITTY_VT_COLOR_KIND(cell.background_source) == SHITTY_VT_COLOR_DEFAULT_BACKGROUND && (cell.grapheme_len == 0 || cell.grapheme[0] < 0x80)) {
            static constexpr CharacterColor foreground(COLOR_SPACE_DEFAULT, DEFAULT_FORE_COLOR);
            static constexpr CharacterColor background(COLOR_SPACE_DEFAULT, DEFAULT_BACK_COLOR);
            if (cell.grapheme_len == 0) {
                ch = Character(' ', foreground, background, DEFAULT_RENDITION, setRepl(EF_UNREAL, _replMode));
            } else {
                const char32_t character = char32_t(cell.grapheme[0]);
                ch = Character(character, foreground, background, DEFAULT_RENDITION, setRepl(EF_REAL | asciiWordFlags(character), _replMode));
                _text = column + 1;
                _used = column + 1;
            }
            return;
        }

        CharacterColor foreground = convertColor(cell.foreground_source, cell.foreground, _palette);
        CharacterColor background = convertColor(cell.background_source, cell.background, _palette);
        const bool bold = cell.attributes & SHITTY_VT_ATTR_BOLD;
        const bool faint = cell.attributes & SHITTY_VT_ATTR_FAINT;
        RenditionFlags rendition = DEFAULT_RENDITION;
        if (cell.attributes != 0) {
            if (bold) {
                rendition |= RE_BOLD;
            }
            if (faint) {
                rendition |= RE_FAINT;
            }
            if (cell.attributes & SHITTY_VT_ATTR_ITALIC) {
                rendition |= RE_ITALIC;
            }
            if (cell.attributes & SHITTY_VT_ATTR_BLINK) {
                rendition |= RE_BLINK;
            }
            if (cell.attributes & SHITTY_VT_ATTR_CONCEAL) {
                rendition |= RE_CONCEAL;
            }
            if (cell.attributes & SHITTY_VT_ATTR_STRIKE) {
                rendition |= RE_STRIKEOUT;
            }
            if (cell.attributes & SHITTY_VT_ATTR_OVERLINE) {
                rendition |= RE_OVERLINE;
            }
        }
        if (cell.underline_style >= 1 && cell.underline_style <= 5) {
            rendition |= RenditionFlags(cell.underline_style) * RE_UNDERLINE_BIT;
        }
        // Mirror Screen::updateEffectiveRendition(): inverse swaps the
        // colors in the cell; bold brightens and faint dims the foreground.
        if (cell.attributes & SHITTY_VT_ATTR_INVERSE) {
            rendition |= RE_REVERSE;
            std::swap(foreground, background);
        }
        if (bold && !faint) {
            foreground.setIntensive();
        } else if (faint && !bold) {
            foreground.setFaint();
        }

        ExtraFlags flags = EF_REAL;
        char32_t character = ' ';
        const size_t graphemeLength = cell.grapheme_len;
        if (graphemeLength == 0) {
            // never written, or erased: Koncrepe's erased cells are not "real"
            flags = EF_UNREAL;
        } else if (graphemeLength == 1) {
            character = char32_t(cell.grapheme[0]);
            flags |= Screen::wordFlags(character);
            if (Character::emojiPresentation(character)) {
                flags |= EF_EMOJI_REPRESENTATION;
            }
        } else {
            const auto *grapheme = reinterpret_cast<const char32_t *>(cell.grapheme);
            character = _screen->engineExtendedChar(grapheme, ushort(qMin<size_t>(graphemeLength, 0xffff)));
            rendition |= RE_EXTENDED_CHAR;
            for (size_t i = 0; i < graphemeLength; ++i) {
                if (grapheme[i] == 0xfe0f || Character::emojiPresentation(grapheme[i])) {
                    flags |= EF_EMOJI_REPRESENTATION;
                    break;
                }
            }
        }

        if (SHITTY_VT_COLOR_KIND(cell.underline_source) != SHITTY_VT_COLOR_DEFAULT_FOREGROUND && (rendition & RE_UNDERLINE_MASK) != 0) {
            const int index = _screen->engineUnderlineColor(convertColor(cell.underline_source, cell.underline_color, _palette));
            flags = SetULColor(flags, index);
        }

        flags = setRepl(flags, _replMode);
        ch = Character(character, foreground, background, rendition, flags);
        if (graphemeLength > 0) {
            _text = column + cell.width;
        }

        if (cell.width == 2 && column + 1 < _columns) {
            Character &right = _line[column + 1];
            right = ch;
            right.setRightHalfOfDoubleWide();
            right.flags = setRepl(EF_UNREAL, _replMode);
        }

        const bool blank = graphemeLength == 0 && background == CharacterColor(COLOR_SPACE_DEFAULT, DEFAULT_BACK_COLOR)
            && (rendition & (RE_REVERSE | RE_UNDERLINE_MASK | RE_OVERLINE | RE_STRIKEOUT)) == 0;
        if (!blank) {
            _used = column + cell.width;
        }
    }

    /** The line, without its trailing untouched blanks; @p length receives its text length. */
    QVector<Character> finish(int *length)
    {
        _line.resize(qMin(_used, _columns));
        *length = _text;
        return std::move(_line);
    }

private:
    const ShittyEmulation &_emulation;
    Screen *const _screen;
    const int _columns;
    const uint32_t *const _palette;

    QVector<Character> _line;
    int _used = 0; // length of the line once trailing untouched blanks are dropped
    int _text = 0; // length of the text on it
    int _replMode = REPL_None;
    QVarLengthArray<SemanticMark, 4> _replChanges;
    int _nextChange = 0;
};

QVector<Character> ShittyEmulation::readLine(Screen *screen, uint32_t index, int *length, qint64 absoluteLine)
{
    LineBuilder builder(*this, screen);
    builder.start(absoluteLine);
    shitty_vt_row_cells(
        _vt,
        index,
        [](void *user, uint16_t, uint16_t column, const shitty_vt_cell *cell) {
            static_cast<LineBuilder *>(user)->add(column, *cell);
        },
        &builder);
    return builder.finish(length);
}

void ShittyEmulation::readView(Screen *screen, int lines, qint64 firstLine, QVector<QVector<Character>> &result, QVector<int> &lengths)
{
    // One frame for the whole grid, where reading row by row takes one each.
    // The engine walks it row-major, so one line is built at a time.
    struct Reader {
        LineBuilder builder;
        QVector<QVector<Character>> &result;
        QVector<int> &lengths;
        qint64 firstLine;
        int lines;
        int row = -1;

        void moveTo(int next)
        {
            while (row < next) {
                if (row >= 0) {
                    result[row] = builder.finish(&lengths[row]);
                }
                ++row;
                if (row < lines) {
                    builder.start(firstLine < 0 ? -1 : firstLine + row);
                }
            }
        }
    } reader{LineBuilder(*this, screen), result, lengths, firstLine, lines};

    result.resize(lines);
    lengths.fill(0, lines);
    reader.moveTo(0);
    shitty_vt_each_cell(
        _vt,
        [](void *user, uint16_t row, uint16_t column, const shitty_vt_cell *cell) {
            auto *reader = static_cast<Reader *>(user);
            if (row >= reader->lines) {
                return;
            }
            reader->moveTo(row);
            reader->builder.add(column, *cell);
        },
        &reader);
    reader.moveTo(lines);
}

void ShittyEmulation::syncModes()
{
    const uint32_t modes = shitty_vt_modes(_vt);
    const uint32_t previous = _modes;
    const bool known = _modesKnown;
    _modes = modes;
    _modesKnown = true;
    const auto changed = [&](uint32_t bit) {
        return !known || ((previous ^ modes) & bit) != 0;
    };

    if (changed(SHITTY_VT_MODE_ALT_SCREEN)) {
        setScreen((modes & SHITTY_VT_MODE_ALT_SCREEN) ? 1 : 0);
        if (!(modes & SHITTY_VT_MODE_ALT_SCREEN)) {
            // back on the primary screen: resume counting its scrollback
            drainHistory();
        }
    }
    if (changed(SHITTY_VT_MODE_MOUSE_CLICK)) {
        Q_EMIT programRequestsMouseTracking(modes & SHITTY_VT_MODE_MOUSE_CLICK);
    }
    if (changed(SHITTY_VT_MODE_BRACKETED_PASTE)) {
        Q_EMIT programBracketedPasteModeChanged(modes & SHITTY_VT_MODE_BRACKETED_PASTE);
    }
    // Only follow the application's changes, never the engine's default,
    // which would override the profile's "alternate scrolling" setting.
    if (known && changed(SHITTY_VT_MODE_ALTERNATE_SCROLL)) {
        Q_EMIT enableAlternateScrolling(modes & SHITTY_VT_MODE_ALTERNATE_SCROLL);
    }
    if (known && changed(SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT)) {
        Q_EMIT programRequestedSynchronizedUpdate(modes & SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT);
    }

    for (Screen *screen : _screen) {
        if (modes & SHITTY_VT_MODE_SCREEN_REVERSE) {
            screen->setMode(MODE_Screen);
        } else {
            screen->resetMode(MODE_Screen);
        }
    }
}

void ShittyEmulation::syncScreen()
{
    const uint32_t modes = shitty_vt_modes(_vt);
    if (modes & SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT) {
        // the application is mid-frame; show the last complete one
        syncModes();
        return;
    }
    drainHistory();
    syncModes();

    Screen *screen = _currentScreen;
    const bool alternate = modes & SHITTY_VT_MODE_ALT_SCREEN;
    const int lines = qMin(_lines, screen->getLines());

    // Read the live grid through the view, which is parked in the scrollback
    // between reads (see drainHistory()): bring it down for the read.
    if (!alternate && _parkedOffset != 0) {
        shitty_vt_scroll_to(_vt, 0);
    }
    readView(screen, lines, alternate ? -1 : _scrolledLines, _viewLines, _viewLengths);
    const uint32_t historyRows = alternate ? 0 : shitty_vt_history_rows(_vt);
    const uint32_t offset = 0;

    for (int y = 0; y < lines; ++y) {
        const uint32_t index = historyRows + uint32_t(y);
        const qint64 absolute = alternate ? -1 : _scrolledLines + y;
        QVector<Character> line = std::move(_viewLines[y]);
        LineProperty property = alternate ? LineProperty() : lineSemantics(absolute);
        property.length = qint16(qMin(_viewLengths[y], 0x7fff));
        property.flags.f.wrapped = shitty_vt_row_wrap_length(_vt, index) != 0;
        screen->setEngineLine(y, std::move(line), property);
    }

    const shitty_vt_cursor cursor = shitty_vt_cursor_state(_vt);
    const int cursorLine = int(cursor.row) - int(offset);
    screen->setEngineCursor(qMin(int(cursor.column), _columns - 1), cursorLine);
    // A hidden cursor style also covers an input method preview, which
    // positions the cursor for its candidate window without drawing it.
    const bool cursorVisible = (modes & SHITTY_VT_MODE_CURSOR_VISIBLE) && cursorLine >= 0 && cursorLine < _lines;
    for (Screen *s : _screen) {
        if (cursorVisible) {
            s->setMode(MODE_Cursor);
        } else {
            s->resetMode(MODE_Cursor);
        }
    }

    // DECSCUSR. The engine reports its default shape until the application
    // asks for one, and that default must not override the profile's; a
    // hollow block only means the engine thinks it lost focus.
    int style = cursor.style;
    if (style == 2 || style == 0) {
        style = _cursorStyle;
    }
    if (style != _cursorStyle) {
        if (_cursorStyle != -1 || style != 1) {
            switch (style) {
            case 1:
                Q_EMIT setCursorStyleRequest(Enum::BlockCursor, false);
                break;
            case 3:
                Q_EMIT setCursorStyleRequest(Enum::UnderlineCursor, false);
                break;
            case 4:
                Q_EMIT setCursorStyleRequest(Enum::IBeamCursor, false);
                break;
            default:
                break;
            }
        }
        _cursorStyle = style;
    }

    if (!alternate && _parkedOffset != 0) {
        _knownHistoryRows = historyRows;
        parkEngineView();
    }
    _screenDirty = false;
}

void ShittyEmulation::syncCursor()
{
    const uint32_t modes = shitty_vt_modes(_vt);
    if (modes & SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT) {
        return;
    }
    drainHistory();
    const bool alternate = modes & SHITTY_VT_MODE_ALT_SCREEN;
    if (alternate != (_currentScreen == _screen[1])) {
        syncModes();
    }
    const uint32_t offset = alternate ? 0 : shitty_vt_scroll_offset(_vt);
    const shitty_vt_cursor cursor = shitty_vt_cursor_state(_vt);
    _currentScreen->setEngineCursor(qMin(int(cursor.column), _columns - 1), int(cursor.row) - int(offset));
}

void ShittyEmulation::aboutToShowOutput()
{
    if (_screenDirty) {
        syncScreen();
    }
}

// ---------------------------------------------------------------------------
// Geometry, reset, history

void ShittyEmulation::setImageSize(int lines, int columns)
{
    if (lines < 1 || columns < 1) {
        return;
    }
    const bool changed = lines != _lines || columns != _columns;
    if (changed) {
        // whatever scrolled off at the old size belongs to the history first
        drainHistory();
    }

    // Koncrepe's own model resizes by its own rules (history reflow, lines
    // moved between screen and history); the engine then does the same to
    // its state and the screen model is refreshed from it.
    Emulation::setImageSize(lines, columns);

    if (changed) {
        _lines = lines;
        _columns = columns;
        shitty_vt_resize(_vt, uint16_t(qMin(columns, 0xffff)), uint16_t(qMin(lines, 0xffff)));
        flushReplies();
        dispatchEngineEvents();
        // The engine reflowed its own scrollback too; that reshuffle is not
        // output, so start counting afresh.
        resetHistoryTracking();
        resetSemantics();
        _screenDirty = true;
        syncScreen();
    }
}

void ShittyEmulation::clearEntireScreen()
{
    // Like Screen::clearEntireScreen(): the screen goes to the history first.
    drainHistory();
    Screen *screen = _currentScreen;
    const bool primary = screen == _screen[0];
    if (primary) {
        for (int y = 0; y < _lines; ++y) {
            int length = 0;
            QVector<Character> line = readLine(screen, shitty_vt_history_rows(_vt) + uint32_t(y), &length);
            LineProperty property;
            property.length = qint16(qMin(length, 0x7fff));
            screen->appendEngineHistoryLine(std::move(line), property);
        }
    }
    // CAN first: an application's unfinished sequence must not swallow ours
    static const char clear[] = "\x18\033[H\033[2J";
    feedEngine(clear, int(sizeof(clear) - 1));
    resetHistoryTracking();
    _screenDirty = true;
    bufferedUpdate();
}

void ShittyEmulation::reset(bool softReset, bool preservePrompt)
{
    Q_EMIT updateDroppedLines(_currentScreen->getLines());

    const QByteArray currentCodec(encoder().name());

    drainHistory();
    // CAN first: an application's unfinished sequence must not swallow ours
    if (softReset) {
        static const char decstr[] = "\x18\033[!p";
        feedEngine(decstr, int(sizeof(decstr) - 1));
        _setupPending = true;
    } else if (preservePrompt) {
        // Keep the line the cursor is on at the top of a cleared screen.
        const shitty_vt_cursor cursor = shitty_vt_cursor_state(_vt);
        const int line = int(cursor.row) - int(shitty_vt_scroll_offset(_vt));
        QByteArray sequence("\x18\033[!p\033[r");
        if (line > 0) {
            // delete the lines above the prompt so it moves to the top
            sequence += "\033[H\033[" + QByteArray::number(line) + "M";
        }
        sequence += "\033[2;1H\033[J\033[1;" + QByteArray::number(int(cursor.column) + 1) + "H";
        feedEngine(sequence.constData(), sequence.size());
        _setupPending = true;
    } else {
        static const char ris[] = "\x18\033c";
        feedEngine(ris, int(sizeof(ris) - 1));
        _setupPending = true;
    }
    _scanner.reset();
    _heldBack.clear();
    if (_setupPending) {
        pushEngineSetup();
    }
    resetHistoryTracking();
    resetSemantics();

    _screen[0]->reset(softReset, preservePrompt);
    _screen[1]->reset(softReset, preservePrompt);

    if (!currentCodec.isEmpty()) {
        setCodec(currentCodec);
    } else {
        setCodec(QByteArrayLiteral("UTF-8"));
    }

    _cursorStyle = -1;
    Q_EMIT resetCursorStyleRequest();

    _screenDirty = true;
    bufferedUpdate();
}

void ShittyEmulation::clearHistory()
{
    drainHistory();
    Emulation::clearHistory();
}

char ShittyEmulation::eraseChar() const
{
    // The pty's erase character follows the keytab, as it always has.
    if (_keyTranslator != nullptr) {
        const KeyboardTranslator::Entry entry = _keyTranslator->findEntry(Qt::Key_Backspace, Qt::NoModifier, KeyboardTranslator::NoState);
        if (!entry.text().isEmpty()) {
            return entry.text().at(0);
        }
    }
    return '\x7f';
}

void ShittyEmulation::setMode(int)
{
    // modes are the engine's business
}

void ShittyEmulation::resetMode(int)
{
}

// ---------------------------------------------------------------------------
// Theme

void ShittyEmulation::setEngineColors(const QColor *table, const QColor &cursor)
{
    _theme[0] = table[DEFAULT_FORE_COLOR];
    _theme[1] = table[DEFAULT_BACK_COLOR];
    for (int i = 0; i < 8; ++i) {
        _theme[2 + i] = table[2 + i];
        _theme[2 + 8 + i] = table[BASE_COLORS + 2 + i];
    }
    _themeCursor = cursor.isValid() ? cursor : table[DEFAULT_FORE_COLOR];
    _haveTheme = true;
    _setupPending = true;
    if (_scanner.atGround()) {
        pushEngineSetup();
        _screenDirty = true;
        bufferedUpdate();
    }
}

void ShittyEmulation::pushEngineSetup()
{
    _setupPending = false;
    // metaSendsEscape, altSendsEscape
    QByteArray sequence("\033[?1036h\033[?1039h");
    if (!_haveTheme) {
        shitty_vt_feed(_vt, reinterpret_cast<const uint8_t *>(sequence.constData()), size_t(sequence.size()));
        return;
    }
    for (int i = 0; i < 16; ++i) {
        const QColor &color = _theme[2 + i];
        sequence += "\033]4;" + QByteArray::number(i) + ';' + colorSpec(color) + "\033\\";
        _enginePalette[i] = packColor(color);
    }
    sequence += "\033]10;" + colorSpec(_theme[0]) + "\033\\";
    sequence += "\033]11;" + colorSpec(_theme[1]) + "\033\\";
    sequence += "\033]12;" + colorSpec(_themeCursor) + "\033\\";
    // Straight to the engine: none of this is the application speaking,
    // so the side channel must not see it.
    shitty_vt_feed(_vt, reinterpret_cast<const uint8_t *>(sequence.constData()), size_t(sequence.size()));
    flushReplies();
    dispatchEngineEvents();
}

// ---------------------------------------------------------------------------
// Input

void ShittyEmulation::beginEngineInput()
{
    // Output may have scrolled lines off that were not drained yet, and the
    // engine is about to snap its view to the bottom: count them now.
    drainHistory();
    _inputDepth++;
    if (_parkedOffset != 0) {
        shitty_vt_scroll_to(_vt, 0);
    }
}

void ShittyEmulation::endEngineInput()
{
    _inputDepth--;
    flushReplies();
    dispatchEngineEvents();
    // Input never scrolls: put the view back where the count continues from.
    const bool primary = !(shitty_vt_modes(_vt) & (SHITTY_VT_MODE_ALT_SCREEN | SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT));
    if (_inputDepth == 0 && primary && !_historyStale) {
        _knownHistoryRows = shitty_vt_history_rows(_vt);
        parkEngineView();
    }
    // Input can change what the engine shows (an input method preview,
    // a selection); refresh the model on the next repaint.
    _screenDirty = true;
}

void ShittyEmulation::sendString(const QByteArray &string)
{
    Q_EMIT sendData(string);
}

void ShittyEmulation::sendText(const QString &text)
{
    if (!text.isEmpty()) {
        QKeyEvent event(QEvent::KeyPress, 0, Qt::NoModifier, text);
        sendKeyEvent(&event);
    }
}

KeyboardTranslator::States ShittyEmulation::keyboardStates(Qt::KeyboardModifiers modifiers) const
{
    const uint32_t modes = shitty_vt_modes(_vt);
    KeyboardTranslator::States states = KeyboardTranslator::AnsiState;
    if (modes & SHITTY_VT_MODE_APP_CURSOR_KEYS) {
        states |= KeyboardTranslator::CursorKeysState;
    }
    if (modes & SHITTY_VT_MODE_ALT_SCREEN) {
        states |= KeyboardTranslator::AlternateScreenState;
    }
    if ((modes & SHITTY_VT_MODE_APP_KEYPAD) && (modifiers & Qt::KeypadModifier)) {
        states |= KeyboardTranslator::ApplicationKeypadState;
    }
    return states;
}

void ShittyEmulation::emulateUpDown(int up, const KeyboardTranslator::Entry &entry, QByteArray &textToSend, int toColumn)
{
    // Moves the shell's cursor up or down through a multi-line input with
    // Left/Right, as far as the lines' lengths say.
    if (up == 0) {
        return;
    }
    Screen *screen = _currentScreen;
    const int cursorX = screen->getCursorX();
    const int cursorY = screen->getCursorY();
    const int history = screen->getHistLines();
    int firstLine;
    int lastLine;
    int targetLine;
    if (up > 0) {
        firstLine = cursorY - up + history;
        lastLine = cursorY - 1 + history;
        targetLine = 0;
    } else {
        firstLine = cursorY + history;
        lastLine = cursorY - up + history;
        targetLine = lastLine - firstLine;
    }
    if (firstLine < 0 || lastLine >= screen->getLines() + history) {
        return;
    }

    QVector<LineProperty> lineProperties = screen->getLineProperties(firstLine, lastLine);
    const int targetColumn = qMin(toColumn == -1 ? cursorX : toColumn, lineProperties[targetLine].length - 1);
    if (up < 0) {
        lineProperties.removeLast();
    }
    int count = up > 0 ? cursorX - targetColumn : targetColumn - cursorX;
    for (const LineProperty &property : std::as_const(lineProperties)) {
        count += property.length + 1 - property.flags.f.wrapped;
    }
    for (int i = 0; i < count; i++) {
        textToSend += entry.text();
    }
}

bool ShittyEmulation::handleSemanticUpDown(QKeyEvent *event)
{
    if (event->type() != QEvent::KeyPress) {
        return false;
    }
    const bool up = event->key() == Qt::Key_Up;
    TerminalDisplay *view = _currentScreen->currentTerminalDisplay();
    if ((!up && event->key() != Qt::Key_Down) || (event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier || view == nullptr || _keyTranslator == nullptr) {
        // any other key ends a run of vertical moves
        _targetColumn = -1;
        return false;
    }
    syncCursor();
    Screen *screen = _currentScreen;
    const int cursorX = screen->getCursorX();
    const int cursorY = screen->getCursorY();
    if (screen->replMode() != REPL_INPUT || !view->semanticUpDown()
        || !((up && screen->replModeStart() <= std::make_pair(cursorY - 1, cursorX))
             || (!up && std::make_pair(cursorY + 1, cursorX) <= screen->replModeEnd()))) {
        _targetColumn = -1;
        return false;
    }
    const KeyboardTranslator::Entry entry = _keyTranslator->findEntry(up ? Qt::Key_Left : Qt::Key_Right, Qt::NoModifier, keyboardStates(Qt::NoModifier));
    if (_targetColumn == -1) {
        _targetColumn = cursorX;
    }
    QByteArray textToSend;
    emulateUpDown(up ? 1 : -1, entry, textToSend, _targetColumn);
    Q_EMIT sendData(textToSend);
    return true;
}

void ShittyEmulation::moveCursorInInput(int column, int line)
{
    // A click in a shell's input line moves its cursor there, by sending the
    // Left/Right keys it would take.
    TerminalDisplay *view = _currentScreen->currentTerminalDisplay();
    if (_keyTranslator == nullptr || (view != nullptr && view->getReadOnly())) {
        return;
    }
    syncCursor();
    Screen *screen = _currentScreen;
    const auto point = std::make_pair(line, column);
    if (!(screen->replModeStart() <= point && point <= screen->replModeEnd())) {
        return;
    }
    const KeyboardTranslator::States states = keyboardStates(Qt::NoModifier);
    const KeyboardTranslator::Entry keys[2] = {_keyTranslator->findEntry(Qt::Key_Left, Qt::NoModifier, states),
                                               _keyTranslator->findEntry(Qt::Key_Right, Qt::NoModifier, states)};
    const int history = screen->getHistLines();
    const QVector<LineProperty> lineProperties = screen->getLineProperties(line + history, line + history);
    column = qMin(column, int(lineProperties[0].length));
    const int cursorX = screen->getCursorX();
    const int cursorY = screen->getCursorY();
    QByteArray textToSend;
    if (cursorY != line) {
        emulateUpDown(cursorY - line, keys[line > cursorY], textToSend, column);
    } else {
        const KeyboardTranslator::Entry &entry = keys[cursorX < column];
        for (int i = 0; i < qAbs(column - cursorX); i++) {
            textToSend += entry.text();
        }
    }
    Q_EMIT sendData(textToSend);
}

bool ShittyEmulation::handleKeyBinding(QKeyEvent *event)
{
    if (_keyTranslator == nullptr || event->type() != QEvent::KeyPress) {
        return false;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const KeyboardTranslator::States states = keyboardStates(modifiers);
    const KeyboardTranslator::Entry entry = _keyTranslator->findEntry(event->key(), modifiers, states);
    if (entry.isNull()) {
        return false;
    }

    TerminalDisplay *currentView = _currentScreen->currentTerminalDisplay();
    if (entry.command() != KeyboardTranslator::NoCommand) {
        if (entry.command() & KeyboardTranslator::EraseCommand) {
            Q_EMIT sendData(QByteArray(1, eraseChar()));
        }
        if (currentView == nullptr) {
            return true;
        }
        const KeyboardTranslator::Command command = entry.command();
        if (command & KeyboardTranslator::ScrollPageUpCommand) {
            currentView->scrollScreenWindow(ScreenWindow::ScrollPages, -1);
        } else if (command & KeyboardTranslator::ScrollPageDownCommand) {
            currentView->scrollScreenWindow(ScreenWindow::ScrollPages, 1);
        } else if (command & KeyboardTranslator::ScrollLineUpCommand) {
            currentView->scrollScreenWindow(ScreenWindow::ScrollLines, -1);
        } else if (command & KeyboardTranslator::ScrollLineDownCommand) {
            currentView->scrollScreenWindow(ScreenWindow::ScrollLines, 1);
        } else if (command & KeyboardTranslator::ScrollUpToTopCommand) {
            currentView->scrollScreenWindow(ScreenWindow::ScrollLines, -currentView->screenWindow()->currentLine());
        } else if (command & KeyboardTranslator::ScrollDownToBottomCommand) {
            currentView->scrollScreenWindow(ScreenWindow::ScrollLines, lineCount());
        } else if (command & KeyboardTranslator::ScrollPromptUpCommand) {
            currentView->scrollScreenWindow(ScreenWindow::ScrollPrompts, -1);
        } else if (command & KeyboardTranslator::ScrollPromptDownCommand) {
            currentView->scrollScreenWindow(ScreenWindow::ScrollPrompts, 1);
        }
        return true;
    }

    if (entry.text().isEmpty()) {
        return false;
    }
    // A stock binding: leave it to the engine (see stockTranslator()).
    const KeyboardTranslator *stock = stockTranslator();
    if (stock != nullptr) {
        const KeyboardTranslator::Entry stockEntry = stock->findEntry(event->key(), modifiers, states);
        if (stockEntry == entry) {
            return false;
        }
    }

    // The keytab's own binding, sent the way the keytab engine always did.
    QByteArray textToSend;
    const bool wantsAltModifier = entry.modifiers() & entry.modifierMask() & Qt::AltModifier;
    const bool wantsMetaModifier = entry.modifiers() & entry.modifierMask() & Qt::MetaModifier;
    const bool wantsAnyModifier = entry.state() & entry.stateMask() & KeyboardTranslator::AnyModifierState;
    if ((modifiers & Qt::AltModifier) && !(wantsAltModifier || wantsAnyModifier) && !event->text().isEmpty()) {
        textToSend.prepend("\033");
    }
    if ((modifiers & Qt::MetaModifier) && !(wantsMetaModifier || wantsAnyModifier) && !event->text().isEmpty()) {
        textToSend.prepend("\030@s");
    }
    textToSend += entry.text(true, modifiers);
    Q_EMIT sendData(textToSend);
    return true;
}

bool ShittyEmulation::sendKeyToEngine(QKeyEvent *event)
{
    const bool release = event->type() == QEvent::KeyRelease;
    if (release && event->isAutoRepeat()) {
        // X11 reports each repeat as a release/press pair
        return true;
    }

    bool numLock = false;
    shitty_vt_key_event key{};
    key.key = namedKey(event, &numLock);
    key.action = release ? SHITTY_VT_KEY_RELEASE : (event->isAutoRepeat() ? SHITTY_VT_KEY_REPEAT : SHITTY_VT_KEY_PRESS);
    key.modifiers = engineModifiers(event->modifiers()) | (numLock ? SHITTY_VT_MOD_NUM_LOCK : 0);

    const uint32_t base = baseCodepoint(event->nativeScanCode());
    if (key.key == SHITTY_VT_KEY_UNKNOWN) {
        const int qtKey = event->key();
        if (qtKey <= 0 || qtKey >= 0x01000000) {
            return false; // no identity the engine knows (dead keys, ...)
        }
        key.key = SHITTY_VT_KEY_PRINTABLE;
        // Qt reports letters upper case and shifted symbols as the symbol:
        // the unshifted identity is the lower case letter, or the base
        // layout's key for a shifted symbol.
        uint32_t layout = QChar::toLower(char32_t(qtKey));
        if ((event->modifiers() & Qt::ShiftModifier) && !QChar::isLetter(char32_t(qtKey)) && base >= 0x20 && base < 0x7f) {
            layout = base;
        }
        key.layout_codepoint = layout;
        key.base_codepoint = base != 0 ? base : (layout < 0x80 ? layout : 0);
        if (event->modifiers() & Qt::ShiftModifier) {
            const QList<uint> text = event->text().toUcs4();
            key.shifted_codepoint = (text.size() == 1 && text[0] >= 0x20 && text[0] != 0x7f) ? text[0] : 0;
        }
    }

    shitty_vt_key(_vt, &key);
    if (!release && !(key.modifiers & (SHITTY_VT_MOD_CONTROL | SHITTY_VT_MOD_SUPER))) {
        const QList<uint> text = event->text().toUcs4();
        for (uint codepoint : text) {
            if (codepoint >= 0x20 && codepoint != 0x7f) {
                shitty_vt_text(_vt, codepoint, key.modifiers);
            }
        }
    }
    shitty_vt_input_flush(_vt);
    return true;
}

void ShittyEmulation::sendKeyEvent(QKeyEvent *event)
{
    TerminalDisplay *currentView = _currentScreen->currentTerminalDisplay();
    const bool isReadOnly = currentView != nullptr && currentView->getReadOnly();
    if (isReadOnly) {
        return;
    }

    const bool press = event->type() == QEvent::KeyPress;
    if (press && (event->modifiers() & Qt::ControlModifier)) {
        switch (event->key()) {
        case Qt::Key_S:
            Q_EMIT flowControlKeyPressed(true);
            break;
        case Qt::Key_C:
        case Qt::Key_Q:
            Q_EMIT flowControlKeyPressed(false);
            break;
        }
    }

    // Text without a key: a paste (already bracketed by the view when the
    // application asked for it) or text sent on the user's behalf.
    if (event->key() == 0) {
        if (press && !event->text().isEmpty()) {
            Q_EMIT sendData(_encoder.encode(event->text()));
        }
        return;
    }

    if (handleSemanticUpDown(event) || handleKeyBinding(event)) {
        return;
    }

    // The engine encodes text as UTF-8; a session in another encoding gets
    // its typed text encoded the way the profile says.
    if (press && !engineIsUtf8() && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
        const QString text = event->text();
        bool plain = !text.isEmpty();
        for (const QChar c : text) {
            plain = plain && c.unicode() >= 0x20 && c.unicode() != 0x7f;
        }
        if (plain) {
            QByteArray bytes = _encoder.encode(text);
            if (event->modifiers() & Qt::AltModifier) {
                bytes.prepend('\033');
            }
            Q_EMIT sendData(bytes);
            return;
        }
    }

    beginEngineInput();
    sendKeyToEngine(event);
    endEngineInput();
}

void ShittyEmulation::sendMouseEvent(int cb, int cx, int cy, int eventType)
{
    if (cx < 1 || cy < 1) {
        return;
    }
    // a click to move the cursor in a shell's input line (shell integration)
    if (eventType == 3) {
        moveCursorInInput(cx - 1, cy - 1);
        return;
    }
    // Only an application that asked for the mouse gets it: the engine
    // would otherwise start a selection of its own.
    if (!(shitty_vt_modes(_vt) & SHITTY_VT_MODE_MOUSE_CLICK)) {
        return;
    }

    uint16_t modifiers = 0;
    if (cb & 4) {
        modifiers |= SHITTY_VT_MOD_SHIFT;
    }
    if (cb & 8) {
        modifiers |= SHITTY_VT_MOD_ALT;
    }
    if (cb & 16) {
        modifiers |= SHITTY_VT_MOD_CONTROL;
    }
    const int button = cb & ~(4 | 8 | 16);
    const int column = cx - 1;
    const int row = cy - 1;

    beginEngineInput();
    _inPointerEvent = true;
    if (button >= 64) {
        // wheel: 64 up, 65 down
        shitty_vt_mouse_scroll(_vt, 0.0, button == 64 ? 1.0 : -1.0, column, row, modifiers);
    } else if (eventType == 1) {
        shitty_vt_mouse_motion(_vt, column, row, modifiers);
    } else if ((button & 3) != 3) {
        // Koncrepe numbers buttons left, middle, right; the engine left, right, middle
        static const int engineButton[] = {SHITTY_VT_MOUSE_LEFT, SHITTY_VT_MOUSE_MIDDLE, SHITTY_VT_MOUSE_RIGHT};
        const double time = double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
        shitty_vt_mouse_button(_vt, engineButton[button & 3], eventType == 0 ? 1 : 0, column, row, modifiers, time);
    }
    _inPointerEvent = false;
    endEngineInput();
}

void ShittyEmulation::sendExactMouseEvent(int, int, int, int)
{
    // The engine reports pointer positions itself, from the cell events.
}

void ShittyEmulation::focusChanged(bool focused)
{
    beginEngineInput();
    shitty_vt_focus(_vt, focused ? 1 : 0);
    endEngineInput();
}

#include "moc_ShittyEmulation.cpp"
