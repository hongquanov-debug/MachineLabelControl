/* =============================================================================
 * PMeMS Controller 1 - logic mockup giao diện.
 *
 * MỌI DỮ LIỆU Ở ĐÂY LÀ GIẢ LẬP để thấy giao diện "sống". Khi viết bằng PyQt6:
 *   - phần "SIM" thay bằng dữ liệu thật: PmemsClient (tools/pc_config) đọc
 *     Modbus TCP, và module xử lý ảnh của bạn.
 *   - mỗi hàm render...() tương ứng một slot cập nhật widget (xem README.md).
 * ========================================================================== */
"use strict";

const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const pad = (n, w = 2) => String(n).padStart(w, "0");
const now = () => new Date();
const hhmmss = (d = now()) => `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
const rnd = (a, b) => a + Math.random() * (b - a);

/* ------------------------------------------------------------------------ */
/* Trạng thái ứng dụng (trong PyQt6: một class AppState / model)            */
/* ------------------------------------------------------------------------ */
const STATES = {
  POWER_ON: { text: "POWER ON", cls: "" },
  HOME_REQ: { text: "CẦN HOMING", cls: "pause" },
  HOMING:   { text: "HOMING", cls: "pause" },
  READY:    { text: "READY", cls: "ready" },
  RUN:      { text: "RUNNING", cls: "run" },
  PAUSE:    { text: "PAUSED", cls: "pause" },
  ALARM:    { text: "ALARM", cls: "alarm" },
  ESTOP:    { text: "E-STOP", cls: "estop" },
  JOG:      { text: "CHẠY TAY", cls: "ready" },
};

const ALARMS = {
  1: ["ESTOP_ACTIVATED", "Nút dừng khẩn cấp đang nhấn", "err"],
  2: ["SAFETY_DOOR_OPEN", "Cửa bảo vệ bị mở", "err"],
  3: ["AIR_PRESSURE_LOW", "Áp suất khí nén < 0.5 MPa", "err"],
  4: ["CABINET_OVERTEMP", "Nhiệt độ tủ điện > 55 °C", "warn"],
  5: ["VISION_TIMEOUT", "3 lon liên tiếp không có kết quả Vision", "err"],
  6: ["VACUUM_FEED_MISS", "Đầu hút không giữ được tem", "warn"],
  7: ["LABEL_TAPE_EMPTY", "Hết cuộn tem / đứt băng tem", "warn"],
  8: ["CYL_P5_TIMEOUT", "Xi lanh dán P5 quá 150 ms", "err"],
  20: ["LI32_BUS_DISCONNECT", "Tuột cáp module ngõ vào LI32 / LI mở rộng", "err"],
};

const S = {
  state: "READY",
  speedSet: 120,
  offset: 0,
  enc: 0,                // mm băng tải đã chạy
  cans: [],              // lon đang trên băng tải
  nextId: 1,
  lastRelease: 0,
  fed: 0, ok: 0, ng: 0,
  runSec: 0,
  alarms: new Set(),
  recent: [],            // lon đã qua Vision gần nhất
  camLast: null,         // kết quả Vision gần nhất
  camStats: { ok: 0, ng: 0, tmo: 0, crc: 0, sum: 0, n: 0 },
  inspOk: 0, inspNg: 0, released: 0,
  log: [],
  logPaused: false,
  logFilter: "all",
  jogAxis: 0,
  activeRecipe: 1,
  selRecipe: 1,
  /* Chuỗi an toàn - thanh ghi 30024 (true = an toàn). reset = nút SAFETY_RESET trên tủ */
  safety: { estop: true, door: true, air: true, reset: false, bus: true },
  /* Bản mẫu: thêm ?bench=1 hoặc ?src=gpio vào đường dẫn để xem các chế độ khác */
  bench: new URLSearchParams(location.search).get("bench") === "1",   // 30023 bit3
  srcLi: new URLSearchParams(location.search).get("src") !== "gpio",  // 30023 bit4
};

/* ------------------------------------------------------------------------ */
/* Điều hướng trang  (PyQt6: QButtonGroup + QStackedWidget.setCurrentIndex)  */
/* ------------------------------------------------------------------------ */
$$("#nav button").forEach(b => b.addEventListener("click", () => {
  $$("#nav button").forEach(x => x.classList.toggle("active", x === b));
  $$(".page").forEach(p => p.classList.toggle("active", p.id === "page-" + b.dataset.page));
}));

// Tab con trong một trang (PyQt6: QTabWidget)
$$("[data-tabs]").forEach(bar => {
  bar.addEventListener("click", e => {
    const b = e.target.closest("button[data-sub]"); if (!b) return;
    $$("button", bar).forEach(x => x.classList.toggle("active", x === b));
    const page = bar.closest(".page");
    $$(".subpage", page).forEach(s => s.classList.toggle("active", s.id === b.dataset.sub));
  });
});

// Nút chọn một trong nhiều (segmented control)
$$(".seg").forEach(seg => seg.addEventListener("click", e => {
  const b = e.target.closest("button"); if (!b) return;
  $$("button", seg).forEach(x => x.classList.toggle("active", x === b));
  seg.dispatchEvent(new CustomEvent("change", { detail: b.dataset.v }));
}));

/* ------------------------------------------------------------------------ */
/* Hộp thoại xác nhận  (PyQt6: QMessageBox.question)                         */
/* ------------------------------------------------------------------------ */
function confirmBox(title, text, onYes) {
  $("#modal-title").textContent = title;
  $("#modal-text").textContent = text;
  $("#modal").classList.add("show");
  $("#modal-yes").onclick = () => { $("#modal").classList.remove("show"); onYes(); };
  $("#modal-no").onclick = () => $("#modal").classList.remove("show");
}

/* ------------------------------------------------------------------------ */
/* Nhật ký                                                                   */
/* ------------------------------------------------------------------------ */
function addLog(level, src, code, msg) {
  S.log.unshift({ t: now(), level, src, code, msg });
  if (S.log.length > 400) S.log.pop();
  if (!S.logPaused) renderLog();
}

/* ------------------------------------------------------------------------ */
/* Lệnh máy  (PyQt6: plc.send_command(MachineCmd.START) ...)                 */
/* ------------------------------------------------------------------------ */
function setState(st) {
  if (S.state === st) return;
  addLog("info", "FSM", "", `Trạng thái: ${STATES[S.state].text} → ${STATES[st].text}`);
  S.state = st;
  renderHeader();
}

function raiseAlarm(code) {
  if (S.alarms.has(code)) return;
  S.alarms.add(code);
  const a = ALARMS[code];
  addLog(a[2] === "err" ? "err" : "warn", "Alarm", "ALM_" + pad(code), `${a[0]} – ${a[1]}`);
  if (a[2] === "err" && (S.state === "RUN" || S.state === "PAUSE")) setState(code <= 2 ? "ESTOP" : "ALARM");
  renderAlarms();
}

$$("[data-cmd]").forEach(b => b.addEventListener("click", () => {
  const c = b.dataset.cmd;
  if (c === "start") {
    if (S.alarms.size && [...S.alarms].some(x => ALARMS[x][2] === "err")) return addLog("warn", "HMI", "", "START bị từ chối: còn alarm");
    if (["READY", "PAUSE"].includes(S.state)) setState("RUN");
  } else if (c === "pause") {
    if (S.state === "RUN") setState("PAUSE");
  } else if (c === "stop") {
    confirmBox("Dừng máy?", "Ngừng cấp lon và dừng băng tải. Các lon đang trên băng tải vẫn được theo dõi.", () => {
      if (["RUN", "PAUSE"].includes(S.state)) setState("READY");
    });
  } else if (c === "reset") {
    if (S.state === "ESTOP") {
      // Giống firmware: lệnh 40001 = 4 KHÔNG đưa máy ra khỏi E-Stop
      return addLog("warn", "HMI", "ALM_01", "Đang E-Stop: nhả nút E-Stop, đóng cửa rồi nhấn SAFETY_RESET trên tủ điện");
    }
    S.alarms.clear(); renderAlarms();
    addLog("info", "HMI", "", "Xóa alarm (40001 = 4)");
    if (S.state === "ALARM") setState("READY");
  } else if (c === "home") {
    if (["RUN", "PAUSE"].includes(S.state)) return;
    confirmBox("Về gốc toàn máy?", "Nhả P4, nâng P5, xả băng tải 5 giây rồi về gốc các trục M6, M3, M4, M5. Đảm bảo không có người trong vùng máy.", () => {
      setState("HOMING");
      S.cans = [];
      setTimeout(() => { if (S.state === "HOMING") setState("READY"); }, 3500);
    });
  }
}));

$$("[data-speed]").forEach(b => b.addEventListener("click", () => {
  S.speedSet = Math.max(60, Math.min(156, S.speedSet + +b.dataset.speed));
  $("#dash-speed-set").textContent = S.speedSet;
}));
$$("[data-offset]").forEach(b => b.addEventListener("click", () => {
  S.offset = Math.max(-18, Math.min(18, S.offset + +b.dataset.offset));
  $("#dash-offset").textContent = S.offset.toFixed(1);
}));

/* ------------------------------------------------------------------------ */
/* SIM: băng tải, lon, Vision  (chạy mỗi 100 ms)                             */
/* ------------------------------------------------------------------------ */
const ST = { cam: 50, orient: 300, label: 650, insp: 950, rej: 1250, end: 2000 };

function simStep(dt) {
  const running = S.state === "RUN" || S.state === "PAUSE";
  const v = running ? S.speedSet * 120 / 60 : (S.state === "HOMING" ? 40 : 0); // mm/s
  const dx = v * dt;
  S.enc += dx;
  if (S.state === "RUN") S.runSec += dt;

  // cấp lon mỗi 120 mm (P1/P2)
  if (S.state === "RUN" && S.enc - S.lastRelease >= 120) {
    S.lastRelease = S.enc;
    S.cans.push({ id: S.nextId++, x: 0, st: "new", theta: null, q: null, t: now() });
    S.fed++; S.released++;
  }

  for (const c of S.cans) {
    c.x += dx;
    if (c.st === "new" && c.x >= ST.cam) visionResult(c);
    if (c.st === "vok" && c.x >= ST.orient) { c.st = "ori"; $("#st-or-id").textContent = "#" + c.id; $("#st-or-deg").textContent = (c.theta * 0.695).toFixed(1) + "°"; }
    if (c.st === "ori" && c.x >= ST.label) {
      if (Math.random() < 0.985) { c.st = "lab"; } else { c.st = "ng"; c.why = "Hụt chân không"; raiseAlarm(6); }
    }
    if (c.st === "lab" && c.x >= ST.insp) {
      if (Math.random() < 0.995) { c.st = "pass"; S.inspOk++; } else { c.st = "ng"; c.why = "Mất tem (S5)"; S.inspNg++; }
    }
    if ((c.st === "ng" || c.st === "vng") && c.x >= ST.rej && !c.rejected) {
      c.rejected = true; S.ng++; c.y = 0;
      addLog("warn", "P8", "", `Loại lon #${c.id}: ${c.why}`);
    }
    if (c.rejected) c.y = (c.y || 0) + 4;
  }
  S.cans = S.cans.filter(c => {
    if (c.x >= ST.end) { if (c.st === "pass") S.ok++; return false; }
    return !(c.rejected && c.y > 40);
  });

  if (S.state === "RUN" && Math.random() < 0.0004) raiseAlarm(4);   // thỉnh thoảng có cảnh báo để thấy giao diện alarm
}

