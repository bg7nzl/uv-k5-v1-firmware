#!/usr/bin/env python3
"""
CAT Control Web UI — Flask backend (COM enumeration, CatRadio session).
Run from repo:  python webui/server.py
Or:           cd tools/cat_control && python webui/server.py
"""

from __future__ import annotations

import os
import sys
import threading
from pathlib import Path
from typing import Any, Optional

# Parent package: tools/cat_control
_ROOT = Path(__file__).resolve().parent.parent
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from flask import Flask, jsonify, request, send_from_directory

from cat_protocol import (
    PARAM_RX_FREQ,
    PARAM_TX_FREQ,
    PARAM_TX_OFFSET,
    PARAM_OFFSET_DIR,
    PARAM_RX_TONE_TYPE,
    PARAM_RX_TONE_CODE,
    PARAM_TX_TONE_TYPE,
    PARAM_TX_TONE_CODE,
    PARAM_MODULATION,
    PARAM_TX_POWER,
    PARAM_BANDWIDTH,
    PARAM_SQUELCH,
    PARAM_VOX_SWITCH,
    PARAM_VOX_LEVEL,
    PARAM_VOX_DELAY,
    PARAM_MIC_GAIN,
    PARAM_SPEAKER_GAIN,
    PARAM_DAC_GAIN,
    PARAM_COMPANDER,
    PARAM_SCRAMBLE,
    PARAM_BUSY_LOCK,
    PARAM_STEP,
    PARAM_MIC_BAR,
    PARAM_RSSI,
    freq_from_10hz,
    freq_to_10hz,
)
from cat_radio import CatRadio

try:
    from serial.tools import list_ports
except ImportError:
    list_ports = None  # type: ignore

_STATIC = Path(__file__).resolve().parent / "static"

app = Flask(__name__, static_folder=str(_STATIC), static_url_path="/static")

_radio: Optional[CatRadio] = None
_radio_lock = threading.Lock()


def _get_radio() -> Optional[CatRadio]:
    with _radio_lock:
        return _radio


def _decode_settings(raw: dict[int, int]) -> dict[str, Any]:
    """Raw param_id → JSON for UI. ``tx_freq_mhz`` is always effective TX (MHz)."""
    def g(pid: int, default: int = 0) -> int:
        return int(raw.get(pid, default))

    rx_10 = g(PARAM_RX_FREQ)
    off_10 = g(PARAM_TX_OFFSET)
    d = g(PARAM_OFFSET_DIR)
    tx_10 = g(PARAM_TX_FREQ)

    if d == 1:
        eff_10 = rx_10 + off_10
    elif d == 2:
        eff_10 = (rx_10 - off_10) if rx_10 >= off_10 else 0
    else:
        eff_10 = tx_10

    rx_mhz = freq_from_10hz(rx_10)
    tx_mhz = freq_from_10hz(eff_10)
    off_mhz = freq_from_10hz(off_10)

    return {
        "rx_freq_mhz": round(rx_mhz, 5),
        "tx_freq_mhz": round(tx_mhz, 5),
        "tx_offset_mhz": round(off_mhz, 5),
        "offset_dir": d,
        "rx_tone_type": g(PARAM_RX_TONE_TYPE),
        "rx_tone_code": g(PARAM_RX_TONE_CODE),
        "tx_tone_type": g(PARAM_TX_TONE_TYPE),
        "tx_tone_code": g(PARAM_TX_TONE_CODE),
        "modulation": g(PARAM_MODULATION),
        "tx_power": g(PARAM_TX_POWER),
        "bandwidth": g(PARAM_BANDWIDTH),
        "squelch": g(PARAM_SQUELCH),
        "vox_switch": g(PARAM_VOX_SWITCH),
        "vox_level": g(PARAM_VOX_LEVEL),
        "vox_delay": g(PARAM_VOX_DELAY),
        "mic_gain": g(PARAM_MIC_GAIN),
        "speaker_gain": g(PARAM_SPEAKER_GAIN),
        "dac_gain": g(PARAM_DAC_GAIN),
        "compander": g(PARAM_COMPANDER),
        "scramble": g(PARAM_SCRAMBLE),
        "busy_lock": g(PARAM_BUSY_LOCK),
        "step_index": g(PARAM_STEP),
        "mic_bar": g(PARAM_MIC_BAR),
        "rssi_raw": g(PARAM_RSSI),
    }


