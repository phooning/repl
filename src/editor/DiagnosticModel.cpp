#include "editor/DiagnosticModel.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

QString normalizedPathForComparison(const QString &reportedFile, const QString &currentFilePath)
{
    if (reportedFile.isEmpty() || currentFilePath.isEmpty()) {
        return {};
    }

    const QFileInfo currentInfo(currentFilePath);
    QFileInfo reportedInfo(reportedFile);
    if (reportedInfo.isRelative()) {
        reportedInfo = QFileInfo(currentInfo.dir(), reportedFile);
    }

    const QString canonical = reportedInfo.canonicalFilePath();
    if (!canonical.isEmpty()) {
        return QDir::cleanPath(canonical);
    }
    return QDir::cleanPath(reportedInfo.absoluteFilePath());
}

bool refersToCurrentFile(const QString &reportedFile, const QString &currentFilePath)
{
    const QFileInfo currentInfo(currentFilePath);
    const QString currentCanonical = currentInfo.canonicalFilePath();
    const QString currentPath = QDir::cleanPath(currentCanonical.isEmpty()
        ? currentInfo.absoluteFilePath()
        : currentCanonical);
    return normalizedPathForComparison(reportedFile, currentFilePath) == currentPath;
}

Diagnostic makeDiagnostic(const QRegularExpressionMatch &match)
{
    Diagnostic diagnostic;
    diagnostic.file = match.captured(1).trimmed();
    diagnostic.line = match.captured(2).toInt();
    diagnostic.column = match.captured(3).toInt();
    diagnostic.endLine = diagnostic.line;
    diagnostic.endColumn = diagnostic.column + 1;
    diagnostic.severity = match.captured(4).toLower();
    diagnostic.code = match.captured(5).trimmed();
    diagnostic.message = match.captured(6).trimmed();
    return diagnostic;
}

} // namespace

QVector<Diagnostic> DiagnosticModel::parseCompilerOutput(const QString &output, const QString &currentFilePath)
{
    QVector<Diagnostic> diagnostics;

    const QRegularExpression classicPattern(
        R"(^(.+?)\((\d+),(\d+)\):\s+(error|warning)\s+(TS\d+):\s+(.+)$)",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression colonPattern(
        R"(^(.+?):(\d+):(\d+)\s+-?\s*(error|warning)\s+(TS\d+):\s+(.+)$)",
        QRegularExpression::CaseInsensitiveOption);

    const QStringList lines = output.split('\n');
    diagnostics.reserve(lines.size());

    qsizetype lastDiagnosticIndex = -1;
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QRegularExpressionMatch match = classicPattern.match(line);
        if (!match.hasMatch()) {
            match = colonPattern.match(line);
        }

        if (match.hasMatch()) {
            Diagnostic diagnostic = makeDiagnostic(match);
            if (refersToCurrentFile(diagnostic.file, currentFilePath)) {
                diagnostics.push_back(diagnostic);
                lastDiagnosticIndex = diagnostics.size() - 1;
            } else {
                lastDiagnosticIndex = -1;
            }
            continue;
        }

        if (lastDiagnosticIndex >= 0 && (rawLine.startsWith(' ') || rawLine.startsWith('\t'))) {
            diagnostics[lastDiagnosticIndex].message += '\n' + line;
        }
    }

    return diagnostics;
}

QJsonArray DiagnosticModel::toMonacoMarkers(const QVector<Diagnostic> &diagnostics)
{
    QJsonArray markers;
    for (const Diagnostic &diagnostic : diagnostics) {
        QJsonObject marker;
        marker["startLineNumber"] = qMax(1, diagnostic.line);
        marker["startColumn"] = qMax(1, diagnostic.column);
        marker["endLineNumber"] = qMax(1, diagnostic.endLine);
        marker["endColumn"] = qMax(qMax(1, diagnostic.column) + 1, diagnostic.endColumn);
        marker["severity"] = diagnostic.severity == "warning" ? 4 : 8;
        marker["source"] = "TypeScript";

        QString message = diagnostic.message;
        if (!diagnostic.code.isEmpty()) {
            message = diagnostic.code + ": " + message;
        }
        marker["message"] = message;
        markers.push_back(marker);
    }
    return markers;
}
