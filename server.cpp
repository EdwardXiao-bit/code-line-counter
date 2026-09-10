// server.cpp - web UI for the line counter.
//
// Usage:
//   cloc-server [port]    start the local web server (default port 8080),
//                         then open http://localhost:8080 in a browser.

#include "counter.hpp"
// Raise httplib's built-in per-request file-count limit (default 1024) so a
// large dragged folder is not rejected with an empty response.
#define CPPHTTPLIB_MULTIPART_FORM_DATA_FILE_MAX_COUNT 100000
#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Return a JSON listing of directory `path` (or the available drives when
// `path` is empty). Used by the folder browser in the UI.
static std::string browseJson(const std::string& path) {
    std::string cur, parent;
    std::vector<std::string> drives, dirs;
    bool listDrives = false;

    if (path.empty()) {
        listDrives = true;
    } else {
        std::error_code ec;
        const fs::path p = u8path(path).lexically_normal();
        if (!fs::is_directory(p, ec) || ec) {
            listDrives = true;
        } else {
            cur = pathToUtf8(p);
            parent = pathToUtf8(p.parent_path());
            std::error_code ec2;
            for (fs::directory_iterator it(p, ec2), end; it != end; it.increment(ec2)) {
                if (ec2) { ec2.clear(); break; }
                std::error_code fec;
                if (it->is_directory(fec) && !fec)
                    dirs.push_back(pathToUtf8(it->path().filename()));
            }
            std::sort(dirs.begin(), dirs.end());
        }
    }

    if (listDrives) {
        for (char c = 'A'; c <= 'Z'; ++c) {
            const std::string d = std::string(1, c) + ":\\";
            std::error_code ec;
            if (fs::exists(d, ec) && !ec)
                drives.push_back(d);
        }
    }

    std::string out = "{\"ok\":true,\"listDrives\":" + std::string(listDrives ? "true" : "false");
    out += ",\"path\":\"" + jsonEscape(cur) + "\"";
    out += ",\"parent\":\"" + jsonEscape(parent) + "\"";
    out += ",\"drives\":[";
    for (std::size_t i = 0; i < drives.size(); ++i) {
        if (i) out += ",";
        out += "\"" + jsonEscape(drives[i]) + "\"";
    }
    out += "],\"dirs\":[";
    for (std::size_t i = 0; i < dirs.size(); ++i) {
        if (i) out += ",";
        out += "\"" + jsonEscape(dirs[i]) + "\"";
    }
    out += "]}";
    return out;
}

// Create a unique temporary directory and return its path.
static std::string makeTempDir() {
    const auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() / ("cloc_upload_" + std::to_string(t))).string();
}

