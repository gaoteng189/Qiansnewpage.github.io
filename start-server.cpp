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
#include <QProgressBar>
#include <QMessageBox>

#include <windows.h>
#include <shellapi.h>
#include <winreg.h>

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
    border-radius: 18px;
    min-height: 36px;
    padding: 8px 28px;
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
QProgressBar {
    background-color: #EFDBD1;
    border: none;
    border-radius: 5px;
    height: 10px;
}
QProgressBar::chunk {
    background-color: #8D6E63;
    border-radius: 5px;
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

// ---------- 运行环境（Node.js / Tailscale）----------
static const char* NODE_URL = "https://nodejs.org/dist/v24.20.0/node-v24.20.0-win-x64.zip";
static const char* TAILSCALE_URL = "https://pkgs.tailscale.com/stable/tailscale-setup-latest.exe";

static bool nodeExists() {
    return QFile::exists(g_root + "/node/node.exe");
}

static bool tailscaleExists() {
    return QFile::exists(QString::fromLatin1(TAILSCALE_EXE));
}

// 通过注册表查找 Tailscale 的 MSI 卸载命令
static QString tailscaleUninstallString() {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                      0, KEY_READ, &hk) != ERROR_SUCCESS)
        return QString();
    QString result;
    wchar_t name[256];
    DWORD i = 0;
    for (;;) {
        DWORD nameLen = 256;
        if (RegEnumKeyExW(hk, i++, name, &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        HKEY sub = nullptr;
        if (RegOpenKeyExW(hk, name, 0, KEY_READ, &sub) == ERROR_SUCCESS) {
            wchar_t dn[256] = {0};
            DWORD dnLen = sizeof(dn);
            wchar_t us[512] = {0};
            DWORD usLen = sizeof(us);
            if (RegQueryValueExW(sub, L"DisplayName", nullptr, nullptr, (LPBYTE)dn, &dnLen) == ERROR_SUCCESS &&
                lstrcmpW(dn, L"Tailscale") == 0) {
                if (RegQueryValueExW(sub, L"UninstallString", nullptr, nullptr, (LPBYTE)us, &usLen) == ERROR_SUCCESS)
                    result = QString::fromWCharArray(us);
                RegCloseKey(sub);
                break;
            }
            RegCloseKey(sub);
        }
    }
    RegCloseKey(hk);
    return result;
}

// 获取远端文件大小（curl HEAD，跟随重定向）
static qint64 httpSize(const QString& url) {
    QProcess p;
    p.start("curl.exe", { "-sIL", url });
    if (!p.waitForFinished(20000)) return -1;
    QString out = QString::fromUtf8(p.readAllStandardOutput());
    QRegularExpression re("(?i)content-length:\\s*(\\d+)");
    qint64 last = -1;
    auto it = re.globalMatch(out);
    while (it.hasNext()) last = it.next().captured(1).toLongLong();
    return last;
}

static QString humanSize(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number((double)bytes / 1024, 'f', 1) + " KB";
    return QString::number((double)bytes / (1024 * 1024), 'f', 1) + " MB";
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
        setFixedSize(520, 660);

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
        nodeValue = new QLabel(this);
        nodeValue->setObjectName("statusValue");
        tailscaleValue = new QLabel(this);
        tailscaleValue->setObjectName("statusValue");

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
        addRow("Node.js", nodeValue);
        addRow("Tailscale", tailscaleValue);

        // 按钮行 1（服务控制）
        startBtn = new QPushButton("Start", this);
        stopBtn = new QPushButton("Stop", this);
        stopBtn->setObjectName("stopBtn");
        auto* btnRow1 = new QHBoxLayout();
        btnRow1->addStretch();
        btnRow1->addWidget(startBtn);
        btnRow1->addWidget(stopBtn);
        btnRow1->addStretch();

        // 按钮行 2（环境管理）
        installBtn = new QPushButton("安装环境", this);
        installBtn->setObjectName("setPortBtn");
        cleanBtn = new QPushButton("清理环境", this);
        auto* btnRow2 = new QHBoxLayout();
        btnRow2->addStretch();
        btnRow2->addWidget(installBtn);
        btnRow2->addWidget(cleanBtn);
        btnRow2->addStretch();

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

        // 进度条
        progressBar = new QProgressBar(this);
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        progressBar->setTextVisible(false);
        progressBar->setFixedHeight(10);

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
        layout->addLayout(btnRow1);
        layout->addLayout(btnRow2);
        layout->addLayout(portRow);
        layout->addWidget(progressBar);
        layout->addWidget(hint);
        layout->addStretch();

        connect(startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
        connect(stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);
        connect(setPortBtn, &QPushButton::clicked, this, &MainWindow::onSetPort);
        connect(installBtn, &QPushButton::clicked, this, &MainWindow::installEnv);
        connect(cleanBtn, &QPushButton::clicked, this, &MainWindow::cleanEnv);

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
        QString nodeExe = nodeExists() ? (g_root + "/node/node.exe") : "node";
        g_node->start(nodeExe, { "server.js", QString::number(g_port) });

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

    // 开始异步下载（带进度）
    void startDownload(const QString& url, const QString& dest, const QString& stage) {
        dlDest = dest;
        dlStage = stage;
        dlTotal = httpSize(url);
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        hint->setText(stage == "node" ? "正在下载 Node.js..." : "正在下载 Tailscale...");

        if (dlProc) { dlProc->deleteLater(); dlProc = nullptr; }
        dlProc = new QProcess(this);
        connect(dlProc, &QProcess::finished, this, &MainWindow::onDlFinished);
        dlProc->start("curl.exe", { "-L", "-o", dest, url });

        if (!dlTimer) {
            dlTimer = new QTimer(this);
            connect(dlTimer, &QTimer::timeout, this, &MainWindow::onDlTick);
        }
        dlTimer->start(200);
    }

    // 下载进度刷新（按已下载字节 / 总字节）
    void onDlTick() {
        qint64 done = QFile(dlDest).size();
        QString name = dlStage == "node" ? "Node.js" : "Tailscale";
        if (dlTotal > 0) {
            int pct = (int)(done * 100 / dlTotal);
            if (pct > 100) pct = 100;
            progressBar->setValue(pct);
            hint->setText(QString("正在下载 %1：%2 / %3 (%4%)")
                              .arg(name).arg(humanSize(done)).arg(humanSize(dlTotal)).arg(pct));
        } else {
            hint->setText(QString("正在下载 %1：%2").arg(name).arg(humanSize(done)));
        }
    }

    // 下载完成后的分支处理
    void onDlFinished(int code, QProcess::ExitStatus) {
        dlTimer->stop();
        if (dlProc) { dlProc->deleteLater(); dlProc = nullptr; }
        if (code != 0 || !QFile::exists(dlDest)) {
            progressBar->setRange(0, 100);
            progressBar->setValue(0);
            hint->setText("下载失败，请检查网络后重试");
            refreshStatus();
            return;
        }
        if (dlStage == "node") extractNode();
        else if (dlStage == "tailscale") runTailscaleInstaller();
    }

    // 解压 Node.js 便携版（非静默，显示解压提示）
    void extractNode() {
        progressBar->setRange(0, 0);  // busy 模式
        hint->setText("正在解压 Node.js...");
        auto* p = new QProcess(this);
        connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
            p->deleteLater();
            QFile::remove(dlDest);
            QDir old(g_root + "/node");
            if (old.exists()) old.removeRecursively();
            bool ok = QDir().rename(g_root + "/node-v24.20.0-win-x64", g_root + "/node");
            progressBar->setRange(0, 100);
            progressBar->setValue(0);
            hint->setText(ok ? "Node.js 安装完成" : "Node.js 解压完成，但目录重命名失败");
            refreshStatus();
            continueInstall();
        });
        p->start("tar.exe", { "-xf", dlDest, "-C", g_root });
    }

    // 运行 Tailscale 官方安装器（非静默，需 UAC 确认）
    void runTailscaleInstaller() {
        progressBar->setRange(0, 100);
        progressBar->setValue(100);
        hint->setText("正在启动 Tailscale 安装程序（请确认 UAC）...");
        ShellExecuteW(nullptr, L"runas",
                      reinterpret_cast<const wchar_t*>(dlDest.utf16()),
                      nullptr, nullptr, SW_SHOWNORMAL);
        hint->setText("Tailscale 安装程序已启动，完成后请登录");
        refreshStatus();
    }

    // 继续安装下一个缺失的依赖
    void continueInstall() {
        if (!tailscaleExists()) {
            startDownload(TAILSCALE_URL, g_root + "/tailscale-setup.exe", "tailscale");
        } else {
            progressBar->setRange(0, 100);
            progressBar->setValue(100);
            hint->setText("运行环境已就绪");
        }
    }

    // 一键安装缺失的依赖（非静默，带进度）
    void installEnv() {
        if (dlProc && dlProc->state() != QProcess::NotRunning) {
            hint->setText("正在安装中，请稍候...");
            return;
        }
        if (!nodeExists()) { startDownload(NODE_URL, g_root + "/node.zip", "node"); return; }
        if (!tailscaleExists()) { startDownload(TAILSCALE_URL, g_root + "/tailscale-setup.exe", "tailscale"); return; }
        progressBar->setRange(0, 100);
        progressBar->setValue(100);
        hint->setText("运行环境已就绪");
    }

    // 一键清理（卸载）环境：删除 Node 便携版 + 卸载 Tailscale
    void cleanEnv() {
        if (dlProc && dlProc->state() != QProcess::NotRunning) {
            hint->setText("正在安装中，请先等待完成");
            return;
        }
        bool hasNode = nodeExists();
        bool hasTail = tailscaleExists();
        if (!hasNode && !hasTail) {
            hint->setText("没有需要清理的环境");
            return;
        }
        QStringList items;
        if (hasNode) items << "Node.js（便携版目录）";
        if (hasTail) items << "Tailscale";
        auto ret = QMessageBox::question(this, "清理环境",
            "确定要卸载以下环境吗？\n\n  · " + items.join("\n  · ") + "\n\n此操作不可撤销。",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;

        stopServices();

        if (hasNode) {
            QDir(g_root + "/node").removeRecursively();
            QFile::remove(g_root + "/node.zip");
        }
        QFile::remove(g_root + "/tailscale-setup.exe");

        if (hasTail) {
            QString us = tailscaleUninstallString();
            if (!us.isEmpty()) {
                hint->setText("正在启动 Tailscale 卸载程序...");
                QProcess::startDetached("cmd.exe", QStringList() << "/c" << us);
                hint->setText("Tailscale 卸载程序已启动，请在弹出的窗口中完成卸载");
            } else {
                hint->setText("未找到 Tailscale 卸载信息，请通过控制面板卸载");
            }
        } else {
            hint->setText("环境已清理");
        }
        refreshStatus();
    }

    void refreshStatus() {
        portValue->setText(QString::number(g_port));
        backendValue->setText(isRunning(g_node) ? "Running" : "Stopped");
        boardValue->setText(g_funnelUrl.isEmpty() ? "—" : g_funnelUrl + "/message/");
        nodeValue->setText(nodeExists() ? "Installed" : "Missing");
        tailscaleValue->setText(tailscaleExists() ? "Installed" : "Missing");
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
    QLabel* nodeValue;
    QLabel* tailscaleValue;
    QLabel* hint;
    QPushButton* startBtn;
    QPushButton* stopBtn;
    QPushButton* setPortBtn;
    QPushButton* installBtn;
    QPushButton* cleanBtn;
    QLineEdit* portEdit;
    QProgressBar* progressBar;
    QProcess* dlProc = nullptr;
    qint64 dlTotal = -1;
    QString dlDest;
    QString dlStage;
    QTimer* dlTimer = nullptr;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyleSheet(M3_QSS);

    // 单 exe 模式下，stub 通过环境变量传递项目根目录
    QByteArray envRoot = qgetenv("START_SERVER_ROOT");
    if (!envRoot.isEmpty()) {
        g_root = QString::fromLocal8Bit(envRoot);
    } else {
        QString appDir = QCoreApplication::applicationDirPath();
        // 若 exe 位于 bin/ 等子目录，则根目录指向 server.js 所在的上级目录
        if (QFile::exists(appDir + "/server.js")) {
            g_root = appDir;
        } else {
            QDir dir(appDir);
            dir.cdUp();
            g_root = dir.absolutePath();
        }
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

