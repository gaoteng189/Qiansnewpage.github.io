import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';
import 'package:webview_flutter/webview_flutter.dart';

import 'local_server.dart';
import 'web_assets.dart';

const String kAppName = '千叶新页';
const Color kPrimary = Color(0xFF0EA5E9);
const Color kSurface = Color(0xFFF3F6F9);

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const QiansApp());
}

class QiansApp extends StatelessWidget {
  const QiansApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: kAppName,
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: kPrimary),
        scaffoldBackgroundColor: kSurface,
        useMaterial3: true,
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  LocalServer? _server;
  WebViewController? _controller;
  String _stage = '正在准备…';
  double _progress = 0;
  String? _error;
  bool _ready = false;

  @override
  void initState() {
    super.initState();
    _boot();
  }

  @override
  void dispose() {
    _server?.stop();
    super.dispose();
  }

  /// 解压站点资源 → 启动内置服务 → 打开页面
  Future<void> _boot() async {
    setState(() {
      _error = null;
      _ready = false;
      _stage = '正在准备站点资源…';
      _progress = 0;
    });

    try {
      final support = await getApplicationSupportDirectory();
      final www = await ensureWebRoot(support);

      if (mounted) setState(() => _stage = '正在启动本地服务…');

      final server = LocalServer(webRoot: www.path, dataDir: support.path);
      final port = await server.start();
      _server = server;

      final controller = WebViewController()
        ..setJavaScriptMode(JavaScriptMode.unrestricted)
        ..setBackgroundColor(kSurface)
        ..setNavigationDelegate(
          NavigationDelegate(
            onProgress: (value) {
              if (mounted) {
                setState(() => _progress = (value / 100).clamp(0.0, 1.0));
              }
            },
            onPageFinished: (_) {
              if (mounted) setState(() => _progress = 1);
            },
            onWebResourceError: (err) {
              if (!mounted) return;
              if (err.isForMainFrame ?? false) {
                setState(() => _error = '页面加载失败：${err.description}');
              }
            },
          ),
        )
        ..loadRequest(Uri.parse('http://127.0.0.1:$port/'));

      if (!mounted) return;
      setState(() {
        _controller = controller;
        _ready = true;
        _stage = '';
      });
    } catch (e) {
      if (mounted) setState(() => _error = '启动失败：$e');
    }
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, _) async {
        if (didPop) return;
        final controller = _controller;
        if (controller != null && await controller.canGoBack()) {
          await controller.goBack();
          return;
        }
        if (mounted) SystemNavigator.pop();
      },
      child: Scaffold(
        body: SafeArea(child: _buildBody()),
      ),
    );
  }

  Widget _buildBody() {
    final error = _error;
    if (error != null) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(28),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.error_outline, size: 44, color: kPrimary),
              const SizedBox(height: 14),
              const Text('出错了',
                  style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
              const SizedBox(height: 8),
              Text(
                error,
                textAlign: TextAlign.center,
                style: const TextStyle(fontSize: 13, color: Color(0xFF52443C)),
              ),
              const SizedBox(height: 20),
              FilledButton(
                onPressed: _boot,
                style: FilledButton.styleFrom(backgroundColor: kPrimary),
                child: const Text('重试'),
              ),
            ],
          ),
        ),
      );
    }

    final controller = _controller;
    if (!_ready || controller == null) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const SizedBox(
              width: 34,
              height: 34,
              child: CircularProgressIndicator(strokeWidth: 3, color: kPrimary),
            ),
            const SizedBox(height: 16),
            Text(_stage,
                style: const TextStyle(fontSize: 14, color: Color(0xFF52443C))),
          ],
        ),
      );
    }

    return Stack(
      children: [
        WebViewWidget(controller: controller),
        if (_progress < 1)
          Positioned(
            left: 0,
            right: 0,
            top: 0,
            child: LinearProgressIndicator(
              value: _progress == 0 ? null : _progress,
              minHeight: 3,
              color: kPrimary,
              backgroundColor: Colors.transparent,
            ),
          ),
      ],
    );
  }
}
