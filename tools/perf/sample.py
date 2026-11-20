"""Poor man's sampling profiler: suspends one thread of a process ~2000 times a second, records RIP, resolves with
llvm-symbolizer against the exe's PDB.
usage: sample.py <pid> <thread-id|busiest|topN> <seconds> <exe> [top N]
Needs llvm-symbolizer (LLVM_SYMBOLIZER=<path>, or on PATH) and the exe's PDB next to it."""
import collections, ctypes, ctypes.wintypes as wt, os, shutil, subprocess, sys, time

SYMBOLIZER = os.environ.get('LLVM_SYMBOLIZER') or shutil.which('llvm-symbolizer') or 'llvm-symbolizer'

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
THREAD_ALL = 0x001F03FF
CONTEXT_CONTROL = 0x00100001


class M128A(ctypes.Structure):
    _fields_ = [('Low', ctypes.c_uint64), ('High', ctypes.c_int64)]


class CONTEXT(ctypes.Structure):
    _pack_ = 16
    _fields_ = [('P1Home', ctypes.c_uint64), ('P2Home', ctypes.c_uint64), ('P3Home', ctypes.c_uint64),
                ('P4Home', ctypes.c_uint64), ('P5Home', ctypes.c_uint64), ('P6Home', ctypes.c_uint64),
                ('ContextFlags', wt.DWORD), ('MxCsr', wt.DWORD),
                ('SegCs', wt.WORD), ('SegDs', wt.WORD), ('SegEs', wt.WORD), ('SegFs', wt.WORD), ('SegGs', wt.WORD),
                ('SegSs', wt.WORD), ('EFlags', wt.DWORD),
                ('Dr0', ctypes.c_uint64), ('Dr1', ctypes.c_uint64), ('Dr2', ctypes.c_uint64), ('Dr3', ctypes.c_uint64),
                ('Dr6', ctypes.c_uint64), ('Dr7', ctypes.c_uint64),
                ('Rax', ctypes.c_uint64), ('Rcx', ctypes.c_uint64), ('Rdx', ctypes.c_uint64), ('Rbx', ctypes.c_uint64),
                ('Rsp', ctypes.c_uint64), ('Rbp', ctypes.c_uint64), ('Rsi', ctypes.c_uint64), ('Rdi', ctypes.c_uint64),
                ('R8', ctypes.c_uint64), ('R9', ctypes.c_uint64), ('R10', ctypes.c_uint64), ('R11', ctypes.c_uint64),
                ('R12', ctypes.c_uint64), ('R13', ctypes.c_uint64), ('R14', ctypes.c_uint64), ('R15', ctypes.c_uint64),
                ('Rip', ctypes.c_uint64), ('Flt', ctypes.c_byte * 512), ('Vec', M128A * 26),
                ('VectorControl', ctypes.c_uint64), ('DebugControl', ctypes.c_uint64),
                ('LastBranchToRip', ctypes.c_uint64), ('LastBranchFromRip', ctypes.c_uint64),
                ('LastExceptionToRip', ctypes.c_uint64), ('LastExceptionFromRip', ctypes.c_uint64)]


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ThreadID', wt.DWORD), ('th32OwnerProcessID', wt.DWORD),
                ('tpBasePri', wt.LONG), ('tpDeltaPri', wt.LONG), ('dwFlags', wt.DWORD)]


class FILETIME(ctypes.Structure):
    _fields_ = [('lo', wt.DWORD), ('hi', wt.DWORD)]


def threads(pid):
    snap = k32.CreateToolhelp32Snapshot(4, 0)
    te = THREADENTRY32(); te.dwSize = ctypes.sizeof(te)
    out = []
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            out.append(te.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap)
    return out


def cpu_time(h):
    a, b, c, d = FILETIME(), FILETIME(), FILETIME(), FILETIME()
    k32.GetThreadTimes(h, ctypes.byref(a), ctypes.byref(b), ctypes.byref(c), ctypes.byref(d))
    return ((c.hi << 32) | c.lo) + ((d.hi << 32) | d.lo)


def thread_name(h):
    p = ctypes.c_wchar_p()
    try:
        if k32.GetThreadDescription(h, ctypes.byref(p)) >= 0 and p.value:
            return p.value
    except Exception:
        pass
    return ''


