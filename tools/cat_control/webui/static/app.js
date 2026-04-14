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

  function rssiToDbm(raw) {
    return (raw / 2) - 160;
  }

  function dbmToSMeter(dBm) {
    if (dBm <= -121) return "S0";
    if (dBm <= -115) return "S1";
    if (dBm <= -109) return "S2";
    if (dBm <= -103) return "S3";
    if (dBm <= -97)  return "S4";
    if (dBm <= -91)  return "S5";
    if (dBm <= -85)  return "S6";
    if (dBm <= -79)  return "S7";
    if (dBm <= -73)  return "S8";
    if (dBm <= -63)  return "S9";
    const over = dBm + 63;
    return "S9+" + Math.min(Math.round(over), 99);
  }

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
      z.textContent = "关";
      sel.appendChild(z);
      ctcssHz10.forEach((hz10, i) => {
        const o = document.createElement("option");
        o.value = String(i);
        const hz = hz10 / 10;
        o.textContent = `${hz.toFixed(1)} Hz`;
        sel.appendChild(o);
      });
    };
    mk("rx_ctcss");
    mk("tx_ctcss");
    mk("ch_rx_ctcss");
    mk("ch_tx_ctcss");
  }

  /** CTCSS 下拉 → 协议参数（仅 CTCSS / 关） */
  function toneFromCtcssSelect(selId) {
    const v = $(selId).value;
    if (v === "") return { tone_type: 0, tone_code: 0 };
    const idx = parseInt(v, 10);
    const n = ctcssHz10.length;
    if (!Number.isFinite(idx) || idx < 0 || (n > 0 && idx >= n)) {
      return { tone_type: 0, tone_code: 0 };
    }
    return { tone_type: 1, tone_code: idx };
  }

  function updatePowerLabel() {
    const p = parseInt($("tx_power").value, 10) || 0;
    $("powerLabel").textContent = POWER_LABELS[p] || "?";
  }

  function activateTab(name) {
    document.querySelectorAll(".tab").forEach((btn) => {
      btn.classList.toggle("active", btn.dataset.tab === name);
    });
    document.querySelectorAll(".panel").forEach((p) => {
      p.classList.toggle("active", p.id === "panel-" + name);
    });
  }

  function setupTabs() {
    document.querySelectorAll(".tab").forEach((btn) => {
      btn.addEventListener("click", () => activateTab(btn.dataset.tab));
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
    txEl.readOnly = true;
    if (hint) {
      hint.textContent = duplex ? "（= 接收 ± 偏移）" : "（= 接收频率）";
    }
    const rx = parseFloat($("rx_freq_mhz").value);
    if (!Number.isFinite(rx)) return;
    if (duplex) {
      const off = parseFloat($("tx_offset_mhz").value);
      if (!Number.isFinite(off)) return;
      const t =
        dir === 1 ? rx + off : rx >= off ? rx - off : 0;
      txEl.value = t.toFixed(5);
    } else {
      txEl.value = rx.toFixed(5);
    }
  }

  async function pollStatus() {
    try {
      const s = await api("/api/status");
      $("telTx").textContent = s.tx_active ? "发射" : "—";
      $("telTx").classList.toggle("on", !!s.tx_active);
      $("telRx").textContent = s.rx_active ? "接收" : "—";
      $("telRx").classList.toggle("on", !!s.rx_active);

      const rssiRaw = s.rssi ?? 0;
      const dBm = rssiToDbm(rssiRaw);
      const sMeter = dbmToSMeter(dBm);
      $("telRssi").textContent = `${dBm} dBm  ${sMeter}`;

      const bv = s.battery_mv;
      $("telBatt").textContent =
        bv != null ? (bv / 100).toFixed(2) + " V" : "—";

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
    if (s.rx_tone_type === 1 && s.rx_tone_code < ctcssHz10.length) {
      $("rx_ctcss").value = String(s.rx_tone_code);
    } else {
      $("rx_ctcss").value = "";
    }
    if (s.tx_tone_type === 1 && s.tx_tone_code < ctcssHz10.length) {
      $("tx_ctcss").value = String(s.tx_tone_code);
    } else {
      $("tx_ctcss").value = "";
    }
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
    const rxT = toneFromCtcssSelect("rx_ctcss");
    out.rx_tone_type = rxT.tone_type;
    out.rx_tone_code = rxT.tone_code;
    const txT = toneFromCtcssSelect("tx_ctcss");
    out.tx_tone_type = txT.tone_type;
    out.tx_tone_code = txT.tone_code;
    return out;
  }

  function channelPayloadFromForm() {
    updateFreqLinkage();
    const rxT = toneFromCtcssSelect("rx_ctcss");
    const txT = toneFromCtcssSelect("tx_ctcss");
    const dir = parseInt($("offset_dir").value, 10) || 0;
    const rx = parseFloat($("rx_freq_mhz").value);
    const off = parseFloat($("tx_offset_mhz").value);
    if (!Number.isFinite(rx) || !Number.isFinite(off)) {
      throw new Error("接收频率或发射偏移无效");
    }
    return {
      rx_freq_mhz: rx,
      tx_offset_mhz: off,
      offset_dir: dir,
      rx_tone_type: rxT.tone_type,
      rx_tone_code: rxT.tone_code,
      tx_tone_type: txT.tone_type,
      tx_tone_code: txT.tone_code,
    };
  }

  function applyChannelToForm(ch) {
    $("rx_freq_mhz").value = String(ch.rx_freq_mhz);
    $("tx_offset_mhz").value = String(ch.tx_offset_mhz);
    $("offset_dir").value = String(ch.offset_dir);
    if (ch.rx_tone_type === 1 && ch.rx_tone_code < ctcssHz10.length) {
      $("rx_ctcss").value = String(ch.rx_tone_code);
    } else {
      $("rx_ctcss").value = "";
    }
    if (ch.tx_tone_type === 1 && ch.tx_tone_code < ctcssHz10.length) {
      $("tx_ctcss").value = String(ch.tx_tone_code);
    } else {
      $("tx_ctcss").value = "";
    }
    updateFreqLinkage();
  }

  function ctcssLabel(ch, which) {
    const t = which === "rx" ? ch.rx_tone_type : ch.tx_tone_type;
    const c = which === "rx" ? ch.rx_tone_code : ch.tx_tone_code;
    if (t === 1 && c < ctcssHz10.length) {
      return (ctcssHz10[c] / 10).toFixed(1) + " Hz";
    }
    return "关";
  }

  function formatDuplex(ch) {
    const od = ch.offset_dir;
    const off = Number(ch.tx_offset_mhz);
    if (od === 0) return "同频";
    const sign = od === 1 ? "+" : "−";
    return sign + off.toFixed(5) + " MHz";
  }

  function truncateNote(s, n) {
    if (!s) return "—";
    const t = String(s).trim();
    if (t.length <= n) return t;
    return t.slice(0, n) + "…";
  }

  function openChModal() {
    $("chModal").hidden = false;
    $("chModal").setAttribute("aria-hidden", "false");
  }

  function closeChModal() {
    $("chModal").hidden = true;
    $("chModal").setAttribute("aria-hidden", "true");
  }

  function channelToModal(ch) {
    $("ch_id").value = ch.id || "";
    $("ch_name").value = ch.name || "";
    $("ch_note").value = ch.note || "";
    $("ch_rx_freq_mhz").value = String(ch.rx_freq_mhz);
    $("ch_tx_offset_mhz").value = String(ch.tx_offset_mhz);
    $("ch_offset_dir").value = String(ch.offset_dir);
    if (ch.rx_tone_type === 1 && ch.rx_tone_code < ctcssHz10.length) {
      $("ch_rx_ctcss").value = String(ch.rx_tone_code);
    } else {
      $("ch_rx_ctcss").value = "";
    }
    if (ch.tx_tone_type === 1 && ch.tx_tone_code < ctcssHz10.length) {
      $("ch_tx_ctcss").value = String(ch.tx_tone_code);
    } else {
      $("ch_tx_ctcss").value = "";
    }
  }

  function payloadFromModal() {
    const rxT = toneFromCtcssSelect("ch_rx_ctcss");
    const txT = toneFromCtcssSelect("ch_tx_ctcss");
    const rx = parseFloat($("ch_rx_freq_mhz").value);
    const off = parseFloat($("ch_tx_offset_mhz").value);
    const dir = parseInt($("ch_offset_dir").value, 10) || 0;
    if (!Number.isFinite(rx) || !Number.isFinite(off)) {
      throw new Error("接收频率或发射偏移无效");
    }
    return {
      name: $("ch_name").value,
      note: $("ch_note").value,
      rx_freq_mhz: rx,
      tx_offset_mhz: off,
      offset_dir: dir,
      rx_tone_type: rxT.tone_type,
      rx_tone_code: rxT.tone_code,
      tx_tone_type: txT.tone_type,
      tx_tone_code: txT.tone_code,
    };
  }

  async function loadChannelList() {
    const data = await api("/api/channels");
    const rows = data.channels || [];
    const tb = $("channelTableBody");
    tb.innerHTML = "";
    for (const ch of rows) {
      const tr = document.createElement("tr");
      const tdName = document.createElement("td");
      tdName.textContent = ch.name || "（未命名）";
      const tdRx = document.createElement("td");
      tdRx.className = "mono";
      tdRx.textContent = Number(ch.rx_freq_mhz).toFixed(5);
      const tdDup = document.createElement("td");
      tdDup.className = "mono";
      tdDup.textContent = formatDuplex(ch);
      const tdRxa = document.createElement("td");
      tdRxa.textContent = ctcssLabel(ch, "rx");
      const tdTxa = document.createElement("td");
      tdTxa.textContent = ctcssLabel(ch, "tx");
      const tdNote = document.createElement("td");
      tdNote.textContent = truncateNote(ch.note, 48);
      const tdAct = document.createElement("td");
      tdAct.className = "row-actions";
      const bLoad = document.createElement("button");
      bLoad.type = "button";
      bLoad.className = "btn secondary btn-tiny";
      bLoad.textContent = "载入";
      bLoad.addEventListener("click", () => {
        applyChannelToForm(ch);
        activateTab("freq");
        setMsg("已载入到频率表单", "ok");
      });
      const bEdit = document.createElement("button");
      bEdit.type = "button";
      bEdit.className = "btn secondary btn-tiny";
      bEdit.textContent = "编辑";
      bEdit.addEventListener("click", () => {
        $("chModalTitle").textContent = "编辑频道";
        channelToModal(ch);
        openChModal();
      });
      const bDel = document.createElement("button");
      bDel.type = "button";
      bDel.className = "btn danger btn-tiny";
      bDel.textContent = "删除";
      bDel.addEventListener("click", async () => {
        if (!confirm(`删除频道「${ch.name || ch.id}」？`)) return;
        try {
          await api(`/api/channels/${encodeURIComponent(ch.id)}`, {
            method: "DELETE",
          });
          setMsg("已删除", "ok");
          await loadChannelList();
        } catch (e) {
          setMsg(e.message || String(e), "err");
        }
      });
      tdAct.appendChild(bLoad);
      tdAct.appendChild(bEdit);
      tdAct.appendChild(bDel);
      tr.appendChild(tdName);
      tr.appendChild(tdRx);
      tr.appendChild(tdDup);
      tr.appendChild(tdRxa);
      tr.appendChild(tdTxa);
      tr.appendChild(tdNote);
      tr.appendChild(tdAct);
      tb.appendChild(tr);
    }
  }

  async function saveChModal() {
    let body;
    try {
      body = payloadFromModal();
    } catch (e) {
      setMsg(e.message || String(e), "err");
      return;
    }
    const id = $("ch_id").value.trim();
    try {
      if (id) {
        await api(`/api/channels/${encodeURIComponent(id)}`, {
          method: "PUT",
          body: JSON.stringify(body),
        });
        setMsg("已保存", "ok");
      } else {
        await api("/api/channels", {
          method: "POST",
          body: JSON.stringify(body),
        });
        setMsg("已新建频道", "ok");
      }
      closeChModal();
      await loadChannelList();
    } catch (e) {
      setMsg(e.message || String(e), "err");
    }
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

    $("btnChannelSaveFromForm").addEventListener("click", () => {
      try {
        const p = channelPayloadFromForm();
        $("chModalTitle").textContent = "保存为新频道";
        channelToModal({
          id: "",
          name: "",
          note: "",
          ...p,
        });
        openChModal();
      } catch (e) {
        setMsg(e.message || String(e), "err");
      }
    });
    $("btnChannelRefresh").addEventListener("click", () =>
      loadChannelList().catch((e) => setMsg(e.message, "err"))
    );
    $("btnChannelNewEmpty").addEventListener("click", () => {
      $("chModalTitle").textContent = "新建频道";
      channelToModal({
        id: "",
        name: "",
        note: "",
        rx_freq_mhz: 145.0,
        tx_offset_mhz: 0,
        offset_dir: 0,
        rx_tone_type: 0,
        rx_tone_code: 0,
        tx_tone_type: 0,
        tx_tone_code: 0,
      });
      openChModal();
    });
    $("chModalSave").addEventListener("click", () =>
      saveChModal().catch((e) => setMsg(e.message, "err"))
    );
    $("chModalCancel").addEventListener("click", closeChModal);
    $("chModalBackdrop").addEventListener("click", closeChModal);
    $("chModalCloseX").addEventListener("click", closeChModal);
    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape" && !$("chModal").hidden) closeChModal();
    });

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

    try {
      await loadChannelList();
    } catch (e) {
      setMsg("频道列表加载失败: " + e.message, "err");
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