function visionResult(c) {
  const r = Math.random();
  const proc = rnd(18, 42);
  if (r < 0.015) {               // quá 80 ms
    c.st = "vng"; c.why = "Vision timeout"; c.q = "TIMEOUT"; S.camStats.tmo++;
  } else if (r < 0.04) {
    c.st = "vng"; c.why = "Không thấy khoen"; c.q = "NG_NO_TAB"; S.camStats.ng++;
  } else {
    c.st = "vok"; c.q = "OK"; c.theta = rnd(-180, 180); S.camStats.ok++;
  }
  if (c.q !== "TIMEOUT") { S.camStats.sum += proc; S.camStats.n++; }
  S.camLast = { id: c.id, theta: c.theta, q: c.q, proc, t: now() };
  S.recent.unshift(c);
  if (S.recent.length > 30) S.recent.pop();
  addRowCamHistory(S.camLast);
  if (c.q !== "OK") addLog(c.q === "TIMEOUT" ? "warn" : "info", "Vision", c.q === "TIMEOUT" ? "ALM_05" : "", `Lon #${c.id}: ${c.q}`);
}

/* ------------------------------------------------------------------------ */
/* Header + footer                                                           */
/* ------------------------------------------------------------------------ */
function renderHeader() {
  const s = STATES[S.state];
  const pill = $("#hdr-state");
  pill.textContent = s.text;
  pill.className = "state-pill " + s.cls;
  $("#hdr-speed").textContent = S.state === "RUN" ? S.speedSet : 0;
}

function renderClock() {
  const d = now();
  $("#clock-time").textContent = hhmmss(d);
  $("#clock-date").textContent = `${pad(d.getDate())}/${pad(d.getMonth() + 1)}/${d.getFullYear()}`;
}

function renderAlarms() {
  const list = [...S.alarms].sort((a, b) => a - b);
  const box = $("#dash-alarms");
  box.innerHTML = list.length ? "" : '<div class="empty">Không có alarm</div>';
  for (const c of list) {
    const a = ALARMS[c];
    box.insertAdjacentHTML("beforeend",
      `<div class="alarm-item ${a[2]}"><div><b>ALM_${pad(c)} ${a[0]}</b><small>${a[1]}</small></div></div>`);
  }
  $("#dash-alarm-count").textContent = list.length;
  const badge = $("#nav-badge");
  badge.textContent = list.length;
  badge.classList.toggle("show", list.length > 0);
  const f = $("#footer");
  f.classList.toggle("has-alarm", list.length > 0);
  $("#ftr-dot").className = "dot " + (list.length ? "err" : "ok");
  $("#ftr-alarm").textContent = list.length
    ? list.map(c => `ALM_${pad(c)} ${ALARMS[c][0]}`).join("   •   ")
    : "Không có alarm";
}

