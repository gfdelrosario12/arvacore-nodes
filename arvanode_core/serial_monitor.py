"""
arvanode_core.serial_monitor - Dedicated Standalone Serial Monitor & Hardware Controller
ArvaCore Smart Irrigation Ecosystem (2026)

Features:
- Pure-Python implementation: Operates without requiring Arduino IDE or Arduino CLI.
- Zero-Dependency Fallback: Automatically uses native POSIX termios/fcntl when pyserial
  is not installed, ensuring immediate out-of-the-box operation on Linux/macOS.
- Dynamic Port Detection: Automatically resolves connected ESP32 boards (CP2102, CH340, FTDI).
- Hardware ESP32 Reset: Direct control of DTR/RTS lines to pulse EN/IO0 for software-triggered reboots.
- Colorized Syntax Highlighting: Automatic ANSI formatting for ArvaNode firmware tags:
  [BOOT], [PAIRING], [TELEMETRY], [INFO], [WARN], [ERROR].
- Multi-threaded Streaming: Non-blocking background worker with line & byte event callbacks.
- Interactive Terminal Console: Send text/commands, toggle timestamps, and handle hotkeys.
"""

import fcntl
import glob
import os
import re
import select
import struct
import sys
import termios
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple, Union

# Optional PySerial import
try:
    import serial
    HAS_PYSERIAL = True
except ImportError:
    serial = None
    HAS_PYSERIAL = False

# Standard Baud Rates
SUPPORTED_BAUDS = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]
DEFAULT_MONITOR_BAUD = 115200

# Termios baud mapping
TERMIOS_BAUD_MAP = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: termios.B230400,
    460800: termios.B460800,
    921600: termios.B921600,
}

# ANSI Color Codes
COLOR_RESET = "\033[0m"
COLOR_DIM = "\033[2m"
COLOR_BOLD = "\033[1m"
COLOR_CYAN = "\033[36m"
COLOR_GREEN = "\033[32m"
COLOR_YELLOW = "\033[33m"
COLOR_RED = "\033[31m"
COLOR_MAGENTA = "\033[35m"
COLOR_BLUE = "\033[34m"
COLOR_WHITE = "\033[37m"
COLOR_BG_RED = "\033[41m\033[97m"


def discover_esp32_ports() -> List[Dict[str, Any]]:
    """
    Scans Linux /dev and /sys for USB serial converters without external dependencies.
    Identifies CP210x, CH340, FTDI, and CDC ACM chips typically found on ESP32 boards.
    """
    devices = []
    by_id_map: Dict[str, str] = {}
    by_id_dir = Path("/dev/serial/by-id")
    if by_id_dir.exists():
        for link in by_id_dir.iterdir():
            try:
                target = link.resolve()
                by_id_map[str(target)] = link.name
            except Exception:
                pass

    patterns = ["/dev/ttyUSB*", "/dev/ttyACM*"]
    found_paths: List[str] = []
    for pat in patterns:
        found_paths.extend(glob.glob(pat))

    for dev_path in sorted(found_paths):
        dev_name = Path(dev_path).name
        sys_path = Path(f"/sys/class/tty/{dev_name}/device")
        desc = by_id_map.get(dev_path, "USB Serial Device")
        driver_name = "unknown"

        if sys_path.exists():
            driver_link = sys_path / "driver"
            if driver_link.exists():
                try:
                    driver_name = driver_link.resolve().name
                except Exception:
                    pass

        chip_type = "Unknown ESP32/UART"
        if "cp210" in driver_name.lower() or "cp210" in desc.lower():
            chip_type = "CP2102/CP2104 USB-UART (ESP32 Standard)"
        elif "ch34" in driver_name.lower() or "ch34" in desc.lower():
            chip_type = "CH340/CH341 USB-UART (ESP32 NodeMCU Standard)"
        elif "ftdi" in driver_name.lower() or "ftdi" in desc.lower():
            chip_type = "FTDI USB-UART"
        elif "acm" in dev_name.lower():
            chip_type = "Native USB CDC / ESP32-S2/S3"

        readable = os.access(dev_path, os.R_OK)
        writable = os.access(dev_path, os.W_OK)

        devices.append({
            "port": dev_path,
            "description": desc,
            "driver": driver_name,
            "chip_type": chip_type,
            "readable": readable,
            "writable": writable,
        })

    return devices


