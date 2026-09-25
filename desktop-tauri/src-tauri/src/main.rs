// 千叶新页桌面版（Tauri 2）
//
// 架构与旧版 Electron 一致：Rust 侧起一个只监听 127.0.0.1 的 HTTP 服务，
// 窗口直接加载 http://127.0.0.1:<port>/ ，因此前端 6 个页面一行都不用改。
//
// 与旧版的差别只在壳层：这里用系统 WebView2，不打包 Chromium，
// 安装包从 191 MB 降到个位数 MB。
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod api;
mod store;

use std::path::PathBuf;
use std::sync::atomic::{AtomicU8, Ordering};

use tauri::menu::{MenuBuilder, MenuItemBuilder, PredefinedMenuItem, SubmenuBuilder};
use tauri::window::Color;
use tauri::{AppHandle, Manager, WebviewUrl, WebviewWindowBuilder};
use tauri_plugin_dialog::{DialogExt, MessageDialogKind};
use tauri_plugin_opener::OpenerExt;

use store::AppState;

const APP_NAME: &str = "千叶新页";
const BASE_PORT: u16 = 50304;

/// 站点里工具入口用的是 target="_blank"。WebView2 下这种链接会请求新窗口，
/// 而我们希望它在本窗口内导航（配合菜单里的「后退」），站外链接再由
/// on_navigation 交给系统浏览器。逻辑与移动端 Flutter 版保持一致。
const BLANK_SHIM: &str = r#"
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
"#;

/// 运行期状态，供菜单事件读取
struct Runtime {
    port: u16,
}

/// 当前缩放百分比（100 表示原始大小）
struct Zoom(AtomicU8);

fn main() {
    tauri::Builder::default()
        // 单实例插件必须最先注册；再次启动时把已有窗口拉到前台
        .plugin(tauri_plugin_single_instance::init(|app, _argv, _cwd| {
            if let Some(win) = app.get_webview_window("main") {
                let _ = win.unminimize();
                let _ = win.set_focus();
            }
        }))
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(Zoom(AtomicU8::new(100)))
        .setup(|app| {
            let handle = app.handle().clone();

            // 数据目录与旧版 Electron 的 app.getPath('userData') 一致，
            // 这样从旧版升级过来可以沿用原有的留言与小说
            let data_dir = dirs::data_dir()
                .ok_or("无法定位用户数据目录")?
                .join(APP_NAME);
            let state = AppState::new(resolve_www(&handle)?, data_dir);
            state.ensure_dirs()?;

            // 先把服务起起来，拿到实际端口再建窗口，
            // 否则窗口加载时服务还没就绪会直接显示连接失败
            let port = start_server(state)?;
            app.manage(Runtime { port });

            build_menu(&handle)?;
            create_window(&handle, port)?;

            Ok(())
        })
        .on_menu_event(|app, event| {
            if let Err(e) = handle_menu(app, event.id().as_ref()) {
                eprintln!("[菜单] {e}");
            }
        })
        .run(tauri::generate_context!())
        .expect("千叶新页启动失败");
}

/// 定位网站资源目录：发布版在安装目录的 www/，开发版直接用 src-tauri/www
fn resolve_www(app: &AppHandle) -> Result<PathBuf, String> {
    if let Ok(dir) = app.path().resource_dir() {
        let candidate = dir.join("www");
        if candidate.join("index.html").exists() {
            return Ok(candidate);
        }
    }

    let dev = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("www");
    if dev.join("index.html").exists() {
        return Ok(dev);
    }

    Err("找不到网站资源目录 www/，请先运行 scripts/build-www.js".to_string())
}

/// 启动本地服务并等待端口就绪
fn start_server(state: AppState) -> Result<u16, String> {
    let (tx, rx) = std::sync::mpsc::channel::<Result<u16, String>>();

    tauri::async_runtime::spawn(async move {
        match api::serve(state, BASE_PORT).await {
            Ok((port, server)) => {
                let _ = tx.send(Ok(port));
                // 持有服务任务，否则函数返回时服务就被回收了
                let _ = server.await;
            }
            Err(e) => {
                let _ = tx.send(Err(e));
            }
        }
    });

    // 绑定端口是瞬间完成的，这里等一小会儿不影响启动观感
    rx.recv().map_err(|_| "本地服务启动失败".to_string())?
}

