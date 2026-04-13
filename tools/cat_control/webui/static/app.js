(function () {
  "use strict";

  const POWER_LABELS = [
    "LOW1",
    "LOW2",
    "LOW3",
    "LOW4",
    "LOW5",
    "MID",
    "HIGH",
    "P7",
  ];

  const $ = (id) => document.getElementById(id);

  let statusTimer = null;
  let ctcssHz10 = [];

  function setMsg(text, kind) {
    const el = $("msg");
    el.textContent = text || "";
    el.className = "msg" + (kind ? " " + kind : "");
  }

  async function api(path, opts) {
    const r = await fetch(path, {
      headers: { Accept: "application/json", "Content-Type": "application/json" },
      ...opts,
    });
    const data = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(data.error || r.statusText || String(r.status));
    return data;
  }

  async function loadPorts() {
    const data = await api("/api/ports");
    const sel = $("portSelect");
    const cur = sel.value;
    sel.innerHTML = "";
    const ports = data.ports || [];
    if (!ports.length) {
      const o = document.createElement("option");
      o.value = "";
      o.textContent = "（无可用串口）";
      sel.appendChild(o);
      return;
    }
    for (const p of ports) {
      const o = document.createElement("option");
      o.value = p.device;
      o.textContent = p.description
        ? `${p.device} — ${p.description}`
        : p.device;
      sel.appendChild(o);
    }
    if (cur && [...sel.options].some((x) => x.value === cur)) sel.value = cur;
  }

  function fillCtcssSelects() {
    const mk = (selId) => {
      const sel = $(selId);
      sel.innerHTML = "";
      const z = document.createElement("option");
      z.value = "";
      z.textContent = "— 选 CTCSS —";
      sel.appendChild(z);
      ctcssHz10.forEach((hz10, i) => {
        const o = document.createElement("option");
        o.value = String(i);
        const hz = hz10 / 10;
        o.textContent = `${hz.toFixed(1)} Hz (#${i})`;
        sel.appendChild(o);
      });
    };
    mk("rx_ctcss_idx");
    mk("tx_ctcss_idx");
  }

  function onCtcssPick(which) {
    const idxSel = $(which + "_ctcss_idx");
    const v = idxSel.value;
    if (v === "") return;
    $(which + "_tone_type").value = "1";
    $(which + "_tone_code").value = v;
  }

  function updatePowerLabel() {
    const p = parseInt($("tx_power").value, 10) || 0;
    $("powerLabel").textContent = POWER_LABELS[p] || "?";
  }

  function setupTabs() {
    document.querySelectorAll(".tab").forEach((btn) => {
      btn.addEventListener("click", () => {
        const name = btn.dataset.tab;
        document.querySelectorAll(".tab").forEach((b) =>
          b.classList.toggle("active", b === btn)
        );
        document.querySelectorAll(".panel").forEach((p) => {
          p.classList.toggle("active", p.id === "panel-" + name);
        });
      });
    });
  }

  function setConnected(on, port) {
    $("btnConnect").disabled = on;
    $("btnDisconnect").disabled = !on;
    $("portSelect").disabled = on;
    $("baudSelect").disabled = on;
    $("connPill").textContent = on ? "已连接 " + (port || "") : "未连接";
    $("connPill").classList.toggle("on", on);
    $("telemetry").hidden = !on;
    $("mainPanels").hidden = !on;
    if (!on) {
      if (statusTimer) {
        clearInterval(statusTimer);
        statusTimer = null;
      }
    }
  }

  function updateFreqLinkage() {
    const dir = parseInt($("offset_dir").value, 10) || 0;
    const txEl = $("tx_freq_mhz");
    const hint = $("tx_freq_hint");
    const duplex = dir === 1 || dir === 2;
    txEl.readOnly = duplex;
    if (hint) {
      hint.textContent = duplex
        ? "（= 接收 ± 偏移）"
        : "";
    }
    if (duplex) {
      const rx = parseFloat($("rx_freq_mhz").value);
      const off = parseFloat($("tx_offset_mhz").value);
      if (Number.isFinite(rx) && Number.isFinite(off)) {
        const t = dir === 1 ? rx + off : rx - off;
        txEl.value = t.toFixed(5);
      }
    }
  }

  async function pollStatus() {
    try {
      const s = await api("/api/status");
      $("telTx").textContent = s.tx_active ? "开" : "关";
      $("telTx").classList.toggle("on", !!s.tx_active);
      $("telRx").textContent = s.rx_active ? "开" : "关";
      $("telRx").classList.toggle("on", !!s.rx_active);
      $("telRssi").textContent = String(s.rssi ?? "—");
      const mv = s.battery_mv;
      $("telBatt").textContent =
        mv != null ? (mv / 1000).toFixed(2) + " V" : "—";
      $("telVox").textContent = s.vox_triggered ? "触发" : "—";
    } catch (e) {
      /* ignore transient */
    }
  }

  function applySettingsToForm(s) {
    const num = (id, v) => {
      const el = $(id);
      if (el && v !== undefined && v !== null) el.value = String(v);
    };
    num("rx_freq_mhz", s.rx_freq_mhz);
    num("tx_freq_mhz", s.tx_freq_mhz);
    num("tx_offset_mhz", s.tx_offset_mhz);
    num("offset_dir", s.offset_dir);
    num("rx_tone_type", s.rx_tone_type);
    num("tx_tone_type", s.tx_tone_type);
    num("rx_tone_code", s.rx_tone_code);
    num("tx_tone_code", s.tx_tone_code);
    num("modulation", s.modulation);
    num("bandwidth", s.bandwidth);
    num("tx_power", s.tx_power);
    num("squelch", s.squelch);
    num("vox_switch", s.vox_switch);
    num("vox_level", s.vox_level);
    num("vox_delay", s.vox_delay);
    num("mic_gain", s.mic_gain);
    num("speaker_gain", s.speaker_gain);
    num("dac_gain", s.dac_gain);
    num("compander", s.compander);
    num("scramble", s.scramble);
    num("busy_lock", s.busy_lock);
    num("step_index", s.step_index);
    $("mic_bar_ro").textContent = String(s.mic_bar ?? "—");
    $("rssi_raw_ro").textContent = String(s.rssi_raw ?? "—");

    if (s.rx_tone_type === 1 && s.rx_tone_code < ctcssHz10.length) {
      $("rx_ctcss_idx").value = String(s.rx_tone_code);
    } else $("rx_ctcss_idx").value = "";

    if (s.tx_tone_type === 1 && s.tx_tone_code < ctcssHz10.length) {
      $("tx_ctcss_idx").value = String(s.tx_tone_code);
    } else $("tx_ctcss_idx").value = "";

    updatePowerLabel();
    updateFreqLinkage();
  }

  function collectSettings() {
    const floatKeys = new Set([
      "rx_freq_mhz",
      "tx_freq_mhz",
      "tx_offset_mhz",
    ]);
    const keys = [
      "rx_freq_mhz",
      "tx_freq_mhz",
      "tx_offset_mhz",
      "offset_dir",
      "rx_tone_type",
      "rx_tone_code",
      "tx_tone_type",
      "tx_tone_code",
      "modulation",
      "bandwidth",
      "tx_power",
      "squelch",
      "vox_switch",
      "vox_level",
      "vox_delay",
      "mic_gain",
      "speaker_gain",
      "dac_gain",
      "compander",
      "scramble",
      "busy_lock",
      "step_index",
    ];
    const dir = parseInt($("offset_dir").value, 10) || 0;
    const duplex = dir === 1 || dir === 2;

    const out = {};
    for (const k of keys) {
      if (duplex && k === "tx_freq_mhz") continue;
      const el = $(k);
      if (!el) continue;
      if (el.type === "number" || el.type === "range") {
        const v = parseFloat(el.value);
        if (!Number.isFinite(v)) continue;
        out[k] = floatKeys.has(k) ? v : Math.round(v);
      } else {
        const v = parseInt(el.value, 10);
        if (!Number.isFinite(v)) continue;
        out[k] = v;
      }
    }
    return out;
  }

  async function reloadSettings() {
    setMsg("读取中…", "");
    const s = await api("/api/settings");
    applySettingsToForm(s);
    setMsg("已从电台刷新", "ok");
  }

  async function doConnect() {
    const port = $("portSelect").value;
    const baudrate = parseInt($("baudSelect").value, 10);
    if (!port) {
      setMsg("请选择串口", "err");
      return;
    }
    setMsg("连接中…", "");
    try {
      await api("/api/connect", {
        method: "POST",
        body: JSON.stringify({ port, baudrate }),
      });
      setConnected(true, port);
      setMsg("已连接", "ok");
      await reloadSettings();
      if (statusTimer) clearInterval(statusTimer);
      statusTimer = setInterval(pollStatus, 900);
      pollStatus();
    } catch (e) {
      setMsg(e.message || String(e), "err");
      setConnected(false);
    }
  }

  async function doDisconnect() {
    try {
      await api("/api/disconnect", { method: "POST" });
    } catch (e) {
      /* ignore */
    }
    setConnected(false);
    setMsg("已断开", "");
  }

  async function doApply() {
    setMsg("写入中…", "");
    try {
      updateFreqLinkage();
      const body = collectSettings();
      body.apply = true;
      await api("/api/settings", {
        method: "POST",
        body: JSON.stringify(body),
      });
      setMsg("已应用", "ok");
      await reloadSettings();
    } catch (e) {
      setMsg(e.message || String(e), "err");
    }
  }

  async function init() {
    setupTabs();
    const meta = await api("/api/meta");
    ctcssHz10 = meta.ctcss_hz10 || [];
    fillCtcssSelects();

    $("rx_ctcss_idx").addEventListener("change", () =>
      onCtcssPick("rx")
    );
    $("tx_ctcss_idx").addEventListener("change", () =>
      onCtcssPick("tx")
    );
    $("tx_power").addEventListener("input", updatePowerLabel);

    ["rx_freq_mhz", "tx_offset_mhz"].forEach((id) => {
      $(id).addEventListener("input", updateFreqLinkage);
      $(id).addEventListener("change", updateFreqLinkage);
    });
    $("offset_dir").addEventListener("change", updateFreqLinkage);

    $("btnRefreshPorts").addEventListener("click", () =>
      loadPorts().catch((e) => setMsg(e.message, "err"))
    );
    $("btnConnect").addEventListener("click", () =>
      doConnect().catch((e) => setMsg(e.message, "err"))
    );
    $("btnDisconnect").addEventListener("click", doDisconnect);
    $("btnReload").addEventListener("click", () =>
      reloadSettings().catch((e) => setMsg(e.message, "err"))
    );
    $("btnApply").addEventListener("click", () =>
      doApply().catch((e) => setMsg(e.message, "err"))
    );

    try {
      await loadPorts();
    } catch (e) {
      setMsg("枚举串口失败: " + e.message, "err");
    }

    try {
      const c = await api("/api/connected");
      if (c.connected) {
        setConnected(true, c.port);
        await reloadSettings();
        if (statusTimer) clearInterval(statusTimer);
        statusTimer = setInterval(pollStatus, 900);
        pollStatus();
      }
    } catch (e) {
      /* ignore */
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
