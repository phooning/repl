#include "app/MainWindow.h"

#include "app/ConsolePane.h"
#include "app/EditorPane.h"
#include "runtime/TsRunner.h"

#include <QAction>
#include <QCloseEvent>
#include <QIcon>
#include <QKeySequence>
#include <QPointer>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>

namespace {

constexpr int kAutorunDebounceMs = 500;

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , runner_(new TsRunner(this))
    , editor_(new EditorPane(runner_->currentFilePath(), this))
    , console_(new ConsolePane(this))
{
    setWindowTitle(QStringLiteral("REPL V1"));

    auto *toolbar = addToolBar(QStringLiteral("Run"));
    toolbar->setMovable(false);

    runAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("media-playback-start")), QStringLiteral("Run"));
    runAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    runAction_->setToolTip(QStringLiteral("Run current file with tsx"));

    typecheckAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("emblem-ok")), QStringLiteral("Typecheck"));
    typecheckAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));
    typecheckAction_->setToolTip(QStringLiteral("Run tsgo or tsc --noEmit"));

    stopAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("process-stop")), QStringLiteral("Stop"));
    stopAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+.")));
    stopAction_->setToolTip(QStringLiteral("Stop the running process"));
    stopAction_->setEnabled(false);

    autorunAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("media-playlist-repeat")), QStringLiteral("Autorun"));
    autorunAction_->setCheckable(true);
    autorunAction_->setChecked(false);
    autorunAction_->setToolTip(QStringLiteral("Automatically run after editor changes settle"));

    vimAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("input-keyboard")), QStringLiteral("Vim"));
    vimAction_->setCheckable(true);
    vimAction_->setChecked(false);
    vimAction_->setToolTip(QStringLiteral("Toggle Vim keybindings"));

    clearAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-clear")), QStringLiteral("Clear"));
    clearAction_->setToolTip(QStringLiteral("Clear output console"));

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(editor_);
    splitter->addWidget(console_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    setCentralWidget(splitter);

    statusBar()->showMessage(QStringLiteral("Current file: %1").arg(runner_->currentFilePath()));

    autorunTimer_.setSingleShot(true);
    autorunTimer_.setInterval(kAutorunDebounceMs);

    connect(runAction_, &QAction::triggered, this, &MainWindow::runCurrentFile);
    connect(typecheckAction_, &QAction::triggered, this, &MainWindow::typecheckCurrentFile);
    connect(stopAction_, &QAction::triggered, this, &MainWindow::stopCurrentCommand);
    connect(autorunAction_, &QAction::toggled, this, &MainWindow::handleAutorunToggled);
    connect(vimAction_, &QAction::toggled, this, [this](bool enabled) {
        editor_->setVimModeEnabled(enabled);
        statusBar()->showMessage(enabled
                ? QStringLiteral("Vim mode enabled")
                : QStringLiteral("Vim mode disabled"));
    });
    connect(&autorunTimer_, &QTimer::timeout, this, &MainWindow::runAutorunNow);
    connect(clearAction_, &QAction::triggered, console_, &ConsolePane::clear);
    connect(editor_, &EditorPane::contentChanged, this, &MainWindow::scheduleAutorun);
    connect(editor_, &EditorPane::readyChanged, this, [this](bool ready) {
        statusBar()->showMessage(ready
            ? QStringLiteral("Current file: %1").arg(runner_->currentFilePath())
            : QStringLiteral("Editor failed to load"));
    });

    wireRunner();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    runner_->stop();
    QMainWindow::closeEvent(event);
}

void MainWindow::runCurrentFile()
{
    autorunTimer_.stop();
    startCommandFromEditor(ActiveCommandKind::ManualRun);
}

void MainWindow::typecheckCurrentFile()
{
    autorunTimer_.stop();
    startCommandFromEditor(ActiveCommandKind::ManualTypecheck);
}

void MainWindow::stopCurrentCommand()
{
    if (runner_->isRunning()) {
        runner_->stop();
        return;
    }

    if (activeCommand_ != ActiveCommandKind::None) {
        activeCommand_ = ActiveCommandKind::None;
        autorunPendingAfterStop_ = false;
        setBusy(false);
        statusBar()->showMessage(QStringLiteral("Process stopped"));
    }
}

void MainWindow::scheduleAutorun()
{
    if (!autorunAction_->isChecked()) {
        return;
    }

    autorunTimer_.start();

    if (activeCommand_ == ActiveCommandKind::Autorun) {
        const bool alreadyPending = autorunPendingAfterStop_;
        autorunPendingAfterStop_ = true;
        if (runner_->isRunning() && !alreadyPending) {
            console_->appendLine(QStringLiteral("\nAutorun superseded by newer edit."));
            runner_->stop();
        }
        return;
    }

    if (activeCommand_ == ActiveCommandKind::ManualRun || activeCommand_ == ActiveCommandKind::ManualTypecheck) {
        autorunPendingAfterStop_ = true;
    }
}

