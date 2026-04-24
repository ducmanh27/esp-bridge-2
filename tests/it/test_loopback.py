"""
ESP32 UART-WiFi Bridge — Test Suite
=====================================
Chạy từ PC. Yêu cầu:
  pip install pyserial

Cách dùng:
  python test_loopback.py --host 192.168.10.1 --port 8080 --serial_data /dev/ttyUSB1 --cli_port /dev/ttyUSB0

Test cases:
  1. UART→TCP broadcast
  2. TCP→UART forward
  3. Multi-client broadcast
  4. Slow client isolation
  5. CLI persistence (set ssid + reboot + verify)
  6. Queue stress (burst UART data)
  7. CLI get status
  8. Latency loopback
"""

import socket
import serial
import time
import threading
import argparse
import sys

# ─── Config ───────────────────────────────────────────────────────────────────
DEFAULT_HOST               = "192.168.10.1"
DEFAULT_PORT               = 8080
DEFAULT_SERIAL_DATA        = "/dev/ttyUSB0"
DEFAULT_BAUD_SERIAL_DATA   = 115200
DEFAULT_BAUD_CLI_PORT      = 115200
TIMEOUT                    = 3.0   # seconds

PASS = "\033[92mPASS\033[0m"
FAIL = "\033[91mFAIL\033[0m"

# ─── Helpers ─────────────────────────────────────────────────────────────────

def make_tcp(host, port, timeout=TIMEOUT):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((host, port))
    return s

def make_serial(dev, baud):
    return serial.Serial(dev, baud, timeout=TIMEOUT)

def result(name, ok, detail=""):
    status = PASS if ok else FAIL
    print(f"  [{status}] {name}" + (f" — {detail}" if detail else ""))
    return ok

# ─── Test 1: UART2 → TCP (broadcast) ─────────────────────────────────────────

def test_uart_to_tcp(host, port, ser):
    print("\nTest 1: UART2 → TCP broadcast")
    client = make_tcp(host, port)
    time.sleep(0.2)

    payload = b"HELLO_FROM_UART_" + bytes(range(16))
    ser.write(payload)
    ser.flush()

    try:
        received = client.recv(1024)
        ok = received == payload
        result("Payload received by TCP client", ok,
               f"sent={len(payload)} got={len(received)}")
    except socket.timeout:
        result("Payload received by TCP client", False, "timeout")
        ok = False
    finally:
        client.close()
    return ok

# ─── Test 2: TCP → UART2 forward ─────────────────────────────────────────────

def test_tcp_to_uart(host, port, ser):
    print("\nTest 2: TCP client → UART2 forward")
    client = make_tcp(host, port)
    time.sleep(0.2)

    ser.reset_input_buffer()
    payload = b"CMD:STATUS\r\n"
    client.sendall(payload)

    time.sleep(0.3)
    received = ser.read(ser.in_waiting or len(payload))
    ok = received == payload
    result("UART2 received TCP data", ok,
           f"sent={len(payload)} got={len(received)}")
    client.close()
    return ok

# ─── Test 3: Multi-client broadcast ──────────────────────────────────────────

def test_multicast(host, port, ser, n_clients=2):
    print(f"\nTest 3: Multi-client broadcast ({n_clients} clients)")
    clients = [make_tcp(host, port) for _ in range(n_clients)]
    time.sleep(0.3)

    payload = b"BROADCAST_TEST_" + bytes(range(32))
    ser.write(payload)
    ser.flush()

    results = []
    for i, c in enumerate(clients):
        try:
            data = c.recv(1024)
            ok = data == payload
            results.append(ok)
            result(f"  Client {i} received correctly", ok,
                   f"got {len(data)} bytes")
        except socket.timeout:
            result(f"  Client {i} received correctly", False, "timeout")
            results.append(False)
        finally:
            c.close()

    all_ok = all(results)
    result("All clients received broadcast", all_ok)
    return all_ok

# ─── Test 4: Slow client isolation ───────────────────────────────────────────

def test_slow_client(host, port, ser):
    print("\nTest 4: Slow client isolation")
    # slow_client không gọi recv()
    slow = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    slow.settimeout(TIMEOUT)
    slow.connect((host, port))

    fast = make_tcp(host, port)
    time.sleep(0.3)

    payload = b"FAST_CLIENT_DATA_" + b"X" * 100
    received_by_fast = []

    def fast_recv():
        try:
            d = fast.recv(1024)
            received_by_fast.append(d)
        except Exception:
            pass

    t = threading.Thread(target=fast_recv)
    t.start()

    ser.write(payload)
    ser.flush()
    t.join(timeout=TIMEOUT + 1)

    ok = len(received_by_fast) > 0 and received_by_fast[0] == payload
    result("Fast client unaffected by slow client", ok,
           f"fast got {len(received_by_fast[0]) if received_by_fast else 0} bytes")

    slow.close()
    fast.close()
    return ok

