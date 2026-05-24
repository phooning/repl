#include "app/ConsolePane.h"

#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

ConsolePane::ConsolePane(QWidget *parent)
    : QWidget(parent)
    , console_(new QPlainTextEdit(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(console_);

    console_->setReadOnly(true);
    console_->setLineWrapMode(QPlainTextEdit::NoWrap);
    console_->document()->setMaximumBlockCount(8000);
    console_->setPlaceholderText("Run and typecheck output appears here.");
    console_->setStyleSheet(
        "QPlainTextEdit {"
        "  background: #111318;"
        "  color: #e6edf3;"
        "  border: 0;"
        "  font-family: 'JetBrains Mono', 'SFMono-Regular', Consolas, monospace;"
        "  font-size: 13px;"
        "  padding: 10px;"
        "}"
        "QPlainTextEdit::selection { background: #264f78; }");
}

void ConsolePane::clear()
{
    console_->clear();
}

void ConsolePane::appendText(const QString &text)
{
    QTextCursor cursor = console_->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    console_->setTextCursor(cursor);
    console_->ensureCursorVisible();
}

void ConsolePane::appendLine(const QString &text)
{
    appendText(text + '\n');
}