/* ------------------------------------------------------------------------ */
/* Trang Tổng quan                                                           */
/* ------------------------------------------------------------------------ */
const CV = { x0: 20, x1: 600, y: 78 };
const mm2x = mm => CV.x0 + (CV.x1 - CV.x0) * mm / 2000;

function buildConveyor() {
  const svg = $("#conveyor-svg");
  const sts = [[0, "S2"], [50, "Camera"], [300, "Chỉnh góc"], [650, "Dán tem"], [950, "S5"], [1250, "Loại P8"], [2000, "Ra"]];
  let h = `<rect x="${CV.x0}" y="${CV.y - 14}" width="${CV.x1 - CV.x0}" height="28" rx="14" fill="#202a35" stroke="#2f3b49"/>`;
  for (let i = 0; i < 40; i++) h += `<line class="belt-line" x1="${CV.x0 + i * 15}" y1="${CV.y - 12}" x2="${CV.x0 + i * 15}" y2="${CV.y + 12}" stroke="#2a3542"/>`;
  sts.forEach(([mm, name], i) => {
    const x = mm2x(mm);
    const up = i === 1 ? 16 : 0;   // nhãn "Camera" đặt cao hơn để không đè lên "S2"
    h += `<line x1="${x}" y1="${CV.y - 30 - up}" x2="${x}" y2="${CV.y - 16}" stroke="#56677a" stroke-width="2"/>`;
    h += `<text x="${x}" y="${CV.y - 36 - up}" fill="#8b98a6" font-size="11" text-anchor="middle">${name}</text>`;
    h += `<text x="${x}" y="${CV.y + 32 + (i === 1 ? 10 : 0)}" fill="#56677a" font-size="9" text-anchor="middle">${mm}</text>`;
  });
  h += `<g id="cans"></g>`;
  h += `<text x="${CV.x1}" y="${CV.y + 50}" fill="#56677a" font-size="9" text-anchor="end">mm từ S2</text>`;
  svg.innerHTML = h;
}

function renderConveyor() {
  const col = { new: "#56677a", vok: "#3b8bf6", ori: "#3b8bf6", lab: "#22c55e", pass: "#22c55e", ng: "#ef4444", vng: "#ef4444" };
  $("#cans").innerHTML = S.cans.map(c => {
    const fill = c.st === "new" && c.x >= ST.cam ? "#38bdf8" : col[c.st];
    return `<circle cx="${mm2x(c.x)}" cy="${CV.y + (c.y || 0)}" r="7" fill="${fill}" stroke="#0e1318" stroke-width="2"/>`;
  }).join("");
  // vạch băng tải chạy
  const off = (S.enc * 0.29) % 15;
  $$(".belt-line").forEach((l, i) => { const x = CV.x0 + ((i * 15 + off) % 600); l.setAttribute("x1", x); l.setAttribute("x2", x); });
}

function renderDashboard() {
  const total = S.ok + S.ng;
  $("#k-speed").textContent = S.state === "RUN" ? S.speedSet : 0;
  $("#k-fed").textContent = S.fed;
  $("#k-ok").textContent = S.ok;
  $("#k-ng").textContent = S.ng;
  const y = total ? (S.ok / total * 100) : 0;
  $("#k-yield").textContent = total ? y.toFixed(1) + " %" : "–";
  $("#k-yield-bar").style.width = y + "%";
  $("#k-belt").textContent = S.cans.length;
  $("#k-runtime").textContent = `${pad(Math.floor(S.runSec / 3600))}:${pad(Math.floor(S.runSec / 60) % 60)}`;
  $("#dash-recent").innerHTML = S.recent.slice(0, 8).map(c => {
    const res = c.rejected ? '<span class="tag err">Loại</span>' : c.st === "pass" ? '<span class="tag ok">Đạt</span>'
      : c.st === "ng" || c.st === "vng" ? '<span class="tag err">NG</span>' : '<span class="tag muted">Đang chạy</span>';
    const q = c.q === "OK" ? '<span class="tag ok">OK</span>' : `<span class="tag ${c.q === "TIMEOUT" ? "warn" : "err"}">${c.q}</span>`;
    return `<tr><td class="mono">#${c.id}</td><td class="num">${c.theta == null ? "–" : c.theta.toFixed(2) + "°"}</td><td>${q}</td><td>${res}</td></tr>`;
  }).join("");
}

/* ------------------------------------------------------------------------ */
/* Vẽ ảnh nắp lon  (PyQt6: QLabel.setPixmap(QPixmap.fromImage(frame)) và     */
/* QPainter vẽ lớp phủ: vòng tròn nắp, tia góc, chữ)                         */
/* ------------------------------------------------------------------------ */
function drawLid(cv, res, big) {
  // Khớp số điểm ảnh của canvas với kích thước hiển thị -> hình tròn không bị méo
  if (cv.clientWidth && (cv.width !== cv.clientWidth || cv.height !== cv.clientHeight)) {
    cv.width = cv.clientWidth; cv.height = cv.clientHeight;
  }
  const g = cv.getContext("2d");
  const W = cv.width, H = cv.height, cx = W / 2, cy = H / 2;
  const R = Math.min(W, H) * 0.38;
  g.fillStyle = "#050607"; g.fillRect(0, 0, W, H);
  if (!res) { g.fillStyle = "#56677a"; g.font = "13px sans-serif"; g.textAlign = "center"; g.fillText("Chờ ảnh…", cx, cy); return; }
  // nắp lon
  const grd = g.createRadialGradient(cx - R * .3, cy - R * .3, R * .1, cx, cy, R);
  grd.addColorStop(0, "#c9ced3"); grd.addColorStop(1, "#6d747b");
  g.fillStyle = grd; g.beginPath(); g.arc(cx, cy, R, 0, 7); g.fill();
  g.strokeStyle = "#9aa1a8"; g.lineWidth = R * .06; g.beginPath(); g.arc(cx, cy, R * .9, 0, 7); g.stroke();
  // khoen (tab)
  const th = res.theta == null ? rnd(0, 360) : res.theta;
  const a = th * Math.PI / 180;
  if (res.q !== "NG_NO_TAB") {
    g.save(); g.translate(cx, cy); g.rotate(a);
    g.fillStyle = "#a9b0b7"; g.strokeStyle = "#5c6369"; g.lineWidth = 2;
    g.beginPath(); g.ellipse(R * .38, 0, R * .34, R * .2, 0, 0, 7); g.fill(); g.stroke();
    g.fillStyle = "#3a3f44"; g.beginPath(); g.ellipse(R * .5, 0, R * .14, R * .09, 0, 0, 7); g.fill();
    g.fillStyle = "#8c939a"; g.beginPath(); g.arc(0, 0, R * .07, 0, 7); g.fill();
    g.restore();
  }
  // lớp phủ nhận diện
  const okc = res.q === "OK" ? "#22c55e" : "#ef4444";
  g.strokeStyle = okc; g.lineWidth = 2; g.setLineDash([6, 4]);
  g.beginPath(); g.arc(cx, cy, R, 0, 7); g.stroke(); g.setLineDash([]);
  g.strokeStyle = "rgba(255,255,255,.35)"; g.lineWidth = 1;
  g.beginPath(); g.moveTo(cx - R * 1.15, cy); g.lineTo(cx + R * 1.15, cy); g.moveTo(cx, cy - R * 1.15); g.lineTo(cx, cy + R * 1.15); g.stroke();
  if (res.theta != null) {
    g.strokeStyle = okc; g.lineWidth = big ? 3 : 2;
    g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx + Math.cos(a) * R * 1.1, cy + Math.sin(a) * R * 1.1); g.stroke();
    g.strokeStyle = "#f5a524"; g.lineWidth = 2;
    g.beginPath(); g.arc(cx, cy, R * .3, 0, a, a < 0); g.stroke();
  }
  if (!big) {
    g.fillStyle = okc; g.font = "bold 12px Consolas, monospace"; g.textAlign = "left";
    g.fillText(`#${res.id}  ${res.theta == null ? res.q : res.theta.toFixed(2) + "°"}`, 8, 16);
  }
}

