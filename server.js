/**
 * 留言板后端服务器（零依赖，仅使用 Node.js 原生模块）
 *
 * 运行方式：
 *   node server.js
 *
 * 启动后访问：
 *   http://localhost:50304             —— 站点首页
 *   http://localhost:50304/message/    —— 留言板页面
 *
 * HTTP 接口：
 *   GET    /api/messages      —— 读取全部留言
 *   POST   /api/messages      —— 发布留言（JSON：{ name, message, replyTo? }）
 *                                返回 { ok, token, item }，token 为删除凭证（仅发布者持有）
 *   DELETE /api/messages/<id> —— 删除自己的留言（JSON：{ token }，会连同其回复一起删除）
 *   GET  /api/novels      —— 列出 novels/ 目录下的小说（txt）
 *   GET  /api/novels/<名> —— 下载小说原始字节（前端自行按 UTF-8 / GBK 解码）
 *
 * 留言数据保存在服务器所在主机的 messages.json 文件中；
 * 小说文件放在服务器所在主机的 novels/ 目录中（.txt）。
 */
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const PORT = process.argv[2] || process.env.PORT || 50304;
const HOST = process.env.HOST || '0.0.0.0';
const ROOT = __dirname;
const DATA_FILE = path.join(ROOT, 'messages.json');
const NOVEL_DIR = process.env.NOVEL_DIR || path.join(ROOT, 'novels');
const TXT_EXT = /\.txt$/i;

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.webp': 'image/webp',
  '.webmanifest': 'application/manifest+json',
  '.mp4': 'video/mp4',
  '.mp3': 'audio/mpeg'
};

// ---------- 留言读写 ----------
function readMessages() {
  try {
    const raw = fs.readFileSync(DATA_FILE, 'utf8');
    const data = JSON.parse(raw);
    return Array.isArray(data) ? data : [];
  } catch (e) {
    return [];
  }
}

function writeMessages(list) {
  fs.writeFileSync(DATA_FILE, JSON.stringify(list, null, 2), 'utf8');
}

function sha256(s) {
  return crypto.createHash('sha256').update(String(s)).digest('hex');
}

// 对外输出时去掉 tokenHash（删除凭证的哈希），仅保留展示所需字段
function publicMessages() {
  return readMessages().map((m) => ({
    id: m.id,
    name: m.name,
    message: m.message,
    time: m.time,
    replyTo: m.replyTo || ''
  }));
}

// ---------- 工具 ----------
function send(res, status, payload, headers) {
  const body = JSON.stringify(payload);
  res.writeHead(status, Object.assign({
    'Content-Type': 'application/json; charset=utf-8',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, DELETE, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type'
  }, headers || {}));
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    let size = 0;
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > 1 * 1024 * 1024) {
        reject(new Error('too large'));
        req.destroy();
        return;
      }
      body += chunk;
    });
    req.on('end', () => resolve(body));
    req.on('error', reject);
  });
}

// ---------- 小说书库 ----------
// 列出 novels/ 目录下的 txt 小说
function listNovels() {
  let names;
  try {
    names = fs.readdirSync(NOVEL_DIR);
  } catch (e) {
    return [];
  }
  const list = [];
  for (const name of names) {
    if (!TXT_EXT.test(name)) continue;
    try {
      const st = fs.statSync(path.join(NOVEL_DIR, name));
      if (!st.isFile()) continue;
      list.push({ name, size: st.size, mtime: Math.round(st.mtimeMs) });
    } catch (e) {
      // 单个文件读取失败不影响其它文件
    }
  }
  list.sort((a, b) => a.name.localeCompare(b.name, 'zh-CN'));
  return list;
}

// 把请求里的书名解析为 novels/ 目录下安全的文件路径（防目录穿越）
function resolveNovel(name) {
  const base = path.basename(String(name || '').trim());
  if (!base || base === '.' || base === '..') return null;
  if (!TXT_EXT.test(base)) return null;
  const full = path.normalize(path.join(NOVEL_DIR, base));
  if (!full.startsWith(NOVEL_DIR + path.sep)) return null;
  return full;
}

function serveStatic(res, urlPath) {
  let pathname;
  try {
    pathname = decodeURIComponent(urlPath);
  } catch (e) {
    send(res, 400, { error: '无效的请求路径' });
    return;
  }

  // 目录请求默认返回 index.html
  if (pathname === '/' || pathname === '') pathname = '/index.html';
  if (pathname.endsWith('/')) pathname += 'index.html';

  // 防止目录穿越
  const filePath = path.normalize(path.join(ROOT, pathname));
  if (!filePath.startsWith(ROOT + path.sep) && filePath !== path.join(ROOT, 'index.html')) {
    res.writeHead(403, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Forbidden');
    return;
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end('<h1>404 Not Found</h1>');
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
    res.end(data);
  });
}

