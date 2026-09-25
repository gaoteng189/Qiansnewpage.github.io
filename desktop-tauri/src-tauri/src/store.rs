//! 数据层：留言读写、小说书库、ID 与删除凭证生成
//!
//! 数据格式与 `server.js` 完全一致（`messages.json`、`novels/`），
//! 因此从 Electron 版升级过来的用户可以直接沿用原有数据。

use std::fs;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

/// 一条留言（含服务端内部字段 tokenHash）
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Message {
    pub id: String,
    pub name: String,
    pub message: String,
    pub time: String,
    #[serde(rename = "replyTo", default)]
    pub reply_to: String,
    #[serde(rename = "tokenHash", default)]
    pub token_hash: String,
}

/// 对外输出用：去掉 tokenHash，只保留展示所需字段
#[derive(Debug, Clone, Serialize)]
pub struct PublicMessage {
    pub id: String,
    pub name: String,
    pub message: String,
    pub time: String,
    #[serde(rename = "replyTo")]
    pub reply_to: String,
}

impl From<&Message> for PublicMessage {
    fn from(m: &Message) -> Self {
        Self {
            id: m.id.clone(),
            name: m.name.clone(),
            message: m.message.clone(),
            time: m.time.clone(),
            reply_to: m.reply_to.clone(),
        }
    }
}

/// 小说书库里的一项
#[derive(Debug, Clone, Serialize)]
pub struct NovelInfo {
    pub name: String,
    pub size: u64,
    pub mtime: i64,
}

/// 应用状态：静态资源根目录 + 可写数据目录
#[derive(Clone)]
pub struct AppState {
    /// 静态资源根目录（安装目录下的 www/）
    pub www: PathBuf,
    /// 可写数据目录（%APPDATA%\千叶新页）
    pub data_dir: PathBuf,
    /// 小说书库目录
    pub novel_dir: PathBuf,
}

impl AppState {
    pub fn new(www: PathBuf, data_dir: PathBuf) -> Self {
        let novel_dir = data_dir.join("novels");
        Self {
            www,
            data_dir,
            novel_dir,
        }
    }

    pub fn messages_file(&self) -> PathBuf {
        self.data_dir.join("messages.json")
    }

    /// 建好数据目录；小说目录里放一份说明文件，方便用户知道该往哪放书
    pub fn ensure_dirs(&self) -> std::io::Result<()> {
        fs::create_dir_all(&self.data_dir)?;
        fs::create_dir_all(&self.novel_dir)?;

        let readme = self.novel_dir.join("README.md");
        if !readme.exists() {
            // 建说明文件失败不影响主流程
            let _ = fs::write(
                &readme,
                "# 小说书库\n\n把 `.txt` 小说文件放到这个目录，应用内「小说阅读器 → 服务器书库」即可读到。\n",
            );
        }
        Ok(())
    }

    /// 读取全部留言；文件不存在或损坏时返回空列表（与 JS 版行为一致）
    pub fn read_messages(&self) -> Vec<Message> {
        let Ok(raw) = fs::read_to_string(self.messages_file()) else {
            return Vec::new();
        };
        serde_json::from_str(&raw).unwrap_or_default()
    }

    pub fn write_messages(&self, list: &[Message]) -> std::io::Result<()> {
        let text = serde_json::to_string_pretty(list)
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))?;
        fs::write(self.messages_file(), text)
    }

    pub fn list_novels(&self) -> Vec<NovelInfo> {
        let Ok(entries) = fs::read_dir(&self.novel_dir) else {
            return Vec::new();
        };

        let mut list: Vec<NovelInfo> = entries
            .filter_map(|e| e.ok())
            .filter_map(|e| {
                let name = e.file_name().to_string_lossy().to_string();
                if !is_txt(&name) {
                    return None;
                }
                let meta = e.metadata().ok()?;
                if !meta.is_file() {
                    return None;
                }
                let mtime = meta
                    .modified()
                    .ok()
                    .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
                    .map(|d| d.as_millis() as i64)
                    .unwrap_or(0);
                Some(NovelInfo {
                    name,
                    size: meta.len(),
                    mtime,
                })
            })
            .collect();

        list.sort_by(|a, b| a.name.cmp(&b.name));
        list
    }

    /// 把请求里的书名解析成书库目录下的安全路径（防目录穿越）
    pub fn resolve_novel(&self, raw: &str) -> Option<PathBuf> {
        // 只取最后一段文件名，天然挡掉 ../ 之类
        let base = Path::new(raw.trim())
            .file_name()?
            .to_string_lossy()
            .to_string();
        if base.is_empty() || base == "." || base == ".." || !is_txt(&base) {
            return None;
        }

        let full = self.novel_dir.join(&base);
        // 双保险：确认结果仍在书库目录内
        if !full.starts_with(&self.novel_dir) {
            return None;
        }
        Some(full)
    }
}

fn is_txt(name: &str) -> bool {
    name.to_lowercase().ends_with(".txt")
}

/// 与 Node 的 `crypto.createHash('sha256').update(s).digest('hex')` 等价
pub fn sha256_hex(input: &str) -> String {
    let mut hasher = Sha256::new();
    hasher.update(input.as_bytes());
    hex::encode(hasher.finalize())
}

/// 删除凭证：16 字节随机数的十六进制串（对应 JS 的 randomBytes(16).toString('hex')）
pub fn generate_token() -> String {
    let mut buf = [0u8; 16];
    rand::Rng::fill(&mut rand::rng(), &mut buf);
    hex::encode(buf)
}

/// 留言 ID：`<毫秒-base36>-<随机-base36>`，与 JS 版格式一致
pub fn generate_id() -> String {
    let ms = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0);

    let mut rnd = [0u8; 4];
    rand::Rng::fill(&mut rand::rng(), &mut rnd);
    let n = u32::from_le_bytes(rnd) as u64;

    let suffix = to_base36(n);
    let suffix = &suffix[..suffix.len().min(6)];
    format!("{}-{}", to_base36(ms), suffix)
}

fn to_base36(mut n: u64) -> String {
    const DIGITS: &[u8] = b"0123456789abcdefghijklmnopqrstuvwxyz";
    if n == 0 {
        return "0".to_string();
    }
    let mut out = Vec::new();
    while n > 0 {
        out.push(DIGITS[(n % 36) as usize]);
        n /= 36;
    }
    out.reverse();
    String::from_utf8(out).unwrap_or_else(|_| "0".to_string())
}

/// UTC 毫秒级 ISO 8601 时间，对应 JS 的 `new Date().toISOString()`
pub fn now_iso() -> String {
    chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Millis, true)
}

/// 按 JS 的 `String(x).trim().slice(0, n)` 语义截断（按字符而非字节）
pub fn trim_slice(input: &str, max_chars: usize) -> String {
    input.trim().chars().take(max_chars).collect()
}