/* ------------------------------------------------------------------------ */
/* Trang Camera                                                              */
/* ------------------------------------------------------------------------ */
function addRowCamHistory(r) {
  const tb = $("#cam-history");
  const q = r.q === "OK" ? '<span class="tag ok">0x00 OK</span>'
    : r.q === "TIMEOUT" ? '<span class="tag warn">— timeout</span>' : '<span class="tag err">0x01 NO_TAB</span>';
  tb.insertAdjacentHTML("afterbegin",
    `<tr><td class="mono">${hhmmss(r.t)}</td><td class="mono">${r.id}</td><td class="num">${r.theta == null ? "–" : r.theta.toFixed(2)}</td>` +
    `<td class="num">${r.theta == null ? "–" : Math.round(r.theta * 100)}</td><td>${q}</td><td class="num">${r.q === "TIMEOUT" ? "&gt;80" : r.proc.toFixed(0)} ms</td>` +
    `<td class="muted">${r.q === "OK" ? "" : "Lon sẽ bị loại tại P8"}</td></tr>`);
  while (tb.rows.length > 60) tb.deleteRow(-1);
}

let camMode = "live";
$("#cam-mode").addEventListener("change", e => { camMode = e.detail; });

function renderCamera() {
  const r = S.camLast;
  const cs = S.camStats;
  const show = camMode === "ng" ? S.recent.find(c => c.q !== "OK") : null;
  const res = camMode === "ng" ? (show ? { id: show.id, theta: show.theta, q: show.q } : null) : r;
  if (camMode !== "last" || !renderCamera.frozen) { drawLid($("#cam-canvas"), res, true); renderCamera.frozen = camMode === "last"; }
  drawLid($("#mini-cam"), r, false);
  if (!r) return;
  $("#mini-cam-info").textContent = `${(cs.n ? cs.sum / cs.n : 0).toFixed(0)} ms`;
  $("#cam-osd").innerHTML = `CAN #${r.id}<br>θ = ${r.theta == null ? "–" : r.theta.toFixed(2) + "°"}`;
  $("#cam-osd-r").innerHTML = `EXP ${$("#v-exp").textContent}<br>${r.q === "TIMEOUT" ? "&gt;80" : r.proc.toFixed(1)} ms`;
  $("#cam-theta").textContent = r.theta == null ? "–" : (r.theta >= 0 ? "+" : "") + r.theta.toFixed(2) + "°";
  const qt = $("#cam-q");
  qt.textContent = r.q; qt.className = "tag " + (r.q === "OK" ? "ok" : r.q === "TIMEOUT" ? "warn" : "err");
  $("#cam-proc").textContent = r.q === "TIMEOUT" ? ">80 ms" : r.proc.toFixed(1) + " ms";
  $("#cam-avg").textContent = (cs.n ? cs.sum / cs.n : 0).toFixed(1) + " ms";
  $("#cam-ok").textContent = cs.ok; $("#cam-ng").textContent = cs.ng;
  $("#cam-tmo").textContent = cs.tmo; $("#cam-crc").textContent = cs.crc;
}

$$("input[type=range]").forEach(r => r.addEventListener("input", () => {
  const v = r.dataset.scale ? (r.value * r.dataset.scale).toFixed(2) : r.value;
  $("#" + r.dataset.out).textContent = v + (r.dataset.unit || "");
}));

/* ------------------------------------------------------------------------ */
/* Trang Giám sát                                                            */
/* ------------------------------------------------------------------------ */
const AXES = [
  { n: "M2", f: "Chỉnh góc (bánh PU)", u: "°", pos: 0, lim: "±180°" },
  { n: "M3", f: "Mâm xoay 6 đầu", u: "°", pos: 0, lim: "0 – 360°" },
  { n: "M4", f: "Kéo tem", u: "mm", pos: 0, lim: "không" },
  { n: "M5", f: "Dịch làn tem", u: "mm", pos: 0, lim: "0 – 45 mm" },
  { n: "M6", f: "Nâng hạ (M6a+b)", u: "mm", pos: 30, lim: "0 – 98 mm" },
];
const DI_NAMES = ["S7_LANE1","S8_LANE2","S9_LANE3","S10_LANE4","P1_EXT","P1_RET","P2_EXT","P2_RET","P3_EXT","P3_RET","P4_EXT","P4_RET","P5_UP","P5_DOWN","P8_EXT","P8_RET","VAC_SW_1","VAC_SW_2","VAC_SW_3","VAC_SW_4","VAC_SW_5","VAC_SW_6","S0_QUEUE","S1_RELEASE","ALM_M2","ALM_M3","ALM_M4","ALM_M5","ALM_M6A","ALM_M6B","S14_TAPE_END","SPARE_32"];
const DO_NAMES = ["CYL_P1","CYL_P2","CYL_P3","CYL_P4","CYL_P5","VALVE_P7","CYL_P8","MTR_M7","VAC_HEAD_1","VAC_HEAD_2","VAC_HEAD_3","VAC_HEAD_4","VAC_HEAD_5","VAC_HEAD_6","VAC_BLOW","SPARE_16","ENA_M2","ENA_M4","ENA_M5","ENA_M6","SON_M3","TOWER_RED","TOWER_YEL","TOWER_GRN"];
const CYLS = [["P1", "Chặn lon A"], ["P2", "Nêm chen khe"], ["P3", "Chốt chỉnh góc"], ["P4", "Kẹp lon"], ["P5", "Hạ dán tem"], ["P8", "Đẩy loại"]];
const cylState = { P1: true, P2: false, P3: false, P4: false, P5: false, P8: false };

