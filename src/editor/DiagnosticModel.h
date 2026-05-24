#pragma once

#include <QJsonArray>
#include <QString>
#include <QVector>

struct Diagnostic
{
    QString file;
    int line = 1;
    int column = 1;
    int endLine = 1;
    int endColumn = 2;
    QString severity = "error";
    QString code;
    QString message;
};

class DiagnosticModel final
{
public:
    static QVector<Diagnostic> parseCompilerOutput(const QString &output, const QString &currentFilePath);
    static QJsonArray toMonacoMarkers(const QVector<Diagnostic> &diagnostics);
};

Q_DECLARE_METATYPE(Diagnostic)
Q_DECLARE_METATYPE(QVector<Diagnostic>)
