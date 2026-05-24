#pragma once

#include <QVector>
#include <QWidget>

#include <functional>

class EditorBridge;
class QWebChannel;
class QWebEngineView;
struct Diagnostic;

class EditorPane : public QWidget
{
    Q_OBJECT

public:
    explicit EditorPane(const QString &currentFilePath, QWidget *parent = nullptr);

    void currentText(std::function<void(const QString &, bool)> callback);
    void setDiagnostics(const QVector<Diagnostic> &diagnostics);
    void clearDiagnostics();
    void setVimModeEnabled(bool enabled);

signals:
    void contentChanged();
    void readyChanged(bool ready);

private:
    static QString editorHtml(const QString &currentFilePath);
    void applyVimModePreference();

    EditorBridge *bridge_ = nullptr;
    QWebChannel *channel_ = nullptr;
    QWebEngineView *view_ = nullptr;
    bool vimModeEnabled_ = false;
};