function buildMonitor() {
  $("#di-grid").innerHTML = DI_NAMES.map((n, i) =>
    `<div class="io"><i class="led" id="di${i}"></i><code>${Math.floor(i / 8)}.${i % 8}</code><span>${n}</span></div>`).join("");
  $("#do-grid").innerHTML = DO_NAMES.map((n, i) =>
    `<div class="io"><i class="led" id="do${i}"></i><code>${Math.floor(i / 8)}.${i % 8}</code><span>${n}</span></div>`).join("");
  buildSafety();
  $("#cyl-grid").innerHTML = CYLS.map(([k, t]) =>
    `<div class="cyl"><div class="row"><b>${k}</b><span class="muted">${t}</span></div>` +
    `<div class="row"><span><i class="led" id="cyl-${k}-ext" style="display:inline-block;vertical-align:-2px"></i> Ra</span>` +
    `<span><i class="led" id="cyl-${k}-ret" style="display:inline-block;vertical-align:-2px"></i> Về</span></div>` +
    `<button class="btn sm" data-cyl="${k}">Đảo</button></div>`).join("");
  $$("[data-cyl]").forEach(b => b.addEventListener("click", () => {
    if (S.state !== "JOG") return addLog("warn", "HMI", "", "Chỉ điều khiển xi lanh ở chế độ chạy tay");
    cylState[b.dataset.cyl] = !cylState[b.dataset.cyl];
  }));
}

function renderMonitor() {
  const run = S.state === "RUN";
  const v = run ? S.speedSet * 2 : 0;
  // trạng thái trạm
  const set = (id, txt, cls) => { const e = $(id); e.textContent = txt; e.className = "st-state " + (cls || ""); };
  set("#st-feed", run ? "Đang cấp" : "Chặn (P1 ra)", run ? "ok-t" : "muted");
  $("#st-s0").textContent = "Có lon"; $("#st-rel").textContent = S.released;
  const ori = S.cans.find(c => c.x > 270 && c.x < 330 && c.st !== "vng");
  set("#st-orient", ori ? "Đang chỉnh #" + ori.id : "Chờ lon", ori ? "info-t" : "muted");
  $("#st-or-ms").textContent = ori ? Math.round(rnd(180, 240)) + " ms" : "– ms";
  const lab = S.cans.find(c => c.x > 600 && c.x < 670);
  set("#st-label", lab ? "Dán #" + lab.id : "Chờ lon", lab ? "ok-t" : "muted");
  $("#st-head").textContent = "#" + (1 + Math.floor(S.enc / 120) % 6);
  $("#st-vac").textContent = S.alarms.has(6) ? "MISS" : "OK";
  $("#st-lb-ms").textContent = lab ? Math.round(rnd(285, 310)) + " ms" : "– ms";
  set("#st-insp", run ? "Đang kiểm" : "Chờ lon", run ? "ok-t" : "muted");
  $("#st-insp-ok").textContent = S.inspOk; $("#st-insp-ng").textContent = S.inspNg;
  const rej = S.cans.some(c => c.rejected && c.y < 20);
  set("#st-rej", rej ? "ĐANG ĐẨY" : "Sẵn sàng", rej ? "err-t" : "muted");
  $("#st-rej-n").textContent = S.ng; $("#st-p8").textContent = rej ? "Ra" : "Về";

  // trục
  if (run) { AXES[1].pos = (S.enc / 120 * 60) % 360; AXES[2].pos += 0.8; }
  const homed = !["HOME_REQ", "ESTOP", "POWER_ON"].includes(S.state);
  $("#axis-table").innerHTML = AXES.map((a, i) => {
    const moving = (run && (i === 1 || i === 2 || (i === 0 && ori))) || (S.state === "HOMING");
    return `<tr><td><b>${a.n}</b></td><td>${a.f}</td><td class="num">${a.pos.toFixed(2)} ${a.u}</td>` +
      `<td class="num">${moving ? (i === 1 ? "142 kHz" : rnd(5, 40).toFixed(1) + " kHz") : "0"}</td>` +
      `<td>${homed ? '<span class="tag ok">OK</span>' : '<span class="tag warn">Chưa</span>'}</td>` +
      `<td>${S.state === "ESTOP" ? '<span class="tag muted">Tắt</span>' : '<span class="tag ok">Bật</span>'}</td>` +
      `<td>${moving ? '<span class="tag info">Đang chạy</span>' : '<span class="tag muted">Đứng yên</span>'}</td></tr>`;
  }).join("");

  // an toàn
  $("#vfd-hz").textContent = (v / 6.24).toFixed(2) + " Hz";
  $("#vfd-a").textContent = (run ? rnd(1.1, 1.3) : 0).toFixed(1) + " A";

  // I/O
  const di = new Array(32).fill(false), dout = new Array(24).fill(false);
  di[0] = di[1] = di[2] = di[3] = true; di[22] = true;
  di[4] = !run || Math.random() > .3; di[5] = !di[4];
  di[6] = run && Math.random() > .7; di[7] = !di[6];
  di[8] = !!ori; di[9] = !ori; di[10] = !!ori; di[11] = !ori;
  di[12] = !lab; di[13] = !!lab; di[15] = !rej; di[14] = rej;
  for (let i = 16; i < 22; i++) di[i] = run && i - 16 !== 5;
  dout[0] = di[4]; dout[2] = !!ori; dout[3] = !!ori; dout[4] = !!lab; dout[6] = rej; dout[7] = run;
  for (let i = 8; i < 13; i++) dout[i] = run;
  dout[20] = S.state !== "ESTOP"; dout[21] = S.alarms.size > 0; dout[22] = S.state === "PAUSE"; dout[23] = run;
  if (S.state === "ESTOP") dout[16] = dout[17] = dout[18] = dout[19] = true;
  di.forEach((on, i) => { const e = $("#di" + i); e.classList.toggle("on", on); e.classList.toggle("red", i >= 24 && i <= 30); });
  dout.forEach((on, i) => $("#do" + i).classList.toggle("on", on));

  // xi lanh (chạy tay)
  for (const [k] of CYLS) {
    $(`#cyl-${k}-ext`).classList.toggle("on", cylState[k]);
    $(`#cyl-${k}-ret`).classList.toggle("on", !cylState[k]);
  }
  const ax = AXES[S.jogAxis];
  $("#jog-pos").textContent = ax.pos.toFixed(2) + " " + ax.u;
  $("#jog-lim").textContent = ax.lim;
}

// chạy tay: chọn trục, giữ nút để chạy
$("#jog-axis").addEventListener("click", e => {
  const b = e.target.closest("button"); if (!b) return;
  $$("#jog-axis button").forEach(x => x.classList.toggle("active", x === b));
  S.jogAxis = +b.dataset.ax;
});
let jogTimer = null;
function jogStart(dir) {
  if (S.state !== "JOG") return addLog("warn", "HMI", "", "Hãy vào chế độ chạy tay trước");
  jogTimer = setInterval(() => { AXES[S.jogAxis].pos += dir * 0.25; }, 50);
}
const jogStop = () => { clearInterval(jogTimer); jogTimer = null; };
for (const [id, dir] of [["#jog-neg", -1], ["#jog-pos-btn", 1]]) {
  $(id).addEventListener("pointerdown", () => jogStart(dir));
  ["pointerup", "pointerleave"].forEach(ev => $(id).addEventListener(ev, jogStop));
}
$("#jog-enter").addEventListener("click", () => {
  if (S.state === "JOG") { setState("READY"); $("#jog-enter").textContent = "Vào chế độ chạy tay"; }
  else if (S.state === "READY") { setState("JOG"); $("#jog-enter").textContent = "Thoát chế độ chạy tay"; }
  else addLog("warn", "HMI", "", "Chỉ vào chạy tay khi máy READY");
});

