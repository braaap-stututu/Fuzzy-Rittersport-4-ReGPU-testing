"""Drive FM4 from boot to the frozen grid (World Tour, first event) by pressing A until the log says frozen.
usage: route.py <log file> [timeout s]"""
import json, os, socket, sys, time

PORT = int(os.environ.get('REGPU_DEBUG_PORT', '7351'))
log = sys.argv[1]
timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 400


def cmd(line):
    try:
        s = socket.create_connection(('127.0.0.1', PORT), timeout=30)
        s.sendall((line + '\n').encode())
        data = b''
        while not data.endswith(b'\n'):
            c = s.recv(65536)
            if not c:
                break
            data += c
        s.close()
        return json.loads(data)
    except Exception as e:
        return {'ok': False, 'error': str(e)}


def frozen():
    try:
        with open(log, 'r', errors='replace') as f:
            return 'grid test: frozen' in f.read()
    except OSError:
        return False


t0 = time.time()
n = 0
while time.time() - t0 < timeout:
    if frozen():
        time.sleep(3)
        print(f'frozen after {time.time() - t0:.0f} s, {n} presses')
        sys.exit(0)
    r = cmd('press START 120' if n % 4 == 3 and time.time() - t0 < 40 else 'press A 150')
    n += 1
    time.sleep(1.5 if r.get('ok') else 3)
print('timed out')
sys.exit(1)
