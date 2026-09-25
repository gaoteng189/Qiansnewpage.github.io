//! 本地 HTTP 服务：路由与 `server.js` 逐条对齐，前端页面无需任何改动
//!
//! 相比 Node 版额外获得 Range 支持（tower-http 的 `ServeDir` 自带），
//! 视频拖动进度条因此能正常工作。

use std::net::Ipv4Addr;

use axum::body::Bytes;
use axum::extract::{DefaultBodyLimit, Path, State};
use axum::http::{header, HeaderValue, Method, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::{delete, get};
use axum::Router;
use serde::Deserialize;
use serde_json::{json, Value};
use tokio::net::TcpListener;
use tokio::task::JoinHandle;
use tower_http::cors::{Any, CorsLayer};
use tower_http::services::ServeDir;

use crate::store::{
    generate_id, generate_token, now_iso, sha256_hex, trim_slice, AppState, Message, PublicMessage,
};

/// 请求体上限，与 `server.js` 的 1 MB 保持一致
const MAX_BODY: usize = 1024 * 1024;

/// 优先采用的端口（与旧版 Electron 一致），被占用时顺延
const PORT_ATTEMPTS: u16 = 30;

#[derive(Debug, Default, Deserialize)]
struct PostBody {
    #[serde(default)]
    name: Option<String>,
    #[serde(default)]
    message: Option<String>,
    #[serde(rename = "replyTo", default)]
    reply_to: Option<String>,
}

#[derive(Debug, Default, Deserialize)]
struct DeleteBody {
    #[serde(default)]
    token: Option<String>,
}

// ---------- 启动 ----------

/// 启动本地服务，返回 (实际端口, 后台任务句柄)
pub async fn serve(state: AppState, preferred_port: u16) -> Result<(u16, JoinHandle<()>), String> {
    let listener = bind_port(preferred_port, PORT_ATTEMPTS)
        .await
        .map_err(|e| format!("绑定端口失败：{e}"))?;
    let port = listener.local_addr().map_err(|e| e.to_string())?.port();

    let app = build_router(state);
    println!("[服务] 已启动: http://127.0.0.1:{port}/");

    let handle = tokio::spawn(async move {
        if let Err(e) = axum::serve(listener, app).await {
            eprintln!("[服务] 异常退出：{e}");
        }
    });

    Ok((port, handle))
}

/// 从 `preferred` 起顺延找空闲端口（对应 Electron 版的 `listenFree`），
/// 全被占用时退回让系统分配，避免因为端口冲突导致启动失败。
async fn bind_port(preferred: u16, attempts: u16) -> std::io::Result<TcpListener> {
    for offset in 0..attempts {
        let port = preferred.saturating_add(offset);
        if let Ok(listener) = TcpListener::bind((Ipv4Addr::LOCALHOST, port)).await {
            return Ok(listener);
        }
    }
    TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await
}

fn build_router(state: AppState) -> Router {
    // 静态资源走 ServeDir：自带目录补 index.html 与 Range 支持
    let static_files = ServeDir::new(state.www.clone())
        .append_index_html_on_directories(true)
        .not_found_service(get(not_found));

    Router::new()
        .route("/api/messages", get(get_messages).post(post_message))
        .route("/api/messages/{id}", delete(delete_message))
        .route("/api/novels", get(get_novels))
        .route("/api/novels/{name}", get(download_novel))
        .fallback_service(static_files)
        .layer(
            CorsLayer::new()
                .allow_origin(Any)
                .allow_methods([Method::GET, Method::POST, Method::DELETE])
                .allow_headers([header::CONTENT_TYPE]),
        )
        // 放宽到 2 MB，超过 1 MB 时由 handler 返回 JSON 格式的 413，
        // 否则 axum 会先拦下并返回非 JSON 响应，前端解析会报错
        .layer(DefaultBodyLimit::max(MAX_BODY * 2))
        .with_state(state)
}

// ---------- 留言板 ----------

/// GET /api/messages —— 读取全部留言（不含删除凭证哈希）
async fn get_messages(State(state): State<AppState>) -> Response {
    let list = state.read_messages();
    let public: Vec<PublicMessage> = list.iter().map(PublicMessage::from).collect();
    json_response(StatusCode::OK, to_value(public))
}

/// POST /api/messages —— 发布留言，返回删除凭证（仅发布者持有）
async fn post_message(State(state): State<AppState>, body: Bytes) -> Response {
    if body.len() > MAX_BODY {
        return json_response(StatusCode::PAYLOAD_TOO_LARGE, json!({ "error": "请求体过大" }));
    }
    let Ok(data) = serde_json::from_slice::<PostBody>(&body) else {
        return json_response(StatusCode::BAD_REQUEST, json!({ "error": "无效的 JSON 数据" }));
    };

    let name = trim_slice(data.name.as_deref().unwrap_or(""), 50);
    let message = trim_slice(data.message.as_deref().unwrap_or(""), 1000);
    if message.is_empty() {
        return json_response(StatusCode::BAD_REQUEST, json!({ "error": "留言内容不能为空" }));
    }

    let mut list = state.read_messages();

    // 只有指向已存在留言的 replyTo 才生效，否则当作普通留言
    let raw_reply = data.reply_to.as_deref().unwrap_or("").trim().to_string();
    let reply_to = if !raw_reply.is_empty() && list.iter().any(|m| m.id == raw_reply) {
        raw_reply
    } else {
        String::new()
    };

    let token = generate_token();
    let item = Message {
        id: generate_id(),
        name: if name.is_empty() {
            "匿名".to_string()
        } else {
            name
        },
        message,
        time: now_iso(),
        reply_to,
        token_hash: sha256_hex(&token),
    };
    list.push(item.clone());

    if let Err(e) = state.write_messages(&list) {
        return json_response(
            StatusCode::INTERNAL_SERVER_ERROR,
            json!({ "error": format!("写入留言失败：{e}") }),
        );
    }

    println!(
        "[留言] {}{}: {}",
        item.name,
        if item.reply_to.is_empty() { "" } else { "（回复）" },
        first_chars(&item.message, 80)
    );

    json_response(
        StatusCode::OK,
        json!({
            "ok": true,
            "token": token,
            "item": PublicMessage::from(&item),
        }),
    )
}

/// DELETE /api/messages/{id} —— 删除自己的留言（连同其直接回复）
async fn delete_message(
    State(state): State<AppState>,
    Path(id): Path<String>,
    body: Bytes,
) -> Response {
    if body.len() > MAX_BODY {
        return json_response(StatusCode::PAYLOAD_TOO_LARGE, json!({ "error": "请求体过大" }));
    }
    let Ok(data) = serde_json::from_slice::<DeleteBody>(&body) else {
        return json_response(StatusCode::BAD_REQUEST, json!({ "error": "无效的 JSON 数据" }));
    };

    let token = data.token.unwrap_or_default();
    if token.is_empty() {
        return json_response(StatusCode::BAD_REQUEST, json!({ "error": "缺少删除凭证" }));
    }

    let mut list = state.read_messages();
    let Some(pos) = list.iter().position(|m| m.id == id) else {
        return json_response(StatusCode::NOT_FOUND, json!({ "error": "留言不存在" }));
    };

    // 用哈希比对，明文凭证只在发布时返回过一次
    if list[pos].token_hash != sha256_hex(&token) {
        return json_response(StatusCode::FORBIDDEN, json!({ "error": "无权删除这条留言" }));
    }

    let removed = list
        .iter()
        .filter(|m| m.id == id || m.reply_to == id)
        .count();
    list.retain(|m| m.id != id && m.reply_to != id);

    if let Err(e) = state.write_messages(&list) {
        return json_response(
            StatusCode::INTERNAL_SERVER_ERROR,
            json!({ "error": format!("删除失败：{e}") }),
        );
    }

    println!("[删除] {id}（连同 {} 条回复）", removed.saturating_sub(1));
    json_response(StatusCode::OK, json!({ "ok": true, "removed": removed }))
}

// ---------- 小说书库 ----------

/// GET /api/novels —— 列出书库里的 txt
async fn get_novels(State(state): State<AppState>) -> Response {
    json_response(StatusCode::OK, to_value(state.list_novels()))
}

/// GET /api/novels/{name} —— 下发原始字节，交给前端按编码解码
async fn download_novel(State(state): State<AppState>, Path(name): Path<String>) -> Response {
    let Some(path) = state.resolve_novel(&name) else {
        return json_response(StatusCode::BAD_REQUEST, json!({ "error": "无效的文件名" }));
    };

    match tokio::fs::read(&path).await {
        Ok(bytes) => {
            let len = bytes.len();
            println!(
                "[小说] 下发 {}（{len} 字节）",
                path.file_name().unwrap_or_default().to_string_lossy()
            );
            (
                StatusCode::OK,
                [
                    (header::CONTENT_TYPE, "application/octet-stream"),
                    (header::CACHE_CONTROL, "no-cache"),
                ],
                bytes,
            )
                .into_response()
        }
        Err(_) => json_response(StatusCode::NOT_FOUND, json!({ "error": "小说不存在" })),
    }
}

// ---------- 兜底 ----------

/// 静态资源找不到时返回与 server.js 一致的 404 页面
async fn not_found() -> Response {
    (
        StatusCode::NOT_FOUND,
        [(header::CONTENT_TYPE, "text/html; charset=utf-8")],
        "<h1>404 Not Found</h1>",
    )
        .into_response()
}

fn json_response(status: StatusCode, body: Value) -> Response {
    (
        status,
        [(
            header::CONTENT_TYPE,
            HeaderValue::from_static("application/json; charset=utf-8"),
        )],
        body.to_string(),
    )
        .into_response()
}

fn to_value<T: serde::Serialize>(value: T) -> Value {
    serde_json::to_value(value).unwrap_or_else(|_| Value::Array(Vec::new()))
}

fn first_chars(s: &str, max: usize) -> String {
    s.chars().take(max).collect()
}
