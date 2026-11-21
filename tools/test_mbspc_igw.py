#!/usr/bin/env python3
"""Tests mbspc on Iron Grip: Warlord BSPs (IBSP version 48).

Compiles the q3map2 igw test room with q3map2's "-game igw", builds its
AAS with mbspc the way the stock IGW maps' were made (-optimize
-forcesidesvisible), and checks the AAS is one the game loads: id's
"EAAS" version 5, made for that very BSP (the game refuses an AAS whose
BSP checksum is not the map's) and with walkable areas and reachabilities.

Exits with 0 if the test passes, 1 if it fails.

  test_mbspc_igw.py --q3map2 PATH --mbspc PATH [--workdir DIR] [-v]
"""

import argparse
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# The q3map2 igw test's game base directory, copied into the working
# directory as "base", the igw game's base directory
FIXTURE_DIR = Path(__file__).resolve().parent.parent / "tests" / "q3map2" / "igw" / "base"

# Generous upper limit for a single q3map2 or mbspc invocation
TOOL_TIMEOUT_SEC = 600

# The AAS lumps checked (be_aas_def.h's AASLUMP_*) and their record sizes
AASLUMP_AREAS = 7
AASLUMP_AREASETTINGS = 8
AASLUMP_REACHABILITY = 9
AAS_AREA_SIZE = 48
AAS_AREASETTINGS_SIZE = 28
AAS_REACHABILITY_SIZE = 44
# aas_areasettings_t.areaflags bit of areas a bot can stand in
AREA_GROUNDED = 1


class TestFailure(Exception):
  pass


def tool(args, cwd, *cmd):
  """Runs a tool in cwd; its output is printed when verbose or when it
  fails."""
  cmd = [str(c) for c in cmd]
  try:
    proc = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors="replace", timeout=TOOL_TIMEOUT_SEC)
  except subprocess.TimeoutExpired:
    raise TestFailure(f"timed out after {TOOL_TIMEOUT_SEC}s: {shlex.join(cmd)}")
  if args.verbose or proc.returncode != 0:
    print("$ " + shlex.join(cmd) + "\n" + proc.stdout, flush=True)
  if proc.returncode != 0:
    raise TestFailure(f"exit code {proc.returncode}: {shlex.join(cmd)}")
  return proc.stdout


def md4(data):
  """RFC 1320 MD4 digest of data, which Python's hashlib may lack."""
  def rotl(x, n):
    return ((x << n) | (x >> (32 - n))) & 0xffffffff

  length = len(data)
  data = data + b"\x80" + b"\0" * ((55 - length) % 64) + struct.pack("<Q", length * 8)
  h = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]
  for offset in range(0, len(data), 64):
    x = struct.unpack_from("<16I", data, offset)
    a, b, c, d = h
    # round 1: F, the words in order
    for i in range(16):
      a = rotl((a + ((b & c) | (~b & d)) + x[i]) & 0xffffffff, (3, 7, 11, 19)[i % 4])
      a, b, c, d = d, a, b, c
    # round 2: G, the words by column
    for i in range(16):
      k = (i % 4) * 4 + i // 4
      a = rotl((a + ((b & c) | (b & d) | (c & d)) + x[k] + 0x5a827999) & 0xffffffff, (3, 5, 9, 13)[i % 4])
      a, b, c, d = d, a, b, c
    # round 3: H, the words in bit reversed order
    for i in range(16):
      k = (0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15)[i]
      a = rotl((a + (b ^ c ^ d) + x[k] + 0x6ed9eba1) & 0xffffffff, (3, 9, 11, 15)[i % 4])
      a, b, c, d = d, a, b, c
    h = [(v + n) & 0xffffffff for v, n in zip(h, (a, b, c, d))]
  return struct.pack("<4I", *h)


