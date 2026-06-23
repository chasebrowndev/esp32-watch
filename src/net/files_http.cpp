// AP file server. Single-file impl: brings up softAP, runs an
// ESPAsyncWebServer with a small REST surface for /sd browse + transfer,
// serves a static terminal-styled UI from PROGMEM.
//
// Endpoints:
//   GET  /                 -> index.html
//   GET  /api/list?path=/  -> JSON dir listing { entries: [{n,d,s}, ...] }
//   GET  /dl?path=...      -> stream a file
//   POST /up?path=DIR      -> multipart upload, saved as DIR/<filename>
//   POST /rm?path=...      -> delete file or empty directory
//   POST /mkdir?path=...   -> create directory
//
// Security model: the AP is WPA2 with a NVS-persisted password (created
// once on first start). That gates network access. There's no per-request
// auth on top — anyone on the AP can browse the card. Treat this as a
// "transient AP for trusted use" feature, not a public service.
#include "files_http.h"
#include "../../include/config.h"

#if FEAT_WIFI

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <ESPAsyncWebServer.h>
#include <SD_MMC.h>
#include <string.h>
#include "../features/sdcard.h"

namespace {
  bool              s_running = false;
  AsyncWebServer*   s_http    = nullptr;

  const uint8_t LOG_MAX = 10;
  files_http::Hit s_hits[LOG_MAX];
  uint8_t         s_hitCount = 0;

  void logHit(const char* op, const char* path) {
    if (s_hitCount < LOG_MAX) {
      auto& h = s_hits[s_hitCount++];
      h.ms = millis();
      strncpy(h.op,   op,   sizeof(h.op)   - 1); h.op[sizeof(h.op)     - 1] = 0;
      strncpy(h.path, path, sizeof(h.path) - 1); h.path[sizeof(h.path) - 1] = 0;
    } else {
      // Shift ring left by 1, append at end.
      for (uint8_t i = 1; i < LOG_MAX; ++i) s_hits[i - 1] = s_hits[i];
      auto& h = s_hits[LOG_MAX - 1];
      h.ms = millis();
      strncpy(h.op,   op,   sizeof(h.op)   - 1); h.op[sizeof(h.op)     - 1] = 0;
      strncpy(h.path, path, sizeof(h.path) - 1); h.path[sizeof(h.path) - 1] = 0;
    }
  }

  // Reject any path containing "..", null, or not starting with "/".
  // SD_MMC paths are relative to mount root; we accept paths like "/foo/bar".
  bool safePath(const String& p) {
    if (p.length() == 0) return false;
    if (p[0] != '/') return false;
    if (p.indexOf("..") >= 0) return false;
    return true;
  }

