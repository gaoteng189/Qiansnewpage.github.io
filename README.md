# Qiansnewpage

个人站点：在线工具箱 + 小游戏 + 小说阅读 + 留言板，统一采用 Material Design 3（M3）棕色扁平设计语言。

## 功能

- **在线工具箱**（`index.html`）：函数图像绘制、Base64 编解码、SHA-256 哈希、随机密码生成器、JSON 格式化/校验、时间戳转换、URL 编解码、颜色转换、文本统计与整理，全部纯前端、无需联网、数据不离开浏览器。
- **2D 跑酷小游戏**（`game/`）：Canvas 自绘图形，支持简单/普通/困难三档难度、最高分记录与音效开关。
- **视频播放测试**（`video/`）：视频播放器页面。
- **小说阅读器**（`novel/`）：可导入本地 txt，也可从「服务器书库」读取服务器 `novels/` 目录里的小说（经 Funnel 下发）；自动识别「第 x 章」等标题生成目录，支持全文搜索、书签、阅读时长统计、字号/行距调节、默认/护眼/夜间三种背景、章节键盘切换与阅读进度记忆。
- **待办清单**（`todo/`）：添加/勾选/编辑/删除待办，支持按状态筛选与清除已完成，数据保存在浏览器本地。
- **留言板**（`message/`）：HTTP 轮询留言板，支持回复（楼中楼）、表情快捷输入、删除自己的留言与数量统计，数据保存在运行服务器的主机上，可公网访问。

## 目录结构

```text
├── index.html          # 首页（在线工具箱）
├── game/               # 2D 跑酷小游戏
├── video/              # 视频播放测试
├── novel/              # 小说阅读器
├── novels/             # 服务器端小说书库（txt 文件不入库）
├── message/            # 留言板页面
├── todo/               # 待办清单
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
- `POST /api/messages` —— 发布留言，JSON 请求体 `{ "name": "...", "message": "...", "replyTo": "可选，被回复留言的 id" }`
  返回 `{ ok, token, item }`，其中 `token` 是删除凭证（仅发布者持有，服务端只保存其哈希）
- `DELETE /api/messages/<id>` —— 删除自己的留言，JSON 请求体 `{ "token": "..." }`，会连同其下的回复一起删除
- `GET /api/novels` —— 列出服务器书库（`novels/` 目录）里的小说
- `GET /api/novels/<文件名>` —— 下载小说原文，前端自行按 UTF-8 / GBK 解码

小说书库：把 `.txt` 小说放进服务器的 `novels/` 目录即可，阅读页的「服务器书库」会自动列出
（该目录下的 txt 已被 `.gitignore` 忽略，不会提交到仓库）。

### 公网访问（Tailscale Funnel）

留言板通过 [Tailscale Funnel](https://tailscale.com/kb/1223/funnel) 暴露到公网，固定 HTTPS 地址、无需自有域名：

```bash
tailscale funnel --bg 50304
```

地址形如 `https://<机器名>.<tailnet名>.ts.net/message/`。

### 一键启动（Windows）

直接运行 `bin/start-server.exe`：

- **启动**：启动 `server.js` 后端 + 启用 Tailscale Funnel，并显示公网地址
- **停止**：停止后端并关闭 Funnel
- 端口可在界面修改，保存到 `.server-port`

## 技术栈

- 前端：原生 HTML / CSS / JavaScript
- 后端：Node.js（零依赖，原生 http / fs / crypto）
- 启动程序：C++ / Qt6（MSYS2 UCRT64 工具链）
- 内网穿透：Tailscale Funnel
