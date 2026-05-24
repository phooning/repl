#pragma once

#include <QWidget>

class QPlainTextEdit;

class ConsolePane : public QWidget
{
    Q_OBJECT

public:
    explicit ConsolePane(QWidget *parent = nullptr);

    void clear();
    void appendText(const QString &text);
    void appendLine(const QString &text);

private:
    QPlainTextEdit *console_ = nullptr;
};