// ---------- 服务器 ----------
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  const pathname = url.pathname;
  const startedAt = Date.now();

  // 请求日志：记录方法、路径、状态码与耗时
  res.on('finish', () => {
    const time = new Date().toLocaleTimeString('zh-CN', { hour12: false });
    const ms = Date.now() - startedAt;
    console.log(`[${time}] ${req.method} ${pathname} -> ${res.statusCode} (${ms}ms)`);
  });

  // 预检请求
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, DELETE, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type'
    });
    res.end();
    return;
  }

  // 留言 API
  if (pathname === '/api/messages') {
    if (req.method === 'GET') {
      send(res, 200, publicMessages());
      return;
    }
    if (req.method === 'POST') {
      let body;
      try {
        body = await readBody(req);
      } catch (e) {
        send(res, 413, { error: '请求体过大' });
        return;
      }
      let data;
      try {
        data = JSON.parse(body || '{}');
      } catch (e) {
        send(res, 400, { error: '无效的 JSON 数据' });
        return;
      }
      const name = String(data.name || '').trim().slice(0, 50);
      const message = String(data.message || '').trim().slice(0, 1000);
      if (!message) {
        send(res, 400, { error: '留言内容不能为空' });
        return;
      }
      const list = readMessages();
      const replyTo = String(data.replyTo || '').trim();
      const item = {
        id: Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 8),
        name: name || '匿名',
        message,
        time: new Date().toISOString(),
        replyTo: replyTo && list.some((m) => m.id === replyTo) ? replyTo : ''
      };
      // 删除凭证：明文只返回给发布者，服务端仅保存哈希
      const token = crypto.randomBytes(16).toString('hex');
      item.tokenHash = sha256(token);
      list.push(item);
      try {
        writeMessages(list);
      } catch (e) {
        send(res, 500, { error: '写入留言失败：' + e.message });
        return;
      }
      console.log(`[留言] ${item.name}${item.replyTo ? '（回复）' : ''}: ${item.message.slice(0, 80)}`);
      send(res, 200, {
        ok: true,
        token,
        item: {
          id: item.id,
          name: item.name,
          message: item.message,
          time: item.time,
          replyTo: item.replyTo
        }
      });
      return;
    }
    send(res, 405, { error: '不支持的请求方法' });
    return;
  }

  // 删除留言（需携带发布时返回的 token；连同其直接回复一起删除）
  if (pathname.startsWith('/api/messages/')) {
    if (req.method !== 'DELETE') {
      send(res, 405, { error: '不支持的请求方法' });
      return;
    }
    let id;
    try {
      id = decodeURIComponent(pathname.slice('/api/messages/'.length));
    } catch (e) {
      send(res, 400, { error: '无效的留言 ID' });
      return;
    }
    let body;
    try {
      body = await readBody(req);
    } catch (e) {
      send(res, 413, { error: '请求体过大' });
      return;
    }
    let data;
    try {
      data = JSON.parse(body || '{}');
    } catch (e) {
      send(res, 400, { error: '无效的 JSON 数据' });
      return;
    }
    const token = String(data.token || '');
    if (!token) {
      send(res, 400, { error: '缺少删除凭证' });
      return;
    }
    const list = readMessages();
    const target = list.find((m) => m.id === id);
    if (!target) {
      send(res, 404, { error: '留言不存在' });
      return;
    }
    if (target.tokenHash !== sha256(token)) {
      send(res, 403, { error: '无权删除这条留言' });
      return;
    }
    const removed = list.filter((m) => m.id === id || m.replyTo === id).length;
    const next = list.filter((m) => m.id !== id && m.replyTo !== id);
    try {
      writeMessages(next);
    } catch (e) {
      send(res, 500, { error: '删除失败：' + e.message });
      return;
    }
    console.log(`[删除] ${id}（连同 ${removed - 1} 条回复）`);
    send(res, 200, { ok: true, removed });
    return;
  }

  // 小说书库：列表
  if (pathname === '/api/novels') {
    if (req.method === 'GET') {
      send(res, 200, listNovels());
      return;
    }
    send(res, 405, { error: '不支持的请求方法' });
    return;
  }

  // 小说书库：下载原始字节（交给前端按编码解码）
  if (pathname.startsWith('/api/novels/')) {
    if (req.method !== 'GET') {
      send(res, 405, { error: '不支持的请求方法' });
      return;
    }
    let rawName;
    try {
      rawName = decodeURIComponent(pathname.slice('/api/novels/'.length));
    } catch (e) {
      send(res, 400, { error: '无效的文件名' });
      return;
    }
    const filePath = resolveNovel(rawName);
    if (!filePath) {
      send(res, 400, { error: '无效的文件名' });
      return;
    }
    fs.readFile(filePath, (err, data) => {
      if (err) {
        send(res, 404, { error: '小说不存在' });
        return;
      }
      res.writeHead(200, {
        'Content-Type': 'application/octet-stream',
        'Content-Length': data.length,
        'Access-Control-Allow-Origin': '*',
        'Cache-Control': 'no-cache'
      });
      res.end(data);
      console.log(`[小说] 下发 ${path.basename(filePath)}（${data.length} 字节）`);
    });
    return;
  }

  // 静态文件
  serveStatic(res, pathname);
});

server.listen(PORT, HOST, () => {
  console.log('留言板服务器已启动');
  console.log(`  本机访问:  http://localhost:${PORT}/`);
  console.log(`  留言页面:  http://localhost:${PORT}/message/`);
  console.log(`  数据文件:  ${DATA_FILE}`);
  console.log('按 Ctrl+C 停止服务器');
});
