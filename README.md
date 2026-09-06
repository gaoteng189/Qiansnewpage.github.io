# Qiansnewpage

个人站点：在线工具箱 + 小游戏 + 留言板，统一采用 Material Design 3（M3）棕色扁平设计语言。

## 功能

- **在线工具箱**（`index.html`）：函数图像绘制、Base64 编解码、SHA-256 哈希、随机密码生成器，全部纯前端、无需联网、数据不离开浏览器。
- **2D 跑酷小游戏**（`game/`）：Canvas 自绘图形的小游戏。
- **视频播放测试**（`video/`）：视频播放器页面。
- **留言板**（`message/`）：HTTP 轮询留言板，数据保存在运行服务器的主机上，可公网访问。

## 目录结构

```
├── index.html          # 首页（在线工具箱）
├── game/               # 2D 跑酷小游戏
├── video/              # 视频播放测试
├── message/            # 留言板页面
├── server.js           # 留言板后端（零依赖 Node.js）
├── start-server.cpp    # 启动程序源码（Qt6 GUI）
├── stub.cpp            # 单 exe 启动器（Win32，内嵌运行时依赖）
└── bin/                # 编译好的 start-server.exe（单文件）
```

## 部署

### 静态页面（GitHub Pages）

首页、游戏、视频托管在 GitHub Pages：`https://qiansnewpage.github.io/`

### 留言板后端

留言板数据保存在运行服务器的主机上（`messages.json`），需在本地运行：

```bash
node server.js 50304
```

HTTP 接口：

- `GET /api/messages` —— 读取全部留言
- `POST /api/messages` —— 发布留言，JSON 请求体 `{ "name": "...", "message": "..." }`

### 公网访问（Tailscale Funnel）

留言板通过 [Tailscale Funnel](https://tailscale.com/kb/1223/funnel) 暴露到公网，固定 HTTPS 地址、无需自有域名：

```bash
tailscale funnel --bg 50304
```

地址形如 `https://<机器名>.<tailnet名>.ts.net/message/`。

### 一键启动（Windows）

直接运行 `bin/start-server.exe`：

- **Start**：启动 `server.js` 后端 + 启用 Tailscale Funnel，并显示公网地址
- **Stop**：停止后端并关闭 Funnel
- 端口可在界面修改，保存到 `.server-port`

## 技术栈

- 前端：原生 HTML / CSS / JavaScript
- 后端：Node.js（零依赖，原生 http / fs / crypto）
- 启动程序：C++ / Qt6（MSYS2 UCRT64 工具链）
- 内网穿透：Tailscale Funnel
