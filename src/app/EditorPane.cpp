#include "app/EditorPane.h"

#include "editor/DiagnosticModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <memory>

namespace {

QString jsonStringLiteral(const QString &value)
{
    QJsonArray wrapper;
    wrapper.push_back(value);
    QString json = QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));
    json.remove(0, 1);
    json.chop(1);
    return json;
}

QString defaultSource()
{
    return QStringLiteral(
        "type Greeting = { name: string };\n"
        "\n"
        "const greet = ({ name }: Greeting) => `Hello, ${name}!`;\n"
        "\n"
        "console.log(greet({ name: \"TypeScript\" }));\n");
}

} // namespace

class EditorBridge : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void notifyContentChanged()
    {
        emit contentChanged();
    }

signals:
    void contentChanged();
};

EditorPane::EditorPane(const QString &currentFilePath, QWidget *parent)
    : QWidget(parent)
    , bridge_(new EditorBridge(this))
    , channel_(new QWebChannel(this))
    , view_(new QWebEngineView(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_);

    channel_->registerObject(QStringLiteral("replBridge"), bridge_);
    view_->page()->setWebChannel(channel_);

    connect(bridge_, &EditorBridge::contentChanged, this, &EditorPane::contentChanged);
    connect(view_, &QWebEngineView::loadFinished, this, [this](bool ok) {
        emit readyChanged(ok);
        applyVimModePreference();
    });

    view_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    view_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);

    const QString basePath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets/editor.html"));
    view_->setHtml(editorHtml(currentFilePath), QUrl::fromLocalFile(basePath));
}

void EditorPane::currentText(std::function<void(const QString &, bool)> callback)
{
    QPointer<QWebEnginePage> page = view_->page();
    if (!page) {
        callback({}, false);
        return;
    }

    auto delivered = std::make_shared<bool>(false);
    QTimer::singleShot(2000, this, [page, delivered, callback]() mutable {
        if (*delivered) {
            return;
        }
        *delivered = true;
        callback({}, false);
    });

    page->runJavaScript(
        QStringLiteral("window.replGetValue ? window.replGetValue() : '';"),
        [page, delivered, callback = std::move(callback)](const QVariant &result) mutable {
            if (*delivered) {
                return;
            }
            *delivered = true;
            callback(result.toString(), page && result.isValid());
        });
}

void EditorPane::setDiagnostics(const QVector<Diagnostic> &diagnostics)
{
    if (!view_->page()) {
        return;
    }
    const QJsonDocument document(DiagnosticModel::toMonacoMarkers(diagnostics));
    const QString markers = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    view_->page()->runJavaScript(QStringLiteral("window.replSetDiagnostics && window.replSetDiagnostics(%1);").arg(markers));
}

void EditorPane::clearDiagnostics()
{
    if (!view_->page()) {
        return;
    }
    view_->page()->runJavaScript(QStringLiteral("window.replSetDiagnostics && window.replSetDiagnostics([]);"));
}

void EditorPane::setVimModeEnabled(bool enabled)
{
    vimModeEnabled_ = enabled;
    applyVimModePreference();
}

void EditorPane::applyVimModePreference()
{
    if (!view_->page()) {
        return;
    }

    const QString enabled = vimModeEnabled_ ? QStringLiteral("true") : QStringLiteral("false");
    view_->page()->runJavaScript(QStringLiteral(
                                     "(function () {"
                                     "window.replVimDesired = %1;"
                                     "if (window.replSetVimModeEnabled) {"
                                     "window.replSetVimModeEnabled(%1);"
                                     "} else if (window.replApplyVimMode) {"
                                     "window.replApplyVimMode();"
                                     "}"
                                     "})();")
                                     .arg(enabled));
}

