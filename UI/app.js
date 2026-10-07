// BOUNCE editor page. C++ owns every value: controls read and write JUCE relays, and the graph
// draws the view model C++ computes with the same schedule function the audio thread uses.
import * as Juce from "./juce/index.js";

const $ = id => document.getElementById(id);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const backend = window.__JUCE__.backend;
const native = name => Juce.getNativeFunction(name);
const SVGNS = "http://www.w3.org/2000/svg";

// ============================================================================ scale
const STAGE_W = 540, STAGE_H = 312;
function fit() {
  const s = Math.min(innerWidth / STAGE_W, innerHeight / STAGE_H);
  $("stage").style.transform = `scale(${s})`;
}
addEventListener("resize", fit);
fit();

// ============================================================================ parameters
const slider = name => Juce.getSliderState(name);
const toggle = name => Juce.getToggleState(name);
const combo = name => Juce.getComboBoxState(name);

const DEFAULTS = {
  amount: 35, outputDb: 0, freeMs: 125, motion: 0, decayDb: -18, pitchPathSt: 0,
  sourceMs: 100, tightMs: 0, thresholdDb: -24, retriggerMs: 90, repeats: 4,
};
for (let i = 1; i <= 8; ++i) {
  DEFAULTS[`tap${i}Time`] = i / 8;
  DEFAULTS[`tap${i}LevelDb`] = 0;
  DEFAULTS[`tap${i}PitchSt`] = 0;
}

const normOf = (s, v) => {
  const p = s.properties;
  return Math.pow(clamp((v - p.start) / (p.end - p.start), 0, 1), p.skew);
};
const val = name => slider(name).getScaledValue();
const on = name => !!toggle(name).getValue();

// A single, complete host gesture.
function setValue(name, v) {
  const s = slider(name);
  s.sliderDragStarted();
  s.setNormalisedValue(normOf(s, v));
  s.sliderDragEnded();
}

// ============================================================================ formatting
const minus = "−";
const sgn = (v, d = 0) => (v > 0.0001 ? "+" : v < -0.0001 ? minus : "") + Math.abs(v).toFixed(d);
const fmtMs = v => (v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + " s" : Math.round(v) + " ms");
const fmtDb = (v, d = 0) => sgn(v, d) + " dB";
const fmtSt = v => sgn(v, Math.abs(v - Math.round(v)) > 0.05 ? 1 : 0) + " st";
const SHORT_DIV = ["1/4", "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T", "1/32"];

let snap = false;
try { snap = localStorage.getItem("bounce.snap") === "1"; } catch (e) {}

// ============================================================================ render batching
let renderQueued = false;
function refresh() {
  if (renderQueued) return;
  renderQueued = true;
  requestAnimationFrame(() => { renderQueued = false; renderControls(); });
}

// ============================================================================ sliders
const sliders = [];
function makeSlider(el, name, opts = {}) {
  const s = slider(name);
  const w = { el, name, s, opts };
  const fill = el.querySelector(".fill"), thumb = el.querySelector(".thumb");

  w.update = () => {
    const n = clamp(s.getNormalisedValue(), 0, 1);
    thumb.style.left = n * 100 + "%";
    if (opts.bipolar !== undefined) {
      const c = normOf(s, opts.bipolar);
      fill.style.left = Math.min(c, n) * 100 + "%";
      fill.style.width = Math.abs(n - c) * 100 + "%";
    } else {
      fill.style.left = "0";
      fill.style.width = n * 100 + "%";
    }
    if (opts.valueEl) $(opts.valueEl).textContent = opts.fmt(s.getScaledValue());
    el.setAttribute("aria-valuetext", opts.fmt ? opts.fmt(s.getScaledValue()) : "");
  };

  el.addEventListener("pointerdown", e => {
    e.preventDefault();
    el.focus();
    try { el.setPointerCapture(e.pointerId); } catch (err) {}
    const rect = el.getBoundingClientRect();
    // Absolute: the thumb goes where you point. Shift: fine, relative to where it is.
    let n = s.getNormalisedValue(), lastX = e.clientX;
    s.sliderDragStarted();
    if (!e.shiftKey) { n = clamp((e.clientX - rect.left) / rect.width, 0, 1); s.setNormalisedValue(n); }
    const move = ev => {
      n = ev.shiftKey ? clamp(n + (ev.clientX - lastX) / rect.width * 0.15, 0, 1)
                      : clamp((ev.clientX - rect.left) / rect.width, 0, 1);
      lastX = ev.clientX;
      s.setNormalisedValue(n);
    };
    const up = () => {
      s.sliderDragEnded();
      el.removeEventListener("pointermove", move);
      el.removeEventListener("pointerup", up);
      el.removeEventListener("pointercancel", up);
    };
    el.addEventListener("pointermove", move);
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
  });

  el.addEventListener("dblclick", () => { if (name in DEFAULTS) setValue(name, DEFAULTS[name]); });

  el.addEventListener("wheel", e => {
    e.preventDefault();
    s.sliderDragStarted();
    s.setNormalisedValue(clamp(s.getNormalisedValue() - Math.sign(e.deltaY) * (e.shiftKey ? 0.002 : 0.01), 0, 1));
    s.sliderDragEnded();
  }, { passive: false });

  el.addEventListener("keydown", e => {
    const step = e.shiftKey ? 0.002 : 0.01;
    let d = 0;
    if (e.key === "ArrowRight" || e.key === "ArrowUp") d = step;
    if (e.key === "ArrowLeft" || e.key === "ArrowDown") d = -step;
    if (!d) return;
    e.preventDefault();
    s.sliderDragStarted();
    s.setNormalisedValue(clamp(s.getNormalisedValue() + d, 0, 1));
    s.sliderDragEnded();
  });

  s.valueChangedEvent.addListener(refresh);
  s.propertiesChangedEvent.addListener(refresh);
  sliders.push(w);
  return w;
}

