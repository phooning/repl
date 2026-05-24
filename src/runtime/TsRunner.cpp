#include "runtime/TsRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

constexpr qsizetype kMaxOutputBytes = 1024 * 1024;
constexpr int kRunTimeoutMs = 15000;
constexpr int kTypecheckTimeoutMs = 30000;
constexpr int kKillGraceMs = 1500;

QString quoteForDisplay(const QString &value)
{
    if (!value.contains(' ')) {
        return value;
    }
    QString quoted = value;
    quoted.replace('"', "\\\"");
    return '"' + quoted + '"';
}

bool writeTextFile(const QString &path, const QString &contents)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(contents.toUtf8());
    return file.commit();
}

} // namespace

TsRunner::TsRunner(QObject *parent)
    : QObject(parent)
    , projectRoot_(discoverProjectRoot())
{
    qRegisterMetaType<QVector<Diagnostic>>("QVector<Diagnostic>");

    processTimer_.setSingleShot(true);
    killTimer_.setSingleShot(true);
    connect(&processTimer_, &QTimer::timeout, this, &TsRunner::handleProcessTimeout);
    connect(&killTimer_, &QTimer::timeout, this, &TsRunner::forceKill);

    writeWorkspaceSupportFiles();
}

TsRunner::~TsRunner()
{
    if (!process_) {
        return;
    }

    processTimer_.stop();
    killTimer_.stop();
    process_->kill();
    process_->waitForFinished(kKillGraceMs);
}

bool TsRunner::isRunning() const
{
    return process_ && process_->state() != QProcess::NotRunning;
}

QString TsRunner::workspacePath() const
{
    return workspace_.path();
}

QString TsRunner::currentFilePath() const
{
    return QDir(workspace_.path()).filePath(fileName_);
}

void TsRunner::run(const QString &code)
{
    if (isRunning()) {
        failFast(QStringLiteral("A process is already running. Use Stop before starting another command.\n"));
        return;
    }

    if (!writeCurrentFile(code)) {
        failFast(QStringLiteral("Unable to prepare temp workspace at %1\n").arg(workspace_.path()));
        return;
    }

    const ToolCommand tsx = resolveTool(QStringLiteral("tsx"), {fileName_});
    if (tsx.program.isEmpty()) {
        failFast(QStringLiteral("tsx was not found in node_modules/.bin, pnpm/npm exec, PATH, or /usr/bin.\n"));
        return;
    }

    startProcess(JobKind::Run, tsx);
}

void TsRunner::typecheck(const QString &code)
{
    if (isRunning()) {
        failFast(QStringLiteral("A process is already running. Use Stop before starting another command.\n"));
        return;
    }

    if (!writeCurrentFile(code)) {
        failFast(QStringLiteral("Unable to prepare temp workspace at %1\n").arg(workspace_.path()));
        return;
    }

    const ToolCommand checker = typecheckerCommand();
    if (checker.program.isEmpty()) {
        failFast(QStringLiteral("Neither tsgo nor tsc was found in node_modules/.bin, pnpm/npm exec, PATH, or /usr/bin.\n"));
        return;
    }

    startProcess(JobKind::Typecheck, checker);
}

void TsRunner::stop()
{
    requestStop(QStringLiteral("Stopping process."));
}

bool TsRunner::writeCurrentFile(const QString &code)
{
    if (!workspace_.isValid()) {
        return false;
    }
    return writeWorkspaceSupportFiles() && writeTextFile(currentFilePath(), code);
}