def _encode_settings(body: dict[str, Any]) -> dict[int, int]:
    """
    JSON → param map. Frequency model (one path at a time):
    - Duplex (offset_dir 1/2): only RX + offset + direction — do not set PARAM_TX_FREQ.
    - Simplex (offset_dir 0): RX + TX, offset cleared on air side.
    """
    out: dict[int, int] = {}

    _freq = ("rx_freq_mhz", "tx_freq_mhz", "tx_offset_mhz", "offset_dir")
    if any(k in body for k in _freq):
        od = int(body.get("offset_dir", 0)) & 0xFF
        if od in (1, 2):
            if "rx_freq_mhz" in body:
                out[PARAM_RX_FREQ] = freq_to_10hz(float(body["rx_freq_mhz"]))
            if "tx_offset_mhz" in body:
                out[PARAM_TX_OFFSET] = freq_to_10hz(abs(float(body["tx_offset_mhz"])))
            out[PARAM_OFFSET_DIR] = od
        else:
            if "rx_freq_mhz" in body:
                out[PARAM_RX_FREQ] = freq_to_10hz(float(body["rx_freq_mhz"]))
            if "tx_freq_mhz" in body:
                out[PARAM_TX_FREQ] = freq_to_10hz(float(body["tx_freq_mhz"]))
            out[PARAM_TX_OFFSET] = 0
            out[PARAM_OFFSET_DIR] = 0

    if "rx_tone_type" in body:
        out[PARAM_RX_TONE_TYPE] = int(body["rx_tone_type"]) & 0xFF
    if "rx_tone_code" in body:
        out[PARAM_RX_TONE_CODE] = int(body["rx_tone_code"]) & 0xFFFF
    if "tx_tone_type" in body:
        out[PARAM_TX_TONE_TYPE] = int(body["tx_tone_type"]) & 0xFF
    if "tx_tone_code" in body:
        out[PARAM_TX_TONE_CODE] = int(body["tx_tone_code"]) & 0xFFFF

    if "modulation" in body:
        out[PARAM_MODULATION] = int(body["modulation"]) & 0xFF
    if "tx_power" in body:
        out[PARAM_TX_POWER] = int(body["tx_power"]) & 0xFF
    if "bandwidth" in body:
        out[PARAM_BANDWIDTH] = int(body["bandwidth"]) & 0xFF
    if "squelch" in body:
        out[PARAM_SQUELCH] = int(body["squelch"]) & 0xFF

    if "vox_switch" in body:
        out[PARAM_VOX_SWITCH] = int(body["vox_switch"]) & 0xFF
    if "vox_level" in body:
        out[PARAM_VOX_LEVEL] = int(body["vox_level"]) & 0xFF
    if "vox_delay" in body:
        out[PARAM_VOX_DELAY] = int(body["vox_delay"]) & 0xFF

    if "mic_gain" in body:
        out[PARAM_MIC_GAIN] = int(body["mic_gain"]) & 0xFF
    if "speaker_gain" in body:
        out[PARAM_SPEAKER_GAIN] = int(body["speaker_gain"]) & 0xFF
    if "dac_gain" in body:
        out[PARAM_DAC_GAIN] = int(body["dac_gain"]) & 0xFF

    if "compander" in body:
        out[PARAM_COMPANDER] = int(body["compander"]) & 0xFF
    if "scramble" in body:
        out[PARAM_SCRAMBLE] = int(body["scramble"]) & 0xFF
    if "busy_lock" in body:
        out[PARAM_BUSY_LOCK] = int(body["busy_lock"]) & 0xFF
    if "step_index" in body:
        out[PARAM_STEP] = int(body["step_index"]) & 0xFF

    return out