/* ------------------------------------------------------------------------ */
/* An toàn  (PyQt6: đọc status.estop_ok, door_closed, air_ok_sw, reset_pressed,  */
/* io_bus_ok, safety_ext_raw (30024) và safety_bench_mode (30023 bit3))       */
/* ------------------------------------------------------------------------ */
const SF = [   // khóa, tên, chữ khi an toàn, chữ khi không an toàn, bit LI mở rộng, tên ngắn
  ["estop", "Nút dừng khẩn cấp", "Đã nhả", "ĐANG NHẤN", 0, "E-Stop nhả"],
  ["door", "Cửa bảo vệ", "Đóng", "ĐANG MỞ", 1, "Cửa đóng"],
  ["air", "Áp suất khí nén", "≥ 0.5 MPa", "THIẾU KHÍ", 2, "Đủ khí nén"],
  ["reset", "Nút SAFETY_RESET", "Không nhấn", "ĐANG NHẤN", 3, "Nút Reset"],
  ["bus", "Cáp module LI", "Kết nối tốt", "MẤT KẾT NỐI", -1, "Cáp LI"],
];

/** Giá trị firmware đọc được: mất cáp LI thì mọi tín hiệu bị coi là KHÔNG an toàn. */
function sfRead() {
  const s = S.safety;
  if (S.bench) return { estop: true, door: true, air: true, reset: false, bus: s.bus };
  const failAll = S.srcLi && !s.bus;
  return { estop: s.estop && !failAll, door: s.door && !failAll, air: s.air && !failAll, reset: s.reset && !failAll, bus: s.bus };
}
function sfRaw() {
  const s = S.safety;
  if (!S.srcLi) return 0;
  if (!s.bus) return 0xFF;
  return (s.estop ? 1 : 0) | (s.door ? 2 : 0) | (s.air ? 4 : 0) | (s.reset ? 8 : 0);
}
const km1On = () => !S.bench && S.state !== "ESTOP" && S.state !== "POWER_ON";

function buildSafety() {
  $("#sf-tiles").innerHTML = SF.map(([k, name]) =>
    `<button class="sf-tile" data-sf="${k}"><small>${name}</small><b id="sft-${k}">–</b><span class="src" id="sfs-${k}"></span></button>`).join("");
  $("#sf-power").innerHTML =
    `<div class="sf-tile" style="cursor:default"><small>Contactor KM1 (nguồn động lực)</small><b id="sft-km1">–</b><span class="src">GPIO PC4</span></div>` +
    `<div class="sf-tile" style="cursor:default"><small>Phanh trục nâng M6</small><b id="sft-brake">–</b><span class="src">GPIO PC5</span></div>`;
  const names = ["E-Stop", "Cửa", "Khí", "Reset", "–", "–", "–", "–"];
  $("#sf-bits").innerHTML = names.map((n, i) =>
    `<div class="bit ${i < 4 ? "used" : ""}" id="sfb-${i}"><b>0</b>b${i} ${n}</div>`).reverse().join("");
  $("#sf-check").innerHTML = [
    ["Bình thường (nhả E-Stop, đóng cửa, đủ khí)", "0x07"],
    ["Nhấn E-Stop", "0x06"],
    ["Mở cửa bảo vệ", "0x05"],
    ["Khóa van khí", "0x03"],
    ["Giữ nút SAFETY_RESET", "0x0F"],
    ["Rút cáp LI", "Bus --"],
  ].map(([t, v], i) => `<tr><td>${t}</td><td class="num">${v}</td><td id="sfc-${i}"></td></tr>`).join("");
  $("#safety").innerHTML = SF.map(([k, , , , , short]) =>
    `<div class="io" id="sfm-${k}"><i class="led"></i><span>${short}</span></div>`).join("") +
    `<div class="io" id="sfm-km1"><i class="led"></i><span>KM1 đóng</span></div>`;

  // Bản mẫu: nhấn ô để giả lập tín hiệu (bản PyQt6 chỉ hiển thị, không có thao tác này)
  $("#sf-tiles").addEventListener("click", e => {
    const t = e.target.closest("[data-sf]"); if (!t) return;
    if (S.bench) return addLog("warn", "Safety", "", "Chế độ bàn thử: chuỗi an toàn bị bỏ qua");
    const k = t.dataset.sf;
    if (k === "reset") {             // nút nhấn nhả: giữ 1 giây
      S.safety.reset = true;
      setTimeout(() => { S.safety.reset = false; }, 1000);
    } else {
      S.safety[k] = !S.safety[k];
    }
  });
}

/** Phản ứng của firmware với chuỗi an toàn (giả lập io.c + machine.c). */
function simSafety() {
  const r = sfRead();
  const moving = ["RUN", "PAUSE", "HOMING", "JOG"].includes(S.state);
  if (!r.bus) raiseAlarm(20);
  if (!r.estop) {
    raiseAlarm(1);
    if (S.state !== "ESTOP") { S.cans = []; setState("ESTOP"); }
  }
  if (!r.door) {
    raiseAlarm(2);
    if (moving) { S.cans = []; setState("ESTOP"); }
  } else if (!moving && S.state !== "ESTOP" && S.alarms.has(2)) {
    S.alarms.delete(2);              // cửa đóng lại khi máy đứng yên: tự xóa
    renderAlarms();
  }
  if (!r.air) raiseAlarm(3);
  // Thoát E-Stop: E-Stop nhả + cửa đóng + nhấn SAFETY_RESET (nút vật lý trên tủ)
  if (S.state === "ESTOP" && r.estop && r.door && r.reset) {
    S.alarms.clear();
    renderAlarms();
    addLog("info", "Safety", "", "SAFETY_RESET – thoát E-Stop, cần homing lại");
    setState("HOME_REQ");
  }
}

