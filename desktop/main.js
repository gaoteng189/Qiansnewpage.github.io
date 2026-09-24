// Electron 主进程：内置 HTTP 服务 + 主窗口 + 中文菜单
'use strict';

const { app, BrowserWindow, Menu, shell, dialog } = require('electron');
const path = require('path');
const fs = require('fs');

const APP_NAME = '千叶新页';
const BASE_PORT = 50304;

// 打包后网站资源位于应用目录下的 www/
const WEB_DIR = path.join(__dirname, 'www');

let win = null;
let server = null;
let port = BASE_PORT;

// ---------- 单实例 ----------
if (!app.requestSingleInstanceLock()) {
    app.quit();
} else {
    app.on('second-instance', () => {
        if (!win) return;
        if (win.isMinimized()) win.restore();
        win.focus();
    });
}

// ---------- 数据目录 ----------
function ensureNovelDir(dir) {
    try {
        fs.mkdirSync(dir, { recursive: true });
        const readme = path.join(dir, 'README.md');
        if (!fs.existsSync(readme)) {
            fs.writeFileSync(
                readme,
                '# 小说书库\n\n把 `.txt` 小说文件放进这个目录，应用内「小说阅读器 → 服务器书库」即可读到。\n',
                'utf8'
            );
        }
    } catch (e) {
        // 建目录失败不影响主流程
    }
}

// ---------- 内置 HTTP 服务 ----------
// 从 50304 起顺延找一个空闲端口，避免与本地已运行的服务冲突
function listenFree(srv, startPort, tries) {
    return new Promise((resolve, reject) => {
        let p = startPort;
        let left = tries;
        const attempt = () => {
            const onError = (err) => {
                if (err && err.code === 'EADDRINUSE' && left > 0) {
                    left -= 1;
                    p += 1;
                    attempt();
                } else {
                    reject(err);
                }
            };
            srv.once('error', onError);
            srv.listen(p, '127.0.0.1', () => {
                srv.removeListener('error', onError);
                resolve(p);
            });
        };
        attempt();
    });
}

async function startServer() {
    const dataDir = app.getPath('userData');
    const novelDir = path.join(dataDir, 'novels');
    ensureNovelDir(novelDir);

    // server.js 在模块加载时就读取这些环境变量，必须先设置再 require
    process.env.STATIC_ROOT = WEB_DIR;
    process.env.DATA_DIR = dataDir;
    process.env.NOVEL_DIR = novelDir;

    const { createServer } = require(path.join(WEB_DIR, 'server.js'));
    server = createServer();
    port = await listenFree(server, BASE_PORT, 30);

    console.log(`[desktop] 本地服务已启动: http://127.0.0.1:${port}/`);
    console.log(`[desktop] 数据目录: ${dataDir}`);
}

// ---------- 窗口 ----------
function url(p) {
    return `http://127.0.0.1:${port}${p}`;
}

function createWindow() {
    win = new BrowserWindow({
        width: 1200,
        height: 840,
        minWidth: 760,
        minHeight: 540,
        backgroundColor: '#FDF8F5',
        title: APP_NAME,
        icon: path.join(__dirname, 'build', 'icon.png'),
        autoHideMenuBar: false,
        webPreferences: {
            contextIsolation: true,
            nodeIntegration: false,
            spellcheck: false
        }
    });

    win.loadURL(url('/'));

    // target="_blank" 的站外链接交给系统浏览器
    win.webContents.setWindowOpenHandler(({ url: target }) => {
        if (/^https?:/i.test(target) && !target.startsWith(`http://127.0.0.1:${port}`)) {
            shell.openExternal(target);
            return { action: 'deny' };
        }
        return { action: 'allow' };
    });

    // 阻止窗口本身导航到站外地址
    win.webContents.on('will-navigate', (e, target) => {
        if (!target.startsWith(`http://127.0.0.1:${port}`)) {
            e.preventDefault();
            if (/^https?:/i.test(target)) shell.openExternal(target);
        }
    });

    win.on('closed', () => {
        win = null;
    });
}

function go(p) {
    if (win) win.loadURL(url(p));
}

// ---------- 菜单 ----------
function buildMenu() {
    const template = [
        {
            label: '文件',
            submenu: [
                { label: '回到首页', accelerator: 'CmdOrCtrl+H', click: () => go('/') },
                { type: 'separator' },
                { label: '打开数据目录', click: () => shell.openPath(app.getPath('userData')) },
                { type: 'separator' },
                { role: 'quit', label: '退出' }
            ]
        },
        {
            label: '页面',
            submenu: [
                { label: '在线工具箱', click: () => go('/') },
                { label: '小说阅读器', click: () => go('/novel/') },
                { label: '待办清单', click: () => go('/todo/') },
                { label: '留言板', click: () => go('/message/') },
                { label: '2D 跑酷', click: () => go('/game/') },
                { label: '视频播放', click: () => go('/video/') }
            ]
        },
        {
            label: '视图',
            submenu: [
                { label: '刷新', accelerator: 'F5', click: () => { if (win) win.reload(); } },
                { type: 'separator' },
                { role: 'zoomIn', label: '放大' },
                { role: 'zoomOut', label: '缩小' },
                { role: 'resetZoom', label: '重置缩放' },
                { type: 'separator' },
                { role: 'togglefullscreen', label: '全屏' },
                { role: 'toggleDevTools', label: '开发者工具' }
            ]
        },
        {
            label: '帮助',
            submenu: [
                { label: '关于', click: showAbout }
            ]
        }
    ];
    Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

function showAbout() {
    dialog.showMessageBox(win, {
        type: 'info',
        title: '关于 ' + APP_NAME,
        message: APP_NAME,
        detail: [
            `版本 ${app.getVersion()}`,
            `Electron ${process.versions.electron} · Chromium ${process.versions.chrome}`,
            '',
            '个人站点桌面版：工具箱 / 小说阅读 / 待办 / 留言板 / 小游戏',
            `本地服务：http://127.0.0.1:${port}/`,
            `数据目录：${app.getPath('userData')}`
        ].join('\n'),
        buttons: ['好的'],
        noLink: true
    });
}

// ---------- 生命周期 ----------
app.whenReady().then(async () => {
    try {
        await startServer();
    } catch (e) {
        dialog.showErrorBox('启动失败', '本地服务启动失败：' + ((e && e.message) || e));
        app.quit();
        return;
    }

    buildMenu();
    createWindow();

    app.on('activate', () => {
        if (BrowserWindow.getAllWindows().length === 0) createWindow();
    });
});

app.on('window-all-closed', () => {
    if (server) {
        try { server.close(); } catch (e) { /* 忽略 */ }
        server = null;
    }
    app.quit();
});