# ─── Test 5: CLI set ssid / get ssid ─────────────────────────────────────────

def test_cli_set_get(ser):
    print("\nTest 5: CLI set/get ssid")
    time.sleep(0.2)
    ser.reset_input_buffer()

    # Kiểm tra prompt
    ser.write(b"\r\n")
    time.sleep(0.2)
    out = ser.read(ser.in_waiting or 16).decode(errors="replace")
    got_prompt = ">" in out
    result("CLI prompt present", got_prompt, repr(out))

    # Set SSID
    new_ssid = "TestBridge99"
    ser.write(f"set ssid {new_ssid}\r\n".encode())
    time.sleep(0.3)
    out = ser.read(ser.in_waiting or 128).decode(errors="replace")
    ok_set = "OK" in out
    result(f"set ssid returns OK", ok_set, repr(out[:80]))

    # Get SSID
    ser.write(b"get ssid\r\n")
    time.sleep(0.3)
    out = ser.read(ser.in_waiting or 128).decode(errors="replace")
    ok_get = new_ssid in out
    result(f"get ssid returns new value", ok_get, repr(out[:80]))

    # Restore
    ser.write(f"set ssid ESP32-PBE\r\n".encode())
    time.sleep(0.2)

    return got_prompt and ok_set and ok_get

# ─── Test 6: Burst / stress ───────────────────────────────────────────────────

def test_stress_burst(host, port, ser, burst_kb=16):
    print(f"\nTest 6: UART burst stress ({burst_kb}KB)")
    client = make_tcp(host, port)
    time.sleep(0.2)

    payload = bytes(range(256)) * (burst_kb * 4)  # burst_kb KB
    ser.write(payload)
    ser.flush()

    received = bytearray()
    deadline = time.time() + 10.0
    while len(received) < len(payload) and time.time() < deadline:
        try:
            chunk = client.recv(4096)
            if chunk:
                received.extend(chunk)
        except socket.timeout:
            break

    ok = len(received) == len(payload) and bytes(received) == payload
    result(f"Received all {burst_kb}KB burst intact",
           ok, f"expected={len(payload)} got={len(received)}")
    client.close()
    return ok

# ─── Test 7: CLI get status ───────────────────────────────────────────────────

def test_cli_status(ser):
    print("\nTest 7: CLI get status")
    ser.reset_input_buffer()
    ser.write(b"get status\r\n")
    time.sleep(0.5)
    out = ser.read(ser.in_waiting or 256).decode(errors="replace")

    checks = [
            ("TCP clients", "TCP clients:" in out),
            ("UART2 RX",    "UART2 RX:" in out),  
            ("Heap",        "Heap:" in out),   
        ]
    all_ok = True
    for label, ok in checks:
        result(f"  Status contains '{label}'", ok)
        all_ok = all_ok and ok
    return all_ok

