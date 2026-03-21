#!/usr/bin/env python3
"""
Exercise all PC→radio digimode commands (k5-v5 app/digmode.h) against real hardware.

Does not replace FT8 scripts; use together with ft8_send_batch.py / ft8_send_symbols.py.
"""

import argparse
import statistics
import struct
import sys
import time

import serial

SYNC = 0xAB


def xor_crc(data: bytes) -> int:
    c = 0
    for b in data:
        c ^= b
    return c


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    hdr = bytes([SYNC, cmd, len(payload)])
    body = hdr + payload
    return body + bytes([xor_crc(body)])


def parse_one(data: bytearray):
    while len(data) >= 4:
        if data[0] != SYNC:
            idx = data.find(bytes([SYNC]), 1)
            if idx < 0:
                return None, data
            data = data[idx:]
            continue
        length = data[2]
        fs = 3 + length + 1
        if len(data) < fs:
            return None, data
        if xor_crc(memoryview(data)[: fs - 1]) != data[fs - 1]:
            data = data[1:]
            continue
        cmd = data[1]
        payload = bytes(data[3 : 3 + length])
        rest = data[fs:]
        return (cmd, payload), rest
    return None, data


def drain_ack(ser, timeout=0.3):
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < deadline:
        buf += ser.read(128)
        while True:
            r, buf = parse_one(buf)
            if r is None:
                break
            cmd, pl = r
            if cmd == 0x05 and len(pl) >= 6:
                orig = pl[0]
                ok = sum(1 for b in pl[1:6] if b == 0) >= 3
                return orig, ok
    return None, None


def collect_acks(ser, timeout=0.5):
    """Collect all ACK frames in order within timeout."""
    deadline = time.monotonic() + timeout
    buf = bytearray()
    out = []
    while time.monotonic() < deadline:
        buf += ser.read(128)
        while True:
            r, buf = parse_one(buf)
            if r is None:
                break
            cmd, pl = r
            if cmd == 0x05 and len(pl) >= 6:
                orig = pl[0]
                ok = sum(1 for b in pl[1:6] if b == 0) >= 3
                out.append((orig, ok))
    return out


