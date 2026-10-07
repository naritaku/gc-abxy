// Markdown for agents (Cloudflare Pages Functions). A request whose Accept prefers text/markdown to text/html gets the
// page's Markdown copy (index.md next to index.html, made by the site build from the page itself); everyone else,
// browsers included, gets the HTML as before. site/_routes.json sends only the pages through here.
// Every page also gets a Link header naming llms.txt, the sitemap and its Markdown copy (RFC 8288).

function q(accept, type) {
  // the q value the Accept header gives a media type (0 when it is not named; */* and text/* count for text/html only)
  let best = -1, exact = false;
  for (const part of accept.split(",")) {
    const [range, ...params] = part.trim().toLowerCase().split(";");
    const m = params.map((p) => p.trim()).find((p) => p.startsWith("q="));
    const v = m ? parseFloat(m.slice(2)) : 1;
    if (range === type) { best = exact ? Math.max(best, v) : v; exact = true; }
    else if (!exact && type === "text/html" && (range === "*/*" || range === "text/*")) best = Math.max(best, v);
  }
  return Number.isNaN(best) || best < 0 ? 0 : best;
}

function prefersMarkdown(accept) {
  const md = q(accept, "text/markdown");
  return md > 0 && md >= q(accept, "text/html");
}

function links(mdPath) {
  return `</llms.txt>; rel="describedby"; type="text/plain", </sitemap.xml>; rel="sitemap", <${mdPath}>; rel="alternate"; type="text/markdown"`;
}

export async function onRequest({ request, next, env }) {
  const url = new URL(request.url);
  if (!url.pathname.endsWith("/") || !["GET", "HEAD"].includes(request.method)) return next();
  const mdPath = url.pathname + "index.md";
  if (prefersMarkdown(request.headers.get("Accept") || "")) {
    const md = await env.ASSETS.fetch(new URL(mdPath, url));
    if (md.ok) {
      if (request.method === "HEAD") await md.body?.cancel();
      const res = new Response(request.method === "HEAD" ? null : md.body, md);
      res.headers.set("Content-Type", "text/markdown; charset=utf-8");
      res.headers.set("Vary", "Accept");
      res.headers.set("Link", links(mdPath));
      return res;
    }
  }
  const page = await next();
  const res = new Response(page.body, page);
  res.headers.append("Vary", "Accept");
  if ((res.headers.get("Content-Type") || "").startsWith("text/html")) res.headers.set("Link", links(mdPath));
  return res;
}
