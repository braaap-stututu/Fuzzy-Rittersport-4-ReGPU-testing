"""Frozen-grid fps measurement and live A/B through the ReGPU debug server (port REGPU_DEBUG_PORT, default 7351).

  ab.py fps [seconds]                         fps over a window (from the swap counter)
  ab.py ab NAME [rounds] [seconds] [kind]     interleaved off/on rounds; kind = switch (REGPU_* live switch) or env
"""
import json, os, socket, statistics, sys, time

PORT = int(os.environ.get('REGPU_DEBUG_PORT', '7351'))


def cmd(line):
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


def fps(seconds):
    a = cmd('status'); t0 = time.perf_counter()
    time.sleep(seconds)
    b = cmd('status'); t1 = time.perf_counter()
    return (b['frame'] - a['frame']) / (t1 - t0)


def setv(name, on, kind):
    if kind == 'switch':
        return cmd(f'switch {name} {1 if on else 0}')
    return cmd(f'env {name} 1' if on else f'env {name}')


def main():
    if sys.argv[1] == 'fps':
        secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5
        print(f'{fps(secs):.1f} fps')
        return
    if sys.argv[1] == 'ab':
        name = sys.argv[2]
        rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 5
        secs = float(sys.argv[4]) if len(sys.argv) > 4 else 5
        kind = sys.argv[5] if len(sys.argv) > 5 else 'switch'
        off, on = [], []
        for r in range(rounds):
            for state in ((False, True) if r % 2 == 0 else (True, False)):
                print(setv(name, state, kind), flush=True)
                time.sleep(1.5)  # settle (pipeline builds after a switch)
                v = fps(secs)
                (on if state else off).append(v)
                print(f'round {r} {"on " if state else "off"} {v:.1f}', flush=True)
        setv(name, False, kind)
        mo, mn = statistics.median(off), statistics.median(on)
        print(f'{name}: off median {mo:.1f}, on median {mn:.1f}: {100 * (mn / mo - 1):+.1f}%')
        print('off', ' '.join(f'{x:.1f}' for x in off))
        print('on ', ' '.join(f'{x:.1f}' for x in on))


main()