def sync_pc_to_radio(ser, now_pc, rounds=7):
    """NTP-style offset: radio_us ≈ (pc_us + offset) & 0xffffffff (same idea as ft8_*.py)."""
    offsets, rtts = [], []

    for _ in range(rounds):
        t1 = now_pc()
        ser.reset_input_buffer()
        ser.write(build_frame(0x06, struct.pack(">I", t1)))
        buf = bytearray()
        deadline = time.monotonic() + 0.5
        got = None
        while time.monotonic() < deadline:
            buf += ser.read(32)
            while True:
                r, buf = parse_one(buf)
                if r is None:
                    break
                cmd, payload = r
                if cmd == 0x07 and len(payload) >= 8:
                    got = (payload, now_pc())
                    break
            if got:
                break
        if not got:
            continue
        payload, t3 = got
        echo = struct.unpack(">I", payload[:4])[0]
        t2 = struct.unpack(">I", payload[4:8])[0]
        if echo != t1:
            continue
        rtt = (t3 - t1) & 0xFFFFFFFF
        if rtt > 0x7FFFFFFF:
            rtt = 0x100000000 - rtt
        off = (t2 - t1 - rtt // 2) & 0xFFFFFFFF
        if off > 0x7FFFFFFF:
            off -= 0x100000000
        offsets.append(off)
        rtts.append(rtt)
        time.sleep(0.03)
    if len(offsets) < 2:
        return 0, 0
    return int(statistics.median(offsets)), int(statistics.median(rtts))


def main():
    ap = argparse.ArgumentParser(description="Digimode UART protocol self-test")
    ap.add_argument("-p", "--port", default="/dev/ttyUSB0")
    ap.add_argument("-b", "--baud", type=int, default=38400)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.15)
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    t0 = time.monotonic()

    def now_pc():
        return int((time.monotonic() - t0) * 1_000_000) & 0xFFFFFFFF

    print("[0] Clock sync (SYNC_REQ × N)")
    clk_off, clk_rtt = sync_pc_to_radio(ser, now_pc)
    if clk_off != 0 or clk_rtt != 0:
        print(f"  OK  offset={clk_off} us  RTT={clk_rtt} us")
    else:
        print("  WARN  sync failed — deferred SCHED_TX may be off")

    def to_radio(pc_us):
        return (pc_us + clk_off) & 0xFFFFFFFF

    fails = 0

    def check(name, cond, detail=""):
        nonlocal fails
        if cond:
            print(f"  OK  {name}" + (f"  {detail}" if detail else ""))
        else:
            fails += 1
            print(f"  FAIL {name}" + (f"  {detail}" if detail else ""))

    # --- STATUS (0x04) — radio may reply with CMD 0x04 STATUS echo + 3B payload ---
    print("[1] STATUS")
    ser.reset_input_buffer()
    ser.write(build_frame(0x04))
    time.sleep(0.08)
    raw = ser.read(64)
    r, _ = parse_one(bytearray(raw))
    if r and r[0] == 0x04 and len(r[1]) == 3:
        check("STATUS response", True, raw.hex())
    else:
        check("STATUS response", False, raw.hex() if raw else "(empty)")

    # --- NOOP (0x08) — firmware sends no reply ---
    print("[2] NOOP (expect no reply)")
    ser.reset_input_buffer()
    ser.write(build_frame(0x08))
    time.sleep(0.08)
    raw = ser.read(64)
    check("NOOP silent", len(raw) == 0, f"got {len(raw)} B")

    # --- SYNC_REQ (0x06) / SYNC_RESP (0x07) ---
    print("[3] SYNC_REQ → SYNC_RESP")
    ser.reset_input_buffer()
    t1 = int(time.monotonic() * 1_000_000) & 0xFFFFFFFF
    ser.write(build_frame(0x06, struct.pack(">I", t1)))
    time.sleep(0.08)
    raw = ser.read(64)
    r, _ = parse_one(bytearray(raw))
    if r and r[0] == 0x07 and len(r[1]) >= 8:
        echo = struct.unpack(">I", r[1][:4])[0]
        check("SYNC_RESP", echo == t1, raw.hex())
    else:
        check("SYNC_RESP", False, raw.hex() if raw else "(empty)")

    # --- SCHED_TX empty (0x09, len=13, N=0) — ACK only, no TX ---
    print("[4] SCHED_TX empty (RX retune, no TX)")
    base_10hz = 14_417_400  # 144.174 MHz in 10 Hz units
    payload = struct.pack(">I", base_10hz)
    payload += struct.pack(">I", 160_000)
    payload += struct.pack("B", 0xFF)
    payload += struct.pack(">I", 0)
    assert len(payload) == 13
    ser.reset_input_buffer()
    ser.write(build_frame(0x09, payload))
    o, ok = drain_ack(ser)
    check("SCHED_TX ACK", o == 0x09 and ok, f"orig={o} ok={ok}")

    # --- Bad CRC → ACK ERR ---
    print("[5] Invalid CRC → ACK ERR")
    bad = bytearray(build_frame(0x04))
    bad[-1] ^= 0x55
    ser.reset_input_buffer()
    ser.write(bytes(bad))
    o, ok = drain_ack(ser)
    check("NAK on bad CRC", o == 0x04 and ok is False, f"orig={o} ok={ok}")

    # --- START_TX (0x01) / STOP_TX (0x02) — brief carrier, lowest power ---
    print("[6] START_TX (LOW1) → STATUS → STOP_TX")
    ser.reset_input_buffer()
    start_pl = struct.pack(">I", base_10hz) + struct.pack("B", 1)  # LOW1
    ser.write(build_frame(0x01, start_pl))
    o, ok = drain_ack(ser, 0.5)
    check("START_TX ACK", o == 0x01 and ok, f"orig={o} ok={ok}")

    ser.reset_input_buffer()
    ser.write(build_frame(0x04))
    time.sleep(0.05)
    raw = ser.read(64)
    r, _ = parse_one(bytearray(raw))
    tx_on = r and r[0] == 0x04 and len(r[1]) >= 1 and r[1][0] == 1
    check("STATUS tx_active=1", tx_on, raw.hex() if raw else "")

    ser.reset_input_buffer()
    ser.write(build_frame(0x02))
    o, ok = drain_ack(ser, 0.5)
    check("STOP_TX ACK", o == 0x02 and ok, f"orig={o} ok={ok}")

    # --- SET_FREQ (0x03) — need active TX; 5× freq + apply_at ---
    print("[7] START_TX → SET_FREQ (next slot) → STOP_TX")
    ser.reset_input_buffer()
    ser.write(build_frame(0x01, struct.pack(">I", base_10hz) + struct.pack("B", 1)))
    drain_ack(ser, 0.5)

    # Vernier audio offset: 15000 = 1500.0 Hz in 0.1 Hz units
    freq_dhz = 15000
    apply_at = 0  # past → firmware applies without long wait (uint32 timeline)
    set_pl = struct.pack(">H", freq_dhz) * 5 + struct.pack(">I", apply_at)
    ser.reset_input_buffer()
    ser.write(build_frame(0x03, set_pl))
    time.sleep(0.05)
    # SET_FREQ does not send a separate ACK in firmware (only errors on CRC path)
    ser.write(build_frame(0x02))
    o, ok = drain_ack(ser, 0.5)
    check("STOP after SET_FREQ", o == 0x02 and ok, f"orig={o} ok={ok}")

    # --- SCHED_TX 2 tones immediate (short on-air) ---
    print("[8] SCHED_TX 2 symbols @ 160ms (immediate)")
    f0 = 15000
    f1 = 15062  # +6.25 Hz * 10 deci
    payload = struct.pack(">I", base_10hz)
    payload += struct.pack(">I", 160_000)
    payload += struct.pack("B", 1)
    payload += struct.pack(">I", 0)
    payload += struct.pack(">HH", f0, f1)
    ser.reset_input_buffer()
    ser.write(build_frame(0x09, payload))
    # Immediate SCHED_TX: START_TX ACK then SCHED_TX ACK (often one USB read)
    # START_TX + SCHED_TX ACKs, then STOP_TX ACK when the 2-symbol schedule ends
    acks = collect_acks(ser, 0.55)
    seq_ok = acks == [(0x01, True), (0x09, True), (0x02, True)]
    check("SCHED_TX(2) ACKs", seq_ok, str(acks))
    time.sleep(0.45)

    # --- SCHED_APP (0x0A): extend buffer while waiting ---
    print("[9] SCHED_TX deferred + SCHED_APP")
    ser.reset_input_buffer()
    # Radio-local microsecond timestamp (requires [0] sync)
    start_at = to_radio(now_pc() + 800_000)
    payload = struct.pack(">I", base_10hz)
    payload += struct.pack(">I", 100_000)
    payload += struct.pack("B", 1)
    payload += struct.pack(">I", start_at)
    payload += struct.pack(">H", f0)
    ser.write(build_frame(0x09, payload))
    o, ok = drain_ack(ser, 0.5)
    check("SCHED_TX(defer) ACK", o == 0x09 and ok, f"orig={o} ok={ok}")

    ser.reset_input_buffer()
    ser.write(build_frame(0x0A, struct.pack(">H", f1)))
    o, ok = drain_ack(ser, 0.5)
    check("SCHED_APP ACK", o == 0x0A and ok, f"orig={o} ok={ok}")
    time.sleep(1.2)

    ser.close()
    print()
    if fails:
        print(f"Self-test finished: {fails} failure(s)")
        sys.exit(1)
    print("Self-test finished: all checks passed")
    sys.exit(0)


if __name__ == "__main__":
    main()
