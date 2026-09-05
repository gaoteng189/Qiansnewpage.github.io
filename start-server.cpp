// ============================================================
//  Message Board Console (Qt6 GUI)
//  M3 棕色扁平化设计语言，启动/停止后端 + Cloudflare 隧道
// ============================================================
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QProcess>
#include <QFile>
#include <QRegularExpression>
#include <QDir>

#include <windows.h>
#include <shellapi.h>

// ---------- M3 棕色 QSS ----------
static const char* M3_QSS = R"(
QWidget {
    background-color: #FDF8F5;
    color: #1F1B16;
    font-family: "Segoe UI", "Microsoft YaHei", "微软雅黑";
    font-size: 13px;
}
#titleLabel {
    font-size: 19px;
    font-weight: bold;
    color: #1F1B16;
    background: transparent;
}
#subtitleLabel {
    color: #52443C;
    background: transparent;
}
#cardFrame {
    background-color: #F6ECE7;
    border-radius: 20px;
}
QLabel {
    background: transparent;
}
#statusValue {
    color: #1F1B16;
    font-weight: bold;
}
#urlValue {
    color: #8D6E63;
    font-weight: bold;
}
QPushButton {
    background-color: #8D6E63;
    color: #FFFFFF;
    border: none;
    border-radius: 20px;
    padding: 10px 28px;
    font-weight: bold;
    font-size: 14px;
}
QPushButton:hover {
    background-color: #7A5C52;
}
QPushButton:pressed {
    background-color: #6D5148;
}
QPushButton:disabled {
    background-color: #C9B7AE;
}
#stopBtn, #setPortBtn {
    background-color: #EFDBD1;
    color: #3E2723;
}
#stopBtn:hover, #setPortBtn:hover {
    background-color: #E3C8BA;
}
QLineEdit {
    background-color: #FFFFFF;
    border: 1px solid #85736A;
    border-radius: 12px;
    padding: 8px 12px;
    font-size: 14px;
}
QLineEdit:focus {
    border: 2px solid #8D6E63;
}
)";

static QString g_root;
static int g_port = 50304;
static QString g_tunnelUrl;
static QProcess* g_node = nullptr;
static QProcess* g_tunnel = nullptr;

// ---------- 文件读写（UTF-8） ----------
static QString readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

static void writeFile(const QString& path, const QString& content) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(content.toUtf8());
        f.close();
    }
}

// ---------- 端口持久化 ----------
static void loadPort() {
    QString saved = readFile(g_root + "/.server-port").trimmed();
    bool ok = false;
    int p = saved.toInt(&ok);
    if (ok && p >= 1 && p <= 65535) g_port = p;
}

static void savePort(int p) {
    writeFile(g_root + "/.server-port", QString::number(p));
}

// ---------- WS_URL ----------
static void setWsUrl(const QString& url) {
    QString path = g_root + "/message/index.html";
    QString content = readFile(path);
    QRegularExpression re("var WS_URL = 'wss://[^']+';");
    content.replace(re, "var WS_URL = '" + url + "';");
    writeFile(path, content);
}

static void resetWsUrl() {
    setWsUrl("wss://YOUR-TUNNEL.trycloudflare.com");
}

// ---------- 提权 ----------
static bool isElevated() {
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION e{};
        DWORD sz = sizeof(e);
        if (GetTokenInformation(token, TokenElevation, &e, sz, &sz))
            elevated = e.TokenIsElevated;
        CloseHandle(token);
    }
    return elevated != FALSE;
}

