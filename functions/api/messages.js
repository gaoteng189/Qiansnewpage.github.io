/**
 * Cloudflare Pages Function —— 留言 API 反向代理
 *
 * 作用：前端（HTTPS）请求同源 /api/messages，此函数在 Cloudflare 边缘
 * 转发到你本机通过 OpenFrp 内网穿透暴露的 HTTP 后端（node server.js）。
 * 数据仍由后端写入运行服务器主机上的 messages.json。
 */
const UPSTREAM = 'http://1344a5becd3e.ofalias.com:41792';

export async function onRequest(context) {
  const request = context.request;
  const url = new URL(request.url);

  const target = UPSTREAM + url.pathname + url.search;

  const headers = new Headers(request.headers);
  headers.delete('host');

  const init = {
    method: request.method,
    headers,
    redirect: 'manual'
  };

  if (request.method !== 'GET' && request.method !== 'HEAD') {
    init.body = await request.text();
  }

  try {
    const resp = await fetch(target, init);
    const outHeaders = new Headers(resp.headers);
    outHeaders.set('Access-Control-Allow-Origin', '*');
    outHeaders.set('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    outHeaders.set('Access-Control-Allow-Headers', 'Content-Type');
    return new Response(resp.body, {
      status: resp.status,
      statusText: resp.statusText,
      headers: outHeaders
    });
  } catch (e) {
    return new Response(
      JSON.stringify({ error: '后端服务不可用：' + e.message }),
      {
        status: 502,
        headers: { 'Content-Type': 'application/json; charset=utf-8' }
      }
    );
  }
}
