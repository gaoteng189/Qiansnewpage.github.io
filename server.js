/**
 * 留言板后端服务器（零依赖，仅使用 Node.js 原生模块）
 *
 * 运行方式：
 *   node server.js
 *
 * 启动后访问：
 *   http://localhost:3000             —— 站点首页
 *   http://localhost:3000/message/    —— 留言板页面
 *
 * 留言数据保存在服务器所在主机的 messages.json 文件中。
 */
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 3000;
const HOST = process.env.HOST || '0.0.0.0';
const ROOT = __dirname;
const DATA_FILE = path.join(ROOT, 'messages.json');

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

// ---------- 工具 ----------
function send(res, status, payload, headers) {
  const body = JSON.stringify(payload);
  res.writeHead(status, Object.assign({
    'Content-Type': 'application/json; charset=utf-8',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
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

  // 预检请求
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type'
    });
    res.end();
    return;
  }

  // 留言 API
  if (pathname === '/api/messages') {
    if (req.method === 'GET') {
      send(res, 200, readMessages());
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
      const item = {
        id: Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 8),
        name: name || '匿名',
        message,
        time: new Date().toISOString()
      };
      const list = readMessages();
      list.push(item);
      try {
        writeMessages(list);
      } catch (e) {
        send(res, 500, { error: '写入留言失败：' + e.message });
        return;
      }
      send(res, 200, { ok: true, item });
      return;
    }
    send(res, 405, { error: '不支持的请求方法' });
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
