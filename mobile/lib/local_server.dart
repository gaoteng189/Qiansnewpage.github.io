import 'dart:convert';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';
import 'package:path/path.dart' as p;

/// 本机内置服务：静态资源 + 留言板 / 小说书库接口
///
/// 接口与网页版 `server.js` 保持一致，前端代码无需任何改动即可复用。
/// 只监听 127.0.0.1，端口由系统分配，避免与其它应用冲突。
class LocalServer {
  LocalServer({required this.webRoot, required this.dataDir});

  final String webRoot;
  final String dataDir;

  HttpServer? _server;

  int get port => _server?.port ?? 0;

  File get _messagesFile => File(p.join(dataDir, 'messages.json'));
  Directory get _novelDir => Directory(p.join(dataDir, 'novels'));

  static const Map<String, String> _mime = {
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
    '.mp3': 'audio/mpeg',
  };

  Future<int> start() async {
    final dir = Directory(dataDir);
    if (!await dir.exists()) await dir.create(recursive: true);
    if (!await _novelDir.exists()) {
      await _novelDir.create(recursive: true);
      await File(p.join(_novelDir.path, 'README.md')).writeAsString(
        '# 小说书库\n\n把 `.txt` 小说文件放到这个目录，应用内「小说阅读器 → 服务器书库」即可读到。\n',
      );
    }

    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    _server = server;
    server.listen(_handle, onError: (_) {});
    return server.port;
  }

  Future<void> stop() async {
    final server = _server;
    _server = null;
    await server?.close(force: true);
  }

  // ---------- 路由 ----------
  Future<void> _handle(HttpRequest req) async {
    final res = req.response;
    try {
      final path = req.uri.path;

      if (req.method == 'OPTIONS') {
        _cors(res);
        res.statusCode = HttpStatus.noContent;
        await res.close();
        return;
      }

      if (path == '/api/messages') {
        await _messages(req, res);
        return;
      }
      if (path.startsWith('/api/messages/')) {
        await _deleteMessage(req, res, path.substring('/api/messages/'.length));
        return;
      }
      if (path == '/api/novels') {
        await _json(res, HttpStatus.ok, await _listNovels());
        return;
      }
      if (path.startsWith('/api/novels/')) {
        await _serveNovel(req, res, path.substring('/api/novels/'.length));
        return;
      }

      await _serveStatic(req, res, path);
    } catch (e) {
      try {
        res.statusCode = HttpStatus.internalServerError;
        res.write('服务器内部错误');
        await res.close();
      } catch (_) {}
    }
  }

  // ---------- 留言板 ----------
  Future<void> _messages(HttpRequest req, HttpResponse res) async {
    if (req.method == 'GET') {
      await _json(res, HttpStatus.ok, await _readMessages(publicOnly: true));
      return;
    }
    if (req.method != 'POST') {
      await _json(res, HttpStatus.methodNotAllowed, {'error': '不支持的请求方法'});
      return;
    }

    Map<String, dynamic> data;
    try {
      data = await _readJsonBody(req);
    } on _TooLarge {
      await _json(res, HttpStatus.requestEntityTooLarge, {'error': '请求体过大'});
      return;
    } catch (_) {
      await _json(res, HttpStatus.badRequest, {'error': '无效的 JSON 数据'});
      return;
    }

    final name = (data['name'] ?? '').toString().trim();
    final message = (data['message'] ?? '').toString().trim();
    if (message.isEmpty) {
      await _json(res, HttpStatus.badRequest, {'error': '留言内容不能为空'});
      return;
    }

    final list = await _readMessages(publicOnly: false);
    final replyTo = (data['replyTo'] ?? '').toString().trim();
    final validReplyTo = list.any((m) => m['id'] == replyTo) ? replyTo : '';

    final item = <String, dynamic>{
      'id': '${DateTime.now().millisecondsSinceEpoch.toRadixString(36)}-'
          '${Random().nextInt(0x7fffffff).toRadixString(36)}',
      'name': name.isEmpty ? '匿名' : name.substring(0, min(50, name.length)),
      'message': message.length > 1000 ? message.substring(0, 1000) : message,
      'time': DateTime.now().toUtc().toIso8601String(),
      'replyTo': validReplyTo,
    };

    // 删除凭证：明文只返回给发布者，本地只存哈希
    final token = _randomHex(16);
    item['tokenHash'] = sha256.convert(utf8.encode(token)).toString();
    list.add(item);
    await _writeMessages(list);

    final pub = Map<String, dynamic>.from(item)..remove('tokenHash');
    await _json(res, HttpStatus.ok, {'ok': true, 'token': token, 'item': pub});
  }

