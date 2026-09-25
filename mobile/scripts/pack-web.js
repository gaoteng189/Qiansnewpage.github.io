// 准备移动端所需的网站资源（mobile/assets/）
//
// 分两部分：
//   1. www.zip  —— 页面资源（200 KB 左右），首次启动解压到应用目录；
//   2. video/   —— 视频合计 86 MB，若一并解压会让首启卡很久，
//                  因此原样放入 assets，App 只在真正播放某个视频时才落盘。
'use strict';

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..'); // 仓库根目录
const OUT_DIR = path.resolve(__dirname, '..', 'assets');
const ZIP = path.join(OUT_DIR, 'www.zip');
const VIDEO_OUT = path.join(OUT_DIR, 'video');

// 白名单：只打包前端真正用到的资源（server.js 由 Dart 端重写，不打包）
// 注意 video 目录只取 index.html，视频文件走下面的独立复制
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
    'video/index.html'
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
console.log(`已打包 ${existing.length} 项页面资源到 assets/www.zip（${mb} MB）`);

// ---------- 视频：独立存放，运行时按需落盘 ----------
const videoSrc = path.join(ROOT, 'video');
if (!fs.existsSync(videoSrc)) {
    console.warn('  未找到 video/，跳过视频准备');
    process.exit(0);
}

fs.rmSync(VIDEO_OUT, { recursive: true, force: true });
fs.mkdirSync(VIDEO_OUT, { recursive: true });

let videoBytes = 0;
let videoCount = 0;
for (const entry of fs.readdirSync(videoSrc, { withFileTypes: true })) {
    // 页面本身已随 www.zip 发布，这里只要媒体与图片
    if (!entry.isFile() || entry.name.endsWith('.html')) continue;
    const src = path.join(videoSrc, entry.name);
    fs.copyFileSync(src, path.join(VIDEO_OUT, entry.name));
    videoBytes += fs.statSync(src).size;
    videoCount++;
}

const vmb = (videoBytes / 1024 / 1024).toFixed(2);
console.log(`已准备 ${videoCount} 项视频资源到 assets/video/（${vmb} MB，首次播放时按需落盘）`);