makeSlider($("s_motion"), "motion", { bipolar: 0, valueEl: "v_motion", fmt: v => sgn(v) });
makeSlider($("s_amount"), "amount", { valueEl: "v_amount", fmt: v => Math.round(v) + "%" });
makeSlider($("s_decay"), "decayDb", { valueEl: "v_decay", fmt: v => fmtDb(v, Math.abs(v - Math.round(v)) > 0.05 ? 1 : 0) });
makeSlider($("s_pitch"), "pitchPathSt", { bipolar: 0, valueEl: "v_pitch", fmt: fmtSt });
makeSlider($("s_source"), "sourceMs", { valueEl: "v_source", fmt: v => Math.round(v) + " ms" });
makeSlider($("s_tight"), "tightMs", { valueEl: "v_tight", fmt: v => v < 0.5 ? "OFF" : Math.round(v) + " ms" });
makeSlider($("s_threshold"), "thresholdDb", { valueEl: "v_threshold", fmt: v => sgn(v) + " dBFS" });
makeSlider($("s_retrigger"), "retriggerMs", { valueEl: "v_retrigger", fmt: v => Math.round(v) + " ms" });
makeSlider($("s_free"), "freeMs", { valueEl: "v_free", fmt: v => Math.round(v) + " ms" });
makeSlider($("s_output"), "outputDb", { bipolar: 0, valueEl: "v_output", fmt: v => fmtDb(v, 1) });

// Every parameter the page draws from, so any host change redraws.
const watched = ["repeats", "freeMs", "decayDb"];
for (let i = 1; i <= 8; ++i) watched.push(`tap${i}Time`, `tap${i}LevelDb`, `tap${i}PitchSt`);
for (const n of watched) { slider(n).valueChangedEvent.addListener(refresh); slider(n).propertiesChangedEvent.addListener(refresh); }
for (const n of ["enabled", "wetOnly", "sync", "choke", "quantize",
                 ...[1, 2, 3, 4, 5, 6, 7, 8].map(i => `tap${i}On`), ...[1, 2, 3, 4, 5, 6, 7, 8].map(i => `tap${i}Reverse`)])
  toggle(n).valueChangedEvent.addListener(refresh);
combo("division").valueChangedEvent.addListener(refresh);
combo("division").propertiesChangedEvent.addListener(refresh);

// ============================================================================ top strip
const repeats = () => Math.round(val("repeats")) || 4;
$("repDown").addEventListener("click", () => setValue("repeats", clamp(repeats() - 1, 2, 8)));
$("repUp").addEventListener("click", () => setValue("repeats", clamp(repeats() + 1, 2, 8)));
$("powerBtn").addEventListener("click", () => toggle("enabled").setValue(!on("enabled")));

let presets = [], categories = [], openCategory = null;
backend.addEventListener("presets", data => {
  presets = (data && data.presets) || [];
  categories = (data && data.categories) || [];
  buildPresetMenu();
  refresh();
});

function closeMenus(except) {
  for (const id of ["presetMenu", "rateMenu", "settings", "tapPop"])
    if (id !== except) $(id).classList.remove("open");
  if (except !== "tapPop") selectBall(-1);
}

function openAt(menu, anchor) {
  const st = $("stage").getBoundingClientRect(), a = anchor.getBoundingClientRect();
  const s = st.width / STAGE_W;
  menu.style.left = Math.min((a.left - st.left) / s, STAGE_W - menu.offsetWidth - 8) + "px";
  menu.style.top = ((a.bottom - st.top) / s + 4) + "px";
}

// Categories on the left (hover or click to show), that category's presets on the right.
function buildPresetMenu() {
  const m = $("presetMenu");
  const current = vm ? vm.preset : -1;
  const curCat = presets[current] ? presets[current].cat : categories[0];
  if (!openCategory || !m.classList.contains("open")) openCategory = curCat;

  m.innerHTML = "";
  const cats = document.createElement("div"), list = document.createElement("div");
  cats.className = "cats"; list.className = "list";

  for (const c of categories) {
    const b = document.createElement("button");
    const count = presets.filter(p => p.cat === c).length;
    b.innerHTML = `<span>${c}</span><span class="count">${count}</span>`;
    b.classList.toggle("active", c === openCategory);
    b.classList.toggle("hasCur", c === curCat);
    const show = () => { if (openCategory !== c) { openCategory = c; buildPresetMenu(); } };
    b.addEventListener("mouseenter", show);
    b.addEventListener("click", show);
    cats.appendChild(b);
  }

  presets.forEach((p, i) => {
    if (p.cat !== openCategory) return;
    const b = document.createElement("button");
    b.textContent = p.name.toUpperCase();
    if (i === current) b.classList.add("cur");
    b.addEventListener("click", () => { native("loadPreset")(i); m.classList.remove("open"); });
    list.appendChild(b);
  });

  m.appendChild(cats);
  m.appendChild(list);
}

