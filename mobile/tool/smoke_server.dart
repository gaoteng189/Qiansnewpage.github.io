// 内置服务冒烟测试（无需设备/模拟器）：
//   dart run tool/smoke_server.dart
//
// 覆盖点：
//  - 静态资源能否正确定位（Windows 上 p.join 的根相对路径陷阱 → 曾导致全站 404）
//  - 目录请求补 index.html
//  - 子目录页面
//  - Range 请求（视频拖动进度条依赖）
//  - 接口：留言板 / 小说书库
//  - 路径穿越防护
// ignore_for_file: avoid_print
import 'dart:convert';
import 'dart:io';

import 'package:path/path.dart' as p;
import 'package:qiansnewpage/local_server.dart';

int _failed = 0;

void check(String name, bool ok, [String extra = '']) {
  print('${ok ? '  OK  ' : ' FAIL '} $name${extra.isEmpty ? '' : '  ($extra)'}');
  if (!ok) _failed++;
}

Future<void> main() async {
  final tmp = await Directory.systemTemp.createTemp('qians-smoke-');
  final www = Directory(p.join(tmp.path, 'www'));
  await www.create(recursive: true);
  await Directory(p.join(tmp.path, 'novels')).create(recursive: true);

  await File(p.join(www.path, 'index.html')).writeAsString('<h1>home</h1>');
  await Directory(p.join(www.path, 'game')).create(recursive: true);
  await File(p.join(www.path, 'game', 'index.html')).writeAsString('<h1>game</h1>');
  await File(p.join(www.path, 'big.bin')).writeAsBytes(List<int>.filled(1000, 7));
  await File(p.join(tmp.path, 'novels', 'a.txt')).writeAsString('小说内容');

  final server = LocalServer(webRoot: www.path, dataDir: tmp.path);
  final port = await server.start();
  final base = 'http://127.0.0.1:$port';
  final client = HttpClient();

  Future<HttpClientResponse> get(String path, {String? range}) async {
    final req = await client.getUrl(Uri.parse('$base$path'));
    if (range != null) req.headers.set(HttpHeaders.rangeHeader, range);
    return req.close();
  }

  try {
    print('静态资源');

    var res = await get('/');
    var body = await res.transform(utf8.decoder).join();
    check('GET / 返回 200', res.statusCode == 200, '实际 ${res.statusCode}');
    check('GET / 内容为 index.html', body.contains('home'), body);

    res = await get('/game/');
    body = await res.transform(utf8.decoder).join();
    check('GET /game/ 返回 200（目录补 index.html）', res.statusCode == 200,
        '实际 ${res.statusCode}');
    check('GET /game/ 内容正确', body.contains('game'), body);

    res = await get('/game/index.html');
    check('GET /game/index.html 返回 200', res.statusCode == 200, '实际 ${res.statusCode}');

    res = await get('/game');
    await res.drain<void>();
    check('GET /game（无斜杠）不 500', res.statusCode != 500, '实际 ${res.statusCode}');

    res = await get('/big.bin', range: 'bytes=10-19');
    final chunk = await res.fold<List<int>>([], (a, b) => a..addAll(b));
    check('Range 请求返回 206', res.statusCode == 206, '实际 ${res.statusCode}');
    check('Range 请求返回 10 字节', chunk.length == 10, '实际 ${chunk.length}');

    res = await get('/../LICENSE');
    await res.drain<void>();
    check('路径穿越被拒绝', res.statusCode == 403 || res.statusCode == 404,
        '实际 ${res.statusCode}');

    print('接口');
    res = await get('/api/messages');
    body = await res.transform(utf8.decoder).join();
    check('GET /api/messages 返回 200', res.statusCode == 200, '实际 ${res.statusCode}');
    check('GET /api/messages 返回空数组', body.trim() == '[]', body);

    res = await get('/api/novels');
    body = await res.transform(utf8.decoder).join();
    final novels = jsonDecode(body);
    check('GET /api/novels 返回 200', res.statusCode == 200, '实际 ${res.statusCode}');
    check('GET /api/novels 列出 a.txt',
        novels is List && novels.any((e) => e['name'] == 'a.txt'), body);

    print('不存在的资源');
    res = await get('/nope.html');
    await res.drain<void>();
    check('GET /nope.html 返回 404', res.statusCode == 404, '实际 ${res.statusCode}');
  } finally {
    client.close(force: true);
    await server.stop();
    await tmp.delete(recursive: true);
  }

  print('');
  if (_failed == 0) {
    print('全部通过 ✅');
  } else {
    print('失败 $_failed 项 ❌');
    exitCode = 1;
  }
}
