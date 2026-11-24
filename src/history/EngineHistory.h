/*
    SPDX-FileCopyrightText: 2026 Koncrepe Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef ENGINEHISTORY_H
#define ENGINEHISTORY_H

#include "HistoryScroll.h"
#include "HistoryType.h"
#include "koncrepeprivate_export.h"

#include <array>
#include <deque>

namespace Koncrepe
{
/**
 * Where an engine-backed history reads its lines: the terminal engine,
 * which keeps its own scrollback like it does when it runs standalone.
 *
 * Lines are named by absolute number: the count of lines that had scrolled
 * off the primary screen before them.
 */
class KONCREPEPRIVATE_EXPORT EngineHistorySource
{
public:
    virtual ~EngineHistorySource();

    /** The absolute number the next line to enter the history will have. */
    virtual qint64 engineHistoryEnd() const = 0;
    /** How many of the lines before engineHistoryEnd() the engine still holds. */
    virtual int engineHistoryLines() const = 0;
    /**
     * Reads line @p line into @p cells, its text length into @p length and
     * whether it wraps onto the next into @p wrapped. False when the engine
     * cannot be read just now (the alternate screen is shown, or the
     * application is in the middle of a synchronized update).
     */
    virtual bool readEngineHistoryLine(qint64 line, QVector<Character> &cells, int *length, bool *wrapped) = 0;
    /** Changes whenever lines already read may read differently (a new color scheme). */
    virtual quint64 engineHistoryGeneration() const = 0;
};

/**
 * A history of up to a fixed number of lines that stay in the terminal
 * engine: nothing is copied when lines scroll off, they are read when they
 * are shown, searched or saved.
 */
class KONCREPEPRIVATE_EXPORT EngineHistoryType : public HistoryType
{
public:
    EngineHistoryType(EngineHistorySource *source, int lines);

    bool isEnabled() const override;
    int maximumLineCount() const override;
    void scroll(std::unique_ptr<HistoryScroll> &old) const override;

private:
    EngineHistorySource *_source;
    int _lines;
};

class KONCREPEPRIVATE_EXPORT EngineHistoryScroll : public HistoryScroll
{
public:
    EngineHistoryScroll(EngineHistorySource *source, int maxLines);

    int getLines() const override;
    int getMaxLines() const override;
    int getLineLen(const int lineno) const override;
    void getCells(const int lineno, const int colno, const int count, Character res[]) const override;
    bool isWrappedLine(const int lineNumber) const override;
    LineProperty getLineProperty(const int lineno) const override;
    void setLineProperty(const int lineno, LineProperty prop) override;

    // The engine already has the cells: only the line is counted.
    void addCells(const Character a[], const int count) override;
    void addCellsMove(Character a[], const int count) override;
    void addCellsVector(const QVector<Character> &cells) override;
    void addLine(const LineProperty lineProperty = LineProperty()) override;
    void removeCells() override;
    // The engine reflows its own lines.
    int reflowLines(const int columns, std::map<int, int> *deltas = nullptr) override;

    void setMaxLines(int lines);
    EngineHistorySource *source() const
    {
        return _source;
    }

private:
    struct Line {
        qint64 number = -1;
        quint64 generation = 0;
        QVector<Character> cells;
        int length = 0;
        bool wrapped = false;
    };
    const Line *line(int lineno) const;
    void dropOldest();

    EngineHistorySource *const _source;
    int _maxLines;
    // the shell integration flags of each line, oldest first
    std::deque<LineProperty> _properties;
    qint64 _end;
    // lines are read again and again while they are shown
    mutable std::array<Line, 256> _cache;
    mutable Line _unreadable;
};
}

#endif