// Long names shrink a little rather than being cut: 10.5 px down to 8 px at most.
function fitPresetName() {
  const el = $("presetName");
  let size = 10.5;
  el.style.fontSize = size + "px";
  while (el.scrollWidth > el.clientWidth + 0.5 && size > 8) {
    size -= 0.5;
    el.style.fontSize = size + "px";
  }
}

// Previous / next walks the whole list in menu order: category by category.
function menuOrder() {
  const order = [];
  for (const c of categories) presets.forEach((p, i) => { if (p.cat === c) order.push(i); });
  return order;
}
function stepPreset(d) {
  const order = menuOrder();
  if (!order.length) return;
  const at = order.indexOf(vm ? vm.preset : 0);
  native("loadPreset")(order[(at + d + order.length) % order.length]);
}
$("presetPrev").addEventListener("click", () => stepPreset(-1));
$("presetNext").addEventListener("click", () => stepPreset(1));
$("presetBtn").addEventListener("click", e => {
  e.stopPropagation();
  const m = $("presetMenu"), wasOpen = m.classList.contains("open");
  closeMenus();
  if (!wasOpen) { buildPresetMenu(); m.classList.add("open"); openAt(m, $("presetBtn")); }
});

function buildRateMenu() {
  const m = $("rateMenu"), c = combo("division");
  m.innerHTML = "";
  const choices = c.properties.choices || [];
  choices.forEach((name, i) => {
    const b = document.createElement("button");
    b.textContent = name.toUpperCase();
    if (on("sync") && c.getChoiceIndex() === i) b.classList.add("cur");
    b.addEventListener("click", () => {
      c.setChoiceIndex(i);
      if (!on("sync")) toggle("sync").setValue(true);
      m.classList.remove("open");
    });
    m.appendChild(b);
  });
  m.appendChild(document.createElement("hr"));
  const free = document.createElement("button");
  free.textContent = "FREE  " + Math.round(val("freeMs")) + " MS";
  if (!on("sync")) free.classList.add("cur");
  free.addEventListener("click", () => { toggle("sync").setValue(false); m.classList.remove("open"); });
  m.appendChild(free);
}
$("rateBtn").addEventListener("click", e => {
  e.stopPropagation();
  const m = $("rateMenu"), wasOpen = m.classList.contains("open");
  closeMenus();
  if (!wasOpen) { buildRateMenu(); m.classList.add("open"); openAt(m, $("rateBtn")); }
});

$("moreBtn").addEventListener("click", e => {
  e.stopPropagation();
  const p = $("settings"), wasOpen = p.classList.contains("open");
  closeMenus();
  if (!wasOpen) p.classList.add("open");
});

$("t_choke").addEventListener("click", () => toggle("choke").setValue(!on("choke")));
$("t_quant").addEventListener("click", () => toggle("quantize").setValue(!on("quantize")));
$("t_sync").addEventListener("click", () => toggle("sync").setValue(!on("sync")));
$("t_wet").addEventListener("click", () => toggle("wetOnly").setValue(!on("wetOnly")));
$("t_snap").addEventListener("click", () => {
  snap = !snap;
  try { localStorage.setItem("bounce.snap", snap ? "1" : "0"); } catch (e) {}
  refresh();
});
$("resetPattern").addEventListener("click", () => native("resetPattern")());

// Clicking elsewhere closes popovers.
addEventListener("pointerdown", e => {
  if (e.target.closest(".menu, .pop, .ball, #presetBtn, #rateBtn, #moreBtn, .step")) return;
  closeMenus();
});
addEventListener("keydown", e => { if (e.key === "Escape") closeMenus(); });

