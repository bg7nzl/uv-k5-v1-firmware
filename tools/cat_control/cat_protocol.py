"""
CAT Control Protocol — frame builder and parser.

Frame format: SYNC(0xAB) CMD(1B) LEN(1B) PAYLOAD(0..N) CRC8(1B)
CRC8 = XOR of all bytes from SYNC through last payload byte.
"""

import struct
from typing import Optional, Tuple

SYNC = 0xAB

# CMD codes (0x10–0x1B)
CAT_CMD_ENTER       = 0x10
CAT_CMD_EXIT        = 0x11
CAT_CMD_SET_PARAM   = 0x12
CAT_CMD_GET_PARAM   = 0x13
CAT_CMD_PARAM_RESP  = 0x14
CAT_CMD_SET_MULTI   = 0x15
CAT_CMD_GET_ALL     = 0x16
CAT_CMD_ALL_RESP    = 0x17
CAT_CMD_APPLY       = 0x18
CAT_CMD_STATUS      = 0x19
CAT_CMD_STATUS_RESP = 0x1A
CAT_CMD_NOOP        = 0x1B
CAT_CMD_ACK         = 0x05

RESULT_OK  = 0x00
RESULT_ERR = 0x01

# Parameter IDs
PARAM_RX_FREQ       = 0x01
PARAM_TX_FREQ       = 0x02
PARAM_TX_OFFSET     = 0x03
PARAM_OFFSET_DIR    = 0x04
PARAM_RX_TONE_TYPE  = 0x05
PARAM_RX_TONE_CODE  = 0x06
PARAM_TX_TONE_TYPE  = 0x07
PARAM_TX_TONE_CODE  = 0x08
PARAM_MODULATION    = 0x09
PARAM_TX_POWER      = 0x0A
PARAM_BANDWIDTH     = 0x0B
PARAM_SQUELCH       = 0x0C
PARAM_VOX_SWITCH    = 0x0D
PARAM_VOX_LEVEL     = 0x0E
PARAM_VOX_DELAY     = 0x0F
PARAM_MIC_GAIN      = 0x10
PARAM_SPEAKER_GAIN  = 0x11
PARAM_DAC_GAIN      = 0x12
PARAM_COMPANDER     = 0x13
PARAM_SCRAMBLE      = 0x14
PARAM_BUSY_LOCK     = 0x15
PARAM_STEP          = 0x16
PARAM_MIC_BAR       = 0x17
PARAM_RSSI          = 0x18

PARAM_SIZES = {
    PARAM_RX_FREQ: 4, PARAM_TX_FREQ: 4, PARAM_TX_OFFSET: 4,
    PARAM_OFFSET_DIR: 1,
    PARAM_RX_TONE_TYPE: 1, PARAM_RX_TONE_CODE: 2,
    PARAM_TX_TONE_TYPE: 1, PARAM_TX_TONE_CODE: 2,
    PARAM_MODULATION: 1, PARAM_TX_POWER: 1, PARAM_BANDWIDTH: 1,
    PARAM_SQUELCH: 1,
    PARAM_VOX_SWITCH: 1, PARAM_VOX_LEVEL: 1, PARAM_VOX_DELAY: 1,
    PARAM_MIC_GAIN: 1, PARAM_SPEAKER_GAIN: 1, PARAM_DAC_GAIN: 1,
    PARAM_COMPANDER: 1, PARAM_SCRAMBLE: 1, PARAM_BUSY_LOCK: 1,
    PARAM_STEP: 1,
    PARAM_MIC_BAR: 1, PARAM_RSSI: 2,
}

PARAM_NAMES = {
    PARAM_RX_FREQ: "RX_FREQ", PARAM_TX_FREQ: "TX_FREQ",
    PARAM_TX_OFFSET: "TX_OFFSET", PARAM_OFFSET_DIR: "OFFSET_DIR",
    PARAM_RX_TONE_TYPE: "RX_TONE_TYPE", PARAM_RX_TONE_CODE: "RX_TONE_CODE",
    PARAM_TX_TONE_TYPE: "TX_TONE_TYPE", PARAM_TX_TONE_CODE: "TX_TONE_CODE",
    PARAM_MODULATION: "MODULATION", PARAM_TX_POWER: "TX_POWER",
    PARAM_BANDWIDTH: "BANDWIDTH", PARAM_SQUELCH: "SQUELCH",
    PARAM_VOX_SWITCH: "VOX_SWITCH", PARAM_VOX_LEVEL: "VOX_LEVEL",
    PARAM_VOX_DELAY: "VOX_DELAY",
    PARAM_MIC_GAIN: "MIC_GAIN", PARAM_SPEAKER_GAIN: "SPEAKER_GAIN",
    PARAM_DAC_GAIN: "DAC_GAIN",
    PARAM_COMPANDER: "COMPANDER", PARAM_SCRAMBLE: "SCRAMBLE",
    PARAM_BUSY_LOCK: "BUSY_LOCK", PARAM_STEP: "STEP",
    PARAM_MIC_BAR: "MIC_BAR", PARAM_RSSI: "RSSI",
}