fn create_window(app: &AppHandle, port: u16) -> Result<(), String> {
    let url = format!("http://127.0.0.1:{port}/")
        .parse()
        .map_err(|_| "本地服务地址无效".to_string())?;

    let nav_app = app.clone();
    WebviewWindowBuilder::new(app, "main", WebviewUrl::External(url))
        .title(APP_NAME)
        .inner_size(1200.0, 840.0)
        .min_inner_size(760.0, 540.0)
        .background_color(Color(243, 246, 249, 255))
        .initialization_script(BLANK_SHIM)
        .on_navigation(move |target| {
            // 只允许停留在本地服务里，站外链接交给系统浏览器
            if matches!(target.host_str(), Some("127.0.0.1") | Some("localhost")) {
                return true;
            }
            if matches!(target.scheme(), "http" | "https") {
                let _ = nav_app.opener().open_url(target.as_str(), None::<&str>);
            }
            false
        })
        .build()
        .map_err(|e| format!("创建窗口失败：{e}"))?;

    Ok(())
}

// ---------- 菜单 ----------

fn build_menu(app: &AppHandle) -> Result<(), String> {
    let home = MenuItemBuilder::with_id("home", "回到首页")
        .accelerator("CmdOrCtrl+H")
        .build(app)
        .map_err(|err| err.to_string())?;
    let back = MenuItemBuilder::with_id("back", "后退")
        .accelerator("Alt+Left")
        .build(app)
        .map_err(|err| err.to_string())?;
    let open_data = MenuItemBuilder::with_id("open_data", "打开数据目录")
        .build(app)
        .map_err(|err| err.to_string())?;
    let quit = PredefinedMenuItem::quit(app, Some("退出")).map_err(|err| err.to_string())?;

    let sep1 = PredefinedMenuItem::separator(app).map_err(|err| err.to_string())?;
    let sep2 = PredefinedMenuItem::separator(app).map_err(|err| err.to_string())?;
    let file = SubmenuBuilder::new(app, "文件")
        .item(&home)
        .item(&back)
        .item(&sep1)
        .item(&open_data)
        .item(&sep2)
        .item(&quit)
        .build()
        .map_err(|err| err.to_string())?;

    // 与 Electron 版「页面」菜单保持一致
    let pages = [
        ("page_home", "在线工具箱"),
        ("page_novel", "小说阅读器"),
        ("page_todo", "待办清单"),
        ("page_message", "留言板"),
        ("page_game", "2D 跑酷"),
        ("page_video", "视频播放"),
    ];
    let mut page_items = Vec::new();
    for (id, label) in pages.iter() {
        page_items.push(
            MenuItemBuilder::with_id(*id, *label)
                .build(app)
                .map_err(|err| err.to_string())?,
        );
    }
    let mut page_builder = SubmenuBuilder::new(app, "页面");
    for item in page_items.iter() {
        page_builder = page_builder.item(item);
    }
    let page_menu = page_builder.build().map_err(|err| err.to_string())?;

    let reload = MenuItemBuilder::with_id("reload", "刷新")
        .accelerator("F5")
        .build(app)
        .map_err(|err| err.to_string())?;
    let zoom_in = MenuItemBuilder::with_id("zoom_in", "放大")
        .accelerator("CmdOrCtrl+=")
        .build(app)
        .map_err(|err| err.to_string())?;
    let zoom_out = MenuItemBuilder::with_id("zoom_out", "缩小")
        .accelerator("CmdOrCtrl+-")
        .build(app)
        .map_err(|err| err.to_string())?;
    let zoom_reset = MenuItemBuilder::with_id("zoom_reset", "重置缩放")
        .accelerator("CmdOrCtrl+0")
        .build(app)
        .map_err(|err| err.to_string())?;
    let fullscreen =
        PredefinedMenuItem::fullscreen(app, Some("全屏")).map_err(|err| err.to_string())?;
    let devtools = MenuItemBuilder::with_id("devtools", "开发者工具")
        .accelerator("F12")
        .build(app)
        .map_err(|err| err.to_string())?;

    let sep3 = PredefinedMenuItem::separator(app).map_err(|err| err.to_string())?;
    let sep4 = PredefinedMenuItem::separator(app).map_err(|err| err.to_string())?;
    let view = SubmenuBuilder::new(app, "视图")
        .item(&reload)
        .item(&sep3)
        .item(&zoom_in)
        .item(&zoom_out)
        .item(&zoom_reset)
        .item(&sep4)
        .item(&fullscreen)
        .item(&devtools)
        .build()
        .map_err(|err| err.to_string())?;

    let about = MenuItemBuilder::with_id("about", "关于")
        .build(app)
        .map_err(|err| err.to_string())?;
    let help = SubmenuBuilder::new(app, "帮助")
        .item(&about)
        .build()
        .map_err(|err| err.to_string())?;

    let menu = MenuBuilder::new(app)
        .item(&file)
        .item(&page_menu)
        .item(&view)
        .item(&help)
        .build()
        .map_err(|err| err.to_string())?;

    app.set_menu(menu).map_err(|err| err.to_string())?;
    Ok(())
}