// ============================================================================ controls
function renderControls() {
  for (const w of sliders) w.update();

  $("repVal").textContent = String(repeats()).padStart(2, "0");
  $("repDown").disabled = repeats() <= 2;
  $("repUp").disabled = repeats() >= 8;

  const sync = on("sync");
  const div = combo("division").getChoiceIndex();
  $("rateVal").textContent = sync ? (SHORT_DIV[div] || "1/16") : Math.round(val("freeMs")) + "ms";
  $("s_free").closest(".srow").classList.toggle("hidden", sync);

  const enabled = on("enabled");
  $("powerBtn").classList.toggle("off", !enabled);
  $("graph").classList.toggle("disabled", !enabled);

  $("t_sync").classList.toggle("on", sync);
  $("t_choke").classList.toggle("on", on("choke"));
  $("t_quant").classList.toggle("on", on("quantize"));
  const tight = val("tightMs");
  // QUANTIZE needs the host's running grid; say so instead of silently doing nothing.
  const quant = on("quantize") ? (sync && vm && vm.grid ? "QUANTIZE" : sync ? "QUANTIZE (PLAY HOST)" : "QUANTIZE (SYNC OFF)") : "";
  const modes = [on("choke") ? "CHOKE" : "", tight >= 0.5 ? "TIGHT " + Math.round(tight) + " MS" : "", quant].filter(Boolean);
  $("modeWord").textContent = modes.length ? "·  " + modes.join("  ·  ") : "";
  $("t_wet").classList.toggle("on", on("wetOnly"));
  $("t_snap").classList.toggle("on", snap);

  if (vm) {
    const p = presets[vm.preset];
    $("presetName").textContent = p ? p.name.toUpperCase() : "";
    $("presetCat").textContent = "";
    $("presetBtn").title = p ? p.cat + " / " + p.name : "Factory presets";
    fitPresetName();
    $("presetMod").classList.toggle("on", !!vm.mod);
  }

  const m = val("motion");
  $("motionWord").textContent = m > 4 ? "ACCELERATING" : m < -4 ? "SLOWING DOWN" : "EVEN";

  renderTapPop();
  kickPlot();
}

// ============================================================================ view model
let vm = null;
let lastTrig = -1, clipUntil = 0;

backend.addEventListener("vm", data => {
  const first = !vm;
  vm = data;
  if (lastTrig >= 0 && vm.trig > lastTrig) onTrigger();
  lastTrig = vm.trig;
  if (vm.clip) clipUntil = performance.now() + 1500;
  renderHeader();
  if (first) snapDisplay();
  refresh();
});

function renderHeader() {
  const tempo = $("tempo");
  const sync = vm.sync;
  tempo.classList.toggle("fallback", sync && !vm.hostBpm);
  tempo.classList.toggle("free", !sync);
  $("tempoText").textContent = !sync ? "FREE / " + Math.round(val("freeMs")) + " MS"
    : vm.hostBpm ? `${+vm.bpm.toFixed(vm.bpm % 1 ? 1 : 0)} BPM / SYNC` : "120 FALLBACK";

  const badges = [];
  if (vm.shift > 0.05) badges.push(["SOURCE SAFETY +" + Math.round(vm.shift) + " MS", ""]);
  if (vm.cap < 0.9999) badges.push(["16 S LIMIT", ""]);
  if (vm.gap) badges.push(["12 MS GAP", ""]);
  if (performance.now() < clipUntil) badges.push(["CLIP", "clip"]);
  $("badges").innerHTML = badges.map(([t, c]) => `<span class="badge ${c}">${t}</span>`).join("");

  $("footRight").innerHTML = (vm.shift > 0.05 ? `FIRST <b>${fmtMs(vm.first)}</b>&nbsp;&nbsp;·&nbsp;&nbsp;` : "")
    + `LAST HIT <b>${fmtMs(vm.last)}</b>`;
  $("diag").textContent = `TRIG ${vm.trig} · EV ${vm.ev} · DROP ${vm.drop} · CUT ${vm.cut}`;
}