void MainWindow::runAutorunNow()
{
    if (!autorunAction_->isChecked()) {
        return;
    }

    startCommandFromEditor(ActiveCommandKind::Autorun);
}

void MainWindow::handleAutorunToggled(bool enabled)
{
    if (enabled) {
        scheduleAutorun();
        return;
    }

    autorunTimer_.stop();
    autorunPendingAfterStop_ = false;
    if (activeCommand_ != ActiveCommandKind::Autorun) {
        return;
    }

    if (runner_->isRunning()) {
        console_->appendLine(QStringLiteral("\nAutorun disabled; stopping current autorun."));
        runner_->stop();
        return;
    }

    activeCommand_ = ActiveCommandKind::None;
    setBusy(false);
    statusBar()->showMessage(QStringLiteral("Autorun disabled"));
}

void MainWindow::startCommandFromEditor(ActiveCommandKind commandKind)
{
    const bool isAutorun = commandKind == ActiveCommandKind::Autorun;

    if (commandInProgress()) {
        if (isAutorun) {
            const bool alreadyPending = autorunPendingAfterStop_;
            autorunPendingAfterStop_ = true;
            if (activeCommand_ == ActiveCommandKind::Autorun && runner_->isRunning() && !alreadyPending) {
                console_->appendLine(QStringLiteral("\nAutorun superseded by newer edit."));
                runner_->stop();
            }
            return;
        }

        console_->appendLine(QStringLiteral("A process is already running. Use Stop before starting another command."));
        return;
    }

    activeCommand_ = commandKind;
    setBusy(true);
    if (commandKind != ActiveCommandKind::ManualTypecheck) {
        editor_->clearDiagnostics();
    }
    console_->clear();

    QPointer<MainWindow> self(this);
    editor_->currentText([self, commandKind](const QString &code, bool ok) {
        if (!self || self->activeCommand_ != commandKind) {
            return;
        }

        if (commandKind == ActiveCommandKind::Autorun && !self->autorunAction_->isChecked()) {
            self->activeCommand_ = ActiveCommandKind::None;
            self->setBusy(false);
            return;
        }

        if (!ok) {
            self->console_->appendLine(QStringLiteral("Unable to read the editor contents."));
            self->activeCommand_ = ActiveCommandKind::None;
            self->setBusy(false);
            return;
        }

        if (commandKind == ActiveCommandKind::ManualTypecheck) {
            self->runner_->typecheck(code);
        } else {
            self->runner_->run(code);
        }
    });
}

bool MainWindow::commandInProgress() const
{
    return activeCommand_ != ActiveCommandKind::None || runner_->isRunning();
}

void MainWindow::setBusy(bool busy)
{
    runAction_->setEnabled(!busy);
    typecheckAction_->setEnabled(!busy);
    stopAction_->setEnabled(busy);
    clearAction_->setEnabled(true);
}

void MainWindow::wireRunner()
{
    connect(runner_, &TsRunner::commandStarted, this, [this](const QString &commandLine, const QString &workingDirectory) {
        console_->appendLine(QStringLiteral("$ %1").arg(commandLine));
        console_->appendLine(QStringLiteral("# cwd: %1").arg(workingDirectory));
        if (activeCommand_ == ActiveCommandKind::Autorun) {
            console_->appendLine(QStringLiteral("# autorun: enabled"));
        }
        console_->appendLine(QString());
        statusBar()->showMessage(activeCommand_ == ActiveCommandKind::Autorun
            ? QStringLiteral("Autorun running...")
            : QStringLiteral("Running %1").arg(commandLine));
    });

    connect(runner_, &TsRunner::outputReady, console_, &ConsolePane::appendText);

    connect(runner_, &TsRunner::diagnosticsReady, this, [this](const QVector<Diagnostic> &diagnostics) {
        editor_->setDiagnostics(diagnostics);
        if (diagnostics.isEmpty()) {
            console_->appendLine(QStringLiteral("\nNo TypeScript diagnostics."));
        } else {
            console_->appendLine(QStringLiteral("\n%1 diagnostic(s).").arg(diagnostics.size()));
        }
    });

    connect(runner_, &TsRunner::finished, this, [this](int exitCode, bool crashed) {
        activeCommand_ = ActiveCommandKind::None;
        setBusy(false);
        statusBar()->showMessage(crashed
            ? QStringLiteral("Process crashed")
            : QStringLiteral("Process exited with code %1").arg(exitCode));

        if (autorunPendingAfterStop_) {
            autorunPendingAfterStop_ = false;
            if (autorunAction_->isChecked()) {
                scheduleAutorun();
            }
        }
    });
}
