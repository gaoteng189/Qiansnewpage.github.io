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
 * 留言数据保存在服务器所在主机的 messages.json 文件中。
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

// ---------- WebSocket（基于 TCP 的留言传输）----------
const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const clients = new Set();

function wsSend(socket, payload, opcode) {
  const data = Buffer.isBuffer(payload) ? payload : Buffer.from(String(payload), 'utf8');
  const len = data.length;
  let header;
  if (len < 126) {
    header = Buffer.from([0x80 | (opcode || 0x1), len]);
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | (opcode || 0x1);
    header[1] = 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | (opcode || 0x1);
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(len), 2);
  }
  socket.write(Buffer.concat([header, data]));
}

function wsParseFrame(buf) {
  if (buf.length < 2) return null;
  const opcode = buf[0] & 0x0f;
  const masked = (buf[1] & 0x80) !== 0;
  let len = buf[1] & 0x7f;
  let offset = 2;
  if (len === 126) {
    if (buf.length < 4) return null;
    len = buf.readUInt16BE(2);
    offset = 4;
  } else if (len === 127) {
    if (buf.length < 10) return null;
    len = Number(buf.readBigUInt64BE(2));
    offset = 10;
  }
  let maskKey = null;
  if (masked) {
    if (buf.length < offset + 4) return null;
    maskKey = buf.slice(offset, offset + 4);
    offset += 4;
  }
  if (buf.length < offset + len) return null;
  let payload = buf.slice(offset, offset + len);
  if (masked && maskKey) {
    const un = Buffer.alloc(len);
    for (let i = 0; i < len; i++) un[i] = payload[i] ^ maskKey[i % 4];
    payload = un;
  }
  return { opcode, payload, consumed: offset + len };
}

function wsBroadcast(text) {
  for (const s of clients) {
    try { wsSend(s, text); } catch (e) {}
  }
}

function wsOnMessage(socket, text) {
  let msg;
  try { msg = JSON.parse(text); } catch (e) { return; }

  if (msg.type === 'get') {
    wsSend(socket, JSON.stringify({ type: 'list', messages: readMessages() }));
  } else if (msg.type === 'post') {
    const name = String(msg.name || '').trim().slice(0, 50);
    const message = String(msg.message || '').trim().slice(0, 1000);
    if (!message) {
      wsSend(socket, JSON.stringify({ type: 'error', error: '留言内容不能为空' }));
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
      wsSend(socket, JSON.stringify({ type: 'error', error: '写入留言失败：' + e.message }));
      return;
    }
    console.log(`[留言] ${item.name}: ${item.message.slice(0, 80)}`);
    wsBroadcast(JSON.stringify({ type: 'list', messages: list }));
  }
}

function wsHandleSocket(socket) {
  clients.add(socket);
  let buffer = Buffer.alloc(0);

  socket.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
      const frame = wsParseFrame(buffer);
      if (!frame) break;
      buffer = buffer.slice(frame.consumed);
      if (frame.opcode === 0x8) {
        try { wsSend(socket, Buffer.alloc(0), 0x8); } catch (e) {}
        socket.destroy();
        return;
      }
      if (frame.opcode === 0x9) {
        try { wsSend(socket, frame.payload, 0xA); } catch (e) {}
        continue;
      }
      if (frame.opcode === 0x1) {
        wsOnMessage(socket, frame.payload.toString('utf8'));
      }
    }
  });

  socket.on('close', () => clients.delete(socket));
  socket.on('error', () => { clients.delete(socket); socket.destroy(); });
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
      console.log(`[留言] ${item.name}: ${item.message.slice(0, 80)}`);
      send(res, 200, { ok: true, item });
      return;
    }
    send(res, 405, { error: '不支持的请求方法' });
    return;
  }

  // 静态文件
  serveStatic(res, pathname);
});

// WebSocket 升级（留言走 TCP 长连接传输）
server.on('upgrade', (req, socket) => {
  if ((req.headers.upgrade || '').toLowerCase() !== 'websocket') {
    socket.destroy();
    return;
  }
  const key = req.headers['sec-websocket-key'];
  if (!key) {
    socket.destroy();
    return;
  }
  const accept = crypto.createHash('sha1').update(key + WS_GUID).digest('base64');
  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
    'Upgrade: websocket\r\n' +
    'Connection: Upgrade\r\n' +
    'Sec-WebSocket-Accept: ' + accept + '\r\n\r\n'
  );
  wsHandleSocket(socket);
});

server.listen(PORT, HOST, () => {
  console.log('留言板服务器已启动');
  console.log(`  本机访问:  http://localhost:${PORT}/`);
  console.log(`  留言页面:  http://localhost:${PORT}/message/`);
  console.log(`  数据文件:  ${DATA_FILE}`);
  console.log('按 Ctrl+C 停止服务器');
});