def colorize_log_line(line: str) -> str:
    """Applies ANSI syntax highlighting based on firmware log prefixes."""
    line_trimmed = line.rstrip("\r\n")

    # Match ArvaNode firmware prefixes
    if "[BOOT]" in line_trimmed:
        return f"{COLOR_CYAN}{COLOR_BOLD}[BOOT]{COLOR_RESET} {line_trimmed.replace('[BOOT]', '').strip()}"
    elif "[PAIRING]" in line_trimmed:
        return f"{COLOR_YELLOW}{COLOR_BOLD}[PAIRING]{COLOR_RESET} {line_trimmed.replace('[PAIRING]', '').strip()}"
    elif "[TELEMETRY]" in line_trimmed:
        return f"{COLOR_GREEN}{COLOR_BOLD}[TELEMETRY]{COLOR_RESET} {line_trimmed.replace('[TELEMETRY]', '').strip()}"
    elif "[ERROR]" in line_trimmed or "FAIL" in line_trimmed:
        return f"{COLOR_RED}{COLOR_BOLD}{line_trimmed}{COLOR_RESET}"
    elif "[WARN]" in line_trimmed or "WARNING" in line_trimmed:
        return f"{COLOR_MAGENTA}{line_trimmed}{COLOR_RESET}"
    elif "[INFO]" in line_trimmed:
        return f"{COLOR_BLUE}{COLOR_BOLD}[INFO]{COLOR_RESET} {line_trimmed.replace('[INFO]', '').strip()}"
    elif "=====" in line_trimmed or "-----" in line_trimmed:
        return f"{COLOR_DIM}{line_trimmed}{COLOR_RESET}"
    return line_trimmed