bool TsRunner::writeWorkspaceSupportFiles()
{
    if (!workspace_.isValid()) {
        return false;
    }

    QDir dir(workspace_.path());
    if (!dir.mkpath(QStringLiteral("tmp"))) {
        return false;
    }

    const bool packageOk = writeTextFile(
        dir.filePath(QStringLiteral("package.json")),
        QStringLiteral(
            "{\n"
            "  \"private\": true,\n"
            "  \"type\": \"module\"\n"
            "}\n"));

    const bool tsconfigOk = writeTextFile(
        dir.filePath(QStringLiteral("tsconfig.json")),
        QStringLiteral(
            "{\n"
            "  \"compilerOptions\": {\n"
            "    \"allowSyntheticDefaultImports\": true,\n"
            "    \"esModuleInterop\": true,\n"
            "    \"jsx\": \"react-jsx\",\n"
            "    \"lib\": [\"ES2022\", \"DOM\"],\n"
            "    \"module\": \"NodeNext\",\n"
            "    \"moduleResolution\": \"NodeNext\",\n"
            "    \"noEmit\": true,\n"
            "    \"noErrorTruncation\": true,\n"
            "    \"skipLibCheck\": true,\n"
            "    \"strict\": true,\n"
            "    \"target\": \"ES2022\"\n"
            "  },\n"
            "  \"include\": [\"main.tsx\"]\n"
            "}\n"));

    return packageOk && tsconfigOk;
}

void TsRunner::startProcess(JobKind jobKind, const ToolCommand &command)
{
    if (isRunning()) {
        failFast(QStringLiteral("A process is already running. Use Stop before starting another command.\n"));
        return;
    }

    outputBuffer_.clear();
    emittedOutputBytes_ = 0;
    outputCapped_ = false;
    currentJob_ = jobKind;
    process_ = new QProcess(this);
    process_->setWorkingDirectory(workspace_.path());
    process_->setProcessEnvironment(processEnvironment());
    process_->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process_, &QProcess::readyReadStandardOutput, this, [this]() {
        handleProcessBytes(process_->readAllStandardOutput());
    });

    connect(process_, &QProcess::readyReadStandardError, this, [this]() {
        handleProcessBytes(process_->readAllStandardError());
    });

    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            const QString message = process_
                ? QStringLiteral("Process failed to start: %1\n").arg(process_->errorString())
                : QStringLiteral("Process failed to start.\n");
            emit outputReady(message);
            finishProcess(-1, QProcess::CrashExit);
        }
    });

    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int exitCode, QProcess::ExitStatus status) {
        finishProcess(exitCode, status);
    });

    emit commandStarted(displayCommand(command), workspace_.path());
    processTimer_.start(jobKind == JobKind::Run ? kRunTimeoutMs : kTypecheckTimeoutMs);
    process_->start(command.program, command.arguments);
}

void TsRunner::failFast(const QString &message)
{
    emit outputReady(message);
    emit finished(-1, false);
}

void TsRunner::finishProcess(int exitCode, QProcess::ExitStatus status)
{
    if (!process_) {
        return;
    }

    processTimer_.stop();
    killTimer_.stop();

    if (currentJob_ == JobKind::Typecheck) {
        emit diagnosticsReady(DiagnosticModel::parseCompilerOutput(QString::fromUtf8(outputBuffer_), currentFilePath()));
    }

    process_->deleteLater();
    process_ = nullptr;
    emit finished(exitCode, status == QProcess::CrashExit);
}

void TsRunner::handleProcessBytes(const QByteArray &bytes)
{
    if (bytes.isEmpty() || outputCapped_) {
        return;
    }

    const qsizetype remaining = kMaxOutputBytes - emittedOutputBytes_;
    if (remaining <= 0) {
        outputCapped_ = true;
        requestStop(QStringLiteral("Output limit exceeded; stopping process."));
        return;
    }

    const QByteArray accepted = bytes.left(remaining);
    emittedOutputBytes_ += accepted.size();

    if (currentJob_ == JobKind::Typecheck) {
        outputBuffer_.append(accepted);
    }
    emit outputReady(QString::fromUtf8(accepted));

    if (accepted.size() < bytes.size()) {
        outputCapped_ = true;
        requestStop(QStringLiteral("Output limit exceeded; stopping process."));
    }
}