pid = int(sys.argv[1]); which = sys.argv[2]; secs = float(sys.argv[3]); exe = sys.argv[4]
top = int(sys.argv[5]) if len(sys.argv) > 5 else 40
hs = {t: k32.OpenThread(THREAD_ALL, False, t) for t in threads(pid)}
t0 = {t: cpu_time(h) for t, h in hs.items()}
time.sleep(1.0)
busy = sorted(((cpu_time(h) - t0[t]) / 1e7, t) for t, h in hs.items())[::-1]
print('busiest threads (cpu s over 1 s):', ', '.join(f'{t}:{c:.2f} {thread_name(hs[t])}' for c, t in busy[:6]))
tids = [busy[0][1]] if which == 'busiest' else ([busy[i][1] for i in range(int(which[3:]))] if which.startswith('top') else [int(which)])
ctx = CONTEXT();
buf = (ctypes.c_byte * (ctypes.sizeof(CONTEXT) + 16))()
addr = (ctypes.addressof(buf) + 15) & ~15
ctxp = ctypes.cast(addr, ctypes.POINTER(CONTEXT))
psapi0 = ctypes.WinDLL('psapi')
hproc = k32.OpenProcess(0x0410, False, pid)
_mods = (ctypes.c_void_p * 1024)(); _need = wt.DWORD()
psapi0.EnumProcessModules(hproc, _mods, ctypes.sizeof(_mods), ctypes.byref(_need))
exe_lo = _mods[0]; exe_hi = exe_lo + 0x10000000; exe_text_lo = exe_lo + 0x1000
stackbuf = (ctypes.c_char * 4096)()
for tid in tids:
    h = hs[tid]
    rips = collections.Counter()
    callers = collections.Counter()
    end = time.perf_counter() + secs
    n = 0
    while time.perf_counter() < end:
        if k32.SuspendThread(h) != 0xFFFFFFFF:
            ctxp.contents.ContextFlags = CONTEXT_CONTROL
            if k32.GetThreadContext(h, ctxp):
                rip, rsp = ctxp.contents.Rip, ctxp.contents.Rsp
                rips[rip] += 1
                n += 1
                if not (exe_lo <= rip < exe_hi):
                    k32.ReadProcessMemory(hproc, ctypes.c_void_p(rsp), stackbuf, len(stackbuf), None)
                    q = (ctypes.c_uint64 * (len(stackbuf) // 8)).from_buffer(stackbuf)
                    chain = []
                    for v in q:
                        if exe_text_lo <= v < exe_hi:
                            chain.append(v)
                            if len(chain) == 4:
                                break
                    if chain:
                        callers[tuple(chain)] += 1
            k32.ResumeThread(h)
        time.sleep(0.0005)
    # module base of the exe: symbolize with addresses relative to the image base via llvm-symbolizer --adjust-vma? use
    # the process module list instead.
    import ctypes.wintypes
    psapi = ctypes.WinDLL('psapi')
    hp = k32.OpenProcess(0x0410, False, pid)
    mods = (ctypes.c_void_p * 1024)(); need = wt.DWORD()
    psapi.EnumProcessModules(hp, mods, ctypes.sizeof(mods), ctypes.byref(need))
    base = mods[0]
    funcs = collections.Counter()
    addrs = list(rips)
    inexe = [a for a in addrs if base <= a < base + 0x10000000]
    names = {}
    if inexe:
        inp = '\n'.join(hex(a - base + 0x140000000) for a in inexe)
        r = subprocess.run([SYMBOLIZER, '--obj', exe,
                            '--no-inlines', '--functions=short', '--output-style=GNU'], input=inp, capture_output=True, text=True)
        lines = r.stdout.split('\n')
        for i, a in enumerate(inexe):
            if 2 * i < len(lines):
                names[a] = lines[2 * i].strip()
    # Other modules by address range (name only).
    class MODINFO(ctypes.Structure):
        _fields_ = [('base', ctypes.c_void_p), ('size', wt.DWORD), ('entry', ctypes.c_void_p)]
    ranges = []
    for i in range(need.value // ctypes.sizeof(ctypes.c_void_p)):
        mi = MODINFO(); nm = ctypes.create_unicode_buffer(260)
        psapi.GetModuleInformation(hp, ctypes.c_void_p(mods[i]), ctypes.byref(mi), ctypes.sizeof(mi))
        psapi.GetModuleBaseNameW(hp, ctypes.c_void_p(mods[i]), nm, 260)
        ranges.append((mi.base or 0, (mi.base or 0) + mi.size, nm.value))
    def modname(a):
        for lo, hi, nm in ranges:
            if lo <= a < hi:
                return '[' + nm + ']'
        return '[unknown]'
    for a, c in rips.items():
        funcs[names.get(a) or modname(a)] += c
    if callers:
        ca = sorted({a for ch in callers for a in ch})
        r2 = subprocess.run([SYMBOLIZER, '--obj', exe,
                             '--no-inlines', '--functions=short', '--output-style=GNU'],
                            input='\n'.join(hex(a - base + 0x140000000) for a in ca), capture_output=True, text=True)
        l2 = r2.stdout.split('\n')
        nm = {a: (l2[2 * i].strip() if 2 * i < len(l2) else '?') for i, a in enumerate(ca)}
        cf = collections.Counter()
        for ch, c in callers.items():
            cf[' < '.join(nm[a].replace("regpu::`anonymous namespace'::", '') for a in ch)] += c
        print(f'\nthread {tid}: samples outside the exe, by the exe addresses found on the stack (up to 4, innermost first):')
        for f, c in cf.most_common(14):
            print(f'{100 * c / n:5.1f}%  {f[:230]}')
    print(f'\nthread {tid} {thread_name(h)}: {n} samples')
    for f, c in funcs.most_common(top):
        print(f'{100 * c / n:5.1f}%  {f[:150]}')
