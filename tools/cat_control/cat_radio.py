"""
CAT Radio — high-level radio control abstraction over serial port.
"""

import threading
import time
from typing import Optional

import serial

from cat_protocol import (
    SYNC, CAT_CMD_ACK, CAT_CMD_PARAM_RESP, CAT_CMD_STATUS_RESP,
    PARAM_RX_FREQ, PARAM_TX_FREQ, PARAM_TX_OFFSET, PARAM_OFFSET_DIR,
    PARAM_TX_POWER, PARAM_BANDWIDTH, PARAM_MODULATION, PARAM_SQUELCH,
    PARAM_VOX_SWITCH, PARAM_VOX_LEVEL, PARAM_VOX_DELAY,
    PARAM_MIC_GAIN, PARAM_SPEAKER_GAIN, PARAM_DAC_GAIN,
    PARAM_RX_TONE_TYPE, PARAM_RX_TONE_CODE,
    PARAM_TX_TONE_TYPE, PARAM_TX_TONE_CODE,
    frame_enter, frame_exit, frame_noop, frame_apply, frame_status,
    frame_set_param, frame_get_param, frame_set_multi, frame_get_all,
    parse_response, parse_param_resp, parse_status_resp, parse_ack,
    freq_to_10hz, freq_from_10hz,
)


