#!/usr/bin/env python3
"""
CAT Control CLI — interactive command-line tool for radio control.

Usage:
    python cat_cli.py --port /dev/ttyUSB0 [--baud 38400]
"""

import argparse
import sys

from cat_radio import CatRadio
from cat_protocol import freq_to_10hz, freq_from_10hz, PARAM_NAMES


def main():
    parser = argparse.ArgumentParser(description="CAT Radio Control CLI")
    parser.add_argument("--port", "-p", required=True, help="Serial port")
    parser.add_argument("--baud", "-b", type=int, default=38400, help="Baud rate")
    args = parser.parse_args()

    radio = CatRadio(args.port, args.baud)
    try:
        radio.connect()
        print(f"Connected to {args.port} @ {args.baud}")
        print("Type 'help' for commands, 'quit' to exit.")
        print()
    except Exception as e:
        print(f"Failed to connect: {e}")
        sys.exit(1)

    try:
        while True:
            try:
                line = input("CAT> ").strip()
            except EOFError:
                break
            if not line:
                continue

            parts = line.split()
            cmd = parts[0].lower()

            try:
                if cmd in ("quit", "exit", "q"):
                    break

                elif cmd == "help":
                    print_help()

                elif cmd == "freq" and len(parts) >= 2:
                    freq = float(parts[1])
                    radio.set_rx_frequency(freq)
                    radio.apply()
                    print(f"  RX frequency set to {freq:.6f} MHz")

                elif cmd == "txfreq" and len(parts) >= 2:
                    freq = float(parts[1])
                    radio.set_tx_frequency(freq)
                    radio.apply()
                    print(f"  TX frequency set to {freq:.6f} MHz")

                elif cmd == "offset" and len(parts) >= 2:
                    val = parts[1]
                    if val.startswith("+"):
                        direction = "+"
                        offset = float(val[1:])
                    elif val.startswith("-"):
                        direction = "-"
                        offset = float(val[1:])
                    else:
                        direction = "none"
                        offset = float(val)
                    radio.set_offset(offset, direction)
                    radio.apply()
                    print(f"  TX offset set to {direction}{offset:.3f} MHz")

                elif cmd == "power" and len(parts) >= 2:
                    level = int(parts[1])
                    radio.set_power(level)
                    radio.apply()
                    names = ["LOW1", "LOW2", "LOW3", "LOW4", "LOW5", "MID", "HIGH"]
                    name = names[level] if level < len(names) else str(level)
                    print(f"  TX power set to {name}")

                elif cmd == "vox" and len(parts) >= 2:
                    if parts[1].lower() in ("off", "0"):
                        radio.set_vox(False)
                        radio.apply()
                        print("  VOX disabled")
                    else:
                        level = int(parts[1]) if parts[1].lower() != "on" else 3
                        delay = int(parts[2]) if len(parts) > 2 else 10
                        radio.set_vox(True, level, delay)
                        radio.apply()
                        print(f"  VOX enabled, level={level}, delay={delay * 100}ms")

                elif cmd == "squelch" and len(parts) >= 2:
                    level = int(parts[1])
                    radio.set_squelch(level)
                    radio.apply()
                    print(f"  Squelch set to {level}")

                elif cmd == "mic" and len(parts) >= 2:
                    level = int(parts[1])
                    radio.set_mic_gain(level)
                    radio.apply()
                    print(f"  MIC gain set to level {level}")

                elif cmd == "speaker" and len(parts) >= 2:
                    level = int(parts[1])
                    radio.set_speaker_gain(level)
                    radio.apply()
                    print(f"  Speaker gain set to {level}")

                elif cmd == "bw" and len(parts) >= 2:
                    narrow = parts[1].lower() in ("n", "narrow", "1")
                    radio.set_bandwidth(narrow)
                    radio.apply()
                    print(f"  Bandwidth set to {'narrow' if narrow else 'wide'}")

                elif cmd == "ctcss" and len(parts) >= 3:
                    direction = parts[1].lower()
                    code = int(parts[2])
                    if direction == "tx":
                        radio.set_tx_ctcss(code)
                    elif direction == "rx":
                        radio.set_rx_ctcss(code)
                    radio.apply()
                    print(f"  {direction.upper()} CTCSS set to code index {code}")

                elif cmd == "status":
                    status = radio.get_status()
                    print(f"  TX: {'ON' if status['tx_active'] else 'OFF'}  "
                          f"RX: {'ON' if status['rx_active'] else 'OFF'}  "
                          f"RSSI: {status['rssi']}  "
                          f"Battery: {status['battery_mv']}mV  "
                          f"VOX: {'triggered' if status['vox_triggered'] else 'idle'}")

                elif cmd == "apply":
                    radio.apply()
                    print("  Parameters applied to hardware")

                else:
                    print(f"  Unknown command: {cmd}. Type 'help' for usage.")

            except Exception as e:
                print(f"  Error: {e}")

    finally:
        print("Exiting CAT mode...")
        radio.disconnect()


def print_help():
    print("""
Available commands:
  freq <MHz>           Set RX frequency (e.g. freq 145.500)
  txfreq <MHz>         Set TX frequency directly
  offset <+/-MHz>      Set TX offset (e.g. offset +0.600)
  power <0-6>          Set TX power (0=LOW1 ... 6=HIGH)
  vox <off|level> [delay]  VOX control (e.g. vox 3 10)
  squelch <0-9>        Set squelch level
  mic <0-4>            Set MIC gain
  speaker <0-15>       Set speaker gain
  bw <wide|narrow>     Set bandwidth
  ctcss <tx|rx> <code> Set CTCSS tone code index
  status               Query radio status
  apply                Apply pending changes to hardware
  help                 Show this help
  quit                 Exit
""")


if __name__ == "__main__":
    main()