static const std::string kHtml = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>代码行数统计</title>
<style>
  :root { color-scheme: light dark; }
  * { box-sizing: border-box; }
  body { font-family: -apple-system, "Segoe UI", Roboto, "Microsoft YaHei", sans-serif;
         margin: 0; padding: 32px 20px; display: flex; flex-direction: column; align-items: center; }
  .container { width: 100%; max-width: 760px; }
  h1 { font-size: 1.4rem; margin: 0 0 20px; }
  .add-bar { display: flex; gap: 8px; margin-bottom: 10px; }
  input { flex: 1; padding: 10px 12px; font-size: 14px; border: 1px solid #8886;
          border-radius: 6px; background: transparent; color: CanvasText; }
  button { padding: 10px 16px; font-size: 14px; cursor: pointer; border: 1px solid transparent;
           border-radius: 6px; background: #2563eb; color: #fff; }
  button:hover { background: #1d4ed8; }
  button:disabled { opacity: .5; cursor: default; }
  #dropZone { border: 2px dashed #8886; border-radius: 8px; padding: 22px; text-align: center;
              color: #888; margin-bottom: 12px; font-size: 14px; transition: .15s; }
  #dropZone.dragover { border-color: #2563eb; color: #2563eb; background: #2563eb14; }
  #sourceList { display: flex; flex-wrap: wrap; gap: 8px; min-height: 24px; margin-bottom: 14px; }
  .hint { color: #888; font-size: 13px; }
  .chip { display: inline-flex; align-items: center; gap: 6px; background: #8882;
          border-radius: 14px; padding: 4px 8px 4px 12px; font-size: 13px; }
  .chip-x { background: transparent; color: inherit; border: none; padding: 0 4px;
            font-size: 15px; line-height: 1; cursor: pointer; }
  .actions { margin-bottom: 14px; }
  #status { min-height: 20px; margin-bottom: 12px; font-size: 13px; color: #dc2626; }
  table { width: 100%; border-collapse: collapse; font-size: 14px; }
  th, td { padding: 8px 12px; text-align: right; }
  th:first-child, td:first-child { text-align: left; }
  thead th { border-bottom: 2px solid #8884; }
  tbody tr.sum td { border-top: 2px solid #8884; font-weight: 600; }
  tbody tr:not(.sum):hover td { background: #8881; }
  td.num { font-family: ui-monospace, Consolas, monospace; }
  .modal { position: fixed; inset: 0; background: rgba(0,0,0,.45); display: flex;
           align-items: center; justify-content: center; z-index: 10; }
  .modal.hidden { display: none; }
  .modal-box { width: min(600px, 92vw); max-height: 80vh; display: flex; flex-direction: column;
               background: Canvas; color: CanvasText; border: 1px solid #8885;
               border-radius: 10px; overflow: hidden; }
  .modal-head { display: flex; align-items: center; gap: 10px; padding: 12px 14px;
                border-bottom: 1px solid #8884; }
  .modal-head .path { flex: 1; font-size: 13px; color: #888; overflow: hidden;
                      text-overflow: ellipsis; white-space: nowrap; }
  #upBtn, #closeBtn { background: transparent; color: inherit; border: 1px solid #8885; }
  #closeBtn { padding: 4px 10px; font-size: 16px; }
  .browse-list { overflow-y: auto; flex: 1; padding: 6px; }
  .brow { display: flex; align-items: center; justify-content: space-between;
          padding: 10px 12px; border-radius: 6px; cursor: pointer; }
  .brow:hover { background: #8882; }
  .brow-name { font-size: 14px; }
  .brow-enter { font-size: 12px; color: #2563eb; }
  .modal-foot { padding: 12px 14px; border-top: 1px solid #8884; display: flex;
                justify-content: flex-end; }
</style>
</head>
<body>
<div class="container">
  <h1>代码行数统计</h1>

  <div class="add-bar">
    <input id="dir" placeholder="输入目录路径，回车或点「添加」">
    <button id="addBtn">添加</button>
    <button id="browseBtn">浏览…</button>
  </div>

  <div id="dropZone">拖拽文件夹到这里，自动统计</div>

  <div id="sourceList"></div>

  <div class="actions"><button id="go">统计</button></div>

  <div id="status"></div>

  <table id="result">
    <thead><tr><th>Language</th><th>files</th><th>blank</th><th>comment</th><th>code</th></tr></thead>
    <tbody id="body"></tbody>
  </table>
</div>

<div id="modal" class="modal hidden">
  <div class="modal-box">
    <div class="modal-head">
      <button id="upBtn">上一级</button>
      <span class="path" id="curPath"></span>
      <button id="closeBtn">×</button>
    </div>
    <div id="browseList" class="browse-list"></div>
    <div class="modal-foot">
      <button id="selectBtn">选择当前文件夹</button>
    </div>
  </div>
</div>

<script>
  const body = document.getElementById('body');
  const status = document.getElementById('status');
  const dirInput = document.getElementById('dir');
  const btn = document.getElementById('go');
  const addBtn = document.getElementById('addBtn');
  const browseBtn = document.getElementById('browseBtn');
  const sourceList = document.getElementById('sourceList');
  const dropZone = document.getElementById('dropZone');
  const modal = document.getElementById('modal');
  const closeBtn = document.getElementById('closeBtn');
  const upBtn = document.getElementById('upBtn');
  const selectBtn = document.getElementById('selectBtn');
  const curPath = document.getElementById('curPath');
  const browseList = document.getElementById('browseList');

  const LANGS = ['C', 'C++', 'Java', 'Python'];
  const EXT_RE = /\.(c|h|cpp|cc|cxx|hpp|hh|hxx|java|py)$/i;

  let uid = 0;
  const sources = [];       // {id, kind:'path'|'upload', label, path?, result?}
  let browseData = null;

  function el(tag, cls, text) {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text !== undefined) n.textContent = text;
    return n;
  }

  // ---- added folders ----
  function renderSources() {
    sourceList.innerHTML = '';
    if (sources.length === 0) {
      sourceList.append(el('span', 'hint', '尚未添加文件夹（点「统计」将统计当前目录）'));
      return;
    }
    for (const s of sources) {
      const chip = el('span', 'chip');
      chip.append(el('span', null, s.label));
      const x = el('button', 'chip-x', '×');
      x.addEventListener('click', () => removeSource(s.id));
      chip.append(x);
      sourceList.append(chip);
    }
  }

  function removeSource(id) {
    const i = sources.findIndex(s => s.id === id);
    if (i >= 0) sources.splice(i, 1);
    renderSources();
  }

  function addPathSource(path) {
    path = (path || '').trim();
    if (!path) return;
    if (sources.some(s => s.kind === 'path' && s.path === path)) return;
    sources.push({ id: ++uid, kind: 'path', label: path, path });
    renderSources();
  }

  // ---- folder browser ----
  function joinPath(base, name) {
    if (!base) return name;
    return base.replace(/[\\/]+$/, '') + '\\' + name;
  }

  async function loadBrowse(path) {
    try {
      const resp = await fetch('/api/browse?path=' + encodeURIComponent(path || ''));
      browseData = await resp.json();
      renderBrowse();
    } catch (e) {
      status.textContent = '浏览失败：' + e;
    }
  }

  function renderBrowse() {
    const d = browseData;
    if (!d) return;
    browseList.innerHTML = '';
    curPath.textContent = d.listDrives ? '选择磁盘' : (d.path || '');
    selectBtn.disabled = !!d.listDrives;
    if (d.listDrives) {
      for (const drive of d.drives) {
        const row = el('div', 'brow');
        row.append(el('span', 'brow-name', drive));
        row.append(el('span', 'brow-enter', '打开'));
        row.addEventListener('click', () => loadBrowse(drive));
        browseList.append(row);
      }
    } else {
      for (const name of d.dirs) {
        const row = el('div', 'brow');
        row.append(el('span', 'brow-name', name));
        row.append(el('span', 'brow-enter', '进入'));
        row.addEventListener('click', () => loadBrowse(joinPath(d.path, name)));
        browseList.append(row);
      }
    }
  }

  function openBrowser() { modal.classList.remove('hidden'); loadBrowse(''); }
  function closeBrowser() { modal.classList.add('hidden'); }

  upBtn.addEventListener('click', () => {
    if (browseData && browseData.parent) loadBrowse(browseData.parent);
  });
  selectBtn.addEventListener('click', () => {
    if (browseData && !browseData.listDrives && browseData.path) {
      addPathSource(browseData.path);
      closeBrowser();
    }
  });
  closeBtn.addEventListener('click', closeBrowser);
  modal.addEventListener('click', e => { if (e.target === modal) closeBrowser(); });

  // ---- drag & drop (upload folder contents) ----
  function readEntries(reader) {
    return new Promise((res, rej) => reader.readEntries(res, rej));
  }

  async function collectFiles(entry, prefix, out) {
    if (entry.isFile) {
      const file = await new Promise((res, rej) => entry.file(res, rej));
      if (EXT_RE.test(file.name))
        out.push({ path: prefix + file.name, file });
    } else if (entry.isDirectory) {
      const reader = entry.createReader();
      let batch;
      do {
        batch = await readEntries(reader);
        for (const e of batch)
          await collectFiles(e, prefix + entry.name + '/', out);
      } while (batch.length > 0);
    }
  }

  async function uploadFolder(entry) {
    const files = [];
    await collectFiles(entry, '', files);
    if (files.length === 0) return null;
    const fd = new FormData();
    for (const f of files) fd.append('files', f.file, f.path);
    const resp = await fetch('/api/count-files', { method: 'POST', body: fd });
    if (!resp.ok) {
      const text = await resp.text();
      throw new Error('HTTP ' + resp.status + (text ? ': ' + text.slice(0, 200) : ''));
    }
    return await resp.json();
  }

  dropZone.addEventListener('dragover', e => { e.preventDefault(); dropZone.classList.add('dragover'); });
  dropZone.addEventListener('dragleave', () => dropZone.classList.remove('dragover'));
  dropZone.addEventListener('drop', async e => {
    e.preventDefault();
    dropZone.classList.remove('dragover');
    const items = e.dataTransfer.items;
    if (!items) return;
    const entries = [];
    for (const item of items) {
      if (item.kind !== 'file') continue;
      const entry = item.webkitGetAsEntry ? item.webkitGetAsEntry() : null;
      if (entry) entries.push(entry);
    }
    for (const entry of entries) {
      status.textContent = '正在统计拖入的文件夹：' + entry.name + ' …';
      try {
        const result = await uploadFolder(entry);
        if (result && result.ok) {
          sources.push({ id: ++uid, kind: 'upload', label: entry.name + '（已上传）', result });
          renderSources();
          status.textContent = '';
        } else {
          status.textContent = '文件夹 ' + entry.name + ' 中没有受支持的代码文件';
        }
      } catch (err) {
        status.textContent = '上传失败：' + err;
      }
    }
  });

  // ---- counting + rendering ----
  function mergeResults(results) {
    const acc = {};
    for (const name of LANGS) acc[name] = { files: 0, blank: 0, comment: 0, code: 0 };
    for (const r of results) {
      if (!r || !r.ok || !r.languages) continue;
      for (const l of r.languages) {
        const a = acc[l.name];
        if (!a) continue;
        a.files += l.files; a.blank += l.blank; a.comment += l.comment; a.code += l.code;
      }
    }
    const languages = LANGS.map(name => Object.assign({ name }, acc[name]));
    const total = { files: 0, blank: 0, comment: 0, code: 0 };
    for (const l of languages) {
      total.files += l.files; total.blank += l.blank;
      total.comment += l.comment; total.code += l.code;
    }
    return { languages, total };
  }

  function render(data) {
    body.innerHTML = '';
    for (const r of data.languages) {
      const tr = el('tr');
      tr.append(el('td', null, r.name));
      for (const k of ['files', 'blank', 'comment', 'code'])
        tr.append(el('td', 'num', r[k].toLocaleString()));
      body.append(tr);
    }
    const t = data.total;
    const tr = el('tr', 'sum');
    tr.append(el('td', null, 'SUM'));
    for (const k of ['files', 'blank', 'comment', 'code'])
      tr.append(el('td', 'num', t[k].toLocaleString()));
    body.append(tr);
  }

  async function run() {
    btn.disabled = true;
    status.textContent = '';
    try {
      const results = [];
      for (const s of sources) if (s.kind === 'upload' && s.result) results.push(s.result);
      const paths = sources.filter(s => s.kind === 'path').map(s => s.path);
      if (paths.length > 0) {
        const q = paths.map(p => 'dir=' + encodeURIComponent(p)).join('&');
        const resp = await fetch('/api/count?' + q);
        const data = await resp.json();
        if (!data.ok) { status.textContent = '错误：' + (data.error || '未知错误'); return; }
        results.push(data);
      }
      if (results.length === 0) {
        const resp = await fetch('/api/count?dir=.');
        const data = await resp.json();
        if (!data.ok) { status.textContent = '错误：' + (data.error || '未知错误'); return; }
        results.push(data);
      }
      render(mergeResults(results));
    } catch (e) {
      status.textContent = '请求失败：' + e;
    } finally {
      btn.disabled = false;
    }
  }

  btn.addEventListener('click', run);
  addBtn.addEventListener('click', () => { addPathSource(dirInput.value); dirInput.value = ''; });
  browseBtn.addEventListener('click', openBrowser);
  dirInput.addEventListener('keydown', e => {
    if (e.key === 'Enter') { addPathSource(dirInput.value); dirInput.value = ''; }
  });

  renderSources();
</script>
</body>
</html>
)HTML";

int main(int argc, char** argv) {
    int port = 8080;
    if (argc > 1)
        port = std::stoi(argv[1]);

    httplib::Server svr;
    svr.set_payload_max_length(1024ull * 1024 * 1024); // 1 GiB max upload

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kHtml, "text/html; charset=utf-8");
    });

    // Directory browser: /api/browse?path=C:\...
    svr.Get("/api/browse", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(browseJson(req.get_param_value("path")), "application/json; charset=utf-8");
    });

    // Count one or more directories: /api/count?dir=a&dir=b&...
    svr.Get("/api/count", [](const httplib::Request& req, httplib::Response& res) {
        std::vector<fs::path> dirs;
        for (const std::string& d : req.get_param_values("dir"))
            dirs.push_back(u8path(d));
        if (dirs.empty())
            dirs.push_back(fs::path("."));
        res.set_content(toJson(countDirectories(dirs)), "application/json; charset=utf-8");
    });

    // Count uploaded files (drag-and-drop): multipart POST, field name "files".
    svr.Post("/api/count-files", [](const httplib::Request& req, httplib::Response& res) {
        const auto files = req.form.get_files("files");
        if (files.empty()) {
            res.set_content("{\"ok\":false,\"error\":\"no files uploaded\"}", "application/json; charset=utf-8");
            return;
        }
        const fs::path tmp = makeTempDir();
        std::error_code ec;
        fs::create_directories(tmp, ec);
        for (const auto& f : files) {
            const std::string rel = f.filename;
            // Safety: skip absolute paths or parent traversal.
            if (rel.empty() || rel.find("..") != std::string::npos)
                continue;
            const fs::path dest = tmp / fs::path(rel);
            std::error_code fec;
            fs::create_directories(dest.parent_path(), fec);
            std::ofstream out(dest, std::ios::binary);
            if (out)
                out.write(f.content.data(), static_cast<std::streamsize>(f.content.size()));
        }
        const CountResult r = countDirectory(tmp);
        fs::remove_all(tmp, ec);
        res.set_content(toJson(r), "application/json; charset=utf-8");
    });

    std::cout << "Code line counter server: http://localhost:" << port << "\n"
              << "Press Ctrl+C to stop.\n";
    svr.listen("127.0.0.1", port);
    return 0;
}