fn handle_menu(app: &AppHandle, id: &str) -> Result<(), String> {
    let port = app.state::<Runtime>().port;

    // 页面跳转
    let page_path = match id {
        "page_home" => Some("/"),
        "page_novel" => Some("/novel/"),
        "page_todo" => Some("/todo/"),
        "page_message" => Some("/message/"),
        "page_game" => Some("/game/"),
        "page_video" => Some("/video/"),
        _ => None,
    };
    if let Some(path) = page_path {
        return navigate(app, port, path);
    }

    match id {
        "home" => navigate(app, port, "/"),
        "back" => {
            eval(app, "history.back()");
            Ok(())
        }
        "open_data" => {
            let dir = dirs::data_dir()
                .ok_or("无法定位用户数据目录")?
                .join(APP_NAME);
            app.opener()
                .open_path(dir.to_string_lossy().to_string(), None::<&str>)
                .map_err(|e| e.to_string())
        }
        "reload" => {
            eval(app, "location.reload()");
            Ok(())
        }
        "zoom_in" => set_zoom(app, 10),
        "zoom_out" => set_zoom(app, -10),
        "zoom_reset" => set_zoom(app, 0),
        "devtools" => {
            if let Some(win) = app.get_webview_window("main") {
                win.open_devtools();
            }
            Ok(())
        }
        "about" => {
            show_about(app, port);
            Ok(())
        }
        _ => Ok(()),
    }
}

fn navigate(app: &AppHandle, port: u16, path: &str) -> Result<(), String> {
    let Some(win) = app.get_webview_window("main") else {
        return Ok(());
    };
    let url = format!("http://127.0.0.1:{port}{path}")
        .parse()
        .map_err(|_| "地址无效".to_string())?;
    win.navigate(url).map_err(|e| e.to_string())
}

fn eval(app: &AppHandle, js: &str) {
    if let Some(win) = app.get_webview_window("main") {
        let _ = win.eval(js);
    }
}

/// delta 为 0 时重置到 100%
fn set_zoom(app: &AppHandle, delta: i32) -> Result<(), String> {
    let zoom = app.state::<Zoom>();
    let current = zoom.0.load(Ordering::Relaxed) as i32;
    let next = if delta == 0 {
        100
    } else {
        (current + delta).clamp(50, 300)
    };
    zoom.0.store(next as u8, Ordering::Relaxed);

    if let Some(win) = app.get_webview_window("main") {
        win.set_zoom(next as f64 / 100.0)
            .map_err(|e| e.to_string())?;
    }
    Ok(())
}

fn show_about(app: &AppHandle, port: u16) {
    let data_dir = dirs::data_dir()
        .map(|d| d.join(APP_NAME).to_string_lossy().to_string())
        .unwrap_or_else(|| "未知".to_string());

    let detail = format!(
        "版本 {}\nTauri 2 · 系统内置 WebView2\n\n\
         个人站点桌面版：工具箱 / 小说阅读 / 待办 / 留言板 / 小游戏\n\
         本地服务：http://127.0.0.1:{port}/\n\
         数据目录：{data_dir}",
        env!("CARGO_PKG_VERSION")
    );

    app.dialog()
        .message(detail)
        .title(format!("关于 {APP_NAME}"))
        .kind(MessageDialogKind::Info)
        .show(|_| {});
}
