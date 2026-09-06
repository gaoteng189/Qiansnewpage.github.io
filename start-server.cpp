// ============================================================
//  Message Board Console (Qt6 GUI)
//  M3 棕色扁平化设计语言，启动/停止 HTTP 后端
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
static QProcess* g_node = nullptr;
static QString g_funnelUrl;
static const char* TAILSCALE_EXE = "C:\\Program Files\\Tailscale\\tailscale.exe";

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
        auto* subtitle = new QLabel("Message board backend + Tailscale Funnel", this);
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
        boardValue = new QLabel("—", this);
        boardValue->setObjectName("urlValue");
        boardValue->setWordWrap(true);

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
        addRow("Funnel URL", boardValue);

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
    // 启动 Tailscale Funnel（后台模式，公网地址固定为 https://xxx.ts.net）
    void startFunnel() {
        funnelRetry = 0;
        auto* p = new QProcess(this);
        connect(p, &QProcess::finished, p, &QObject::deleteLater);
        p->start(TAILSCALE_EXE, { "funnel", "--bg", QString::number(g_port) });
        QTimer::singleShot(2000, this, [this]() { queryFunnelUrl(); });
    }

    // 查询并解析 Funnel 固定地址
    void queryFunnelUrl() {
        auto* p = new QProcess(this);
        p->setProcessChannelMode(QProcess::MergedChannels);
        connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
            QString out = QString::fromUtf8(p->readAllStandardOutput());
            QString err = QString::fromUtf8(p->readAllStandardError());
            QFile log(g_root + "/funnel-debug.log");
            if (log.open(QIODevice::WriteOnly | QIODevice::Append)) {
                log.write(("exit=" + QString::number(code) + " st=" + QString::number(st) + "\nOUT[" + out + "]\nERR[" + err + "]\n---\n").toUtf8());
                log.close();
            }
            QRegularExpression re("https://[a-z0-9.-]+\\.ts\\.net");
            auto it = re.globalMatch(out);
            if (it.hasNext()) {
                g_funnelUrl = it.next().captured(0);
                hint->setText("Funnel ready: " + g_funnelUrl);
            } else if (funnelRetry < 5) {
                funnelRetry++;
                QTimer::singleShot(2000, this, [this]() { queryFunnelUrl(); });
            } else {
                hint->setText("Funnel URL not detected. Check Tailscale.");
            }
            p->deleteLater();
            refreshStatus();
        });
        p->start(TAILSCALE_EXE, { "funnel", "status" });
    }

    // 关闭 Funnel
    void stopFunnel() {
        auto* p = new QProcess(this);
        connect(p, &QProcess::finished, p, &QObject::deleteLater);
        p->start(TAILSCALE_EXE, { "funnel", "reset" });
    }

    void onStart() {
        if (isRunning(g_node)) {
            hint->setText("Services are already running.");
            return;
        }
        hint->setText("Starting backend...");

        g_node = new QProcess(this);
        g_node->setWorkingDirectory(g_root);
        g_node->start("node", { "server.js", QString::number(g_port) });

        startFunnel();
        hint->setText("Backend started. Starting Tailscale Funnel...");
    }

    void onStop() {
        stopServices();
        hint->setText("Backend stopped.");
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
        boardValue->setText(g_funnelUrl.isEmpty() ? "—" : g_funnelUrl + "/message/");
    }

private:
    bool isRunning(QProcess* p) {
        return p && p->state() != QProcess::NotRunning;
    }

    void stopServices() {
        stopFunnel();
        if (isRunning(g_node)) {
            g_node->kill();
            g_node->waitForFinished(2000);
            g_node->deleteLater();
            g_node = nullptr;
        }
        g_funnelUrl.clear();
    }

    int funnelRetry = 0;
    QLabel* portValue;
    QLabel* backendValue;
    QLabel* boardValue;
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