void TsRunner::handleProcessTimeout()
{
    requestStop(QStringLiteral("Process timed out; stopping process."));
}

void TsRunner::requestStop(const QString &reason)
{
    if (!isRunning()) {
        return;
    }

    emit outputReady(QStringLiteral("\n%1\n").arg(reason));
    process_->terminate();
    killTimer_.start(kKillGraceMs);
}

void TsRunner::forceKill()
{
    if (!isRunning()) {
        return;
    }
    emit outputReady(QStringLiteral("Process did not exit after terminate(); killing it.\n"));
    process_->kill();
}

TsRunner::ToolCommand TsRunner::resolveTool(const QString &toolName, const QStringList &arguments) const
{
    const QString envVar = QStringLiteral("REPL_%1_PATH").arg(toolName.toUpper());
    const QString configured = qEnvironmentVariable(qPrintable(envVar));
    if (!configured.isEmpty() && QFileInfo(configured).isExecutable()) {
        return {configured, arguments, displayCommand({QFileInfo(configured).fileName(), arguments, {}})};
    }

    const QStringList localRoots = {workspace_.path(), projectRoot_};
    for (const QString &root : localRoots) {
        const QString localBin = localBinPath(root, toolName);
        if (!localBin.isEmpty()) {
            return {localBin, arguments, displayCommand({QFileInfo(localBin).fileName(), arguments, {}})};
        }
    }

    const QString pathTool = QStandardPaths::findExecutable(toolName);
    if (!pathTool.isEmpty()) {
        return {pathTool, arguments, displayCommand({QFileInfo(pathTool).fileName(), arguments, {}})};
    }

    const QString archFallback = QStringLiteral("/usr/bin/%1").arg(toolName);
    if (QFileInfo(archFallback).isExecutable()) {
        return {archFallback, arguments, displayCommand({QFileInfo(archFallback).fileName(), arguments, {}})};
    }

    const QString pnpm = pnpmProgram();
    if (!pnpm.isEmpty()) {
        QStringList pnpmArguments = {QStringLiteral("exec"), toolName};
        pnpmArguments.append(arguments);
        QString labelParts = QStringLiteral("pnpm exec %1").arg(toolName);
        for (const QString &argument : arguments) {
            labelParts += ' ' + quoteForDisplay(argument);
        }
        return {pnpm, pnpmArguments, labelParts};
    }

    const QString npm = npmProgram();
    if (!npm.isEmpty()) {
        QStringList npmArguments = {QStringLiteral("exec"), QStringLiteral("--"), toolName};
        npmArguments.append(arguments);
        QString labelParts = QStringLiteral("npm exec -- %1").arg(toolName);
        for (const QString &argument : arguments) {
            labelParts += ' ' + quoteForDisplay(argument);
        }
        return {npm, npmArguments, labelParts};
    }

    return {};
}

TsRunner::ToolCommand TsRunner::typecheckerCommand() const
{
    const QStringList arguments = {
        QStringLiteral("--noEmit"),
        QStringLiteral("--pretty"),
        QStringLiteral("false"),
        QStringLiteral("--noErrorTruncation"),
        QStringLiteral("--project"),
        QStringLiteral("tsconfig.json"),
    };

    ToolCommand tsgo = resolveTool(QStringLiteral("tsgo"), arguments);
    if (!tsgo.program.isEmpty()) {
        return tsgo;
    }
    return resolveTool(QStringLiteral("tsc"), arguments);
}

QString TsRunner::displayCommand(const ToolCommand &command) const
{
    if (!command.label.isEmpty()) {
        return command.label;
    }

    QStringList parts;
    parts.push_back(QFileInfo(command.program).fileName());
    for (const QString &argument : command.arguments) {
        parts.push_back(quoteForDisplay(argument));
    }
    return parts.join(' ');
}