function renderSafety() {
  const r = sfRead();
  // ô trên thanh tiêu đề
  const chip = $("#hdr-safety");
  const bad = !r.bus ? "MẤT CÁP LI" : !r.estop ? "E-STOP" : !r.door ? "CỬA MỞ" : !r.air ? "THIẾU KHÍ" : "";
  chip.className = "safety-chip " + (S.bench ? "bench" : bad ? "bad" : "ok");
  chip.textContent = S.bench ? "BÀN THỬ" : (bad || "AN TOÀN OK");
  $("#app").classList.toggle("bench", S.bench);

  // thẻ tóm tắt (tab Trạm & trục)
  for (const [k] of SF) {
    const el = $("#sfm-" + k);
    const ok = k === "reset" ? true : r[k];
    el.classList.toggle("bad", !ok);
    $(".led", el).className = "led " + (k === "reset" ? (r.reset ? "on" : "") : (ok ? "on" : "on red"));
  }
  $("#sfm-km1 .led").classList.toggle("on", km1On());

  // tab An toàn
  for (const [k, , txtOk, txtBad, bit] of SF) {
    const t = $(`[data-sf="${k}"]`);
    const ok = r[k];
    const skip = S.bench && k !== "bus";
    t.className = "sf-tile " + (skip ? "skip" : k === "reset" ? (ok ? "act" : "ok") : (ok ? "ok" : "bad"));
    $("#sft-" + k).textContent = skip ? "BỎ QUA" : k === "reset" ? (ok ? "ĐANG NHẤN" : txtOk) : (ok ? txtOk : txtBad);
    $("#sfs-" + k).textContent = bit < 0 ? "30024 bit4" : (S.srcLi ? `LI mở rộng · bit ${bit}` : "GPIO PC" + bit);
  }
  const km = km1On();
  $("#sft-km1").textContent = S.bench ? "KHÓA TẮT" : (km ? "Đóng" : "Cắt");
  $("#sft-km1").parentElement.className = "sf-tile " + (km ? "ok" : "bad");
  $("#sft-brake").textContent = km ? "Nhả" : "Đóng phanh";
  $("#sft-brake").parentElement.className = "sf-tile " + (km ? "ok" : "act");
  $("#sf-estop-help").classList.toggle("show", S.state === "ESTOP");
  $("#sf-hint").textContent = S.bench ? "Firmware bản bàn thử" : "Bản mẫu: nhấn vào ô để giả lập tín hiệu";

  // byte thô của IC LI mở rộng
  const raw = sfRaw();
  $("#sf-src").textContent = S.srcLi ? "PCB v1 · 74HCT165" : "PCB v2 · GPIO";
  $("#sf-raw").textContent = S.srcLi ? "0x" + raw.toString(16).toUpperCase().padStart(2, "0") : "—";
  for (let i = 0; i < 8; i++) {
    const one = S.srcLi && ((raw >> i) & 1) === 1;
    const b = $("#sfb-" + i);
    b.classList.toggle("one", one);
    $("b", b).textContent = S.srcLi ? (one ? "1" : "0") : "–";
  }
  const s = S.safety;
  const now_ = [s.estop && s.door && s.air && !s.reset && s.bus, !s.estop, !s.door, !s.air, s.reset, !s.bus];
  now_.forEach((on, i) => { $("#sfc-" + i).innerHTML = on ? '<span class="tag info">◀ hiện tại</span>' : ""; });
}

/** Bật/tắt nút điều khiển theo trạng thái máy (PyQt6: QPushButton.setEnabled) */
function updateButtons() {
  const en = {
    start: ["READY", "PAUSE"], pause: ["RUN"], stop: ["RUN", "PAUSE"],
    reset: ["READY", "ALARM", "HOME_REQ", "ESTOP", "PAUSE", "RUN"], home: ["HOME_REQ", "READY", "ALARM"],
  };
  $$("[data-cmd]").forEach(b => { b.disabled = !en[b.dataset.cmd].includes(S.state); });
}

/* ------------------------------------------------------------------------ */
/* Trang Cấu hình - Recipe  (PyQt6: plc.read_recipe / plc.save_recipe)       */
/* ------------------------------------------------------------------------ */
const RECIPES = Array.from({ length: 16 }, (_, i) => {
  const base = [
    ["CAN_330ML_STD", 66.0, 115.2, 30.0], ["CAN_250ML_LUN", 66.0, 92.0, 6.8],
    ["CAN_330ML_SLK", 58.0, 145.0, 59.8], ["CAN_500ML_CAO", 66.0, 168.0, 82.8],
  ][i] || [`RECIPE_${pad(i + 1)}`, 66.0, 115.2, 30.0];
  return { id: i + 1, name: base[0], diameter: base[1], height: base[2], m6: base[3], tab: 0, speed: 120, press: 40, vac: 50, blow: 30, overrun: 50 };
});
let formDirty = false;

function renderRecipeList() {
  $("#recipe-list").innerHTML = RECIPES.map(r =>
    `<button class="${r.id === S.selRecipe ? "sel" : ""} ${r.id === S.activeRecipe ? "act" : ""}" data-id="${r.id}">` +
    `<span class="no">${r.id}</span><span>${r.name}<small>Ø${r.diameter.toFixed(1)} · H${r.height.toFixed(1)} mm</small></span></button>`).join("");
}

function loadForm(id) {
  const r = RECIPES[id - 1];
  S.selRecipe = id;
  $$("#recipe-form [data-k]").forEach(inp => {
    const k = inp.dataset.k;
    inp.value = k === "scale" ? Math.round(r.diameter * 1000 / 95) : r[k];
    inp.closest(".field").classList.remove("bad");
  });
  $("#rcp-title").textContent = `#${id} · ${r.name}`;
  $("#rcp-active-tag").style.display = id === S.activeRecipe ? "" : "none";
  formDirty = false; $("#rcp-dirty").classList.remove("show");
  renderRecipeList();
}

$("#recipe-list").addEventListener("click", e => {
  const b = e.target.closest("button"); if (!b) return;
  const go = () => loadForm(+b.dataset.id);
  formDirty ? confirmBox("Bỏ thay đổi?", "Recipe đang sửa có thay đổi chưa lưu.", go) : go();
});

$("#recipe-form").addEventListener("input", e => {
  const inp = e.target;
  formDirty = true; $("#rcp-dirty").classList.add("show");
  if (inp.dataset.k === "diameter") $('[data-k="scale"]').value = Math.round(+inp.value * 1000 / 95) || "";
  if (inp.dataset.min != null) {
    const v = +inp.value;
    inp.closest(".field").classList.toggle("bad", inp.value === "" || v < +inp.dataset.min || v > +inp.dataset.max);
  }
});

// Kiểm tra cục bộ giống Recipe.validate() của tools/pc_config, rồi mới gửi
function saveForm(activate) {
  if ($$("#recipe-form .field.bad").length) return addLog("warn", "Recipe", "", "Có giá trị ngoài phạm vi – chưa lưu");
  if (activate && ["RUN", "PAUSE", "HOMING"].includes(S.state)) return addLog("err", "Recipe", "", "ERR_STATE: máy đang chạy, không đổi recipe đang dùng");
  const doIt = () => {
    const r = RECIPES[S.selRecipe - 1];
    $$("#recipe-form [data-k]").forEach(inp => { if (inp.dataset.k !== "scale") r[inp.dataset.k] = inp.type === "number" ? +inp.value : inp.value; });
    if (activate) {
      S.activeRecipe = r.id;
      $("#hdr-recipe").textContent = `${r.id} · ${r.name}`;
      S.speedSet = r.speed; $("#dash-speed-set").textContent = r.speed;
    }
    addLog("info", "Recipe", "", `Đã lưu recipe #${r.id} ${r.name}${activate ? " và dùng ngay (M6 → " + r.m6 + " mm)" : ""}`);
    loadForm(r.id);
  };
  activate ? confirmBox("Lưu và dùng recipe này?", "Trục M6 sẽ chạy tới chiều cao mới. Chỉ thực hiện khi máy không sản xuất.", doIt) : doIt();
}
$("#rcp-save").addEventListener("click", () => saveForm(false));
$("#rcp-activate").addEventListener("click", () => saveForm(true));
$("#rcp-reload").addEventListener("click", () => loadForm(S.selRecipe));
$("#rcp-default").addEventListener("click", () => confirmBox("Nạp giá trị mặc định?", "Form sẽ được điền giá trị nhà máy. Bấm Lưu để ghi vào bộ điều khiển.", () => {
  $('[data-k="press"]').value = 40; $('[data-k="vac"]').value = 50; $('[data-k="blow"]').value = 30; $('[data-k="overrun"]').value = 50;
  formDirty = true; $("#rcp-dirty").classList.add("show");
}));

