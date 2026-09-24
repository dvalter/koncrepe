/*
    SPDX-FileCopyrightText: 2026 Koncrepe Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SHITTYEMULATION_H
#define SHITTYEMULATION_H

// Qt
#include <QColor>
#include <QHash>
#include <QTimer>

// Koncrepe
#include "Emulation.h"
#include "ShittyStreamScanner.h"
#include "characters/Character.h"

struct shitty_vt;
struct shitty_vt_callbacks;

namespace Koncrepe
{
/**
 * A terminal emulation back-end driven by the shitty terminal engine.
 *
 * Everything a terminal emulator decides lives in shitty: parsing the
 * byte stream, the screen and cursor state, modes, replies to queries,
 * and the encoding of keyboard, mouse and focus input according to what
 * the application negotiated (cursor/keypad modes, modifyOtherKeys, the
 * kitty keyboard protocol, every mouse protocol, focus events).
 *
 * Koncrepe keeps what a terminal *application* decides: rendering,
 * selection, search, scrollback storage and its profile-driven policies.
 * The screens inherited from Emulation are therefore models of shitty's
 * state, refreshed from the engine whenever the views are about to repaint:
 *
 *  - The visible grid is copied cell by cell. Colors keep the request the
 *    application made (default, palette entry, direct RGB), so Koncrepe's
 *    color scheme renders them exactly as it would have its own.
 *  - Lines scrolled off the top of the primary screen are copied into
 *    Koncrepe's history as they leave, so every history type the profile
 *    offers (none, fixed, unlimited, file backed) keeps working.
 *  - The profile's color scheme is pushed into the engine, so color
 *    queries (OSC 4/10/11/12), light/dark reports and palette-relative
 *    requests answer with the theme the user picked.
 */
class KONCREPEPRIVATE_EXPORT ShittyEmulation : public Emulation
{
    Q_OBJECT

public:
    ShittyEmulation();
    ~ShittyEmulation() override;

    void clearEntireScreen() override;
    void reset(bool softReset = false, bool preservePrompt = false) override;
    char eraseChar() const override;
    void clearHistory() override;

    /**
     * Sets the colors the engine resolves default and palette colors to:
     * @p table is a color scheme table of TABLE_COLORS entries, @p cursor
     * the cursor color (invalid for the foreground color).
     */
    void setEngineColors(const QColor *table, const QColor &cursor);

    /** One engine cell, copied out of the engine's callback. */
    struct EngineCell;

public Q_SLOTS:
    void setImageSize(int lines, int columns) override;
    void sendString(const QByteArray &string) override;
    void sendText(const QString &text) override;
    void sendKeyEvent(QKeyEvent *event) override;
    void sendMouseEvent(int buttons, int column, int line, int eventType) override;
    void sendExactMouseEvent(int buttons, int x, int y, int eventType) override;
    void focusChanged(bool focused) override;
    void receiveData(const char *text, int length) override;

protected:
    void setMode(int mode) override;
    void resetMode(int mode) override;
    void aboutToShowOutput() override;

private Q_SLOTS:
    void updateSessionAttributes();

private:
    // engine callbacks, collected while the engine runs and acted on after
    static void onTitleChanged(void *user, const uint8_t *title, size_t len);
    static void onBell(void *user);
    static void onClipboardSet(void *user, int clipboard, const uint8_t *bytes, size_t len);
    static void onResizeRequest(void *user, uint16_t columns, uint16_t rows);
    void dispatchEngineEvents();

    /** Feeds UTF-8 @p bytes to the engine, acting on side-channel sequences. */
    void feed(const char *bytes, int length);
    void feedEngine(const char *bytes, int length);
    void handleOsc(const ShittyStreamScanner::Event &event);

    /** Sends whatever the engine produced toward the child. */
    void flushReplies();

    /**
     * Copies lines the engine scrolled off the primary screen into the
     * history. The engine's view is kept parked one row up in its own
     * scrollback: shitty keeps a scrolled-back view anchored to its content,
     * so the parked offset grows by exactly the number of lines that
     * scrolled off, even once the engine's own history is full.
     */
    void drainHistory();
    void parkEngineView();
    void resetHistoryTracking();
    /** Refreshes the current screen model from the engine. */
    void syncScreen();
    void syncModes();

    /** Brackets engine input: the engine snaps its view to the bottom on input. */
    void beginEngineInput();
    void endEngineInput();

    /** Reads row @p index (oldest history row first) of the engine's current screen. */
    QVector<EngineCell> readRow(uint32_t index);
    /** Converts an engine row to a screen line; @p length receives its text length. */
    QVector<Character> convertRow(Screen *screen, const QVector<EngineCell> &cells, int *length) const;
    CharacterColor convertColor(uint16_t source, uint32_t resolved) const;

    /**
     * Brings the engine to Koncrepe's defaults: Alt sends ESC like it always
     * did in Koncrepe, and the profile's colors. Resets undo both.
     */
    void pushEngineSetup();
    bool engineIsUtf8() const;
    bool sendKeyToEngine(QKeyEvent *event);
    bool handleKeyBinding(QKeyEvent *event);

    struct shitty_vt *_vt = nullptr;
    // the engine keeps a pointer to its callbacks, not a copy
    shitty_vt_callbacks *_callbacks = nullptr;
    ShittyStreamScanner _scanner;

    int _columns = 80;
    int _lines = 40;

    // history tracking (see drainHistory)
    uint32_t _parkedOffset = 0;
    uint32_t _knownHistoryRows = 0;
    int _inputDepth = 0;

    uint32_t _modes = 0;
    bool _modesKnown = false;
    int _cursorStyle = -1;
    bool _screenDirty = true;

    // engine setup and theme
    QColor _theme[2 + 16];
    QColor _themeCursor;
    bool _haveTheme = false;
    bool _setupPending = true;
    uint32_t _enginePalette[256];

    // callback output, dispatched once the engine returns
    QByteArray _pendingTitle;
    bool _titlePending = false;
    int _pendingBells = 0;
    QList<QPair<int, QByteArray>> _pendingClipboard;
    bool _resizeRequested = false;
    bool _inPointerEvent = false;

    QHash<int, QString> _pendingSessionAttributesUpdates;
    QTimer *_sessionAttributesUpdateTimer = nullptr;
};

}

#endif
