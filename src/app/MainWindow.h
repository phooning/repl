#pragma once

#include <QMainWindow>
#include <QTimer>

class QAction;
class QCloseEvent;
class ConsolePane;
class EditorPane;
class TsRunner;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class ActiveCommandKind {
        None,
        ManualRun,
        ManualTypecheck,
        Autorun
    };

    void runCurrentFile();
    void typecheckCurrentFile();
    void stopCurrentCommand();
    void scheduleAutorun();
    void runAutorunNow();
    void handleAutorunToggled(bool enabled);
    void startCommandFromEditor(ActiveCommandKind commandKind);
    bool commandInProgress() const;
    void setBusy(bool busy);
    void wireRunner();

    TsRunner *runner_ = nullptr;
    EditorPane *editor_ = nullptr;
    ConsolePane *console_ = nullptr;
    QAction *runAction_ = nullptr;
    QAction *typecheckAction_ = nullptr;
    QAction *stopAction_ = nullptr;
    QAction *autorunAction_ = nullptr;
    QAction *vimAction_ = nullptr;
    QAction *clearAction_ = nullptr;
    QTimer autorunTimer_;
    ActiveCommandKind activeCommand_ = ActiveCommandKind::None;
    bool autorunPendingAfterStop_ = false;
};