def block_checksum(data):
  """id's Com_BlockChecksum, as the engine computes a map's checksum (and
  the AAS's bspchecksum is compared against): the MD4 digest's four
  words XORed, as a signed int."""
  words = struct.unpack("<4I", md4(data))
  return struct.unpack("<i", struct.pack("<I", words[0] ^ words[1] ^ words[2] ^ words[3]))[0]


def read_aas(aas):
  """(ident, version, bspchecksum, data of each lump) of an .aas file."""
  data = aas.read_bytes()
  ident, version = struct.unpack_from("<4si", data)
  # id's AAS_DData: the header past the version is XOR scrambled
  header = bytes(b ^ ((i * 119) & 0xff) for i, b in enumerate(data[8:8 + 4 + 14 * 8]))
  checksum = struct.unpack_from("<i", header)[0]
  lumps = [struct.unpack_from("<ii", header, 4 + 8 * i) for i in range(14)]
  return ident, version, checksum, [data[o:o + n] for o, n in lumps]


def check_aas(aas, bsp):
  ident, version, checksum, lumps = read_aas(aas)
  if (ident, version) != (b"EAAS", 5):
    raise TestFailure(f"expected an EAAS version 5 header, got {ident} {version}")
  expected = block_checksum(bsp.read_bytes())
  if checksum != expected:
    raise TestFailure(f"bspchecksum is {checksum}, the BSP's is {expected}")
  for lump, size in ((AASLUMP_AREAS, AAS_AREA_SIZE), (AASLUMP_AREASETTINGS, AAS_AREASETTINGS_SIZE),
                     (AASLUMP_REACHABILITY, AAS_REACHABILITY_SIZE)):
    if len(lumps[lump]) % size:
      raise TestFailure(f"lump {lump} is {len(lumps[lump])} bytes, not a multiple of {size}")
  settings = [struct.unpack_from("<7i", lumps[AASLUMP_AREASETTINGS], i)
              for i in range(0, len(lumps[AASLUMP_AREASETTINGS]), AAS_AREASETTINGS_SIZE)]
  # area 0 is a dummy; the room's floor and plinth make grounded areas
  # with reachabilities between them
  grounded = [s for s in settings[1:] if s[1] & AREA_GROUNDED]
  if not grounded:
    raise TestFailure(f"no grounded areas among {len(settings) - 1}")
  if not lumps[AASLUMP_REACHABILITY]:
    raise TestFailure("no reachabilities")


def run(args, workdir):
  shutil.copytree(FIXTURE_DIR, workdir / "base")
  bsp = workdir / "base" / "maps" / "build.bsp"
  tool(args, workdir, args.q3map2, "-fs_basepath", workdir, "-game", "igw", "-meta",
       Path("base", "maps", "build.map"))
  tool(args, workdir, args.mbspc, "-optimize", "-forcesidesvisible", "-bsp2aas", bsp)
  check_aas(bsp.with_suffix(".aas"), bsp)


def main():
  parser = argparse.ArgumentParser(description="Test mbspc on Iron Grip: Warlord BSPs.")
  parser.add_argument("--q3map2", required=True, type=Path, help="the q3map2 binary to compile with")
  parser.add_argument("--mbspc", required=True, type=Path, help="the mbspc binary to test")
  parser.add_argument("--workdir", type=Path,
                      help="empty directory to work in (default: a temporary one, removed afterwards)")
  parser.add_argument("-v", "--verbose", action="store_true", help="print the tools' output")
  args = parser.parse_args()
  # the tools run in the working directory, so a relative path would miss
  args.q3map2 = args.q3map2.resolve()
  args.mbspc = args.mbspc.resolve()

  try:
    if args.workdir:
      args.workdir.mkdir(parents=True, exist_ok=True)
      run(args, args.workdir.resolve())
    else:
      with tempfile.TemporaryDirectory(prefix="mbspc-igw-") as tmp:
        run(args, Path(tmp))
  except TestFailure as e:
    print(f"FAIL: {e}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
