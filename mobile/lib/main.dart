import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';
import 'package:webview_flutter/webview_flutter.dart';

import 'local_server.dart';
import 'web_assets.dart';

const String kAppName = '千叶新页';
const Color kPrimary = Color(0xFF0EA5E9);
const Color kSurface = Color(0xFFF3F6F9);
const Color kBorder = Color(0xFFE3E9EF);
const Color kText = Color(0xFF1B1B1B);
const Color kText2 = Color(0xFF5A6672);

/// 站点里工具入口用的是 target="_blank"。
/// 部分 WebView 实现下这种链接不会导航（或没有可用的新窗口），
/// 表现为「点了没反应」。这里注入脚本把 _blank 链接改成原页跳转，
/// 保证点击必定有响应，且随时可以用返回栏退回。
const String _blankShimJs = r'''
(function () {
  if (window.__qiansBlankShim) return;
  window.__qiansBlankShim = true;
  document.addEventListener('click', function (e) {
    var a = e.target && e.target.closest ? e.target.closest('a[target="_blank"]') : null;
    if (!a || !a.href) return;
    e.preventDefault();
    location.href = a.href;
  }, true);
})();
''';

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

  /// 网页是否有上一页（即当前不在首页），用于决定是否显示返回栏
  bool _canGoBack = false;

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
      _canGoBack = false;
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
            onPageFinished: (_) async {
              if (!mounted) return;
              setState(() => _progress = 1);
              await _injectBlankShim();
              await _refreshCanGoBack();
            },
            onWebResourceError: (err) {
              if (!mounted) return;
              if (err.isForMainFrame ?? false) {
                setState(() => _error = '页面加载失败：${err.description}');
              }
            },
          ),
        );

      // 先赋值再发起加载：否则 onPageFinished 回调里读不到 controller，返回栏不会出现
      _controller = controller;
      await controller.loadRequest(Uri.parse('http://127.0.0.1:$port/'));

      if (!mounted) return;
      setState(() {
        _ready = true;
        _stage = '';
      });
      await _refreshCanGoBack();
    } catch (e) {
      if (mounted) setState(() => _error = '启动失败：$e');
    }
  }

  /// 注入 _blank 兜底脚本（注入失败不影响主流程）
  Future<void> _injectBlankShim() async {
    final controller = _controller;
    if (controller == null) return;
    try {
      await controller.runJavaScript(_blankShimJs);
    } catch (_) {}
  }

  /// 同步「能否返回」，用于控制返回栏的显示
  Future<void> _refreshCanGoBack() async {
    final controller = _controller;
    if (controller == null) return;
    final can = await controller.canGoBack();
    if (!mounted || can == _canGoBack) return;
    setState(() => _canGoBack = can);
  }

  /// 返回首页：优先走浏览历史，没历史时直接回到根地址
  Future<void> _goBack() async {
    final controller = _controller;
    if (controller == null) return;
    if (await controller.canGoBack()) {
      await controller.goBack();
    } else {
      final port = _server?.port ?? 0;
      await controller.loadRequest(Uri.parse('http://127.0.0.1:$port/'));
    }
    await _refreshCanGoBack();
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, _) async {
        if (didPop) return;
        if (_canGoBack) {
          await _goBack();
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
                style: const TextStyle(fontSize: 13, color: kText2),
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
                style: const TextStyle(fontSize: 14, color: kText2)),
          ],
        ),
      );
    }

    return Column(
      children: [
        // 非首页时显示返回栏：网页整体下移，不会遮挡页面自身的标题
        if (_canGoBack) _BackBar(onBack: _goBack),
        Expanded(
          child: Stack(
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
          ),
        ),
      ],
    );
  }
}

/// 顶部返回栏：仅在存在上一页（即不在首页）时显示
class _BackBar extends StatelessWidget {
  const _BackBar({required this.onBack});

  final VoidCallback onBack;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 48,
      decoration: const BoxDecoration(
        color: kSurface,
        border: Border(bottom: BorderSide(color: kBorder)),
      ),
      child: Row(
        children: [
          const SizedBox(width: 6),
          Semantics(
            button: true,
            label: '返回首页',
            child: Material(
              color: Colors.transparent,
              borderRadius: BorderRadius.circular(4),
              child: InkWell(
                onTap: onBack,
                borderRadius: BorderRadius.circular(4),
                child: const SizedBox(
                  width: 40,
                  height: 40,
                  child: Icon(Icons.arrow_back_rounded,
                      size: 20, color: kPrimary),
                ),
              ),
            ),
          ),
          const SizedBox(width: 6),
          const Text(
            kAppName,
            style: TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w600,
              color: kText,
            ),
          ),
        ],
      ),
    );
  }
}
