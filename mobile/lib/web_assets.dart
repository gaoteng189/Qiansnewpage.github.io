import 'dart:io';

import 'package:archive/archive.dart';
import 'package:flutter/services.dart';
import 'package:path/path.dart' as p;

/// 页面资源版本号：网站内容有大改动时递增，会触发重新解压
/// - 2 → 3：视频已从 www.zip 中拆出，改为按需落盘
const String kWebAssetsVersion = '3';

/// 确保网站资源已解压到应用目录，返回站点根目录
///
/// APK 里的 assets 是只读的，而内置服务需要按文件流提供静态资源
/// （视频还要支持 Range 请求），因此首次启动先解压到应用数据目录。
Future<Directory> ensureWebRoot(Directory supportDir) async {
  final www = Directory(p.join(supportDir.path, 'www'));
  final stamp = File(p.join(supportDir.path, '.www-version'));

  if (await www.exists() && await stamp.exists()) {
    final current = (await stamp.readAsString()).trim();
    if (current == kWebAssetsVersion) return www;
  }

  if (await www.exists()) {
    await www.delete(recursive: true);
  }
  await www.create(recursive: true);

  final data = await rootBundle.load('assets/www.zip');
  final bytes = data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes);
  final archive = ZipDecoder().decodeBytes(bytes, verify: false);

  for (final entry in archive) {
    final name = p.normalize(entry.name).replaceAll('\\', '/');
    if (name.startsWith('..')) continue;
    final outPath = p.join(www.path, name.replaceAll('/', p.separator));
    if (entry.isFile) {
      final file = File(outPath);
      await file.parent.create(recursive: true);
      await file.writeAsBytes(entry.content, flush: false);
    } else {
      await Directory(outPath).create(recursive: true);
    }
  }

  await stamp.writeAsString(kWebAssetsVersion);
  return www;
}

/// 需要按需落盘的资源：URL 路径前缀 → assets 键前缀
///
/// 视频共 86 MB，若和页面一起解压会让首启明显卡顿（内存峰值达 85 MB）。
/// 改为只在对应文件真的被请求时才从 APK 取出来写盘，单次最多 24.7 MB。
const Map<String, String> kLazyAssets = <String, String>{
  'video/': 'assets/video/',
};

/// 同一个目标文件被并发请求时，只真正落盘一次
final Map<String, Future<String?>> _inFlight = <String, Future<String?>>{};

/// 按需把 APK 内的大体积资源写到磁盘，返回落盘后的绝对路径。
///
/// [webRoot] 是站点根目录（解压目录）。落盘到它内部，静态服务第二次
/// 请求就能直接命中，不再回调。
/// 返回 null 表示该路径不属于按需资源，或 APK 里确实没有这个文件
/// （此时由上层的静态服务正常返回 404）。
Future<String?> materializeAsset(Directory webRoot, String urlPath) async {
  final rel = urlPath.replaceAll(RegExp(r'^/+'), '');
  final assetKey = _assetKeyFor(rel);
  if (assetKey == null) return null;

  final dest = File(p.join(webRoot.path, rel.replaceAll('/', p.separator)));
  if (await dest.exists()) return dest.path;

  // 视频播放器会并发发多个 Range 请求，必须做单飞，否则会重复写 24 MB
  final pending = _inFlight[dest.path];
  if (pending != null) return pending;

  final task = _writeAssetFile(assetKey, dest);
  _inFlight[dest.path] = task;
  try {
    return await task;
  } finally {
    _inFlight.remove(dest.path);
  }
}

Future<String?> _writeAssetFile(String assetKey, File dest) async {
  ByteData data;
  try {
    data = await rootBundle.load(assetKey);
  } catch (_) {
    return null; // APK 里没有这个资源
  }

  await dest.parent.create(recursive: true);
  // 先写 .part 再改名：中途被杀不会留下半截文件被当成已就绪
  final tmp = File('${dest.path}.part');
  await tmp.writeAsBytes(
    data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
    flush: true,
  );
  await tmp.rename(dest.path);
  return dest.path;
}

/// 把 URL 路径映射成 assets 键；不匹配或含可疑路径段时返回 null
String? _assetKeyFor(String rel) {
  final segments = rel.split('/');
  if (segments.any((s) => s.isEmpty || s == '.' || s == '..')) return null;

  for (final entry in kLazyAssets.entries) {
    if (rel.startsWith(entry.key)) {
      return entry.value + rel.substring(entry.key.length);
    }
  }
  return null;
}