class SerialMonitor:
    """
    Dedicated serial monitor driver supporting native POSIX termios or PySerial.
    Handles streaming, line extraction, callbacks, and hardware reset pulses.
    """

    def __init__(
        self,
        port: Optional[str] = None,
        baudrate: int = DEFAULT_MONITOR_BAUD,
        use_pyserial: bool = False
    ):
        self.port = port
        self.baudrate = baudrate
        self.use_pyserial = use_pyserial and HAS_PYSERIAL

        self.fd: Optional[int] = None
        self.ser_obj: Any = None
        self.orig_termios: Optional[List[Any]] = None

        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._line_buffer = ""
        self._lock = threading.Lock()

    def is_open(self) -> bool:
        return self.fd is not None or (self.ser_obj is not None and self.ser_obj.is_open)

    def open(self):
        """Opens and configures the serial port."""
        if not self.port:
            devices = discover_esp32_ports()
            if not devices:
                raise FileNotFoundError(
                    "No ESP32 serial devices detected. Connect board over USB."
                )
            self.port = devices[0]["port"]

        if not os.path.exists(self.port):
            raise FileNotFoundError(f"Serial port '{self.port}' not found.")

        if not os.access(self.port, os.R_OK | os.W_OK):
            raise PermissionError(
                f"Permission denied for port '{self.port}'. Run 'sudo usermod -a -G dialout $USER'."
            )

        if self.use_pyserial and HAS_PYSERIAL:
            self._open_pyserial()
        else:
            self._open_native_termios()

    def _open_pyserial(self):
        self.ser_obj = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            timeout=0.1,
            write_timeout=1.0
        )
        self.ser_obj.dtr = False
        self.ser_obj.rts = False

    def _open_native_termios(self):
        if self.baudrate not in TERMIOS_BAUD_MAP:
            raise ValueError(f"Baud rate {self.baudrate} is not supported by termios.")

        speed = TERMIOS_BAUD_MAP[self.baudrate]

        # Open non-blocking, no controlling tty
        self.fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

        # Configure termios raw 8N1
        self.orig_termios = termios.tcgetattr(self.fd)
        new_attr = list(self.orig_termios)

        # iflag: Disable break, parity, strip, CR-to-NL, software flow control
        new_attr[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                         termios.ISTRIP | termios.INLCR | termios.IGNCR |
                         termios.ICRNL | termios.IXON | termios.IXOFF | termios.IXANY)

        # oflag: Disable post-processing
        new_attr[1] &= ~termios.OPOST

        # cflag: 8 data bits, enable receiver, ignore modem control lines, 1 stop bit, no parity
        new_attr[2] &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.CRTSCTS)
        new_attr[2] |= (termios.CS8 | termios.CREAD | termios.CLOCAL)

        # lflag: Raw mode, no echo, no canonical mode, no extended input processing, no signals
        new_attr[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                         termios.ISIG | termios.IEXTEN)

        # VMIN=0, VTIME=0: non-blocking read
        new_attr[6][termios.VMIN] = 0
        new_attr[6][termios.VTIME] = 0

        # Set baud: Python's termios exposes ispeed/ospeed as list slots 4 and 5
        # (there is no cfsetispeed/cfsetospeed in the stdlib module).
        new_attr[4] = speed
        new_attr[5] = speed

        termios.tcsetattr(self.fd, termios.TCSANOW, new_attr)

        # Initial DTR/RTS clear (normal execution)
        self._set_dtr_native(False)
        self._set_rts_native(False)

    def close(self):
        """Closes the serial connection and restores attributes."""
        self.stop_monitoring()
        with self._lock:
            if self.ser_obj:
                try:
                    self.ser_obj.close()
                except Exception:
                    pass
                self.ser_obj = None

            if self.fd is not None:
                if self.orig_termios:
                    try:
                        termios.tcsetattr(self.fd, termios.TCSANOW, self.orig_termios)
                    except Exception:
                        pass
                try:
                    os.close(self.fd)
                except Exception:
                    pass
                self.fd = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def set_dtr(self, state: bool):
        """Sets DTR line state."""
        if self.ser_obj:
            self.ser_obj.dtr = state
        elif self.fd is not None:
            self._set_dtr_native(state)

    def set_rts(self, state: bool):
        """Sets RTS line state."""
        if self.ser_obj:
            self.ser_obj.rts = state
        elif self.fd is not None:
            self._set_rts_native(state)

    def _set_dtr_native(self, state: bool):
        if self.fd is None:
            return
        bit = struct.pack("I", termios.TIOCM_DTR)
        cmd = termios.TIOCMBIS if state else termios.TIOCMBIC
        try:
            fcntl.ioctl(self.fd, cmd, bit)
        except Exception:
            pass

    def _set_rts_native(self, state: bool):
        if self.fd is None:
            return
        bit = struct.pack("I", termios.TIOCM_RTS)
        cmd = termios.TIOCMBIS if state else termios.TIOCMBIC
        try:
            fcntl.ioctl(self.fd, cmd, bit)
        except Exception:
            pass

    def reset_esp32(self, hold_ms: int = 120):
        """
        Executes hardware reset sequence for ESP32 boards:
        Pulses EN (RTS) low while holding IO0 (DTR) high, then releases EN.
        This reboots the microcontroller cleanly into normal execution mode.
        """
        if not self.is_open():
            return

        # RTS=True pulls EN low (Reset asserted); DTR=False keeps IO0 high (Normal Boot)
        self.set_dtr(False)
        self.set_rts(True)
        time.sleep(hold_ms / 1000.0)

        # Release EN (RTS=False) to allow chip to boot
        self.set_rts(False)
        time.sleep(0.05)

    def write(self, data: Union[bytes, str]) -> int:
        """Sends data over the serial interface."""
        if not self.is_open():
            raise IOError("Serial port is not open.")

        if isinstance(data, str):
            payload = data.encode("utf-8")
        else:
            payload = data

        with self._lock:
            if self.ser_obj:
                return self.ser_obj.write(payload)
            elif self.fd is not None:
                return os.write(self.fd, payload)
        return 0

    def send_line(self, line: str, newline: str = "\r\n") -> int:
        """Sends a text line with specified newline characters."""
        return self.write(line + newline)

    def read_raw(self, max_bytes: int = 2048, timeout_sec: float = 0.05) -> bytes:
        """Non-blocking raw read from serial interface."""
        if not self.is_open():
            return b""

        if self.ser_obj:
            return self.ser_obj.read(max_bytes)

        if self.fd is None:
            return b""

        r, _, _ = select.select([self.fd], [], [], timeout_sec)
        if r:
            try:
                return os.read(self.fd, max_bytes)
            except (BlockingIOError, OSError):
                return b""
        return b""

    def read_lines(self, max_bytes: int = 2048, timeout_sec: float = 0.05) -> List[str]:
        """Reads and parses complete newline-delimited lines from serial stream."""
        raw = self.read_raw(max_bytes=max_bytes, timeout_sec=timeout_sec)
        if not raw:
            return []

        text = raw.decode("utf-8", errors="replace")
        self._line_buffer += text

        lines = []
        while "\n" in self._line_buffer:
            line, self._line_buffer = self._line_buffer.split("\n", 1)
            lines.append(line.rstrip("\r"))

        return lines

    def start_monitoring(
        self,
        on_line: Optional[Callable[[str], None]] = None,
        on_raw: Optional[Callable[[bytes], None]] = None,
        on_error: Optional[Callable[[Exception], None]] = None
    ):
        """Starts asynchronous background thread reading from serial port."""
        if self._running:
            return

        if not self.is_open():
            self.open()

        self._running = True

        def _worker():
            buffer = ""
            while self._running:
                try:
                    raw = self.read_raw(max_bytes=1024, timeout_sec=0.05)
                    if raw:
                        if on_raw:
                            try:
                                on_raw(raw)
                            except Exception:
                                pass

                        text = raw.decode("utf-8", errors="replace")
                        buffer += text
                        while "\n" in buffer:
                            line, buffer = buffer.split("\n", 1)
                            clean_line = line.rstrip("\r")
                            if on_line:
                                try:
                                    on_line(clean_line)
                                except Exception:
                                    pass
                    else:
                        time.sleep(0.01)
                except Exception as ex:
                    if self._running:
                        if on_error:
                            try:
                                on_error(ex)
                            except Exception:
                                pass
                    break

        self._thread = threading.Thread(target=_worker, daemon=True, name="ArvaSerialMonitorWorker")
        self._thread.start()

    def stop_monitoring(self):
        """Stops background reader thread."""
        self._running = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1.0)
        self._thread = None

    def run_console(
        self,
        show_timestamps: bool = True,
        colorize: bool = True,
        auto_reset_on_start: bool = True
    ):
        """
        Interactive terminal monitor runner.
        Provides ANSI colorized output, timestamps, and key controls:
          Ctrl+C: Quit monitor
          Ctrl+R: Reset ESP32 hardware
          Ctrl+L: Clear screen
        """
        if not self.is_open():
            self.open()

        print("=" * 80)
        print(f"📡 ARVACORE DEDICATED SERIAL MONITOR")
        print(f"Port:       {self.port}")
        print(f"Baud Rate:  {self.baudrate}")
        print(f"Driver:     {'PySerial' if self.use_pyserial else 'Native POSIX (termios)'}")
        print(f"Controls:   [Ctrl+C] Quit | [Ctrl+R] Reset ESP32 | [Ctrl+L] Clear Screen")
        print("=" * 80)

        if auto_reset_on_start:
            print(f"{COLOR_DIM}[MONITOR] Pulsing hardware reset (EN) line...{COLOR_RESET}")
            self.reset_esp32()

        # Check if stdin is a TTY for interactive raw character handling
        is_tty = sys.stdin.isatty()
        old_stdin_attr = None
        if is_tty:
            try:
                old_stdin_attr = termios.tcgetattr(sys.stdin.fileno())
                tty_raw = list(old_stdin_attr)
                tty_raw[3] &= ~(termios.ICANON | termios.ECHO)
                tty_raw[6][termios.VMIN] = 1
                tty_raw[6][termios.VTIME] = 0
                termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, tty_raw)
            except Exception:
                old_stdin_attr = None

        self._running = True
        line_buf = ""

        try:
            while self._running:
                readable = [self.fd] if self.fd is not None else []
                if is_tty:
                    readable.append(sys.stdin.fileno())

                r, _, _ = select.select(readable, [], [], 0.05)

                for src in r:
                    # Serial input from ESP32
                    if self.fd is not None and src == self.fd:
                        try:
                            raw = os.read(self.fd, 1024)
                        except (BlockingIOError, OSError):
                            raw = b""

                        if raw:
                            text = raw.decode("utf-8", errors="replace")
                            line_buf += text
                            while "\n" in line_buf:
                                line, line_buf = line_buf.split("\n", 1)
                                clean_line = line.rstrip("\r")

                                out_str = clean_line
                                if colorize:
                                    out_str = colorize_log_line(out_str)

                                if show_timestamps:
                                    ts = datetime.now().strftime("%H:%M:%S.%f")[:12]
                                    print(f"{COLOR_DIM}[{ts}]{COLOR_RESET} {out_str}")
                                else:
                                    print(out_str)
                                sys.stdout.flush()

                    # Keyboard input from user
                    elif is_tty and src == sys.stdin.fileno():
                        try:
                            ch = sys.stdin.read(1)
                        except Exception:
                            ch = ""

                        if not ch:
                            continue

                        # Ctrl+C (ETX)
                        if ch == "\x03":
                            print(f"\n{COLOR_DIM}[MONITOR] Disconnected by operator (Ctrl+C).{COLOR_RESET}")
                            self._running = False
                            break

                        # Ctrl+R (DC2) -> Reset ESP32
                        elif ch == "\x12":
                            print(f"\n{COLOR_BG_RED} [ESP32 HARDWARE RESET TRIGGERED] {COLOR_RESET}")
                            self.reset_esp32()

                        # Ctrl+L (FF) -> Clear terminal
                        elif ch == "\x0c":
                            sys.stdout.write("\033[2J\033[H")
                            sys.stdout.flush()

                        # Enter key -> send newline
                        elif ch == "\r" or ch == "\n":
                            self.write(b"\r\n")
                            sys.stdout.write("\n")
                            sys.stdout.flush()

                        # Regular characters
                        else:
                            self.write(ch.encode("utf-8"))
                            sys.stdout.write(ch)
                            sys.stdout.flush()

        except KeyboardInterrupt:
            print(f"\n{COLOR_DIM}[MONITOR] Interrupted.{COLOR_RESET}")
        finally:
            self._running = False
            if old_stdin_attr is not None:
                try:
                    termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, old_stdin_attr)
                except Exception:
                    pass
            self.close()

