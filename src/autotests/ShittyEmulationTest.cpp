/*
    SPDX-FileCopyrightText: 2026 Koncrepe Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QKeyEvent>
#include <QSignalSpy>
#include <QTest>

#include "../EscapeSequenceUrlExtractor.h"
#include "../Screen.h"
#include "../ScreenWindow.h"
#include "../ShittyEmulation.h"
#include "../ShittyStreamScanner.h"
#include "../characters/CharacterColor.h"
#include "../history/compact/CompactHistoryType.h"

using namespace Koncrepe;

class ShittyEmulationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testText();
    void testWrappedLine();
    void testColors();
    void testWideCharacter();
    void testGraphemeCluster();
    void testHistory();
    void testHistoryBeyondEngineScrollback();
    void testHistoryWithSplitWrites();
    void testEraseScrollback();
    void testAlternateScreen();
    void testHistoryAcrossAlternateScreen();
    void testReplies();
    void testKeys();
    void testKeysFollowApplicationModes();
    void testKittyKeyboardProtocol();
    void testKeytabCommandsStayWithKoncrepe();
    void testMouseTrackingMode();
    void testMouseReport();
    void testBracketedPasteMode();
    void testThemeAnswersColorQueries();
    void testApplicationPaletteOverride();
    void testPaletteResetRestoresTheme();
    void testTitle();
    void testSideChannel();
    void testResize();
    void testClearEntireScreen();
    void testResetPreservingPrompt();
    void testScannerBoundaries();
    void testKittyKeyboardDisabled();
    void testHyperlinks();
    void testShellIntegration();
};

namespace
{
// A color scheme table: foreground, background and the 8 colors, in normal,
// intense and faint variants.
void schemeTable(QColor *table)
{
    for (int i = 0; i < TABLE_COLORS; ++i) {
        table[i] = QColor::fromHsv((i * 37) % 360, 200, 60 + (i * 7) % 190);
    }
}

struct Terminal {
    explicit Terminal(int lines = 10, int columns = 40, int history = 1000)
    {
        emulation.setHistory(CompactHistoryType(history));
        window = emulation.createWindow();
        emulation.setImageSize(lines, columns);
        QObject::connect(&emulation, &Emulation::sendData, &emulation, [this](const QByteArray &data) {
            sent += data;
        });
    }

    void feed(const QByteArray &bytes)
    {
        emulation.receiveData(bytes.constData(), bytes.size());
        // what a repaint would do
        QMetaObject::invokeMethod(&emulation, "showBulk");
    }

    Screen *screen() const
    {
        return window->screen();
    }

    // Every line, history first, as plain text. Decoded by the screen
    // itself, which shares the library's extended character table.
    QStringList lines()
    {
        Screen *s = screen();
        QStringList result;
        const int count = s->getHistLines() + s->getLines();
        for (int line = 0; line < count; ++line) {
            result << lineText(line);
        }
        while (!result.isEmpty() && result.last().isEmpty()) {
            result.removeLast();
        }
        return result;
    }

    QString lineText(int index)
    {
        Screen *s = screen();
        QString text = s->text(index * s->getColumns(), (index + 1) * s->getColumns() - 1, Screen::PlainText);
        if (text.endsWith(QLatin1Char('\n'))) {
            text.chop(1);
        }
        return text;
    }

    QString screenLine(int line)
    {
        return lineText(screen()->getHistLines() + line);
    }

    Character cell(int column, int line)
    {
        Screen *s = screen();
        QVector<Character> image(s->getColumns() * s->getLines());
        const int first = s->getHistLines();
        s->getImage(image.data(), image.size(), first, first + s->getLines() - 1);
        return image.at(line * s->getColumns() + column);
    }

    QByteArray takeSent()
    {
        return std::exchange(sent, {});
    }

    void key(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString &text = QString())
    {
        QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
        emulation.sendKeyEvent(&press);
    }

    ShittyEmulation emulation;
    ScreenWindow *window = nullptr; // owned by the emulation
    QByteArray sent;
};
}

void ShittyEmulationTest::testText()
{
    Terminal t;
    t.feed("hello\r\nworld");
    QCOMPARE(t.screenLine(0), QStringLiteral("hello"));
    QCOMPARE(t.screenLine(1), QStringLiteral("world"));
    QCOMPARE(t.screen()->getCursorX(), 5);
    QCOMPARE(t.screen()->getCursorY(), 1);

    // cursor addressing and erasing are the engine's
    t.feed("\033[1;3HXY\033[2;1H\033[K");
    QCOMPARE(t.screenLine(0), QStringLiteral("heXYo"));
    QCOMPARE(t.screenLine(1), QString());
}

void ShittyEmulationTest::testWrappedLine()
{
    Terminal t(5, 10);
    t.feed("0123456789abc");
    QCOMPARE(t.cell(9, 0).character, char32_t('9'));
    QCOMPARE(t.cell(0, 1).character, char32_t('a'));
    QCOMPARE(t.screen()->getCursorY(), 1);
    const QVector<LineProperty> properties = t.screen()->getLineProperties(0, 1);
    QVERIFY(properties[0].flags.f.wrapped);
    QVERIFY(!properties[1].flags.f.wrapped);
}

void ShittyEmulationTest::testColors()
{
    Terminal t;
    t.feed("\033[31mR\033[0m\033[1;32mG\033[0m\033[38;5;200mI\033[38;2;1;2;3mD\033[0m\033[7mV\033[0m");

    // palette colors stay palette colors, so the color scheme draws them
    QCOMPARE(t.cell(0, 0).foregroundColor, CharacterColor(COLOR_SPACE_SYSTEM, 1));
    QCOMPARE(t.cell(0, 0).backgroundColor, CharacterColor(COLOR_SPACE_DEFAULT, DEFAULT_BACK_COLOR));

    // bold brightens a system color like Koncrepe always did
    CharacterColor boldGreen(COLOR_SPACE_SYSTEM, 2);
    boldGreen.setIntensive();
    QCOMPARE(t.cell(1, 0).foregroundColor, boldGreen);
    QVERIFY(t.cell(1, 0).rendition.f.bold);

    QCOMPARE(t.cell(2, 0).foregroundColor, CharacterColor(COLOR_SPACE_256, 200));
    QCOMPARE(t.cell(3, 0).foregroundColor, CharacterColor(COLOR_SPACE_RGB, 0x010203));

    // inverse swaps default colors
    QCOMPARE(t.cell(4, 0).foregroundColor, CharacterColor(COLOR_SPACE_DEFAULT, DEFAULT_BACK_COLOR));
    QCOMPARE(t.cell(4, 0).backgroundColor, CharacterColor(COLOR_SPACE_DEFAULT, DEFAULT_FORE_COLOR));

    t.feed("\r\n\033[4:3;58;2;255;0;0mU\033[0m\033[3;9mS");
    const Character underlined = t.cell(0, 1);
    QCOMPARE(int(underlined.rendition.f.underline), int(RE_UNDERLINE_CURL));
    QVERIFY((underlined.flags & EF_UNDERLINE_COLOR) != 0);
    const int ulIndex = (underlined.flags & EF_UNDERLINE_COLOR) / EF_UNDERLINE_COLOR_1 - 1;
    QCOMPARE(t.screen()->ulColorTable()[ulIndex], CharacterColor(COLOR_SPACE_RGB, 0xff0000));
    QVERIFY(t.cell(1, 1).rendition.f.italic);
    QVERIFY(t.cell(1, 1).rendition.f.strikeout);
}

void ShittyEmulationTest::testWideCharacter()
{
    Terminal t;
    t.feed(
        "a\xe4\xbd\xa0"
        "b"); // a你b
    QCOMPARE(t.cell(1, 0).character, char32_t(0x4f60));
    QVERIFY(t.cell(2, 0).isRightHalfOfDoubleWide());
    QCOMPARE(t.cell(3, 0).character, char32_t('b'));
    QCOMPARE(t.screen()->getCursorX(), 4);
    QCOMPARE(t.screenLine(0), QStringLiteral("a你b"));
}

void ShittyEmulationTest::testGraphemeCluster()
{
    Terminal t;
    t.feed("e\xcc\x81!"); // e + combining acute
    const Character cluster = t.cell(0, 0);
    QVERIFY(cluster.rendition.f.extended);
    QCOMPARE(t.cell(1, 0).character, char32_t('!'));
    QCOMPARE(t.screenLine(0), QStringLiteral("é!"));
}

void ShittyEmulationTest::testHistory()
{
    Terminal t(5, 20);
    QByteArray output;
    for (int i = 0; i < 20; ++i) {
        output += "line " + QByteArray::number(i) + "\r\n";
    }
    t.feed(output);

    // 20 lines and the empty one the cursor sits on, 5 of them on screen
    QCOMPARE(t.screen()->getHistLines(), 16);
    const QStringList lines = t.lines();
    for (int i = 0; i < 20; ++i) {
        QCOMPARE(lines.at(i), QStringLiteral("line %1").arg(i));
    }
    QCOMPARE(t.screenLine(3), QStringLiteral("line 19"));
}

void ShittyEmulationTest::testHistoryBeyondEngineScrollback()
{
    // Far more lines than the engine keeps itself, in one read.
    Terminal t(5, 20, 20000);
    QByteArray output;
    for (int i = 0; i < 10000; ++i) {
        output += QByteArray::number(i) + "\r\n";
    }
    t.feed(output);

    QCOMPARE(t.screen()->getHistLines(), 10000 - 4);
    const QStringList lines = t.lines();
    for (int i = 0; i < 10000; ++i) {
        QCOMPARE(lines.at(i), QString::number(i));
    }
}

void ShittyEmulationTest::testHistoryWithSplitWrites()
{
    // Reads split anywhere, even inside escape sequences and characters.
    Terminal t(4, 30, 5000);
    QByteArray output;
    for (int i = 0; i < 300; ++i) {
        output += "\033[3" + QByteArray::number(i % 8) + "m" + QByteArray::number(i) + " \xc3\xa9t\xc3\xa9\033[0m\r\n";
    }
    for (int i = 0; i < output.size(); i += 7) {
        t.emulation.receiveData(output.constData() + i, int(qMin<qsizetype>(7, output.size() - i)));
    }
    QMetaObject::invokeMethod(&t.emulation, "showBulk");

    const QStringList lines = t.lines();
    for (int i = 0; i < 300; ++i) {
        QCOMPARE(lines.at(i), QStringLiteral("%1 été").arg(i));
    }
}

void ShittyEmulationTest::testEraseScrollback()
{
    Terminal t(3, 20);
    t.feed("a\r\nb\r\nc\r\nd\r\ne");
    QVERIFY(t.screen()->getHistLines() > 0);
    t.feed("\033[3J");
    QCOMPARE(t.screen()->getHistLines(), 0);
    // and it keeps counting from there: c, d and e scroll off next
    t.feed("\r\nf\r\ng\r\nh");
    QCOMPARE(t.screen()->getHistLines(), 3);
    QCOMPARE(t.lines().first(), QStringLiteral("c"));
}

void ShittyEmulationTest::testAlternateScreen()
{
    Terminal t(4, 20);
    QSignalSpy primary(&t.emulation, &Emulation::primaryScreenInUse);
    t.feed("shell\r\n");
    t.feed("\033[?1049h\033[Hfull screen app");
    QCOMPARE(primary.count(), 1);
    QCOMPARE(primary.last().at(0).toBool(), false);
    QCOMPARE(t.screenLine(0), QStringLiteral("full screen app"));

    // output on the alternate screen never reaches the history
    t.feed("\r\n1\r\n2\r\n3\r\n4\r\n5\r\n");
    t.feed("\033[?1049l");
    QCOMPARE(primary.last().at(0).toBool(), true);
    QCOMPARE(t.screen()->getHistLines(), 0);
    QCOMPARE(t.screenLine(0), QStringLiteral("shell"));
}

void ShittyEmulationTest::testHistoryAcrossAlternateScreen()
{
    // Typing and resizing in a full screen application must not disturb
    // how the primary screen's scrollback is counted.
    Terminal t(4, 20);
    for (int i = 0; i < 10; ++i) {
        t.feed("line " + QByteArray::number(i) + "\r\n");
    }
    const int history = t.screen()->getHistLines();
    QCOMPARE(history, 7);

    t.feed("\033[?1049h\033[Hvim");
    t.key(Qt::Key_J, Qt::NoModifier, QStringLiteral("j"));
    t.emulation.setImageSize(6, 20);
    t.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    t.emulation.setImageSize(4, 20);
    t.feed("\033[?1049l");
    t.key(Qt::Key_L, Qt::NoModifier, QStringLiteral("l"));
    t.feed("next\r\n");

    const QStringList lines = t.lines();
    QCOMPARE(lines.count(QStringLiteral("line 0")), 1);
    QCOMPARE(lines.count(QStringLiteral("line 5")), 1);
    QVERIFY(lines.contains(QStringLiteral("next")));
    QVERIFY(t.screen()->getHistLines() <= history + 3);
}

void ShittyEmulationTest::testReplies()
{
    Terminal t;
    t.feed("ab\033[6n");
    QCOMPARE(t.takeSent(), QByteArray("\033[1;3R"));
    t.feed("\033[c");
    QVERIFY(t.takeSent().startsWith("\033[?"));
}

void ShittyEmulationTest::testKeys()
{
    Terminal t;
    t.key(Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    QCOMPARE(t.takeSent(), QByteArray("a"));
    t.key(Qt::Key_A, Qt::ShiftModifier, QStringLiteral("A"));
    QCOMPARE(t.takeSent(), QByteArray("A"));
    t.key(Qt::Key_C, Qt::ControlModifier, QStringLiteral("\x03"));
    QCOMPARE(t.takeSent(), QByteArray("\x03"));
    t.key(Qt::Key_X, Qt::AltModifier, QStringLiteral("x"));
    QCOMPARE(t.takeSent(), QByteArray("\033x"));
    t.key(Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCOMPARE(t.takeSent(), QByteArray("\r"));
    t.key(Qt::Key_Backspace, Qt::NoModifier, QStringLiteral("\b"));
    QCOMPARE(t.takeSent(), QByteArray("\x7f"));
    t.key(Qt::Key_Up);
    QCOMPARE(t.takeSent(), QByteArray("\033[A"));
    t.key(Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(t.takeSent(), QByteArray("\033[1;5C"));
    t.key(Qt::Key_F1);
    QCOMPARE(t.takeSent(), QByteArray("\033OP"));
    t.key(Qt::Key_F5);
    QCOMPARE(t.takeSent(), QByteArray("\033[15~"));
    t.key(Qt::Key_E, Qt::NoModifier, QStringLiteral("é"));
    QCOMPARE(t.takeSent(), QByteArray("\xc3\xa9"));

    // text without a key (a paste, or text sent for the user) goes as is
    t.emulation.sendText(QStringLiteral("echo hi\r"));
    QCOMPARE(t.takeSent(), QByteArray("echo hi\r"));
}

void ShittyEmulationTest::testKeysFollowApplicationModes()
{
    Terminal t;
    t.feed("\033[?1h"); // DECCKM
    t.key(Qt::Key_Up);
    QCOMPARE(t.takeSent(), QByteArray("\033OA"));
    t.feed("\033[?1l");
    t.key(Qt::Key_Up);
    QCOMPARE(t.takeSent(), QByteArray("\033[A"));
}

void ShittyEmulationTest::testKittyKeyboardProtocol()
{
    Terminal t;
    t.feed("\033[>1u"); // disambiguate escape codes
    t.key(Qt::Key_Escape);
    QCOMPARE(t.takeSent(), QByteArray("\033[27u"));
    t.feed("\033[<u");
    t.key(Qt::Key_Escape);
    QCOMPARE(t.takeSent(), QByteArray("\033"));
}

void ShittyEmulationTest::testKeytabCommandsStayWithKoncrepe()
{
    Terminal t(3, 20);
    t.emulation.setKeyBindings(QStringLiteral("default"));
    // Shift+PageUp scrolls the history in the default keytab: nothing is
    // sent to the application.
    t.key(Qt::Key_PageUp, Qt::ShiftModifier);
    QCOMPARE(t.takeSent(), QByteArray());
    // plain PageUp is the application's
    t.key(Qt::Key_PageUp);
    QCOMPARE(t.takeSent(), QByteArray("\033[5~"));
}

void ShittyEmulationTest::testMouseTrackingMode()
{
    Terminal t;
    QSignalSpy tracking(&t.emulation, &Emulation::programRequestsMouseTracking);
    t.feed("\033[?1000h");
    QCOMPARE(tracking.count(), 1);
    QCOMPARE(tracking.last().at(0).toBool(), true);
    t.feed("\033[?1000l");
    QCOMPARE(tracking.last().at(0).toBool(), false);
}

void ShittyEmulationTest::testMouseReport()
{
    Terminal t;
    t.feed("\033[?1000h\033[?1006h");
    t.takeSent();
    t.emulation.sendMouseEvent(0, 3, 2, 0); // left press at column 3, line 2
    t.emulation.sendMouseEvent(0, 3, 2, 2); // release
    QCOMPARE(t.takeSent(), QByteArray("\033[<0;3;2M\033[<0;3;2m"));
    t.emulation.sendMouseEvent(64, 1, 1, 0); // wheel up
    QCOMPARE(t.takeSent(), QByteArray("\033[<64;1;1M"));

    // Without tracking nothing reaches the application.
    t.feed("\033[?1000l");
    t.emulation.sendMouseEvent(0, 3, 2, 0);
    QCOMPARE(t.takeSent(), QByteArray());
}

void ShittyEmulationTest::testBracketedPasteMode()
{
    Terminal t;
    t.feed("\033[?2004h");
    QVERIFY(t.emulation.programBracketedPasteMode());
    t.feed("\033[?2004l");
    QVERIFY(!t.emulation.programBracketedPasteMode());
}

void ShittyEmulationTest::testThemeAnswersColorQueries()
{
    Terminal t;
    QColor table[TABLE_COLORS];
    schemeTable(table);
    table[DEFAULT_BACK_COLOR] = QColor(0x12, 0x34, 0x56);
    table[DEFAULT_FORE_COLOR] = QColor(0xfe, 0xdc, 0xba);
    table[2 + 1] = QColor(0xaa, 0x11, 0x22); // red
    t.emulation.setEngineColors(table, QColor());

    t.feed("\033]11;?\033\\");
    QCOMPARE(t.takeSent(), QByteArray("\033]11;rgb:1212/3434/5656\033\\"));
    t.feed("\033]10;?\033\\");
    QCOMPARE(t.takeSent(), QByteArray("\033]10;rgb:fefe/dcdc/baba\033\\"));
    t.feed("\033]4;1;?\033\\");
    QCOMPARE(t.takeSent(), QByteArray("\033]4;1;rgb:aaaa/1111/2222\033\\"));

    // and cells keep asking for the palette entry, which the scheme draws
    t.feed("\033[31mR");
    QCOMPARE(t.cell(0, 0).foregroundColor, CharacterColor(COLOR_SPACE_SYSTEM, 1));
}

void ShittyEmulationTest::testApplicationPaletteOverride()
{
    Terminal t;
    QColor table[TABLE_COLORS];
    schemeTable(table);
    t.emulation.setEngineColors(table, QColor());
    // the application redefines red: draw what it set, not the scheme's red
    t.feed("\033]4;1;rgb:01/02/03\033\\\033[31mR");
    QCOMPARE(t.cell(0, 0).foregroundColor, CharacterColor(COLOR_SPACE_RGB, 0x010203));
}

void ShittyEmulationTest::testPaletteResetRestoresTheme()
{
    Terminal t;
    QColor table[TABLE_COLORS];
    schemeTable(table);
    table[DEFAULT_BACK_COLOR] = QColor(0x12, 0x34, 0x56);
    t.emulation.setEngineColors(table, QColor());

    // resets go back to the theme, not to the engine's built-in colors
    t.feed("\033]111\033\\\033]11;?\033\\");
    QCOMPARE(t.takeSent(), QByteArray("\033]11;rgb:1212/3434/5656\033\\"));
    t.feed("\033]4;1;rgb:01/02/03\033\\\033]104\033\\\033[31mR");
    QCOMPARE(t.cell(0, 0).foregroundColor, CharacterColor(COLOR_SPACE_SYSTEM, 1));
    t.feed("\033c\033]11;?\033\\");
    QCOMPARE(t.takeSent(), QByteArray("\033]11;rgb:1212/3434/5656\033\\"));
}

void ShittyEmulationTest::testTitle()
{
    Terminal t;
    QSignalSpy attributes(&t.emulation, &Emulation::sessionAttributeChanged);
    t.feed("\033]2;my title\007");
    QVERIFY(attributes.wait(1000));
    QCOMPARE(attributes.last().at(0).toInt(), 0);
    QCOMPARE(attributes.last().at(1).toString(), QStringLiteral("my title"));
}

void ShittyEmulationTest::testSideChannel()
{
    Terminal t;
    QSignalSpy attributes(&t.emulation, &Emulation::sessionAttributeChanged);
    QSignalSpy progress(&t.emulation, &Emulation::progressChanged);
    QSignalSpy osc777(&t.emulation, &Emulation::osc777Received);

    t.feed("\033]7;file://host/tmp\033\\\033]9;4;1;42\007\033]777;precmd;;\007");
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.last().at(0).toInt(), 42);
    QCOMPARE(osc777.count(), 1);
    QVERIFY(attributes.wait(1000));
    bool sawDirectory = false;
    for (const auto &arguments : std::as_const(attributes)) {
        sawDirectory |= arguments.at(0).toInt() == 7 && arguments.at(1).toString() == QStringLiteral("file://host/tmp");
    }
    QVERIFY(sawDirectory);
    // the engine consumed them as well: nothing leaked onto the screen
    QCOMPARE(t.screenLine(0), QString());
}

void ShittyEmulationTest::testResize()
{
    Terminal t(5, 20);
    t.feed("first\r\nsecond\r\nthird");
    t.emulation.setImageSize(8, 30);
    QCOMPARE(t.emulation.imageSize(), QSize(30, 8));
    // the engine has the new size too: 30 columns fit on a line
    t.feed("\033[8;1H" + QByteArray(30, 'x'));
    QCOMPARE(t.cell(29, 7).character, char32_t('x'));
    QCOMPARE(t.screen()->getCursorY(), 7);
    const QStringList lines = t.lines();
    QVERIFY(lines.contains(QStringLiteral("first")));
    QVERIFY(lines.contains(QStringLiteral("third")));
    QCOMPARE(lines.count(QStringLiteral("first")), 1);
}

void ShittyEmulationTest::testClearEntireScreen()
{
    Terminal t(3, 20);
    t.feed("one\r\ntwo");
    t.emulation.clearEntireScreen();
    QMetaObject::invokeMethod(&t.emulation, "showBulk");
    QCOMPARE(t.screenLine(0), QString());
    QVERIFY(t.lines().contains(QStringLiteral("one")));
}

void ShittyEmulationTest::testResetPreservingPrompt()
{
    Terminal t(4, 20);
    t.feed("junk\r\nmore\r\n$ prompt");
    t.emulation.reset(false, true);
    QMetaObject::invokeMethod(&t.emulation, "showBulk");
    QCOMPARE(t.screenLine(0), QStringLiteral("$ prompt"));
    QCOMPARE(t.screen()->getCursorY(), 0);
    QCOMPARE(t.screen()->getCursorX(), 8);

    // an application's unfinished sequence does not swallow the reset
    t.feed("\033[3");
    t.emulation.reset(false, true);
    t.feed("X");
    QCOMPARE(t.screenLine(0), QStringLiteral("$ promptX"));
}

void ShittyEmulationTest::testScannerBoundaries()
{
    ShittyStreamScanner scanner;
    const QByteArray data("ab\033]7;/x\033\\cd\033[3Jef\033]0;t\007\033c");
    auto event = scanner.scan(data.constData(), data.size(), 0);
    QCOMPARE(event.type, ShittyStreamScanner::EventType::Osc);
    QCOMPARE(event.number, 7);
    QCOMPARE(event.payload, QByteArray("/x"));
    QCOMPARE(event.end, data.indexOf("cd"));
    event = scanner.scan(data.constData(), data.size(), event.end);
    QCOMPARE(event.type, ShittyStreamScanner::EventType::EraseScrollback);
    event = scanner.scan(data.constData(), data.size(), event.end);
    QCOMPARE(event.type, ShittyStreamScanner::EventType::Osc);
    QCOMPARE(event.number, 0);
    event = scanner.scan(data.constData(), data.size(), event.end);
    QCOMPARE(event.type, ShittyStreamScanner::EventType::FullReset);
    QVERIFY(scanner.atGround());

    // split in the middle of a sequence
    ShittyStreamScanner split;
    QCOMPARE(split.scan("\033]7;/p", 6, 0).type, ShittyStreamScanner::EventType::None);
    QVERIFY(!split.atGround());
    event = split.scan("ath\007", 4, 0);
    QCOMPARE(event.payload, QByteArray("/path"));
    QVERIFY(split.atGround());

    // strings the scanner does not care about are skipped whole
    ShittyStreamScanner dcs;
    const QByteArray sixel("\033Pq#0;2;0;0;0#0~~-~~\033\\x");
    QCOMPARE(dcs.scan(sixel.constData(), sixel.size(), 0).type, ShittyStreamScanner::EventType::None);
    QVERIFY(dcs.atGround());
}

void ShittyEmulationTest::testKittyKeyboardDisabled()
{
    Terminal t;
    t.emulation.setKittyKeyboardEnabled(false);
    // the query goes unanswered, the request unheeded, even split in two
    t.feed("\033[?u\033[c");
    const QByteArray replies = t.takeSent();
    QVERIFY(!replies.contains("u"));
    QVERIFY(replies.startsWith("\033[?"));
    t.emulation.receiveData("\033[>", 3);
    t.emulation.receiveData("1u", 2);
    t.key(Qt::Key_Escape);
    QCOMPARE(t.takeSent(), QByteArray("\033"));
    // other sequences split the same way still work
    t.emulation.receiveData("\033[", 2);
    t.emulation.receiveData("6n", 2);
    QCOMPARE(t.takeSent(), QByteArray("\033[1;1R"));

    t.emulation.setKittyKeyboardEnabled(true);
    t.feed("\033[>1u");
    t.key(Qt::Key_Escape);
    QCOMPARE(t.takeSent(), QByteArray("\033[27u"));
}

void ShittyEmulationTest::testHyperlinks()
{
    Terminal t(5, 40);
    Screen *screen = t.screen();
    screen->setEnableUrlExtractor(true);
    screen->urlExtractor()->setAllowedLinkSchema({QStringLiteral("https://")});

    t.feed("see \033]8;;https://example.com/\033\\the link\033]8;;\033\\ and \033]8;;ftp://no\033\\not this\033]8;;\033\\");
    const QVector<ExtractedUrl> urls = screen->urlExtractor()->history();
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0].url, QStringLiteral("https://example.com/"));
    QCOMPARE(urls[0].begin.row, 0);
    QCOMPARE(urls[0].begin.col, 4);
    QCOMPARE(urls[0].end.row, 0);
    QCOMPARE(urls[0].end.col, 12);
    QCOMPARE(t.screenLine(0), QStringLiteral("see the link and not this"));

    // a link arriving in pieces
    t.feed("\r\n");
    t.emulation.receiveData("\033]8;;https://", 13);
    t.emulation.receiveData("kde.org\033", 8);
    t.emulation.receiveData("\\KDE\033]8;;\033\\!", 12);
    QMetaObject::invokeMethod(&t.emulation, "showBulk");
    QCOMPARE(screen->urlExtractor()->history().size(), 2);
    QCOMPARE(screen->urlExtractor()->history().at(1).url, QStringLiteral("https://kde.org"));
    QCOMPARE(t.screenLine(1), QStringLiteral("KDE!"));
}

void ShittyEmulationTest::testShellIntegration()
{
    Terminal t(4, 30);
    t.feed("\033]133;A\007$ \033]133;B\007false\r\n\033]133;C\007oops\r\n\033]133;D;1\007\033]133;A\007$ ");

    Screen *screen = t.screen();
    QVERIFY(screen->hasRepl());
    QCOMPARE(screen->replMode(), REPL_PROMPT);
    QVector<LineProperty> properties = screen->getLineProperties(0, 2);
    QVERIFY(properties[0].flags.f.prompt_start);
    QVERIFY(properties[0].flags.f.error);
    QVERIFY(properties[1].flags.f.error);
    QVERIFY(properties[2].flags.f.prompt_start);
    QVERIFY(!properties[2].flags.f.error);

    // every character knows what printed it
    QCOMPARE(t.cell(0, 0).repl(), int(EF_REPL_PROMPT));
    QCOMPARE(t.cell(2, 0).repl(), int(EF_REPL_INPUT));
    QCOMPARE(t.cell(0, 1).repl(), int(EF_REPL_OUTPUT));
    QCOMPARE(t.cell(0, 2).repl(), int(EF_REPL_PROMPT));

    // and all of it survives the trip into the history
    t.feed("\033]133;B\007yes\r\n\033]133;C\007");
    for (int i = 0; i < 10; ++i) {
        t.feed("y\r\n");
    }
    QVERIFY(screen->getHistLines() > 4);
    properties = screen->getLineProperties(0, 3);
    QVERIFY(properties[0].flags.f.prompt_start);
    QVERIFY(properties[0].flags.f.error);
    QVERIFY(properties[2].flags.f.prompt_start);
    QVector<Character> image(screen->getColumns() * 3);
    screen->getImage(image.data(), image.size(), 0, 2);
    QCOMPARE(image[0].repl(), int(EF_REPL_PROMPT));
    QCOMPARE(image[2].repl(), int(EF_REPL_INPUT));
    QCOMPARE(image[screen->getColumns()].repl(), int(EF_REPL_OUTPUT));
}

QTEST_MAIN(ShittyEmulationTest)

#include "ShittyEmulationTest.moc"
