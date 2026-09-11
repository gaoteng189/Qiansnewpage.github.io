# 小说书库目录

把 `.txt` 小说文件放进这个目录，启动后端（`node server.js` 或 `start-server.exe`）后，
小说阅读页（`/novel/`）的「服务器书库」就会自动列出它们，点击即可阅读。

## 说明

- 只读取本目录下的 `.txt` 文件，支持 UTF-8 / GBK 编码。
- 文件名即书名（去掉 `.txt` 后缀后展示）。
- 通过 Tailscale Funnel 暴露时，公网也能读到这些书（接口：`/api/novels`）。
- 本目录下的 `.txt` 文件**不会被提交到 Git 仓库**（见 `.gitignore`），避免版权与体积问题。
