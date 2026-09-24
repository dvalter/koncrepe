/*
    SPDX-FileCopyrightText: 2007-2008 Robert Knight <robertknight@gmail.com>
    SPDX-FileCopyrightText: 2012 Kurt Hindenburg <kurt.hindenburg@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef HISTORYSIZEDIALOG_H
#define HISTORYSIZEDIALOG_H

// Qt
#include <QDialog>

// Koncrepe
#include "Enumeration.h"
#include "koncrepeprivate_export.h"

namespace Ui
{
class HistorySizeDialog;
}

namespace Koncrepe
{
class KONCREPEPRIVATE_EXPORT HistorySizeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistorySizeDialog(QWidget *parent = nullptr);
    ~HistorySizeDialog() override;

    /** See HistorySizeWidget::setMode. */
    void setMode(Enum::HistoryModeEnum aMode);

    /** See HistorySizeWidget::mode. */
    Enum::HistoryModeEnum mode() const;

    /** See HistorySizeWidget::setLineCount. */
    void setLineCount(int lines);

    /** See HistorySizeWidget::lineCount. */
    int lineCount() const;

    QSize sizeHint() const override;

private:
    Ui::HistorySizeDialog *_ui;
};
}

#endif // HISTORYSIZEDIALOG_H