// ============================================================================ plot
const plot = $("plot");
const X0 = 52, X1 = 500, YT = 24, YB = 100;          // time axis and gain lane
const DB_TOP = 6, DB_BOTTOM = -48;
const el = (tag, attrs = {}, parent) => {
  const e = document.createElementNS(SVGNS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(e);
  return e;
};

plot.innerHTML = `
  <defs>
    <linearGradient id="srcGrad" x1="0" x2="1" y1="0" y2="0">
      <stop offset="0" stop-color="#80d3c1" stop-opacity="0.075"/>
      <stop offset="1" stop-color="#80d3c1" stop-opacity="0.012"/>
    </linearGradient>
    <linearGradient id="pathGrad" gradientUnits="userSpaceOnUse" x1="${X0}" x2="${X1}" y1="0" y2="0">
      <stop offset="0" stop-color="#80d3c1"/>
      <stop offset="0.62" stop-color="#80d3c1"/>
      <stop offset="1" stop-color="#d8b28d" stop-opacity="0.9"/>
    </linearGradient>
    <linearGradient id="swellGrad" x1="0" x2="1" y1="0" y2="0">
      <stop offset="0" stop-color="#d8b28d" stop-opacity="0"/>
      <stop offset="1" stop-color="#d8b28d" stop-opacity="0.26"/>
    </linearGradient>
    <filter id="bloom" x="-1" y="-1" width="3" height="3"><feGaussianBlur stdDeviation="4"/></filter>
    <filter id="soft" x="-0.1" y="-1" width="1.2" height="3"><feGaussianBlur stdDeviation="2"/></filter>
  </defs>`;
const gGrid = el("g", {}, plot);
const srcRect = el("rect", { class: "src", y: YT - 6, height: YB - YT + 6 }, plot);
const srcEdge = el("line", { class: "srcEdge", y1: YT - 6, y2: YB }, plot);
const limitLine = el("line", { class: "limit", y1: YT - 8, y2: YB, visibility: "hidden" }, plot);
el("line", { class: "base", x1: X0, x2: X1 + 4, y1: YB, y2: YB }, plot);
el("line", { class: "grid beat", x1: X0, x2: X0, y1: YT - 8, y2: YB }, plot);
const gWave = el("g", { class: "wave" }, plot);
const gDormant = el("g", {}, plot);
const gStems = el("g", {}, plot);
const pathGlow = el("path", { class: "pathGlow" }, plot);
const pathLine = el("path", { class: "path" }, plot);
const gBalls = el("g", {}, plot);

// The input: thin bars of a hit, left of the time origin. They light on each detected onset.
const BARS = [0.18, 0.3, 0.46, 0.7, 0.92, 1, 0.84, 0.62, 0.74, 0.5, 0.34, 0.22, 0.14];
BARS.forEach((h, i) => el("rect", { x: 6 + i * 2.6, y: 60 - h * 17, width: 1.1, height: h * 34, rx: 0.5 }, gWave));
gWave.style.opacity = 0.42;

const balls = [];
for (let i = 0; i < 8; ++i) {
  const g = el("g", { class: "ball", tabindex: "0", role: "button", "aria-label": `Repeat ${i + 1}` }, gBalls);
  el("circle", { class: "halo", r: 13 }, g);
  el("circle", { class: "ring", r: 9 }, g);
  el("circle", { class: "core", r: 3.1 }, g);
  const num = el("text", { class: "num", y: -15 }, g);
  num.textContent = String(i + 1).padStart(2, "0");
  const tag = el("text", { class: "pitchTag", y: 23 }, g);
  const stem = el("line", { class: "stem" }, gStems);
  const swell = el("path", { class: "swell" }, gStems);
  const dorm = el("circle", { class: "dormant", r: 2.4 }, gDormant);
  const dormNum = el("text", { class: "dormantNum" }, gDormant);
  dormNum.textContent = String(i + 1).padStart(2, "0");
  balls.push({ g, stem, swell, dorm, dormNum, tag, x: X0, y: YB, tx: X0, ty: YB, flashUntil: 0 });
  attachBall(i);
}

let xMax = 600, xMaxTarget = 600;
const xOf = t => X0 + (t / xMax) * (X1 - X0);
const tOf = x => (x - X0) / (X1 - X0) * xMax;
const yOf = db => YT + (DB_TOP - clamp(db, DB_BOTTOM, DB_TOP)) / (DB_TOP - DB_BOTTOM) * (YB - 10 - YT);
const dbOf = y => DB_TOP - (y - YT) / (YB - 10 - YT) * (DB_TOP - DB_BOTTOM);

let dragging = -1, dragView = null;     // the ball under the pointer follows it at once
let selected = -1;

function targets() {
  if (!vm) return;
  const n = vm.n;
  let latest = Math.max(vm.T + vm.shift, vm.src + 10);
  for (let i = 0; i < n; ++i) latest = Math.max(latest, vm.taps[i].t);
  if (dragging < 0) xMaxTarget = niceMax(latest * 1.07);
  vm.taps.forEach((tap, i) => {
    const b = balls[i];
    if (i === dragging && dragView) { b.tx = xOf(dragView.t); b.ty = yOf(dragView.db); return; }
    b.tx = xOf(tap.t);
    b.ty = yOf(tap.db);
  });
}

function niceMax(v) {
  const steps = [100, 125, 150, 200, 250, 300, 400, 500, 600, 750, 800, 1000, 1250, 1500, 2000, 2500, 3000, 4000, 5000, 6000, 8000, 10000, 12000, 16000, 20000];
  return steps.find(s => s >= v) || v;
}

function snapDisplay() {
  targets();
  xMax = xMaxTarget;
  targets();
  for (const b of balls) { b.x = b.tx; b.y = b.ty; }
}

let animating = false;
function kickPlot() { if (!animating) { animating = true; requestAnimationFrame(frame); } }

function frame(now) {
  animating = false;
  if (!vm) return;
  let moving = false;

  const k = 0.3;
  if (Math.abs(xMaxTarget - xMax) > 0.5) { xMax += (xMaxTarget - xMax) * k; moving = true; } else xMax = xMaxTarget;
  targets();
  for (const b of balls) {
    const dx = b.tx - b.x, dy = b.ty - b.y;
    if (Math.abs(dx) > 0.05 || Math.abs(dy) > 0.05) { b.x += dx * (dragging >= 0 ? 0.7 : k); b.y += dy * (dragging >= 0 ? 0.7 : k); moving = true; }
    else { b.x = b.tx; b.y = b.ty; }
  }

  drawPlot(now);
  if (moving || balls.some(b => b.flashUntil > now) || now < inputFlashUntil) kickPlot();
}

function drawPlot(now) {
  const n = vm.n;

  // Grid: one line per nominal interval (stronger on beats when synced).
  const B = vm.B > 0 ? vm.B : 125;
  let lines = "";
  const beatMs = 60000 / (vm.bpm || 120);
  for (let t = B, c = 0; t < xMax && c < 64; t += B, ++c) {
    const x = xOf(t).toFixed(1);
    const beat = vm.sync && Math.abs((t / beatMs) - Math.round(t / beatMs)) < 1e-3;
    lines += `<line class="grid${beat ? " beat" : ""}" x1="${x}" x2="${x}" y1="${YT - 8}" y2="${YB}"/>`;
  }
  gGrid.innerHTML = lines;

  // Capture window.
  const sx = xOf(vm.src);
  srcRect.setAttribute("x", X0);
  srcRect.setAttribute("width", Math.max(0, sx - X0));
  srcEdge.setAttribute("x1", sx); srcEdge.setAttribute("x2", sx);

  // 16 s limit marker when it bites.
  limitLine.setAttribute("visibility", vm.cap < 0.9999 && 16000 < xMax ? "visible" : "hidden");
  limitLine.setAttribute("x1", xOf(16000)); limitLine.setAttribute("x2", xOf(16000));

  // Balls, stems and dormant slots.
  const pts = [];
  for (let i = 0; i < 8; ++i) {
    const b = balls[i], tap = vm.taps[i];
    const active = i < n;
    b.g.style.display = active ? "" : "none";
    b.stem.style.display = active ? "" : "none";
    b.swell.style.display = active && tap.rev ? "" : "none";
    const dormantVisible = !active && tap.t < xMax;
    b.dorm.style.display = dormantVisible ? "" : "none";
    b.dormNum.style.display = dormantVisible ? "" : "none";

    if (!active) {
      const dx = xOf(tap.t);
      b.dorm.setAttribute("cx", dx); b.dorm.setAttribute("cy", YB);
      b.dormNum.setAttribute("x", dx); b.dormNum.setAttribute("y", YB - 6);
      continue;
    }

    const muted = !tap.on;
    b.g.setAttribute("transform", `translate(${b.x.toFixed(2)},${b.y.toFixed(2)})`);
    b.g.classList.toggle("muted", muted);
    b.g.classList.toggle("sel", i === selected);
    b.g.classList.toggle("flash", b.flashUntil > now);
    b.stem.setAttribute("x1", b.x); b.stem.setAttribute("x2", b.x);
    b.stem.setAttribute("y1", b.y + 9); b.stem.setAttribute("y2", YB);
    b.stem.classList.toggle("muted", muted);
    b.g.classList.toggle("rev", !!tap.rev);

    // REVERSE: the tail is heard before the hit - draw it as a swell rising into the ball,
    // exactly as long as the audio's swell.
    if (tap.rev) {
      const sx = Math.max(X0, b.x - (tap.sw / xMax) * (X1 - X0));
      const w = b.x - sx;
      b.swell.setAttribute("d", `M${sx.toFixed(1)},${YB} C${(sx + w * 0.72).toFixed(1)},${YB} ${(b.x - w * 0.12).toFixed(1)},${b.y.toFixed(1)} ${b.x.toFixed(1)},${b.y.toFixed(1)} L${b.x.toFixed(1)},${YB} Z`);
      b.swell.classList.toggle("muted", muted);
    }

    const showPitch = i === selected || i === hovered;
    b.tag.textContent = showPitch ? fmtSt(dragging === i && dragView ? tap.st : tap.st) : "";
    pts.push([b.x, b.y]);
  }

  // The mint-to-champagne path fades toward the quietest, last repeat.
  if (pts.length > 1) {
    const grad = $("pathGrad");
    grad.setAttribute("x1", pts[0][0]);
    grad.setAttribute("x2", pts[pts.length - 1][0]);
  }
  const d = smoothPath(pts);
  pathLine.setAttribute("d", d);
  pathGlow.setAttribute("d", d);

  gWave.style.opacity = now < inputFlashUntil ? 0.95 : 0.42;
}

// Catmull-Rom through the balls, as cubic Beziers.
function smoothPath(p) {
  if (p.length < 2) return "";
  let d = `M${p[0][0].toFixed(1)},${p[0][1].toFixed(1)}`;
  for (let i = 0; i < p.length - 1; ++i) {
    const p0 = p[Math.max(0, i - 1)], p1 = p[i], p2 = p[i + 1], p3 = p[Math.min(p.length - 1, i + 2)];
    const c1 = [p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6];
    const c2 = [p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6];
    d += ` C${c1[0].toFixed(1)},${c1[1].toFixed(1)} ${c2[0].toFixed(1)},${c2[1].toFixed(1)} ${p2[0].toFixed(1)},${p2[1].toFixed(1)}`;
  }
  return d;
}

// ============================================================================ trigger flash
// Lights the input and then each ball at the time its repeat actually plays (taken from the
// real schedule; the view model arrives within ~33 ms of the hit).
let inputFlashUntil = 0;
function onTrigger() {
  const now = performance.now();
  inputFlashUntil = now + 110;
  vm.taps.forEach((tap, i) => {
    if (i >= vm.n || !tap.on) return;
    setTimeout(() => { balls[i].flashUntil = performance.now() + 90; kickPlot(); }, Math.max(0, tap.t - 30));
  });
  kickPlot();
}

// ============================================================================ ball editing
let hovered = -1;
const tapName = (i, what) => `tap${i + 1}${what}`;

function storedTimes() {
  const t = [];
  for (let i = 0; i < 8; ++i) t.push(val(tapName(i, "Time")));
  return t;
}

// Inverse of the timing warp: stored master-grid position for an on-screen time.
function masterFor(i, tActual) {
  const n = vm.n, stored = storedTimes();
  let tNom = Math.max(0.5, tActual - vm.shift);
  if (snap && vm.B > 0) tNom = Math.max(vm.B / 4, Math.round(tNom / (vm.B / 4)) * (vm.B / 4));
  let v = (n / 8) * Math.pow(tNom / vm.T, 1 / vm.p);
  const lo = i === 0 ? 0.005 : stored[i - 1] + 0.002;
  const hi = i === 7 ? 1.0 : stored[i + 1] - 0.002;
  return clamp(v, lo, Math.max(lo, hi));
}

const decayShare = i => val("decayDb") * (vm.n > 1 ? i / (vm.n - 1) : 0);
const trimFor = (i, effDb) => clamp(effDb - decayShare(i), -48, 6);

function selectBall(i) {
  selected = i;
  if (i < 0) $("tapPop").classList.remove("open");
  kickPlot();
}

function openTapPop(i) {
  closeMenus("tapPop");
  selectBall(i);
  $("tapPop").classList.add("open");
  placeTapPop();
  renderTapPop();
}

function placeTapPop() {
  if (selected < 0) return;
  const b = balls[selected], pop = $("tapPop");
  const bx = 14 + b.tx, by = 50 + 26 + b.ty;
  let left = bx + 16, top = by - 30;
  if (left + pop.offsetWidth > STAGE_W - 8) left = bx - 16 - pop.offsetWidth;
  top = clamp(top, 54, STAGE_H - pop.offsetHeight - 8);
  pop.style.left = left + "px";
  pop.style.top = top + "px";
}

function renderTapPop() {
  if (selected < 0 || !vm || !$("tapPop").classList.contains("open")) return;
  if (selected >= vm.n) { selectBall(-1); return; }
  const tap = vm.taps[selected];
  $("tapTitle").textContent = "TAP " + String(selected + 1).padStart(2, "0");
  $("tapRev").classList.toggle("on", !!tap.rev);
  $("tapOn").textContent = tap.on ? "ON" : "MUTED";
  $("tapOn").classList.toggle("on", tap.on);
  $("tapTime").textContent = fmtMs(dragging === selected && dragView ? dragView.t : tap.t);
  $("tapLevel").textContent = fmtDb(dragging === selected && dragView ? dragView.db : tap.db, 1);
  const trimSt = val(tapName(selected, "PitchSt"));
  $("tapPitch").textContent = fmtSt(tap.st);
  $("tapPitch").title = "trim " + fmtSt(trimSt);
  tapPitchSlider?.update();
  placeTapPop();
}

// The popover's pitch slider is re-bound to whichever tap is selected.
let tapPitchSlider = null;
function bindTapPitch(i) {
  const old = $("s_tapPitch");
  const fresh = old.cloneNode(true);
  old.replaceWith(fresh);
  sliders.splice(sliders.indexOf(tapPitchSlider), tapPitchSlider ? 1 : 0);
  tapPitchSlider = makeSlider(fresh, tapName(i, "PitchSt"), { bipolar: 0, fmt: fmtSt });
  tapPitchSlider.update();
}

$("tapRev").addEventListener("click", () => {
  if (selected >= 0) toggle(tapName(selected, "Reverse")).setValue(!on(tapName(selected, "Reverse")));
});
$("tapOn").addEventListener("click", () => {
  if (selected >= 0) toggle(tapName(selected, "On")).setValue(!on(tapName(selected, "On")));
});

// Vertical drag / arrows on a popover value.
function valueEditor(elId, apply) {
  const e = $(elId);
  e.addEventListener("pointerdown", ev => {
    ev.preventDefault();
    try { e.setPointerCapture(ev.pointerId); } catch (err) {}
    let lastY = ev.clientY;
    apply("start");
    const move = m => { apply("delta", (lastY - m.clientY) * (m.shiftKey ? 0.2 : 1)); lastY = m.clientY; };
    const up = () => { apply("end"); e.removeEventListener("pointermove", move); e.removeEventListener("pointerup", up); };
    e.addEventListener("pointermove", move);
    e.addEventListener("pointerup", up);
  });
  e.addEventListener("keydown", ev => {
    const d = ev.key === "ArrowUp" || ev.key === "ArrowRight" ? 1 : ev.key === "ArrowDown" || ev.key === "ArrowLeft" ? -1 : 0;
    if (!d) return;
    ev.preventDefault();
    apply("start"); apply("delta", d * (ev.shiftKey ? 0.2 : 1)); apply("end");
  });
  e.addEventListener("dblclick", () => apply("reset"));
}

function timeGesture(i) {
  const s = slider(tapName(i, "Time"));
  let t = 0;
  return (phase, amount) => {
    if (phase === "start") { s.sliderDragStarted(); t = vm.taps[i].t; }
    if (phase === "delta") { t = Math.max(1, t + amount * 1); s.setNormalisedValue(normOf(s, masterFor(i, t))); }
    if (phase === "end") s.sliderDragEnded();
    if (phase === "reset") setValue(tapName(i, "Time"), (i + 1) / 8);
  };
}
function levelGesture(i) {
  const s = slider(tapName(i, "LevelDb"));
  let db = 0;
  return (phase, amount) => {
    if (phase === "start") { s.sliderDragStarted(); db = vm.taps[i].db; }
    if (phase === "delta") { db = clamp(db + amount * 0.5, -60, 12); s.setNormalisedValue(normOf(s, trimFor(i, db))); }
    if (phase === "end") s.sliderDragEnded();
    if (phase === "reset") setValue(tapName(i, "LevelDb"), 0);
  };
}
valueEditor("tapTime", (p, a) => selected >= 0 && timeGesture._cur(p, a));
valueEditor("tapLevel", (p, a) => selected >= 0 && levelGesture._cur(p, a));
timeGesture._cur = () => {};
levelGesture._cur = () => {};

function useTap(i) {
  timeGesture._cur = timeGesture(i);
  levelGesture._cur = levelGesture(i);
  bindTapPitch(i);
}

const lastDim = new Array(8).fill("time");

function attachBall(i) {
  const g = balls[i].g;

  g.addEventListener("pointerenter", () => { hovered = i; kickPlot(); });
  g.addEventListener("pointerleave", () => { hovered = -1; kickPlot(); });

  g.addEventListener("pointerdown", e => {
    if (!vm) return;
    e.preventDefault();
    g.focus();
    try { g.setPointerCapture(e.pointerId); } catch (err) {}

    const rect = plot.getBoundingClientRect(), scale = rect.width / 512;
    const toLocal = ev => [(ev.clientX - rect.left) / scale, (ev.clientY - rect.top) / scale];
    const [sx, sy] = toLocal(e);
    const start = { x: balls[i].tx, y: balls[i].ty, px: sx, py: sy };
    const timeS = slider(tapName(i, "Time")), levelS = slider(tapName(i, "LevelDb"));
    let moved = false, fx = start.x, fy = start.y, lx = sx, ly = sy;

    const move = ev => {
      const [px, py] = toLocal(ev);
      if (!moved && Math.hypot(px - start.px, py - start.py) < 3) return;
      if (!moved) {
        moved = true;
        dragging = i;
        timeS.sliderDragStarted();
        levelS.sliderDragStarted();
      }
      const k = ev.shiftKey ? 0.2 : 1;            // shift: fine
      fx += (px - lx) * k; fy += (py - ly) * k;
      lx = px; ly = py;

      const x = clamp(fx, X0, X1 + 6), y = clamp(fy, YT - 4, YB - 6);
      const t = Math.max(1, tOf(x));
      const db = clamp(dbOf(y), DB_BOTTOM, DB_TOP);
      const v = masterFor(i, t);
      timeS.setNormalisedValue(normOf(timeS, v));
      levelS.setNormalisedValue(normOf(levelS, trimFor(i, db)));
      if (Math.abs(px - start.px) > Math.abs(py - start.py)) lastDim[i] = "time"; else lastDim[i] = "level";
      dragView = { t, db };
      kickPlot();
      renderTapPop();
    };
    const up = () => {
      g.removeEventListener("pointermove", move);
      g.removeEventListener("pointerup", up);
      g.removeEventListener("pointercancel", up);
      if (moved) {
        timeS.sliderDragEnded();
        levelS.sliderDragEnded();
        dragging = -1;
        dragView = null;
        kickPlot();
      }
      if (!moved || selected === i) { useTap(i); openTapPop(i); }
    };
    g.addEventListener("pointermove", move);
    g.addEventListener("pointerup", up);
    g.addEventListener("pointercancel", up);
  });

  g.addEventListener("dblclick", e => {
    e.preventDefault();
    if (lastDim[i] === "level") setValue(tapName(i, "LevelDb"), 0);
    else setValue(tapName(i, "Time"), (i + 1) / 8);
  });

  // Keyboard: arrows move precisely (shift: coarser), M mutes, Enter opens the details.
  g.addEventListener("keydown", e => {
    if (!vm) return;
    if (e.key === "Enter" || e.key === " ") { e.preventDefault(); useTap(i); openTapPop(i); return; }
    if (e.key === "m" || e.key === "M") { toggle(tapName(i, "On")).setValue(!on(tapName(i, "On"))); return; }
    if (e.key === "r" || e.key === "R") { toggle(tapName(i, "Reverse")).setValue(!on(tapName(i, "Reverse"))); return; }
    const dt = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
    const dd = e.key === "ArrowUp" ? 1 : e.key === "ArrowDown" ? -1 : 0;
    if (!dt && !dd) return;
    e.preventDefault();
    if (dt) { lastDim[i] = "time"; setValue(tapName(i, "Time"), masterFor(i, vm.taps[i].t + dt * (e.shiftKey ? 10 : 1))); }
    if (dd) { lastDim[i] = "level"; setValue(tapName(i, "LevelDb"), trimFor(i, vm.taps[i].db + dd * (e.shiftKey ? 3 : 0.5))); }
  });
}

// ============================================================================ start
native("uiReady")();
refresh();
