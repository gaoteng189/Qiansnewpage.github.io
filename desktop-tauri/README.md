# 千叶新页 · 桌面版（Tauri 2）

用 Tauri 2 重写的桌面端，替换原来的 Electron 版。

## 为什么换

Electron 把整个 Chromium + Node.js 打进安装包，本项目因此达到 **191.89 MB**。
Tauri 改用**系统内置的 WebView2**，不再自带浏览器内核。

| 方案 | 安装包 | 免安装可执行文件 | 说明 |
| --- | --- | --- | --- |
| Electron（旧） | 191.89 MB | 234.70 MB | 含 Chromium 与 Node 运行时 |
| Tauri（本目录） | **87.05 MB** | **6.93 MB** | 免安装版只有几 MB |
| Tauri + `SKIP_VIDEO=1` | ~7 MB | 6.93 MB | 视频不打包，视频页需联网 |

网站资源里 86.6 MB 是 4 个 1080p 视频，真正属于"外壳"的开销只有几 MB ——
所以**要不要打包视频，决定了这个应用是 92 MB 还是 8 MB**。

## 架构

与旧版 Electron 完全一致：Rust 侧起一个只监听 `127.0.0.1` 的 HTTP 服务，
窗口直接加载 `http://127.0.0.1:<port>/`。

**所以前端 6 个页面一行都没有改。** 后端接口在
[`src-tauri/src/api.rs`](src-tauri/src/api.rs) 里逐条对齐 `server.js`：

| 接口 | 说明 |
| --- | --- |
| `GET /api/messages` | 读取留言（自动剔除 tokenHash） |
| `POST /api/messages` | 发布留言，返回删除凭证 |
| `DELETE /api/messages/{id}` | 校验令牌哈希后删除，连同其回复 |
| `GET /api/novels` | 列出书库里的 txt |
| `GET /api/novels/{name}` | 下发原始字节，交给前端按编码解码 |

数据格式与旧版**完全兼容**（同一个 `messages.json`、同一个 `novels/` 目录），
从 Electron 版升级过来可以直接沿用原有留言和书架。

顺带修好一个旧版的小毛病：`server.js` 用 `fs.readFile` 全量返回静态文件，
**不支持 Range**，视频拖动进度条是不灵的；这里用 `tower-http` 的 `ServeDir`，
Range 天然可用。

## 构建前置：MSVC 工具链（必须）

Rust 在 Windows 上用的是 MSVC 工具链，需要 VC++ 生成工具提供链接器。
缺少时 `cargo` 连一个可执行文件都链接不出来（`cargo check` 会因为 build script
需要编译执行而一并失败）。

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools `
  --accept-package-agreements --accept-source-agreements `
  --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

> 这一步需要管理员权限，需要自行在管理员 PowerShell 里执行。
> 下载约 2–4 GB，视网速 10–30 分钟。

验证：

```powershell
Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022' -Directory
# 应能看到 BuildTools 目录；空目录说明还没装成功
```

其余依赖本机已就绪：Rust 1.92、Node 24、WebView2 运行时。

## 构建

```powershell
cd desktop-tauri
npm install          # 只装 @tauri-apps/cli
npm run build        # = node scripts/build-www.js && tauri build
```

产物：

- 安装包 `src-tauri/target/release/bundle/nsis/千叶新页_1.0.0_x64-setup.exe`
- 免安装可执行文件 `src-tauri/target/release/千叶新页.exe`

开发调试（热重载前端改动需要重启，因为资源是复制进去的）：

```powershell
npm run dev
```

只想瘦身、不需要本地视频：

```powershell
$env:SKIP_VIDEO='1'; npm run build
```

## 目录结构

```text
desktop-tauri/
├── package.json               # 只用于拉起 @tauri-apps/cli
├── scripts/build-www.js       # 从仓库根同步网站资源到 src-tauri/www
└── src-tauri/
    ├── Cargo.toml
    ├── tauri.conf.json
    ├── icons/icon.ico
    ├── www/                   # 生成的资源副本（87 MB，已 gitignore）
    └── src/
        ├── main.rs            # 入口、窗口、中文菜单、生命周期
        ├── api.rs             # HTTP 服务与路由
        └── store.rs           # 留言读写、小说书库、ID 与令牌生成
```

## 实测验证

构建产物经过实际运行验证——启动应用后直接对本地服务发 HTTP 请求：

| 项目 | 结果 |
| --- | --- |
| 6 个页面（`/` `/novel/` `/todo/` `/message/` `/game/` `/video/`） | 全部 200 |
| `GET /api/messages`、`GET /api/novels` | 200，返回 `[]` |
| `POST /api/messages` | ok，id 为 base36 格式，token 32 位十六进制 |
| 响应是否泄漏 `tokenHash` | **否** |
| 中文往返（`中文测试：你好，世界 ✓`） | 正确 |
| `DELETE /api/messages/{id}` | ok，removed=1，删后计数归零 |
| 错误处理 | 空内容 400 / 无 token 400 / 不存在 404 / PUT 405 |
| 路径穿越 `/..%2fREADME.md` | 404 拦截 |
| Range `bytes=0-99` | **206，返回 100 字节**（旧版做不到） |
| 数据目录 | `%APPDATA%\千叶新页\`，含 `novels\README.md` |

## 与旧版的差异

功能上对齐了 Electron 版，另外：

- 菜单「文件」里多了**后退**（`Alt+Left`），因为 `target="_blank"` 的内链
  现在改为窗口内导航（由注入脚本统一处理，行为与移动端 Flutter 版一致）；
  站外链接仍然交给系统浏览器。
- 缩放菜单用 `set_zoom` 实现（旧版是 Chromium 内置的 zoom role）。
- 关于对话框显示的是 Tauri + 系统 WebView2，不再显示 Electron/Chromium 版本。
