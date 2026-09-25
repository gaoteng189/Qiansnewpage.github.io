// 把仓库里的网站资源打包成 mobile/assets/www.zip
// 移动端首次启动时解压到应用目录，再由 Dart 内置服务对外提供（与桌面版行为一致）。
'use strict';

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..'); // 仓库根目录
const OUT_DIR = path.resolve(__dirname, '..', 'assets');
const ZIP = path.join(OUT_DIR, 'www.zip');

// 白名单：只打包前端真正用到的资源（server.js 由 Dart 端重写，不打包）
const ITEMS = [
    'index.html',
    'site.webmanifest',
    'favicon.ico',
    'favicon-16x16.png',
    'favicon-32x32.png',
    'apple-touch-icon.png',
    'android-chrome-192x192.png',
    'android-chrome-512x512.png',
    'game',
    'novel',
    'todo',
    'message',
    'video'
];

fs.mkdirSync(OUT_DIR, { recursive: true });
if (fs.existsSync(ZIP)) fs.rmSync(ZIP, { force: true });

const existing = ITEMS.filter((item) => fs.existsSync(path.join(ROOT, item)));
const missing = ITEMS.filter((item) => !fs.existsSync(path.join(ROOT, item)));
missing.forEach((item) => console.warn(`  跳过（不存在）：${item}`));

// Windows 自带的 bsdtar 可按扩展名自动生成标准 zip
execFileSync('tar.exe', ['-a', '-c', '-f', ZIP, ...existing], {
    cwd: ROOT,
    stdio: 'inherit'
});

const mb = (fs.statSync(ZIP).size / 1024 / 1024).toFixed(2);
console.log(`已打包 ${existing.length} 项网站资源到 mobile/assets/www.zip（${mb} MB）`);
