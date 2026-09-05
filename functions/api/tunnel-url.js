/**
 * Cloudflare Pages Function —— 隧道地址查询代理
 *
 * 作用：前端（HTTPS）请求同源 /api/tunnel-url，此函数在 Cloudflare 边缘
 * 转发到 OpenFrp 固定地址的 /api/tunnel-url，获取后端当前 wss 地址。
 */
const UPSTREAM = 'http://1344a5becd3e.ofalias.com:41792';

export async function onRequest(context) {
  const request = context.request;
  const url = new URL(request.url);

  const target = UPSTREAM + url.pathname + url.search;

  const headers = new Headers(request.headers);
  headers.delete('host');

  try {
    const resp = await fetch(target, {
      method: request.method,
      headers,
      redirect: 'manual'
    });
    const outHeaders = new Headers(resp.headers);
    outHeaders.set('Access-Control-Allow-Origin', '*');
    return new Response(resp.body, {
      status: resp.status,
      statusText: resp.statusText,
      headers: outHeaders
    });
  } catch (e) {
    return new Response(JSON.stringify({ url: null }), {
      status: 502,
      headers: { 'Content-Type': 'application/json; charset=utf-8' }
    });
  }
}
