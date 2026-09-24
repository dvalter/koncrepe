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
#include "keyboardtranslator/KeyboardTranslator.h"

#include <vector>

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

    /**
     * Whether applications may use the kitty keyboard protocol. The engine
     * always implements it; when disabled, the requests to enable or query
     * it never reach the engine, so applications see a terminal without it.
     */
    void setKittyKeyboardEnabled(bool enabled);

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
    /** OSC 8: feeds the screen's escape sequence URL extractor. */
    void handleHyperlink(const QByteArray &payload);
    /** OSC 133: shell integration marks (prompt, input, output, exit code). */
    void handleSemanticPrompt(const QByteArray &payload);
    void resetSemantics();
    /** The shell integration line flags recorded for engine line @p line. */
    LineProperty lineSemantics(qint64 line) const;

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
    /** Drains the history and places the model's cursor where the engine's is. */
    void syncCursor();
    void syncModes();

    /** Brackets engine input: the engine snaps its view to the bottom on input. */
    void beginEngineInput();
    void endEngineInput();

    /** Reads row @p index (oldest history row first) of the engine's current screen. */
    QVector<EngineCell> readRow(uint32_t index);
    /** Reads the first @p lines rows of the engine's view. */
    QVector<QVector<EngineCell>> readView(int lines);
    /**
     * Converts an engine row to a screen line; @p length receives its text
     * length. @p line is the row's absolute line on the primary screen (for
     * shell integration marks), or -1.
     */
    QVector<Character> convertRow(Screen *screen, const QVector<EngineCell> &cells, int *length, qint64 line = -1) const;

    /**
     * Brings the engine to Koncrepe's defaults: Alt sends ESC like it always
     * did in Koncrepe, and the profile's colors. Resets undo both.
     */
    void pushEngineSetup();
    bool engineIsUtf8() const;
    bool sendKeyToEngine(QKeyEvent *event);
    bool handleKeyBinding(QKeyEvent *event);
    KeyboardTranslator::States keyboardStates(Qt::KeyboardModifiers modifiers) const;
    /** Semantic Up/Down in a shell's input line; true when handled. */
    bool handleSemanticUpDown(QKeyEvent *event);
    /** Click to move the cursor within a shell's input line. */
    void moveCursorInInput(int column, int line);
    void emulateUpDown(int up, const KeyboardTranslator::Entry &entry, QByteArray &textToSend, int toColumn = -1);

    struct shitty_vt *_vt = nullptr;
    // the engine keeps a pointer to its callbacks, not a copy
    shitty_vt_callbacks *_callbacks = nullptr;
    ShittyStreamScanner _scanner;

    int _columns = 80;
    int _lines = 40;

    // history tracking (see drainHistory)
    // lines that ever left the primary screen: the absolute line of row 0
    qint64 _scrolledLines = 0;
    uint32_t _parkedOffset = 0;
    bool _historyStale = false; // re-base the count when back on the primary screen
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

    // kitty keyboard protocol, and a request held back while incomplete
    bool _kittyKeyboardEnabled = true;
    QByteArray _heldBack;

    // shell integration (OSC 133): where each mode began, in absolute lines
    struct SemanticMark {
        qint64 line;
        int column;
        int mode;
    };
    std::vector<SemanticMark> _semanticMarks;
    int _semanticModeBefore = 0; // the mode before the first mark
    QHash<qint64, LineProperty> _lineSemantics; // for lines still on screen
    int _targetColumn = -1; // semantic Up/Down keeps its column

    QHash<int, QString> _pendingSessionAttributesUpdates;
    QTimer *_sessionAttributesUpdateTimer = nullptr;
};

}

#endif