def _crc8_xor(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
    return crc


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    header = bytes([SYNC, cmd, len(payload)])
    body = header + payload
    return body + bytes([_crc8_xor(body)])


def parse_response(data: bytes) -> Optional[Tuple[int, bytes]]:
    """Parse a single frame. Returns (cmd, payload) or None on error."""
    if len(data) < 4:
        return None
    if data[0] != SYNC:
        return None
    cmd = data[1]
    length = data[2]
    if len(data) < 3 + length + 1:
        return None
    payload = data[3:3 + length]
    expected_crc = _crc8_xor(data[:3 + length])
    if data[3 + length] != expected_crc:
        return None
    return (cmd, payload)


def _encode_value(param_id: int, value: int) -> bytes:
    size = PARAM_SIZES.get(param_id, 1)
    if size == 4:
        return struct.pack(">I", value)
    elif size == 2:
        return struct.pack(">H", value)
    else:
        return bytes([value & 0xFF])


def _decode_value(param_id: int, data: bytes) -> int:
    size = PARAM_SIZES.get(param_id, 1)
    if size == 4 and len(data) >= 4:
        return struct.unpack(">I", data[:4])[0]
    elif size == 2 and len(data) >= 2:
        return struct.unpack(">H", data[:2])[0]
    elif len(data) >= 1:
        return data[0]
    return 0


# Convenience frame builders

def frame_enter() -> bytes:
    return build_frame(CAT_CMD_ENTER)

def frame_exit() -> bytes:
    return build_frame(CAT_CMD_EXIT)

def frame_noop() -> bytes:
    return build_frame(CAT_CMD_NOOP)

def frame_apply() -> bytes:
    return build_frame(CAT_CMD_APPLY)

def frame_status() -> bytes:
    return build_frame(CAT_CMD_STATUS)

def frame_get_all() -> bytes:
    return build_frame(CAT_CMD_GET_ALL)

def frame_set_param(param_id: int, value: int) -> bytes:
    payload = bytes([param_id]) + _encode_value(param_id, value)
    return build_frame(CAT_CMD_SET_PARAM, payload)

def frame_get_param(param_id: int) -> bytes:
    return build_frame(CAT_CMD_GET_PARAM, bytes([param_id]))

def frame_set_multi(params: dict) -> bytes:
    """params: {param_id: value, ...}"""
    payload = bytes([len(params)])
    for pid, val in params.items():
        payload += bytes([pid]) + _encode_value(pid, val)
    return build_frame(CAT_CMD_SET_MULTI, payload)


def parse_param_resp(payload: bytes) -> Optional[Tuple[int, int]]:
    """Parse CAT_PARAM_RESP payload → (param_id, value)."""
    if len(payload) < 2:
        return None
    pid = payload[0]
    val = _decode_value(pid, payload[1:])
    return (pid, val)


def parse_status_resp(payload: bytes) -> Optional[dict]:
    """Parse CAT_STATUS_RESP payload → dict."""
    if len(payload) < 8:
        return None
    return {
        "tx_active":      bool(payload[0]),
        "rx_active":      bool(payload[1]),
        "rssi":           struct.unpack(">H", payload[2:4])[0],
        "battery_mv":     struct.unpack(">H", payload[4:6])[0],
        "vox_triggered":  bool(payload[6]),
        "temperature":    payload[7],
    }


def parse_ack(payload: bytes) -> Optional[Tuple[int, bool]]:
    """Parse ACK payload → (original_cmd, success)."""
    if len(payload) < 1:
        return None
    raw = payload[0]
    if raw & 0x80:
        return (raw & 0x7F, False)
    return (raw, True)


def freq_to_10hz(freq_mhz: float) -> int:
    """Convert MHz float to 10 Hz integer units."""
    return int(round(freq_mhz * 100000))


def freq_from_10hz(val: int) -> float:
    """Convert 10 Hz integer units to MHz float."""
    return val / 100000.0