/* ------------------------------------------------------------------------ */
/* Trang Báo cáo  (PyQt6: pyqtgraph BarGraphItem hoặc QtCharts)              */
/* ------------------------------------------------------------------------ */
function buildReport() {
  const today = new Date().toISOString().slice(0, 10);
  $("#rp-from").value = today; $("#rp-to").value = today;
  const hours = Array.from({ length: 16 }, (_, i) => 6 + i);
  const data = hours.map(h => {
    const stop = h === 10 || h === 15;
    const ok = stop ? Math.round(rnd(3500, 5200)) : Math.round(rnd(6600, 7100));
    return { h, ok, ng: Math.round(ok * rnd(0.008, 0.03)) };
  });
  const tot = data.reduce((a, d) => a + d.ok + d.ng, 0), ok = data.reduce((a, d) => a + d.ok, 0);
  $("#rp-total").textContent = tot.toLocaleString("vi-VN");
  $("#rp-ok").textContent = ok.toLocaleString("vi-VN");
  $("#rp-ng").textContent = (tot - ok).toLocaleString("vi-VN");
  $("#rp-yield").textContent = (ok / tot * 100).toFixed(2) + " %";
  $("#rp-run").textContent = "15:12";
  $("#rp-stops").textContent = "4";

  const W = 600, H = 300, L = 40, B = 24, T = 10, max = 7500;
  const bw = (W - L - 10) / data.length;
  let s = "";
  for (let v = 0; v <= max; v += 2500) {
    const y = H - B - (H - B - T) * v / max;
    s += `<line x1="${L}" x2="${W}" y1="${y}" y2="${y}" stroke="#2a3542"/><text x="${L - 6}" y="${y + 4}" fill="#8b98a6" font-size="10" text-anchor="end">${v / 1000}k</text>`;
  }
  data.forEach((d, i) => {
    const x = L + i * bw + bw * .18, w = bw * .64;
    const hOk = (H - B - T) * d.ok / max, hNg = Math.max(2, (H - B - T) * d.ng / max);
    s += `<rect x="${x}" y="${H - B - hOk}" width="${w}" height="${hOk}" fill="#22c55e" rx="2"/>`;
    s += `<rect x="${x}" y="${H - B - hOk - hNg - 1}" width="${w}" height="${hNg}" fill="#ef4444" rx="1"/>`;
    s += `<text x="${x + w / 2}" y="${H - 8}" fill="#8b98a6" font-size="10" text-anchor="middle">${d.h}h</text>`;
  });
  $("#rp-chart").innerHTML = s;

  const reasons = [["Vision: không thấy khoen", 412], ["Vision: timeout", 156], ["Hụt chân không", 98], ["Mất tem (S5)", 61], ["Lon sai loại", 23]];
  const rmax = reasons[0][1];
  $("#rp-ng-bars").innerHTML = reasons.map(([t, n]) =>
    `<div class="ng-bar"><div class="row"><span>${t}</span><b class="mono">${n}</b></div><div class="bar"><i style="width:${n / rmax * 100}%"></i></div></div>`).join("");
  $("#rp-stop-table").innerHTML = [["ALM_07", "Hết cuộn tem", 2], ["ALM_05", "Vision timeout", 1], ["—", "Dừng tay", 1]]
    .map(([c, t, n]) => `<tr><td class="mono">${c}</td><td>${t}</td><td class="num">${n}</td></tr>`).join("");
}

/* ------------------------------------------------------------------------ */
/* Trang Nhật ký  (PyQt6: QTableView + QSortFilterProxyModel)                */
/* ------------------------------------------------------------------------ */
const LV = { err: ["err", "Lỗi"], warn: ["warn", "Cảnh báo"], info: ["info", "Thông tin"] };
const CAT = { alarm: ["Alarm", "P8"], vision: ["Vision"], comm: ["Modbus", "RS485"], event: ["FSM", "HMI", "Recipe"] };

function renderLog() {
  const q = $("#log-search").value.trim().toLowerCase();
  const rows = S.log.filter(l =>
    (S.logFilter === "all" || CAT[S.logFilter].includes(l.src)) &&
    (!q || (l.msg + l.code + l.src).toLowerCase().includes(q)));
  $("#log-body").innerHTML = rows.slice(0, 200).map(l => {
    const d = l.t;
    return `<tr><td class="mono">${pad(d.getDate())}/${pad(d.getMonth() + 1)} ${hhmmss(d)}</td>` +
      `<td><span class="tag ${LV[l.level][0]}">${LV[l.level][1]}</span></td><td>${l.src}</td>` +
      `<td class="mono">${l.code || ""}</td><td class="msg">${l.msg}</td></tr>`;
  }).join("");
  $("#log-count").textContent = rows.length;
  $("#log-n-err").textContent = S.log.filter(l => l.level === "err").length;
  $("#log-n-warn").textContent = S.log.filter(l => l.level === "warn").length;
  $("#log-n-info").textContent = S.log.filter(l => l.level === "info").length;
}
$("#log-filter").addEventListener("change", e => { S.logFilter = e.detail; renderLog(); });
$("#log-search").addEventListener("input", renderLog);
$("#log-pause").addEventListener("click", e => {
  S.logPaused = !S.logPaused;
  e.target.textContent = S.logPaused ? "Tiếp tục cuộn" : "Tạm dừng cuộn";
  e.target.classList.toggle("warn", S.logPaused);
  if (!S.logPaused) renderLog();
});
$("#log-ack").addEventListener("click", () => { S.alarms.clear(); renderAlarms(); addLog("info", "HMI", "", "Đã xác nhận tất cả alarm"); });

/* ------------------------------------------------------------------------ */
/* Khởi động + vòng lặp  (PyQt6: QTimer 100 ms cho UI, QThread cho Modbus)   */
/* ------------------------------------------------------------------------ */
buildConveyor();
buildMonitor();
buildReport();
loadForm(1);
renderHeader(); renderAlarms(); renderClock();
addLog("info", "Modbus", "", "Kết nối 192.168.1.200:502 thành công");
addLog("info", "RS485", "", "Mở COM3 115200 8N1 (Vision)");
addLog("info", "FSM", "", "Homing hoàn tất – máy READY");

let last = performance.now();
setInterval(() => {
  const t = performance.now(), dt = (t - last) / 1000; last = t;
  simStep(dt);
  simSafety();
  renderSafety();
  updateButtons();
  renderConveyor();
  renderDashboard();
  renderHeader();
  const page = $(".page.active").id;
  if (page === "page-camera" || page === "page-dashboard") renderCamera();
  if (page === "page-monitor") renderMonitor();
}, 100);
setInterval(renderClock, 1000);

// Bản mẫu tự chạy sau 1 giây để thấy dữ liệu chuyển động (xóa khi dùng thật)
setTimeout(() => setState(S.bench ? "HOME_REQ" : "RUN"), 1000);