@app.route("/")
def index():
    return send_from_directory(_STATIC, "index.html")


@app.route("/api/ports")
def api_ports():
    if list_ports is None:
        return jsonify({"ports": [], "error": "pyserial.tools.list_ports unavailable"})
    ports = []
    for p in list_ports.comports():
        ports.append({
            "device": p.device,
            "name": p.name or p.device,
            "description": (p.description or "").strip(),
            "hwid": (p.hwid or "").strip(),
        })
    return jsonify({"ports": ports})


@app.route("/api/connected")
def api_connected():
    r = _get_radio()
    return jsonify({
        "connected": r is not None,
        "port": getattr(r, "_port", None) if r else None,
        "baudrate": getattr(r, "_baudrate", None) if r else None,
    })


@app.route("/api/connect", methods=["POST"])
def api_connect():
    global _radio
    data = request.get_json(force=True, silent=True) or {}
    port = data.get("port")
    baud = int(data.get("baudrate", 38400))
    if not port:
        return jsonify({"ok": False, "error": "missing port"}), 400
    with _radio_lock:
        if _radio is not None:
            try:
                _radio.disconnect()
            except Exception:
                pass
            _radio = None
        try:
            radio = CatRadio(port, baudrate=baud)
            radio.connect()
            _radio = radio
        except Exception as e:
            return jsonify({"ok": False, "error": str(e)}), 400
    return jsonify({"ok": True})


@app.route("/api/disconnect", methods=["POST"])
def api_disconnect():
    global _radio
    with _radio_lock:
        if _radio is not None:
            try:
                _radio.disconnect()
            except Exception:
                pass
            _radio = None
    return jsonify({"ok": True})


@app.route("/api/status")
def api_status():
    r = _get_radio()
    if r is None:
        return jsonify({"error": "not connected"}), 400
    try:
        st = r.get_status()
        return jsonify(st)
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/settings")
def api_settings_get():
    r = _get_radio()
    if r is None:
        return jsonify({"error": "not connected"}), 400
    try:
        raw = r.get_all_params()
        return jsonify(_decode_settings(raw))
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/settings", methods=["POST"])
def api_settings_post():
    r = _get_radio()
    if r is None:
        return jsonify({"ok": False, "error": "not connected"}), 400
    body = request.get_json(force=True, silent=True) or {}
    apply_hw = bool(body.get("apply", True))
    try:
        params = _encode_settings(body)
        if not params:
            return jsonify({"ok": False, "error": "no fields to set"}), 400
        r.set_params(params, apply_hw=apply_hw)
        return jsonify({"ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route("/api/apply", methods=["POST"])
def api_apply():
    r = _get_radio()
    if r is None:
        return jsonify({"ok": False, "error": "not connected"}), 400
    try:
        r.apply()
        return jsonify({"ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


# --- Reference data (CTCSS Hz×10, same order as firmware dcs.c) ---
CTCSS_HZ10 = [
    670, 693, 719, 744, 770, 797, 825, 854, 885, 915,
    948, 974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
    1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679,
    1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
    2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541,
]


@app.route("/api/meta")
def api_meta():
    return jsonify({
        "ctcss_hz10": CTCSS_HZ10,
        "default_baud": 38400,
    })


def main():
    host = os.environ.get("CAT_WEB_HOST", "127.0.0.1")
    port = int(os.environ.get("CAT_WEB_PORT", "8765"))
    print(f"CAT Web UI: http://{host}:{port}/")
    app.run(host=host, port=port, threaded=True, use_reloader=False)


if __name__ == "__main__":
    main()
