"""
vc_bypass_offsets.py — Find VoiceChatEnableApiSecurityCheck offset
Scans running Roblox process memory for the FFlag string, extracts RVA
"""
import ctypes
import ctypes.wintypes
import struct
import sys

PROCESS_VM_READ = 0x10
PROCESS_QUERY_INFORMATION = 0x400
TH32CS_SNAPPROCESS = 2

kernel32 = ctypes.windll.kernel32

class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", ctypes.c_ulong),
        ("cntUsage", ctypes.c_ulong),
        ("th32ProcessID", ctypes.c_ulong),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", ctypes.c_ulong),
        ("cntThreads", ctypes.c_ulong),
        ("th32ParentProcessID", ctypes.c_ulong),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", ctypes.c_ulong),
        ("szExeFile", ctypes.c_wchar * 260),
    ]

def find_pid(name):
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if not snap or snap == -1:
        return 0
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
    if kernel32.Process32FirstW(snap, ctypes.byref(entry)):
        while True:
            if entry.szExeFile.lower() == name.lower():
                kernel32.CloseHandle(ctypes.c_void_p(snap))
                return entry.th32ProcessID
            if not kernel32.Process32NextW(snap, ctypes.byref(entry)):
                break
    kernel32.CloseHandle(ctypes.c_void_p(snap))
    return 0

class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", ctypes.c_ulong),
        ("th32ModuleID", ctypes.c_ulong),
        ("th32ProcessID", ctypes.c_ulong),
        ("GlblcntUsage", ctypes.c_ulong),
        ("ProccntUsage", ctypes.c_ulong),
        ("modBaseAddr", ctypes.c_size_t),
        ("modBaseSize", ctypes.c_ulong),
        ("hModule", ctypes.c_void_p),
        ("szModule", ctypes.c_wchar * 256),
        ("szExePath", ctypes.c_wchar * 260),
    ]

def get_base(pid):
    snap = kernel32.CreateToolhelp32Snapshot(0x8, pid)  # TH32CS_SNAPMODULE
    if not snap or snap == -1:
        return 0
    me = MODULEENTRY32W()
    me.dwSize = ctypes.sizeof(MODULEENTRY32W)
    if kernel32.Module32FirstW(snap, ctypes.byref(me)):
        kernel32.CloseHandle(ctypes.c_void_p(snap))
        return me.modBaseAddr
    kernel32.CloseHandle(ctypes.c_void_p(snap))
    return 0

def main():
    pid = find_pid("RobloxPlayerBeta.exe")
    if not pid:
        print("Roblox not found")
        return

    base = get_base(pid)
    print(f"PID: {pid}, Base: 0x{base:X}")

    proc = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not proc:
        print("OpenProcess failed")
        return

    TARGET = b"VoiceChatEnableApiSecurityCheck"
    tlen = len(TARGET)

    # Read PE header to get module size
    buf = ctypes.create_string_buffer(4096)
    got = ctypes.c_size_t()
    kernel32.ReadProcessMemory(proc, ctypes.c_void_p(base), buf, 4096, ctypes.byref(got))
    dos = struct.unpack_from('<H', buf.raw, 0)[0]
    if dos != 0x5A4D:  # MZ
        print("not a PE")
        return
    pe_off = struct.unpack_from('<I', buf.raw, 0x3C)[0]
    optional_header_off = pe_off + 4 + 20  # PE sig + IMAGE_FILE_HEADER
    size_of_image = struct.unpack_from('<I', buf.raw, optional_header_off + 16)[0]  # SizeOfImage
    print(f"Image size: 0x{size_of_image:X} ({size_of_image / 1024 / 1024:.1f} MB)")

    # Scan .rdata section for the FFlag string
    # .rdata typically has a lot of strings. Read in chunks.
    CHUNK = 0x400000
    scan_start = base + 0x100000  # skip headers
    scan_end = base + size_of_image

    found = 0
    for addr in range(scan_start, scan_end, CHUNK):
        size = min(CHUNK, scan_end - addr)
        chunk = ctypes.create_string_buffer(size)
        kr = kernel32.ReadProcessMemory(proc, ctypes.c_void_p(addr), chunk, size, ctypes.byref(got))
        if not kr or got.value == 0:
            continue
        data = chunk.raw[:got.value]
        idx = data.find(TARGET)
        if idx != -1:
            found = addr + idx
            print(f"String found at RVA: 0x{found - base:X}")
            print(f"Absolute: 0x{found:X}")
            break

    if not found:
        print("String not found in module")
        kernel32.CloseHandle(proc)
        return

    # Now find the FFlag variable that holds the pointer to the string.
    # FFlags use the string as a name, and the address is stored as a pointer.
    # Search .rdata/.data for: string_ptr (8 bytes) + some offset
    str_ptr = found
    str_ptr_bytes = struct.pack('<Q', str_ptr)

    print("Scanning for FFlag variable referencing this string...")
    for addr in range(base + 0x100000, scan_end, CHUNK):
        size = min(CHUNK, scan_end - addr)
        chunk = ctypes.create_string_buffer(size)
        kr = kernel32.ReadProcessMemory(proc, ctypes.c_void_p(addr), chunk, size, ctypes.byref(got))
        if not kr or got.value == 0:
            continue
        data = chunk.raw[:got.value]
        idx = 0
        while True:
            pos = data.find(str_ptr_bytes, idx)
            if pos == -1:
                break
            rva = (addr + pos) - base
            print(f"  Ref at RVA: 0x{rva:X} (abs: 0x{addr + pos:X})")
            # The actual FFlag variable is near here. Dump context.
            ctx_start = max(0, pos - 32)
            ctx = data[ctx_start:pos + 40]
            for off in range(-32, 40, 8):
                if off == 0:
                    print(f"    +{off:>3d}: <string ptr>")
                else:
                    val = struct.unpack_from('<Q', ctx, off + (pos - ctx_start))[0]
                    print(f"    +{off:>3d}: 0x{val:016X}")
            idx = pos + 8

    kernel32.CloseHandle(proc)

if __name__ == "__main__":
    main()
