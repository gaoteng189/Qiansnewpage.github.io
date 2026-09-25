import 'dart:io';

import 'package:archive/archive.dart';
import 'package:flutter/services.dart';
import 'package:path/path.dart' as p;

/// 网站资源版本号：网站内容有大改动时递增，会触发重新解压
const String kWebAssetsVersion = '1';

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