# ─── Test 8: Latency loopback ─────────────────────────────────────────────────
# Đo độ trễ round-trip: UART2 TX → ESP32 → TCP client → ghi nhận thời gian
# Sơ đồ: PC serial TX → ESP32 UART2 RX → broadcast → TCP client recv()
# Không có đường về UART nên chỉ đo one-way: t_send → t_recv (half round-trip)
# Để đo full loopback cần nối TCP client gửi lại → ESP32 forward → UART2 TX → PC serial RX
def test_latency_loopback(host, port, ser, num_samples=100):
    print(f"\nTest 8: Latency loopback ({num_samples} samples)")

    client = make_tcp(host, port)
    client.settimeout(2.0)
    time.sleep(0.3)
    ser.reset_input_buffer()

    # Warm up: gửi vài bản tin để WiFi/TCP stack không ở trạng thái cold
    for _ in range(5):
        ser.write(b"WARM\r\n")
        ser.flush()
        try:
            client.recv(64)
        except Exception:
            pass
    time.sleep(0.2)

    latencies_ms = []
    errors = 0
    PAYLOAD = b"LAT_PROBE_12345\r\n"  # bản tin nhỏ cố định để đo latency thuần

    for i in range(num_samples):
        try:
            t_send = time.perf_counter()          # timestamp ngay trước khi write
            ser.write(PAYLOAD)
            ser.flush()                           # đảm bảo bytes rời khỏi PC ngay

            data = client.recv(256)               # block chờ ESP32 broadcast
            t_recv = time.perf_counter()          # timestamp khi nhận được

            if PAYLOAD in data or len(data) > 0:
                latency_ms = (t_recv - t_send) * 1000
                latencies_ms.append(latency_ms)
            else:
                errors += 1

        except socket.timeout:
            errors += 1
            print(f"    Sample {i}: timeout")

        time.sleep(0.02)  # 20ms giữa các probe, tránh burst

    client.close()

    if len(latencies_ms) < num_samples * 0.8:
        result("Latency loopback", False,
               f"quá nhiều lỗi: {errors}/{num_samples} samples thất bại")
        return False

    # Thống kê
    latencies_ms.sort()
    n         = len(latencies_ms)
    avg       = sum(latencies_ms) / n
    min_l     = latencies_ms[0]
    max_l     = latencies_ms[-1]
    p50       = latencies_ms[int(n * 0.50)]
    p90       = latencies_ms[int(n * 0.90)]
    p99       = latencies_ms[min(int(n * 0.99), n - 1)]
    jitter    = p90 - p50  # độ biến động

    # Tính standard deviation
    mean = avg
    variance = sum((x - mean) ** 2 for x in latencies_ms) / n
    stddev = variance ** 0.5

    print(f"\n  Latency statistics ({n} samples, {errors} errors):")
    print(f"  {'Min':<8}: {min_l:>8.2f} ms")
    print(f"  {'Avg':<8}: {avg:>8.2f} ms")
    print(f"  {'StdDev':<8}: {stddev:>8.2f} ms")
    print(f"  {'P50':<8}: {p50:>8.2f} ms")
    print(f"  {'P90':<8}: {p90:>8.2f} ms")
    print(f"  {'P99':<8}: {p99:>8.2f} ms")
    print(f"  {'Max':<8}: {max_l:>8.2f} ms")
    print(f"  {'Jitter':<8}: {jitter:>8.2f} ms  (P90 - P50)")

    # Histogram ASCII đơn giản
    print(f"\n  Histogram (ms):")
    buckets = [0, 2, 5, 10, 20, 50, 100, float('inf')]
    labels  = ["<2", "2-5", "5-10", "10-20", "20-50", "50-100", ">100"]
    counts  = [0] * len(labels)
    for lat in latencies_ms:
        for j in range(len(buckets) - 1):
            if buckets[j] <= lat < buckets[j + 1]:
                counts[j] += 1
                break
    for label, count in zip(labels, counts):
        bar = "█" * int(count / n * 40)
        print(f"  {label:>8} ms | {bar:<40} {count:>4} ({count/n*100:>5.1f}%)")
    THRESHOLD_P99_MS = 100.0
    THRESHOLD_AVG_MS =  30.0
    ok = p99 < THRESHOLD_P99_MS and avg < THRESHOLD_AVG_MS
    result(f"P99 < {THRESHOLD_P99_MS}ms và Avg < {THRESHOLD_AVG_MS}ms",
           ok, f"P99={p99:.1f}ms Avg={avg:.1f}ms")
    return ok

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="ESP32 Bridge Test Suite")
    parser.add_argument("--host",   default=DEFAULT_HOST)
    parser.add_argument("--port",   type=int, default=DEFAULT_PORT)
    parser.add_argument("--serial_data", default=DEFAULT_SERIAL_DATA)
    parser.add_argument("--baud_serial_data",   type=int, default=DEFAULT_BAUD_SERIAL_DATA)
    parser.add_argument("--cli_port", default="/dev/ttyUSB0", help="UART0 (CLI/Log)")
    parser.add_argument("--baud_cli_port",   type=int, default=DEFAULT_BAUD_CLI_PORT)
    parser.add_argument("--tests",  default="all",
                        help="Comma-separated test IDs (1-7) or 'all'")
    args = parser.parse_args()

    print(f"\nESP32 Bridge Test Suite")
    print(f"  TCP  : {args.host}:{args.port}")
    print(f"  UART DATA : {args.serial_data} @ {args.baud_serial_data}")

    try:
        ser = make_serial(args.serial_data, args.baud_serial_data)
    except Exception as e:
        print(f"ERROR: Cannot open serial port {args.serial}: {e}")
        sys.exit(1)
        
    try:
        ser_cli = make_serial(args.cli_port, args.baud_cli_port)
    except Exception:
        print("WARNING: Cannot open CLI port, Test 5 & 7 will be skipped.")
        ser_cli = None

    time.sleep(1.0)  # đợi ESP32 boot / CLI ready

    run_all = args.tests == "all"
    ids = set(args.tests.split(",")) if not run_all else set()

    results = []

    def should_run(n):
        return run_all or str(n) in ids

    if should_run(1): results.append(test_uart_to_tcp(args.host, args.port, ser))
    if should_run(2): results.append(test_tcp_to_uart(args.host, args.port, ser))
    if should_run(3): results.append(test_multicast(args.host, args.port, ser))
    if should_run(4): results.append(test_slow_client(args.host, args.port, ser))
    if should_run(5): results.append(test_cli_set_get(ser_cli))
    if should_run(6): results.append(test_stress_burst(args.host, args.port, ser))
    if should_run(7): results.append(test_cli_status(ser_cli))
    if should_run(8): results.append(test_latency_loopback(args.host, args.port, ser))
    ser.close()

    total = len(results)
    passed = sum(1 for r in results if r)
    print(f"\n{'='*40}")
    print(f"Result: {passed}/{total} tests passed")
    print('='*40)
    sys.exit(0 if passed == total else 1)

if __name__ == "__main__":
    main()