  // ---- index.html ----
  // Minimal terminal UI. Black bg / red/white text matching the watch theme.
  const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>watch · files</title>
<style>
*{box-sizing:border-box}
html,body{margin:0;background:#0a0a0a;color:#e6e6e6;font:14px ui-monospace,Menlo,Consolas,monospace}
header{padding:10px 14px;border-bottom:1px solid #2a0a0a;background:#100404;color:#ff3344;
  font-weight:600;letter-spacing:1px;display:flex;align-items:center;gap:10px}
header .dot{width:8px;height:8px;background:#ff3344;border-radius:50%;box-shadow:0 0 6px #ff3344}
main{padding:12px}
.row{display:flex;gap:10px;align-items:center;padding:6px 8px;border-bottom:1px solid #1a1a1a}
.row:hover{background:#141414}
.row .n{flex:1;color:#fff;word-break:break-all}
.row .s{color:#888;font-size:12px;width:90px;text-align:right}
.row .a{display:flex;gap:6px}
.row a,.row button{background:#1a0808;color:#ff8090;border:1px solid #4a1414;padding:4px 8px;
  border-radius:0;text-decoration:none;font:inherit;cursor:pointer}
.row a:hover,.row button:hover{background:#3a1010;color:#fff}
.dir .n{color:#ff9080;cursor:pointer}
.dir .n:before{content:"[ "}
.dir .n:after{content:" ]"}
.bar{display:flex;gap:10px;padding:10px 8px;border-bottom:1px solid #2a0a0a;align-items:center;flex-wrap:wrap}
.bar input[type=text]{flex:1;background:#000;color:#fff;border:1px solid #4a1414;padding:6px;font:inherit}
.bar button,.bar label{background:#1a0808;color:#ff8090;border:1px solid #4a1414;padding:6px 10px;cursor:pointer}
.bar button:hover,.bar label:hover{background:#3a1010;color:#fff}
.bar input[type=file]{display:none}
#log{padding:8px;font-size:12px;color:#888;max-height:80px;overflow-y:auto;border-top:1px solid #2a0a0a}
.crumb{color:#ff3344;cursor:pointer}
.crumb:hover{text-decoration:underline}
</style></head>
<body>
<header><span class="dot"></span>watch · files</header>
<div class="bar">
  <span id="crumbs"></span>
  <input id="newdir" type="text" placeholder="new dir name">
  <button onclick="mkdir()">mkdir</button>
  <label>upload<input id="up" type="file" multiple onchange="upload()"></label>
</div>
<main id="list"></main>
<div id="log"></div>
<script>
let path='/';
function log(s){const l=document.getElementById('log');l.innerHTML=`<div>${new Date().toLocaleTimeString()}  ${s}</div>`+l.innerHTML}
function crumbs(){
  const parts=path.split('/').filter(Boolean);
  const html=['<span class="crumb" onclick="cd(\'/\')">~</span>'];
  let acc='';
  for(const p of parts){
    acc+='/'+p;
    html.push(' / <span class="crumb" onclick="cd(\''+acc+'\')">'+p+'</span>');
  }
  document.getElementById('crumbs').innerHTML=html.join('');
}
async function ls(){
  crumbs();
  const r=await fetch('/api/list?path='+encodeURIComponent(path));
  if(!r.ok){document.getElementById('list').textContent='error: '+r.status;return}
  const j=await r.json();
  const out=[];
  if(path!=='/') out.push(`<div class="row dir"><span class="n" onclick="cd('${parent()}')">..</span><span class="s"></span><span class="a"></span></div>`);
  for(const e of j.entries||[]){
    const sub=(path==='/'?'':path)+'/'+e.n;
    if(e.d){
      out.push(`<div class="row dir"><span class="n" onclick="cd('${sub}')">${esc(e.n)}</span><span class="s"></span><span class="a"><button onclick="rm('${sub}')">rm</button></span></div>`);
    } else {
      out.push(`<div class="row"><span class="n">${esc(e.n)}</span><span class="s">${fmt(e.s)}</span><span class="a"><a href="/dl?path=${encodeURIComponent(sub)}" download>dl</a><button onclick="rm('${sub}')">rm</button></span></div>`);
    }
  }
  document.getElementById('list').innerHTML=out.join('');
}
function parent(){const p=path.replace(/\/+$/,'').split('/');p.pop();return p.join('/')||'/'}
function cd(p){path=p||'/';ls()}
function esc(s){return s.replace(/[<>&"]/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]))}
function fmt(n){if(n<1024)return n+'B';if(n<1048576)return (n/1024).toFixed(1)+'K';return (n/1048576).toFixed(1)+'M'}
async function rm(p){
  if(!confirm('rm '+p+' ?'))return;
  const r=await fetch('/rm?path='+encodeURIComponent(p),{method:'POST'});
  log((r.ok?'rm ':'rm-fail ')+p);
  ls();
}
async function mkdir(){
  const n=document.getElementById('newdir').value.trim();
  if(!n)return;
  const p=(path==='/'?'':path)+'/'+n;
  const r=await fetch('/mkdir?path='+encodeURIComponent(p),{method:'POST'});
  log((r.ok?'mkdir ':'mkdir-fail ')+p);
  document.getElementById('newdir').value='';
  ls();
}
async function upload(){
  const inp=document.getElementById('up');
  for(const f of inp.files){
    const fd=new FormData();fd.append('file',f);
    const r=await fetch('/up?path='+encodeURIComponent(path),{method:'POST',body:fd});
    log((r.ok?'up ':'up-fail ')+f.name+' ('+fmt(f.size)+')');
  }
  inp.value='';
  ls();
}
ls();
</script></body></html>)HTML";

  // ---- handlers ----
  void handleList(AsyncWebServerRequest* req) {
    if (!sdcard::mounted()) { req->send(503, "application/json", "{\"err\":\"no sd\"}"); return; }
    String path = req->hasParam("path") ? req->getParam("path")->value() : "/";
    if (!safePath(path)) { req->send(400, "application/json", "{\"err\":\"bad path\"}"); return; }
    File dir = SD_MMC.open(path);
    if (!dir || !dir.isDirectory()) {
      req->send(404, "application/json", "{\"err\":\"not a dir\"}");
      return;
    }
    String body;
    body.reserve(2048);
    body = "{\"entries\":[";
    bool first = true;
    File f = dir.openNextFile();
    int count = 0;
    while (f && count < 200) {
      const char* full = f.name();
      const char* base = strrchr(full, '/');
      base = base ? base + 1 : full;
      if (!first) body += ',';
      body += "{\"n\":\"";
      // basic JSON-string escape: quote + backslash; control chars unlikely on FAT
      for (const char* p = base; *p; ++p) {
        if (*p == '"' || *p == '\\') body += '\\';
        body += *p;
      }
      body += "\",\"d\":";
      body += (f.isDirectory() ? "1" : "0");
      body += ",\"s\":";
      body += String((uint32_t)f.size());
      body += '}';
      first = false;
      ++count;
      f = dir.openNextFile();
    }
    body += "]}";
    req->send(200, "application/json", body);
  }

  void handleDownload(AsyncWebServerRequest* req) {
    if (!sdcard::mounted()) { req->send(503, "text/plain", "no sd"); return; }
    if (!req->hasParam("path")) { req->send(400, "text/plain", "bad"); return; }
    String path = req->getParam("path")->value();
    if (!safePath(path)) { req->send(400, "text/plain", "bad path"); return; }
    if (!SD_MMC.exists(path)) { req->send(404, "text/plain", "missing"); return; }
    const char* base = strrchr(path.c_str(), '/');
    base = base ? base + 1 : path.c_str();
    AsyncWebServerResponse* r = req->beginResponse(SD_MMC, path, "application/octet-stream");
    String disp = String("attachment; filename=\"") + base + "\"";
    r->addHeader("Content-Disposition", disp);
    req->send(r);
    logHit("DL", path.c_str());
  }

  void handleRm(AsyncWebServerRequest* req) {
    if (!sdcard::mounted()) { req->send(503, "text/plain", "no sd"); return; }
    if (!req->hasParam("path")) { req->send(400, "text/plain", "bad"); return; }
    String path = req->getParam("path")->value();
    if (!safePath(path) || path == "/") { req->send(400, "text/plain", "bad path"); return; }
    bool ok = false;
    File f = SD_MMC.open(path);
    if (f) {
      bool isDir = f.isDirectory();
      f.close();
      ok = isDir ? SD_MMC.rmdir(path) : SD_MMC.remove(path);
    }
    if (ok) logHit("RM", path.c_str());
    req->send(ok ? 200 : 500, "text/plain", ok ? "ok" : "fail");
  }

  void handleMkdir(AsyncWebServerRequest* req) {
    if (!sdcard::mounted()) { req->send(503, "text/plain", "no sd"); return; }
    if (!req->hasParam("path")) { req->send(400, "text/plain", "bad"); return; }
    String path = req->getParam("path")->value();
    if (!safePath(path) || path == "/") { req->send(400, "text/plain", "bad path"); return; }
    bool ok = SD_MMC.mkdir(path);
    if (ok) logHit("MK", path.c_str());
    req->send(ok ? 200 : 500, "text/plain", ok ? "ok" : "fail");
  }

  // Multipart upload streamer. ESPAsyncWebServer calls this once per chunk
  // with index==0 for the first chunk and final==true for the last.
  File s_upFile;
  String s_upPath;
  void handleUpload(AsyncWebServerRequest* req, const String& filename,
                    size_t index, uint8_t* data, size_t len, bool final) {
    if (!sdcard::mounted()) return;
    if (index == 0) {
      String dir = req->hasParam("path") ? req->getParam("path")->value() : "/";
      if (!safePath(dir)) dir = "/";
      // Sanitize filename: take basename only, drop any leading dirs.
      String fn = filename;
      int slash = fn.lastIndexOf('/');
      if (slash >= 0) fn = fn.substring(slash + 1);
      if (fn.length() == 0) fn = "upload.bin";
      s_upPath = (dir == "/" ? "" : dir) + "/" + fn;
      // Overwrite if exists.
      if (SD_MMC.exists(s_upPath)) SD_MMC.remove(s_upPath);
      s_upFile = SD_MMC.open(s_upPath, FILE_WRITE);
    }
    if (s_upFile && len) s_upFile.write(data, len);
    if (final) {
      if (s_upFile) s_upFile.close();
      logHit("UP", s_upPath.c_str());
    }
  }
}

namespace files_http {

bool start(const char* ssid, const char* pass) {
  if (s_running) return false;
  s_hitCount = 0;
  for (uint8_t i = 0; i < LOG_MAX; ++i) s_hits[i] = Hit{};

  WiFi.mode(WIFI_AP);
  // Random locally-administered MAC per session. See captive::start for
  // the rationale.
  uint8_t mac[6];
  for (int i = 0; i < 6; ++i) mac[i] = (uint8_t)esp_random();
  mac[0] = (mac[0] & 0xFC) | 0x02;
  esp_wifi_set_mac(WIFI_IF_AP, mac);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                    IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(ssid ? ssid : "watch-files", pass)) return false;

  s_http = new AsyncWebServer(80);
  s_http->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });
  s_http->on("/api/list", HTTP_GET, handleList);
  s_http->on("/dl",       HTTP_GET, handleDownload);
  s_http->on("/rm",       HTTP_POST, handleRm);
  s_http->on("/mkdir",    HTTP_POST, handleMkdir);
  s_http->on("/up", HTTP_POST,
    [](AsyncWebServerRequest* req) { req->send(200, "text/plain", "ok"); },
    handleUpload);
  s_http->onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "nope");
  });
  s_http->begin();

  s_running = true;
  return true;
}

void stop() {
  if (!s_running) return;
  if (s_http) { s_http->end(); delete s_http; s_http = nullptr; }
  WiFi.softAPdisconnect(true);
  s_running  = false;
  s_hitCount = 0;
  if (s_upFile) s_upFile.close();
  s_upPath = "";
}

bool running() { return s_running; }

uint8_t clientCount() {
  return s_running ? WiFi.softAPgetStationNum() : 0;
}

uint8_t snapshot(Hit* out, uint8_t max) {
  uint8_t n = s_hitCount < max ? s_hitCount : max;
  for (uint8_t i = 0; i < n; ++i) out[i] = s_hits[i];
  return n;
}

} // namespace files_http

#else  // FEAT_WIFI disabled stubs

namespace files_http {
  bool    start(const char*, const char*) { return false; }
  void    stop()         {}
  bool    running()      { return false; }
  uint8_t clientCount()  { return 0; }
  uint8_t snapshot(Hit*, uint8_t) { return 0; }
}

#endif
