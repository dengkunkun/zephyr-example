#!/usr/bin/env python3
"""Simple serial monitor for CH341A-based USB UART adapters."""

from __future__ import annotations

import argparse
import os
import select
import sys
import termios
import threading
import tty
from dataclasses import dataclass
from pathlib import Path

import serial
from serial import Serial
from serial.tools import list_ports


@dataclass
class PortInfo:
    device: str
    description: str
    manufacturer: str | None
    product: str | None
    hwid: str


def collect_ports() -> list[PortInfo]:
    ports: list[PortInfo] = []
    for port in list_ports.comports():
        ports.append(
            PortInfo(
                device=port.device,
                description=port.description or "",
                manufacturer=port.manufacturer,
                product=port.product,
                hwid=port.hwid,
            )
        )
    return ports


def print_ports(ports: list[PortInfo]) -> None:
    if not ports:
        print("未检测到任何串口设备。")
        return

    print("检测到的串口设备：")
    for port in ports:
        labels = [port.description]
        if port.manufacturer:
            labels.append(f"manufacturer={port.manufacturer}")
        if port.product:
            labels.append(f"product={port.product}")
        labels.append(port.hwid)
        print(f"- {port.device}: {' | '.join(filter(None, labels))}")


def find_port(port_hint: str) -> str | None:
    hint = port_hint.lower()
    for port in collect_ports():
        candidates = [
            port.device,
            port.description,
            port.manufacturer or "",
            port.product or "",
            port.hwid,
        ]
        if any(hint in item.lower() for item in candidates if item):
            return port.device
    return None


def normalize_newline(mode: str) -> bytes:
    return {
        "none": b"",
        "lf": b"\n",
        "cr": b"\r",
        "crlf": b"\r\n",
    }[mode]


def reader_loop(ser: Serial, stop: threading.Event, log_file: Path | None) -> None:
    log_handle = None
    try:
        if log_file is not None:
            log_file.parent.mkdir(parents=True, exist_ok=True)
            log_handle = log_file.open("a", encoding="utf-8")

        while not stop.is_set():
            data = ser.read(ser.in_waiting or 1)
            if not data:
                continue

            text = data.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()

            if log_handle is not None:
                log_handle.write(text)
                log_handle.flush()
    finally:
        if log_handle is not None:
            log_handle.close()


def interactive_writer(ser: Serial, stop: threading.Event) -> None:
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        while not stop.is_set():
            readable, _, _ = select.select([sys.stdin], [], [], 0.1)
            if not readable:
                continue

            chunk = os.read(fd, 1)
            if not chunk:
                continue

            if chunk == b"\x03":
                raise KeyboardInterrupt

            ser.write(chunk)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)


def send_once(ser: Serial, payload: bytes) -> None:
    ser.write(payload)
    ser.flush()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CH341A 串口日志读写工具")
    parser.add_argument("--port", help="串口设备，例如 /dev/ttyUSB0")
    parser.add_argument("--port-hint", default="CH341A", help="自动查找串口时使用的关键字，默认 CH341A")
    parser.add_argument("--baud", type=int, default=115200, help="波特率，默认 115200")
    parser.add_argument("--list", action="store_true", help="仅列出当前串口设备")
    parser.add_argument("--log", type=Path, help="将接收到的日志追加保存到文件")
    parser.add_argument("--send", help="发送一次字符串后退出")
    parser.add_argument(
        "--newline",
        choices=["none", "lf", "cr", "crlf"],
        default="none",
        help="发送内容后附加的换行类型，默认 none",
    )
    parser.add_argument("--timeout", type=float, default=0.1, help="串口超时时间，默认 0.1 秒")
    return parser.parse_args()


def resolve_port(args: argparse.Namespace) -> str:
    if args.port:
        return args.port

    found = find_port(args.port_hint)
    if found:
        print(f"已找到匹配串口：{found} (hint={args.port_hint})")
        return found

    print(f"未找到名称包含 '{args.port_hint}' 的串口。", file=sys.stderr)
    print_ports(collect_ports())
    raise SystemExit(1)


def main() -> int:
    args = parse_args()
    ports = collect_ports()

    if args.list:
        print_ports(ports)
        return 0

    port = resolve_port(args)
    newline = normalize_newline(args.newline)

    with serial.Serial(
        port=port,
        baudrate=args.baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=args.timeout,
    ) as ser:
        print(f"已打开串口 {port}，参数: {args.baud} 8N1")

        if args.send is not None:
            send_once(ser, args.send.encode("utf-8") + newline)
            return 0

        stop = threading.Event()
        reader = threading.Thread(target=reader_loop, args=(ser, stop, args.log), daemon=True)
        reader.start()

        try:
            interactive_writer(ser, stop)
        except KeyboardInterrupt:
            print("\n收到 Ctrl+C，退出串口终端。")
        finally:
            stop.set()
            reader.join(timeout=1.0)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())