QString EditorPane::editorHtml(const QString &currentFilePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString monacoVsUrl = QUrl::fromLocalFile(appDir.filePath(QStringLiteral("assets/monaco/vs"))).toString();
    const QString monacoLoaderUrl = QUrl::fromLocalFile(appDir.filePath(QStringLiteral("assets/monaco/vs/loader.js"))).toString();
    const QString vimAdapterUrl = QUrl::fromLocalFile(appDir.filePath(QStringLiteral("assets/editor/monaco-vim.js"))).toString();
    const QString modelUri = QUrl::fromLocalFile(currentFilePath).toString();

    QString html = QStringLiteral(R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body, #editor {
      background: #1e1e1e;
      height: 100%;
      margin: 0;
      overflow: hidden;
      width: 100%;
    }
    #fallback {
      background: #1e1e1e;
      border: 0;
      box-sizing: border-box;
      color: #d4d4d4;
      font: 13px/1.55 "JetBrains Mono", "SFMono-Regular", Consolas, monospace;
      height: 100%;
      outline: none;
      padding: 14px 16px;
      resize: none;
      width: 100%;
    }
	    #load-error {
	      background: #2a160d;
	      border-bottom: 1px solid #7a3d16;
	      color: #ffd7ba;
      display: none;
      font: 12px system-ui, sans-serif;
      left: 0;
      padding: 6px 10px;
      position: absolute;
      right: 0;
	      top: 0;
	      z-index: 10;
	    }
	    #vim-status {
	      background: #161b22;
	      border-top: 1px solid #30363d;
	      bottom: 0;
	      color: #9da7b1;
	      display: none;
	      font: 12px "JetBrains Mono", "SFMono-Regular", Consolas, monospace;
	      left: 0;
	      padding: 3px 8px;
	      position: absolute;
	      right: 0;
	      z-index: 11;
	    }
	  </style>
	</head>
	<body>
	  <div id="vim-status"></div>
	  <div id="load-error"></div>
	  <div id="editor"></div>
	  <script src="qrc:///qtwebchannel/qwebchannel.js"></script>
	  <script>
	    const initialCode = __INITIAL_CODE__;
	    const monacoVsUrl = __MONACO_VS_URL__;
	    const monacoLoaderUrl = __MONACO_LOADER_URL__;
	    const vimAdapterUrl = __VIM_ADAPTER_URL__;
	    const modelUri = __MODEL_URI__;
	    window.replBridge = null;
	    window.replBridgeConnectAttempts = 0;
	    window.replBridgeConnecting = false;
	    window.replEditor = null;
	    window.replPendingMarkers = [];
	    window.replVimDesired = window.replVimDesired || false;
	    window.replVimAdapter = null;
	    window.replVimStatusNode = null;
	    window.replVimAvailable = false;
	    window.replVimLoadStarted = false;
	    window.replVimLoadFailed = false;
	    window.replLoadErrorKind = null;

    function connectQtBridge() {
      if (window.replBridge || window.replBridgeConnecting) {
        return;
      }

      if (!window.qt || !window.qt.webChannelTransport || !window.QWebChannel) {
        if (window.replBridgeConnectAttempts < 80) {
          window.replBridgeConnectAttempts += 1;
          window.setTimeout(connectQtBridge, 50);
        }
        return;
      }

      window.replBridgeConnecting = true;
      new QWebChannel(window.qt.webChannelTransport, function (channel) {
        window.replBridge = channel.objects.replBridge;
        window.replBridgeConnecting = false;
      });
    }

    function notifyContentChanged() {
      if (!window.replBridge) {
        connectQtBridge();
      }
      if (window.replBridge && window.replBridge.notifyContentChanged) {
        window.replBridge.notifyContentChanged();
      }
    }

    connectQtBridge();

    window.replGetValue = function () {
      if (window.replEditor && typeof window.replEditor.getValue === 'function') {
        return window.replEditor.getValue();
      }
      const fallback = document.getElementById('fallback');
      return fallback ? fallback.value : initialCode;
    };

	    window.replSetDiagnostics = function (markers) {
	      window.replPendingMarkers = markers || [];
	      if (!window.monaco || !window.replEditor || !window.replEditor.getModel) {
	        return;
      }
      window.monaco.editor.setModelMarkers(
        window.replEditor.getModel(),
        'typescript',
        window.replPendingMarkers
	      );
	    };

	    function showLoadError(message, kind) {
	      const errorNode = document.getElementById('load-error');
	      if (!errorNode) {
	        return;
	      }

	      window.replLoadErrorKind = kind || 'general';
	      errorNode.textContent = message;
	      errorNode.style.display = 'block';
	    }

	    function clearVimLoadError() {
	      if (window.replLoadErrorKind !== 'vim') {
	        return;
	      }

	      const errorNode = document.getElementById('load-error');
	      if (errorNode) {
	        errorNode.textContent = '';
	        errorNode.style.display = 'none';
	      }
	      window.replLoadErrorKind = null;
	    }

	    function disposeVimMode() {
	      try {
	        if (window.replVimAdapter && typeof window.replVimAdapter.dispose === 'function') {
	          window.replVimAdapter.dispose();
	        }
	      } finally {
	        window.replVimAdapter = null;

	        const statusNode = document.getElementById('vim-status');
	        if (statusNode) {
	          statusNode.style.display = 'none';
	          statusNode.textContent = '';
	        }
	      }
	    }

	    function loadVimModeScript() {
	      if (window.replVimLoadStarted || window.replVimLoadFailed) {
	        return;
	      }

	      window.replVimLoadStarted = true;
	      const hadDefine = Object.prototype.hasOwnProperty.call(window, 'define');
	      const previousDefine = window.define;
	      const script = document.createElement('script');

	      window.define = undefined;
	      script.src = vimAdapterUrl;
	      script.onload = function () {
	        if (hadDefine) {
	          window.define = previousDefine;
	        } else {
	          delete window.define;
	        }

	        window.ReplMonacoVim = window.ReplMonacoVim || window.MonacoVim;
	        if (!window.ReplMonacoVim || typeof window.ReplMonacoVim.initVimMode !== 'function') {
	          window.replVimLoadFailed = true;
	          showLoadError('Vim mode is unavailable because monaco-vim failed to initialize.', 'vim');
	          return;
	        }

	        window.replVimAvailable = true;
	        clearVimLoadError();
	        window.replApplyVimMode();
	      };
	      script.onerror = function () {
	        if (hadDefine) {
	          window.define = previousDefine;
	        } else {
	          delete window.define;
	        }

	        window.replVimLoadFailed = true;
	        showLoadError('Vim mode is unavailable because monaco-vim failed to load.', 'vim');
	      };
	      document.head.appendChild(script);
	    }

	    function applyVimMode() {
	      if (!window.replEditor || !window.replEditor.getModel) {
	        return;
	      }

	      if (!window.replVimDesired) {
	        disposeVimMode();
	        clearVimLoadError();
	        return;
	      }

	      if (window.replVimAdapter) {
	        return;
	      }

	      window.ReplMonacoVim = window.ReplMonacoVim || window.MonacoVim;
	      if (!window.ReplMonacoVim || typeof window.ReplMonacoVim.initVimMode !== 'function') {
	        if (window.replVimLoadFailed) {
	          showLoadError('Vim mode is unavailable because monaco-vim failed to load.', 'vim');
	          return;
	        }
	        loadVimModeScript();
	        return;
	      }

	      const statusNode = document.getElementById('vim-status');
	      window.replVimStatusNode = statusNode;
	      if (statusNode) {
	        statusNode.style.display = 'block';
	      }

	      try {
	        window.replVimAdapter = window.ReplMonacoVim.initVimMode(
	          window.replEditor,
	          statusNode
	        );
	        window.replVimAvailable = true;
	        clearVimLoadError();
	      } catch (error) {
	        disposeVimMode();
	        showLoadError(
	          'Vim mode is unavailable because monaco-vim failed to initialize.',
	          'vim'
	        );
	      }
	    }

	    window.replApplyVimMode = applyVimMode;
	    window.replSetVimModeEnabled = function (enabled) {
	      window.replVimDesired = !!enabled;
	      applyVimMode();
	    };

	    function startFallback(message) {
	      disposeVimMode();
	      window.replVimAvailable = false;

	      const editorNode = document.getElementById('editor');
	      editorNode.innerHTML = '';

      const textarea = document.createElement('textarea');
      textarea.id = 'fallback';
      textarea.spellcheck = false;
      textarea.value = window.replGetValue();
      editorNode.appendChild(textarea);

      window.replEditor = {
        getValue: function () { return textarea.value; },
        setValue: function (value) { textarea.value = value; }
      };
      textarea.addEventListener('input', notifyContentChanged);

	      showLoadError(message + ' Vim mode is unavailable in fallback editor.', 'fallback');
	    }
  </script>
  <script>
    const loader = document.createElement('script');
    loader.src = monacoLoaderUrl;
    loader.onload = function () {
      window.require.config({
        paths: {
          vs: monacoVsUrl
        }
      });

      window.require(['vs/editor/editor.main'], function () {
        const model = window.monaco.editor.createModel(
          initialCode,
          'typescript',
          window.monaco.Uri.parse(modelUri)
        );

        const moduleKind = window.monaco.languages.typescript.ModuleKind.NodeNext
          || window.monaco.languages.typescript.ModuleKind.ESNext;
        const moduleResolutionKind = window.monaco.languages.typescript.ModuleResolutionKind.NodeNext
          || window.monaco.languages.typescript.ModuleResolutionKind.NodeJs;

        window.monaco.languages.typescript.typescriptDefaults.setCompilerOptions({
          target: window.monaco.languages.typescript.ScriptTarget.ES2022,
          module: moduleKind,
          moduleResolution: moduleResolutionKind,
          jsx: window.monaco.languages.typescript.JsxEmit.ReactJSX,
          allowNonTsExtensions: true,
          noEmit: true,
          noErrorTruncation: true,
          strict: true
        });

        window.replEditor = window.monaco.editor.create(document.getElementById('editor'), {
          automaticLayout: true,
          fontFamily: "'JetBrains Mono', 'SFMono-Regular', Consolas, monospace",
          fontSize: 13,
          minimap: { enabled: false },
          model,
          scrollBeyondLastLine: false,
          theme: 'vs-dark'
        });

	        window.replEditor.onDidChangeModelContent(notifyContentChanged);
	        window.replSetDiagnostics(window.replPendingMarkers);
	        window.replApplyVimMode();
	      }, function (error) {
	        startFallback('Monaco failed to load: ' + (error && error.message ? error.message : 'unknown error'));
      });
    };
    loader.onerror = function () {
      startFallback('Local Monaco bundle is unavailable; using a plain editor fallback.');
    };
    document.head.appendChild(loader);
  </script>
</body>
</html>
)HTML");

    html.replace(QStringLiteral("__INITIAL_CODE__"), jsonStringLiteral(defaultSource()));
    html.replace(QStringLiteral("__MONACO_VS_URL__"), jsonStringLiteral(monacoVsUrl));
    html.replace(QStringLiteral("__MONACO_LOADER_URL__"), jsonStringLiteral(monacoLoaderUrl));
    html.replace(QStringLiteral("__VIM_ADAPTER_URL__"), jsonStringLiteral(vimAdapterUrl));
    html.replace(QStringLiteral("__MODEL_URI__"), jsonStringLiteral(modelUri));
    return html;
}

#include "EditorPane.moc"