// ---------- 主窗口 ----------
class MainWindow : public QWidget {
public:
    MainWindow() {
        setWindowTitle("Message Board Console");
        setFixedSize(520, 480);

        auto* title = new QLabel("Message Board Console", this);
        title->setObjectName("titleLabel");
        auto* subtitle = new QLabel("Message board backend + Cloudflare Tunnel", this);
        subtitle->setObjectName("subtitleLabel");

        // 状态卡片
        auto* card = new QFrame(this);
        card->setObjectName("cardFrame");
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(20, 18, 20, 18);
        cardLayout->setSpacing(10);

        portValue = new QLabel(this);
        portValue->setObjectName("statusValue");
        backendValue = new QLabel("Stopped", this);
        backendValue->setObjectName("statusValue");
        tunnelValue = new QLabel("Stopped", this);
        tunnelValue->setObjectName("statusValue");
        urlValue = new QLabel("—", this);
        urlValue->setObjectName("urlValue");
        urlValue->setWordWrap(true);

        auto addRow = [&](const QString& label, QLabel* value) {
            auto* row = new QHBoxLayout();
            auto* l = new QLabel(label, this);
            l->setFixedWidth(110);
            l->setStyleSheet("color:#52443C; background:transparent;");
            row->addWidget(l);
            row->addWidget(value, 1);
            cardLayout->addLayout(row);
        };
        addRow("Port", portValue);
        addRow("Backend (node)", backendValue);
        addRow("Tunnel", tunnelValue);
        addRow("Tunnel URL", urlValue);

        // 按钮行
        startBtn = new QPushButton("Start", this);
        stopBtn = new QPushButton("Stop", this);
        stopBtn->setObjectName("stopBtn");
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch();
        btnRow->addWidget(startBtn);
        btnRow->addWidget(stopBtn);
        btnRow->addStretch();

        // 端口行
        auto* portRow = new QHBoxLayout();
        auto* portLabel = new QLabel("Port:", this);
        portLabel->setStyleSheet("color:#52443C; background:transparent;");
        portEdit = new QLineEdit(this);
        portEdit->setText(QString::number(g_port));
        portEdit->setFixedWidth(100);
        portEdit->setAlignment(Qt::AlignCenter);
        setPortBtn = new QPushButton("Apply", this);
        setPortBtn->setObjectName("setPortBtn");
        portRow->addStretch();
        portRow->addWidget(portLabel);
        portRow->addWidget(portEdit);
        portRow->addWidget(setPortBtn);
        portRow->addStretch();

        // 提示
        hint = new QLabel("", this);
        hint->setStyleSheet("color:#52443C; background:transparent;");
        hint->setAlignment(Qt::AlignCenter);

        // 主布局
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(28, 24, 28, 20);
        layout->setSpacing(14);
        layout->addWidget(title);
        layout->addWidget(subtitle);
        layout->addSpacing(4);
        layout->addWidget(card);
        layout->addSpacing(4);
        layout->addLayout(btnRow);
        layout->addLayout(portRow);
        layout->addWidget(hint);
        layout->addStretch();

        connect(startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
        connect(stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);
        connect(setPortBtn, &QPushButton::clicked, this, &MainWindow::onSetPort);

        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::refreshStatus);
        timer->start(1000);

        refreshStatus();
    }

private slots:
    void onStart() {
        if (isRunning(g_node)) {
            hint->setText("Services are already running.");
            return;
        }
        hint->setText("Starting backend...");

        g_node = new QProcess(this);
        g_node->setWorkingDirectory(g_root);
        g_node->start("node", { "server.js", QString::number(g_port) });

        QTimer::singleShot(800, this, [this]() { startTunnel(); });
    }

    void startTunnel() {
        g_tunnel = new QProcess(this);
        g_tunnel->setWorkingDirectory(g_root);
        g_tunnel->setProcessChannelMode(QProcess::MergedChannels);
        connect(g_tunnel, &QProcess::readyReadStandardOutput, this, [this]() {
            static QString buffer;
            buffer += QString::fromUtf8(g_tunnel->readAllStandardOutput());
            QRegularExpression re("https://([a-z0-9-]+\\.trycloudflare\\.com)");
            auto m = re.match(buffer);
            if (m.hasMatch()) {
                QString host = m.captured(1);
                g_tunnelUrl = "https://" + host;
                setWsUrl("wss://" + host);
                hint->setText("Tunnel ready.");
                refreshStatus();
                buffer.clear();
            }
        });
        g_tunnel->start("cloudflared", { "tunnel", "--url", "http://localhost:" + QString::number(g_port) });
        hint->setText("Waiting for tunnel URL...");
    }

    void onStop() {
        stopServices();
        hint->setText("Services stopped. URL reset to placeholder.");
        refreshStatus();
    }

    void onSetPort() {
        bool ok = false;
        int p = portEdit->text().toInt(&ok);
        if (ok && p >= 1 && p <= 65535) {
            g_port = p;
            savePort(p);
            portEdit->setText(QString::number(p));
            hint->setText("Port set to " + QString::number(p));
        } else {
            hint->setText("Invalid port (1-65535).");
        }
        refreshStatus();
    }

    void refreshStatus() {
        portValue->setText(QString::number(g_port));
        backendValue->setText(isRunning(g_node) ? "Running" : "Stopped");
        tunnelValue->setText(isRunning(g_tunnel) ? "Running" : "Stopped");
        urlValue->setText(g_tunnelUrl.isEmpty() ? "—" : g_tunnelUrl);
    }

private:
    bool isRunning(QProcess* p) {
        return p && p->state() != QProcess::NotRunning;
    }

    void stopServices() {
        if (isRunning(g_tunnel)) {
            g_tunnel->kill();
            g_tunnel->waitForFinished(2000);
            g_tunnel->deleteLater();
            g_tunnel = nullptr;
        }
        if (isRunning(g_node)) {
            g_node->kill();
            g_node->waitForFinished(2000);
            g_node->deleteLater();
            g_node = nullptr;
        }
        g_tunnelUrl.clear();
        resetWsUrl();
    }

    QLabel* portValue;
    QLabel* backendValue;
    QLabel* tunnelValue;
    QLabel* urlValue;
    QLabel* hint;
    QPushButton* startBtn;
    QPushButton* stopBtn;
    QPushButton* setPortBtn;
    QLineEdit* portEdit;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyleSheet(M3_QSS);
    QString appDir = QCoreApplication::applicationDirPath();
    // 若 exe 位于 bin/ 等子目录，则根目录指向 server.js 所在的上级目录
    if (QFile::exists(appDir + "/server.js")) {
        g_root = appDir;
    } else {
        QDir dir(appDir);
        dir.cdUp();
        g_root = dir.absolutePath();
    }
    loadPort();

    if (!isElevated()) {
        wchar_t buf[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        ShellExecuteW(nullptr, L"runas", buf, nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    MainWindow w;
    w.show();
    return app.exec();
}