QString TsRunner::localBinPath(const QString &root, const QString &toolName) const
{
    if (root.isEmpty()) {
        return {};
    }

    const QDir binDir(QDir(root).filePath(QStringLiteral("node_modules/.bin")));
    const QString candidate = binDir.filePath(toolName);
    if (QFileInfo(candidate).isExecutable()) {
        return candidate;
    }

#ifdef Q_OS_WIN
    const QString cmdCandidate = binDir.filePath(toolName + QStringLiteral(".cmd"));
    if (QFileInfo(cmdCandidate).isExecutable()) {
        return cmdCandidate;
    }
#endif

    return {};
}

QString TsRunner::pnpmProgram() const
{
    const QStringList names = {
        QStringLiteral("pnpm"),
#ifdef Q_OS_WIN
        QStringLiteral("pnpm.cmd"),
        QStringLiteral("pnpm.exe"),
#endif
    };

    for (const QString &name : names) {
        const QString pathPnpm = QStandardPaths::findExecutable(name);
        if (!pathPnpm.isEmpty()) {
            return pathPnpm;
        }
    }

    const QString homePnpm = QDir::home().filePath(QStringLiteral(".local/share/pnpm/pnpm"));
    if (QFileInfo(homePnpm).isExecutable()) {
        return homePnpm;
    }

    return {};
}

QString TsRunner::npmProgram() const
{
    const QStringList names = {
        QStringLiteral("npm"),
#ifdef Q_OS_WIN
        QStringLiteral("npm.cmd"),
        QStringLiteral("npm.exe"),
#endif
    };

    for (const QString &name : names) {
        const QString pathNpm = QStandardPaths::findExecutable(name);
        if (!pathNpm.isEmpty()) {
            return pathNpm;
        }
    }

    const QString usrBinNpm = QStringLiteral("/usr/bin/npm");
    if (QFileInfo(usrBinNpm).isExecutable()) {
        return usrBinNpm;
    }

    return {};
}

QString TsRunner::discoverProjectRoot() const
{
    const QStringList starts = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath(),
    };

    for (const QString &start : starts) {
        QDir dir(start);
        while (true) {
            if (QFileInfo::exists(dir.filePath(QStringLiteral("package.json")))
                && QFileInfo::exists(dir.filePath(QStringLiteral("node_modules")))) {
                return dir.absolutePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return {};
}

QProcessEnvironment TsRunner::processEnvironment() const
{
    const QProcessEnvironment system = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment environment;

    const QStringList copiedVariables = {
        QStringLiteral("HOME"),
        QStringLiteral("LANG"),
        QStringLiteral("LC_ALL"),
        QStringLiteral("LC_CTYPE"),
        QStringLiteral("USER"),
    };
    for (const QString &variable : copiedVariables) {
        if (system.contains(variable)) {
            environment.insert(variable, system.value(variable));
        }
    }

    QStringList pathParts;
    const QString workspaceBin = QDir(workspace_.path()).filePath(QStringLiteral("node_modules/.bin"));
    if (QFileInfo::exists(workspaceBin)) {
        pathParts.push_back(workspaceBin);
    }
    const QString projectBin = QDir(projectRoot_).filePath(QStringLiteral("node_modules/.bin"));
    if (!projectRoot_.isEmpty() && QFileInfo::exists(projectBin)) {
        pathParts.push_back(projectBin);
    }
    if (system.contains(QStringLiteral("PATH"))) {
        pathParts.push_back(system.value(QStringLiteral("PATH")));
    }
    pathParts.push_back(QStringLiteral("/usr/local/bin"));
    pathParts.push_back(QStringLiteral("/usr/bin"));
    pathParts.push_back(QStringLiteral("/bin"));

    environment.insert(QStringLiteral("PATH"), pathParts.join(QDir::listSeparator()));
    environment.insert(QStringLiteral("TMPDIR"), QDir(workspace_.path()).filePath(QStringLiteral("tmp")));
    environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    environment.insert(QStringLiteral("FORCE_COLOR"), QStringLiteral("0"));

    return environment;
}
