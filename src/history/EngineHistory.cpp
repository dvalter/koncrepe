/*
    SPDX-FileCopyrightText: 2026 Koncrepe Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EngineHistory.h"

#include <algorithm>

using namespace Koncrepe;

EngineHistorySource::~EngineHistorySource() = default;

EngineHistoryType::EngineHistoryType(EngineHistorySource *source, int lines)
    : _source(source)
    , _lines(lines)
{
}

bool EngineHistoryType::isEnabled() const
{
    return true;
}

int EngineHistoryType::maximumLineCount() const
{
    return _lines;
}

void EngineHistoryType::scroll(std::unique_ptr<HistoryScroll> &old) const
{
    if (auto *engineHistory = dynamic_cast<EngineHistoryScroll *>(old.get()); engineHistory != nullptr && engineHistory->source() == _source) {
        engineHistory->setMaxLines(_lines);
        return;
    }
    // Whatever was kept before is what the engine holds: the emulation
    // only switches to this history while the old one is empty, or to start
    // over (a cleared history, a resize that reflowed the engine's lines).
    old = std::make_unique<EngineHistoryScroll>(_source, _lines);
}

EngineHistoryScroll::EngineHistoryScroll(EngineHistorySource *source, int maxLines)
    : HistoryScroll(new EngineHistoryType(source, maxLines))
    , _source(source)
    , _maxLines(qMax(0, maxLines))
    , _end(source->engineHistoryEnd())
{
    _properties.resize(qMin(source->engineHistoryLines(), _maxLines));
}

int EngineHistoryScroll::getLines() const
{
    return int(_properties.size());
}

int EngineHistoryScroll::getMaxLines() const
{
    return _maxLines;
}

const EngineHistoryScroll::Line *EngineHistoryScroll::line(int lineno) const
{
    const qint64 number = _end - qint64(_properties.size()) + lineno;
    const quint64 generation = _source->engineHistoryGeneration();
    Line &entry = _cache[size_t(number) % _cache.size()];
    if (entry.number == number && entry.generation == generation) {
        return &entry;
    }
    if (!_source->readEngineHistoryLine(number, entry.cells, &entry.length, &entry.wrapped)) {
        entry.number = -1;
        return &_unreadable;
    }
    entry.number = number;
    entry.generation = generation;
    return &entry;
}

int EngineHistoryScroll::getLineLen(const int lineno) const
{
    if (lineno < 0 || lineno >= getLines()) {
        return 0;
    }
    return int(line(lineno)->cells.size());
}

void EngineHistoryScroll::getCells(const int lineno, const int colno, const int count, Character res[]) const
{
    if (count <= 0) {
        return;
    }
    const QVector<Character> &cells = (lineno >= 0 && lineno < getLines()) ? line(lineno)->cells : _unreadable.cells;
    const int available = qBound(0, int(cells.size()) - colno, count);
    if (available > 0) {
        std::copy_n(cells.constData() + colno, available, res);
    }
    std::fill(res + available, res + count, Character());
}

bool EngineHistoryScroll::isWrappedLine(const int lineNumber) const
{
    if (lineNumber < 0 || lineNumber >= getLines()) {
        return false;
    }
    return line(lineNumber)->wrapped;
}

LineProperty EngineHistoryScroll::getLineProperty(const int lineno) const
{
    if (lineno < 0 || lineno >= getLines()) {
        return LineProperty();
    }
    LineProperty property = _properties[size_t(lineno)];
    const Line *read = line(lineno);
    property.flags.f.wrapped = read->wrapped;
    property.length = qint16(qMin(read->length, 0x7fff));
    return property;
}

void EngineHistoryScroll::setLineProperty(const int lineno, LineProperty prop)
{
    if (lineno >= 0 && lineno < getLines()) {
        _properties[size_t(lineno)] = prop;
    }
}

void EngineHistoryScroll::addCells(const Character[], const int)
{
    _properties.emplace_back();
    ++_end;
    if (int(_properties.size()) > _maxLines) {
        dropOldest();
    }
}

void EngineHistoryScroll::addCellsMove(Character a[], const int count)
{
    addCells(a, count);
}

void EngineHistoryScroll::addCellsVector(const QVector<Character> &)
{
    addCells(nullptr, 0);
}

void EngineHistoryScroll::addLine(const LineProperty lineProperty)
{
    if (!_properties.empty()) {
        _properties.back() = lineProperty;
    }
}

void EngineHistoryScroll::removeCells()
{
    if (!_properties.empty()) {
        _properties.pop_back();
        --_end;
    }
}

int EngineHistoryScroll::reflowLines(const int, std::map<int, int> *)
{
    return 0;
}

void EngineHistoryScroll::setMaxLines(int lines)
{
    _maxLines = qMax(0, lines);
    _historyType.reset(new EngineHistoryType(_source, _maxLines));
    while (int(_properties.size()) > _maxLines) {
        dropOldest();
    }
}

void EngineHistoryScroll::dropOldest()
{
    _properties.pop_front();
}
