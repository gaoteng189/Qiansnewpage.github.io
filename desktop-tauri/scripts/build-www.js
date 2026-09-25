// 把仓库里的网站静态资源同步到 src-tauri/www
//
// 与 Electron 版的白名单基本一致，区别是**不需要 server.js**——桌面端的
// 后端接口已由 Rust 侧（src-tauri/src/api.rs）重新实现，行为逐条对齐。
//
// 环境变量：
//   SKIP_VIDEO=1  跳过 video/ 目录。安装包会从 ~90 MB 降到个位数 MB，
//                 代价是视频页需要联网（本地没有媒体文件）。
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..'); // 仓库根目录
const DEST = path.resolve(__dirname, '..', 'src-tauri', 'www');
const SKIP_VIDEO = process.env.SKIP_VIDEO === '1';

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

function copyRecursive(src, dest) {
    const st = fs.statSync(src);
    if (st.isDirectory()) {
        fs.mkdirSync(dest, { recursive: true });
        for (const name of fs.readdirSync(src)) {
            if (SKIP_VIDEO && src === path.join(ROOT, 'video') && name !== 'index.html') {
                continue; // 只保留视频页本身，媒体文件交给线上
            }
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
console.log(`已同步 ${copied} 项网站资源到 src-tauri/www（共 ${mb} MB）`);
if (SKIP_VIDEO) {
    console.log('  SKIP_VIDEO=1：已跳过视频媒体文件，视频页需要联网才能播放');
}