  Future<void> _deleteMessage(HttpRequest req, HttpResponse res, String rawId) async {
    if (req.method != 'DELETE') {
      await _json(res, HttpStatus.methodNotAllowed, {'error': '不支持的请求方法'});
      return;
    }

    final id = Uri.decodeComponent(rawId);
    Map<String, dynamic> data;
    try {
      data = await _readJsonBody(req);
    } on _TooLarge {
      await _json(res, HttpStatus.requestEntityTooLarge, {'error': '请求体过大'});
      return;
    } catch (_) {
      await _json(res, HttpStatus.badRequest, {'error': '无效的 JSON 数据'});
      return;
    }

    final token = (data['token'] ?? '').toString();
    if (token.isEmpty) {
      await _json(res, HttpStatus.badRequest, {'error': '缺少删除凭证'});
      return;
    }

    final list = await _readMessages(publicOnly: false);
    final index = list.indexWhere((m) => m['id'] == id);
    if (index < 0) {
      await _json(res, HttpStatus.notFound, {'error': '留言不存在'});
      return;
    }
    final expected = sha256.convert(utf8.encode(token)).toString();
    if (list[index]['tokenHash'] != expected) {
      await _json(res, HttpStatus.forbidden, {'error': '无权删除这条留言'});
      return;
    }

    final removed = list.where((m) => m['id'] == id || m['replyTo'] == id).length;
    list.removeWhere((m) => m['id'] == id || m['replyTo'] == id);
    await _writeMessages(list);
    await _json(res, HttpStatus.ok, {'ok': true, 'removed': removed});
  }

  Future<List<Map<String, dynamic>>> _readMessages({required bool publicOnly}) async {
    try {
      if (!await _messagesFile.exists()) return [];
      final raw = await _messagesFile.readAsString();
      final decoded = jsonDecode(raw);
      if (decoded is! List) return [];
      final result = <Map<String, dynamic>>[];
      for (final item in decoded) {
        if (item is! Map) continue;
        final map = Map<String, dynamic>.from(item);
        if (publicOnly) map.remove('tokenHash');
        result.add(map);
      }
      return result;
    } catch (_) {
      return [];
    }
  }

  Future<void> _writeMessages(List<Map<String, dynamic>> list) async {
    await _messagesFile.writeAsString(const JsonEncoder.withIndent('  ').convert(list));
  }

  // ---------- 小说书库 ----------
  Future<List<Map<String, dynamic>>> _listNovels() async {
    final result = <Map<String, dynamic>>[];
    try {
      if (!await _novelDir.exists()) return result;
      await for (final entity in _novelDir.list()) {
        if (entity is! File) continue;
        final name = p.basename(entity.path);
        if (!name.toLowerCase().endsWith('.txt')) continue;
        final stat = await entity.stat();
        result.add({
          'name': name,
          'size': stat.size,
          'mtime': stat.modified.millisecondsSinceEpoch,
        });
      }
    } catch (_) {}
    result.sort((a, b) => (a['name'] as String).compareTo(b['name'] as String));
    return result;
  }

  String? _resolveNovel(String rawName) {
    final name = p.basename(Uri.decodeComponent(rawName).trim());
    if (name.isEmpty || name == '.' || name == '..') return null;
    if (!name.toLowerCase().endsWith('.txt')) return null;
    return p.join(_novelDir.path, name);
  }

  Future<void> _serveNovel(HttpRequest req, HttpResponse res, String rawName) async {
    if (req.method != 'GET') {
      await _json(res, HttpStatus.methodNotAllowed, {'error': '不支持的请求方法'});
      return;
    }
    final path = _resolveNovel(rawName);
    if (path == null) {
      await _json(res, HttpStatus.badRequest, {'error': '无效的文件名'});
      return;
    }
    final file = File(path);
    if (!await file.exists()) {
      await _json(res, HttpStatus.notFound, {'error': '小说不存在'});
      return;
    }
    final bytes = await file.readAsBytes();
    res.statusCode = HttpStatus.ok;
    res.headers.set(HttpHeaders.contentTypeHeader, 'application/octet-stream');
    res.headers.set(HttpHeaders.contentLengthHeader, '${bytes.length}');
    res.headers.set('Access-Control-Allow-Origin', '*');
    res.headers.set(HttpHeaders.cacheControlHeader, 'no-cache');
    res.add(bytes);
    await res.close();
  }

