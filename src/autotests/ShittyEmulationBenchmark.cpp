/*
    SPDX-FileCopyrightText: 2026 Koncrepe Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// How fast output goes through the shitty engine into Koncrepe's screens
// and history, without any painting: what `cat` of a large file costs
// besides drawing it.

#include <QTest>

#include "../Screen.h"
#include "../ScreenWindow.h"
#include "../ShittyEmulation.h"
#include "../history/HistoryTypeNone.h"
#include "../history/compact/CompactHistoryType.h"

using namespace Koncrepe;

class ShittyEmulationBenchmark : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cat_data();
    void cat();
};

namespace
{
// Output like cat's: 40000 numbered lines of 80 characters.
QByteArray text()
{
    QByteArray result;
    for (int i = 0; i < 40000; ++i) {
        result += QByteArray::number(i).rightJustified(7, '0');
        result += " lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod\r\n"; // a pty turns \n into \r\n
    }
    return result;
}
}

void ShittyEmulationBenchmark::cat_data()
{
    QTest::addColumn<int>("history"); // -1 for none
    QTest::addColumn<int>("repaintEvery"); // bytes between two repaints

    QTest::newRow("history 1000, repaint every 64 KiB") << 1000 << 64 * 1024;
    QTest::newRow("history 10000, repaint every 64 KiB") << 10000 << 64 * 1024;
    QTest::newRow("history 1000, repaint every 4 KiB") << 1000 << 4 * 1024;
    QTest::newRow("no history, repaint every 64 KiB") << -1 << 64 * 1024;
    QTest::newRow("history 100000, repaint every 64 KiB") << 100000 << 64 * 1024;
}

void ShittyEmulationBenchmark::cat()
{
    QFETCH(int, history);
    QFETCH(int, repaintEvery);
    const QByteArray output = text();

    QBENCHMARK {
        ShittyEmulation emulation;
        if (history < 0) {
            emulation.setHistory(HistoryTypeNone());
        } else {
            emulation.setHistory(CompactHistoryType(history));
        }
        ScreenWindow *window = emulation.createWindow();
        emulation.setImageSize(40, 120);

        // in the pieces a pty hands out, with a repaint now and then
        constexpr int Read = 4096;
        int sinceRepaint = 0;
        for (int offset = 0; offset < output.size(); offset += Read) {
            const int length = qMin(Read, int(output.size()) - offset);
            emulation.receiveData(output.constData() + offset, length);
            sinceRepaint += length;
            if (sinceRepaint >= repaintEvery) {
                QMetaObject::invokeMethod(&emulation, "showBulk");
                sinceRepaint = 0;
            }
        }
        QMetaObject::invokeMethod(&emulation, "showBulk");
        QVERIFY(window->screen()->getHistLines() <= qMax(history, 0) + 1);
    }
}

QTEST_GUILESS_MAIN(ShittyEmulationBenchmark)

#include "ShittyEmulationBenchmark.moc"
