// 把仓库里的网站静态资源同步到 desktop/www
// 本地启动与打包都基于这份副本，白名单方式避免把编译产物（exe/payload 等）带进安装包。
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..'); // 仓库根目录
const DEST = path.resolve(__dirname, '..', 'www');

const ITEMS = [
    'index.html',
    'server.js',
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

function copyRecursive(src, dest) {
    const st = fs.statSync(src);
    if (st.isDirectory()) {
        fs.mkdirSync(dest, { recursive: true });
        for (const name of fs.readdirSync(src)) {
            copyRecursive(path.join(src, name), path.join(dest, name));
        }
    } else {
        fs.mkdirSync(path.dirname(dest), { recursive: true });
        fs.copyFileSync(src, dest);
    }
}

function sizeOf(dir) {
    let total = 0;
    for (const name of fs.readdirSync(dir)) {
        const p = path.join(dir, name);
        const st = fs.statSync(p);
        total += st.isDirectory() ? sizeOf(p) : st.size;
    }
    return total;
}

fs.rmSync(DEST, { recursive: true, force: true });
fs.mkdirSync(DEST, { recursive: true });

let copied = 0;
for (const item of ITEMS) {
    const src = path.join(ROOT, item);
    if (!fs.existsSync(src)) {
        console.warn(`  跳过（不存在）：${item}`);
        continue;
    }
    copyRecursive(src, path.join(DEST, item));
    copied += 1;
}

const mb = (sizeOf(DEST) / 1024 / 1024).toFixed(2);
console.log(`已同步 ${copied} 项网站资源到 desktop/www（共 ${mb} MB）`);