class CatRadio:
    """High-level CAT radio control."""

    def __init__(self, port: str, baudrate: int = 38400, timeout: float = 1.0):
        self._port = port
        self._baudrate = baudrate
        self._timeout = timeout
        self._ser: Optional[serial.Serial] = None
        self._heartbeat_thread: Optional[threading.Thread] = None
        self._heartbeat_stop = threading.Event()
        self._lock = threading.Lock()

    def connect(self):
        self._ser = serial.Serial(
            self._port, self._baudrate,
            timeout=self._timeout,
            write_timeout=self._timeout,
        )
        time.sleep(0.1)
        self._send_and_wait_ack(frame_enter())
        self._start_heartbeat()

    def disconnect(self):
        self._stop_heartbeat()
        if self._ser and self._ser.is_open:
            try:
                self._send_and_wait_ack(frame_exit())
            except Exception:
                pass
            self._ser.close()
        self._ser = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.disconnect()

    # --- Frequency ---

    def set_rx_frequency(self, freq_mhz: float):
        self._set_param(PARAM_RX_FREQ, freq_to_10hz(freq_mhz))

    def get_rx_frequency(self) -> float:
        return freq_from_10hz(self._get_param(PARAM_RX_FREQ))

    def set_tx_frequency(self, freq_mhz: float):
        self._set_param(PARAM_TX_FREQ, freq_to_10hz(freq_mhz))

    def set_offset(self, offset_mhz: float, direction: str = "none"):
        dir_map = {"none": 0, "+": 1, "plus": 1, "-": 2, "minus": 2}
        d = dir_map.get(direction.lower(), 0)
        self._set_multi({
            PARAM_TX_OFFSET: freq_to_10hz(abs(offset_mhz)),
            PARAM_OFFSET_DIR: d,
        })

    # --- Tone ---

    def set_tx_ctcss(self, code_index: int):
        self._set_multi({PARAM_TX_TONE_TYPE: 1, PARAM_TX_TONE_CODE: code_index})

    def set_tx_dcs(self, code: int, inverted: bool = False):
        self._set_multi({
            PARAM_TX_TONE_TYPE: 3 if inverted else 2,
            PARAM_TX_TONE_CODE: code,
        })

    def clear_tx_tone(self):
        self._set_param(PARAM_TX_TONE_TYPE, 0)

    def set_rx_ctcss(self, code_index: int):
        self._set_multi({PARAM_RX_TONE_TYPE: 1, PARAM_RX_TONE_CODE: code_index})

    def clear_rx_tone(self):
        self._set_param(PARAM_RX_TONE_TYPE, 0)

    # --- Power / Modulation ---

    def set_power(self, level: int):
        self._set_param(PARAM_TX_POWER, level)

    def set_bandwidth(self, narrow: bool):
        self._set_param(PARAM_BANDWIDTH, 1 if narrow else 0)

    def set_modulation(self, mod: int):
        self._set_param(PARAM_MODULATION, mod)

    def set_squelch(self, level: int):
        self._set_param(PARAM_SQUELCH, level)

    # --- VOX ---

    def set_vox(self, enabled: bool, level: int = 3, delay: int = 10):
        self._set_multi({
            PARAM_VOX_SWITCH: 1 if enabled else 0,
            PARAM_VOX_LEVEL: level,
            PARAM_VOX_DELAY: delay,
        })

    # --- Audio ---

    def set_mic_gain(self, level: int):
        self._set_param(PARAM_MIC_GAIN, level)

    def set_speaker_gain(self, level: int):
        self._set_param(PARAM_SPEAKER_GAIN, level)

    def set_dac_gain(self, level: int):
        self._set_param(PARAM_DAC_GAIN, level)

    # --- Batch & Apply ---

    def configure(self, **kwargs):
        """Set multiple params + apply. Keys are param names (lowercase)."""
        name_to_id = {
            "rx_freq": PARAM_RX_FREQ, "tx_freq": PARAM_TX_FREQ,
            "tx_offset": PARAM_TX_OFFSET, "offset_dir": PARAM_OFFSET_DIR,
            "tx_power": PARAM_TX_POWER, "bandwidth": PARAM_BANDWIDTH,
            "modulation": PARAM_MODULATION, "squelch": PARAM_SQUELCH,
            "vox_switch": PARAM_VOX_SWITCH, "vox_level": PARAM_VOX_LEVEL,
            "vox_delay": PARAM_VOX_DELAY,
            "mic_gain": PARAM_MIC_GAIN, "speaker_gain": PARAM_SPEAKER_GAIN,
            "dac_gain": PARAM_DAC_GAIN,
        }
        params = {}
        for k, v in kwargs.items():
            pid = name_to_id.get(k)
            if pid is not None:
                params[pid] = v
        if params:
            self._set_multi(params)
            self.apply()

    def apply(self):
        self._send_and_wait_ack(frame_apply())

    # --- Status ---

    def get_status(self) -> dict:
        with self._lock:
            self._ser.write(frame_status())
            return self._read_status_resp()

    # --- Low-level ---

    def _set_param(self, param_id: int, value: int):
        self._send_and_wait_ack(frame_set_param(param_id, value))

    def _get_param(self, param_id: int) -> int:
        with self._lock:
            self._ser.write(frame_get_param(param_id))
            return self._read_param_resp(param_id)

    def _set_multi(self, params: dict):
        self._send_and_wait_ack(frame_set_multi(params))

    def _send_and_wait_ack(self, frame: bytes):
        with self._lock:
            self._ser.write(frame)
            self._read_ack()

    def _read_ack(self):
        resp = self._read_frame()
        if resp is None:
            raise TimeoutError("No ACK received")
        cmd, payload = resp
        if cmd == CAT_CMD_ACK:
            result = parse_ack(payload)
            if result and not result[1]:
                raise RuntimeError(f"NAK for cmd 0x{result[0]:02X}")

    def _read_param_resp(self, expected_pid: int) -> int:
        resp = self._read_frame()
        if resp is None:
            raise TimeoutError("No PARAM_RESP received")
        cmd, payload = resp
        if cmd != CAT_CMD_PARAM_RESP:
            raise RuntimeError(f"Unexpected response cmd 0x{cmd:02X}")
        result = parse_param_resp(payload)
        if result is None:
            raise RuntimeError("Failed to parse PARAM_RESP")
        return result[1]

    def _read_status_resp(self) -> dict:
        resp = self._read_frame()
        if resp is None:
            raise TimeoutError("No STATUS_RESP received")
        cmd, payload = resp
        if cmd != CAT_CMD_STATUS_RESP:
            raise RuntimeError(f"Unexpected response cmd 0x{cmd:02X}")
        result = parse_status_resp(payload)
        if result is None:
            raise RuntimeError("Failed to parse STATUS_RESP")
        return result

    def _read_frame(self) -> Optional[tuple]:
        buf = b""
        deadline = time.time() + self._timeout
        while time.time() < deadline:
            chunk = self._ser.read(1)
            if not chunk:
                continue
            buf += chunk
            if len(buf) >= 4:
                result = parse_response(buf)
                if result is not None:
                    return result
                if len(buf) > 256:
                    buf = buf[-64:]
        return None

    # --- Heartbeat ---

    def _start_heartbeat(self):
        self._heartbeat_stop.clear()
        self._heartbeat_thread = threading.Thread(
            target=self._heartbeat_loop, daemon=True)
        self._heartbeat_thread.start()

    def _stop_heartbeat(self):
        self._heartbeat_stop.set()
        if self._heartbeat_thread:
            self._heartbeat_thread.join(timeout=3)
            self._heartbeat_thread = None

    def _heartbeat_loop(self):
        while not self._heartbeat_stop.wait(1.5):
            try:
                with self._lock:
                    if self._ser and self._ser.is_open:
                        self._ser.write(frame_noop())
            except Exception:
                break
