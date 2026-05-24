#pragma once

#include "editor/DiagnosticModel.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

class TsRunner : public QObject
{
    Q_OBJECT

public:
    explicit TsRunner(QObject *parent = nullptr);
    ~TsRunner() override;

    bool isRunning() const;
    QString workspacePath() const;
    QString currentFilePath() const;

public slots:
    void run(const QString &code);
    void typecheck(const QString &code);
    void stop();

signals:
    void commandStarted(const QString &commandLine, const QString &workingDirectory);
    void outputReady(const QString &text);
    void diagnosticsReady(const QVector<Diagnostic> &diagnostics);
    void finished(int exitCode, bool crashed);

private:
    enum class JobKind {
        Run,
        Typecheck
    };

    struct ToolCommand {
        QString program;
        QStringList arguments;
        QString label;
    };

    bool writeCurrentFile(const QString &code);
    bool writeWorkspaceSupportFiles();
    void startProcess(JobKind jobKind, const ToolCommand &command);
    void failFast(const QString &message);
    void finishProcess(int exitCode, QProcess::ExitStatus status);
    void handleProcessBytes(const QByteArray &bytes);
    void handleProcessTimeout();
    void requestStop(const QString &reason);
    void forceKill();
    ToolCommand resolveTool(const QString &toolName, const QStringList &arguments) const;
    ToolCommand typecheckerCommand() const;
    QString displayCommand(const ToolCommand &command) const;
    QString localBinPath(const QString &root, const QString &toolName) const;
    QString pnpmProgram() const;
    QString npmProgram() const;
    QString discoverProjectRoot() const;
    QProcessEnvironment processEnvironment() const;

    QTemporaryDir workspace_;
    QProcess *process_ = nullptr;
    QTimer processTimer_;
    QTimer killTimer_;
    QByteArray outputBuffer_;
    QString fileName_ = QStringLiteral("main.tsx");
    QString projectRoot_;
    JobKind currentJob_ = JobKind::Run;
    qsizetype emittedOutputBytes_ = 0;
    bool outputCapped_ = false;
};