  // ---------- 静态资源 ----------
  Future<void> _serveStatic(HttpRequest req, HttpResponse res, String urlPath) async {
    var rel = Uri.decodeComponent(urlPath);
    if (rel.isEmpty || rel == '/') rel = '/index.html';
    if (rel.endsWith('/')) rel = '${rel}index.html';

    final normalized = p.normalize(rel).replaceAll('\\', '/');
    if (normalized.startsWith('..')) {
      res.statusCode = HttpStatus.forbidden;
      await res.close();
      return;
    }

    final file = File(p.join(webRoot, normalized.replaceAll('/', p.separator)));
    if (!await file.exists()) {
      res.statusCode = HttpStatus.notFound;
      res.headers.set(HttpHeaders.contentTypeHeader, 'text/html; charset=utf-8');
      res.write('<h1>404 Not Found</h1>');
      await res.close();
      return;
    }

    final length = await file.length();
    final mime = _mimeFor(file.path);
    final range = req.headers.value(HttpHeaders.rangeHeader);

    if (range != null && range.startsWith('bytes=')) {
      final spec = range.substring(6).split(',').first.trim();
      final parts = spec.split('-');
      var start = 0;
      var end = length - 1;
      if (parts.isNotEmpty && parts[0].isNotEmpty) {
        start = int.tryParse(parts[0]) ?? 0;
      }
      if (parts.length > 1 && parts[1].isNotEmpty) {
        end = int.tryParse(parts[1]) ?? end;
      }
      if (start > end || start >= length) {
        res.statusCode = HttpStatus.requestedRangeNotSatisfiable;
        res.headers.set(HttpHeaders.contentRangeHeader, 'bytes */$length');
        await res.close();
        return;
      }
      if (end >= length) end = length - 1;
      res.statusCode = HttpStatus.partialContent;
      res.headers.set(HttpHeaders.contentTypeHeader, mime);
      res.headers.set(HttpHeaders.acceptRangesHeader, 'bytes');
      res.headers.set(HttpHeaders.contentRangeHeader, 'bytes $start-$end/$length');
      res.headers.set(HttpHeaders.contentLengthHeader, '${end - start + 1}');
      await res.addStream(file.openRead(start, end + 1));
      await res.close();
      return;
    }

    res.statusCode = HttpStatus.ok;
    res.headers.set(HttpHeaders.contentTypeHeader, mime);
    res.headers.set(HttpHeaders.contentLengthHeader, '$length');
    res.headers.set(HttpHeaders.acceptRangesHeader, 'bytes');
    await res.addStream(file.openRead());
    await res.close();
  }

  // ---------- 辅助 ----------
  String _mimeFor(String filePath) {
    final ext = p.extension(filePath).toLowerCase();
    return _mime[ext] ?? 'application/octet-stream';
  }

  void _cors(HttpResponse res) {
    res.headers.set('Access-Control-Allow-Origin', '*');
    res.headers.set('Access-Control-Allow-Methods', 'GET, POST, DELETE, OPTIONS');
    res.headers.set('Access-Control-Allow-Headers', 'Content-Type');
  }

  Future<void> _json(HttpResponse res, int status, Object? payload) async {
    _cors(res);
    res.statusCode = status;
    res.headers.set(HttpHeaders.contentTypeHeader, 'application/json; charset=utf-8');
    res.write(jsonEncode(payload));
    await res.close();
  }

  Future<Map<String, dynamic>> _readJsonBody(HttpRequest req) async {
    const maxBytes = 1024 * 1024;
    final builder = BytesBuilder(copy: false);
    await for (final chunk in req) {
      builder.add(chunk);
      if (builder.length > maxBytes) throw _TooLarge();
    }
    if (builder.isEmpty) return <String, dynamic>{};
    final decoded = jsonDecode(utf8.decode(builder.takeBytes()));
    if (decoded is Map) return Map<String, dynamic>.from(decoded);
    return <String, dynamic>{};
  }

  String _randomHex(int bytes) {
    final rnd = Random.secure();
    final buffer = StringBuffer();
    for (var i = 0; i < bytes; i++) {
      buffer.write(rnd.nextInt(256).toRadixString(16).padLeft(2, '0'));
    }
    return buffer.toString();
  }
}

class _TooLarge implements Exception {}
