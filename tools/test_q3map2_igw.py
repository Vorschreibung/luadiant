#!/usr/bin/env python3
"""Tests q3map2's Iron Grip: Warlord game ("-game igw").

Compiles a room holding a plinth whose top face alone is marked buildable,
a block with a canbuild shader and an npcclip brush, checks the BSP's brush
sides carry canbuild where the engine expects it (bit 0x400 of the side
flag word), then decompiles it and checks the face marked buildable is
again, while the others are not.

Exits with 0 if the test passes, 1 if it fails.

  test_q3map2_igw.py --q3map2 PATH [--workdir DIR] [-v]
"""

import argparse
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# The test's game base directory, copied into the working directory as
# "base", the igw game's base directory, so compiles never touch the tree
FIXTURE_DIR = Path(__file__).resolve().parent.parent / "tests" / "q3map2" / "igw" / "base"

# Generous upper limit for a single q3map2 invocation
Q3MAP2_TIMEOUT_SEC = 600

# The canbuild bit: of shader contents, of .map face content flags and of
# the brush side flag word
CANBUILD = 0x400


class TestFailure(Exception):
  pass


def q3map2(args, basepath, *q3map2_args):
  """Runs q3map2 for the igw game below basepath and returns its stdout;
  its output is printed when verbose or when it fails."""
  cmd = [str(c) for c in (args.q3map2, "-fs_basepath", basepath, "-game", "igw", *q3map2_args)]
  try:
    proc = subprocess.run(cmd, cwd=basepath, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors="replace", timeout=Q3MAP2_TIMEOUT_SEC)
  except subprocess.TimeoutExpired:
    raise TestFailure(f"timed out after {Q3MAP2_TIMEOUT_SEC}s: {shlex.join(cmd)}")
  if args.verbose or proc.returncode != 0:
    print("$ " + shlex.join(cmd) + "\n" + proc.stdout, flush=True)
  if proc.returncode != 0:
    raise TestFailure(f"exit code {proc.returncode}: {shlex.join(cmd)}")
  return proc.stdout


def read_brush_sides(bsp):
  """Returns (shader name, contents, plane normal, plane dist, side flag
  word) of every brush side of an Iron Grip: Warlord BSP, after checking
  its header and that the sides are 12 bytes."""
  data = bsp.read_bytes()
  ident, version = struct.unpack_from("<4si", data)
  if (ident, version) != (b"IBSP", 48):
    raise TestFailure(f"expected an IBSP version 48 header, got {ident} {version}")
  lumps = [struct.unpack_from("<ii", data, 8 + 8 * i) for i in range(17)]

  def records(lump, fmt):
    offset, length = lumps[lump]
    size = struct.calcsize(fmt)
    if length % size:
      raise TestFailure(f"lump {lump} is {length} bytes, not a multiple of {size}")
    return [struct.unpack_from(fmt, data, offset + i) for i in range(0, length, size)]

  shaders = [(name.split(b"\0")[0].decode(), contents) for name, _, contents in records(1, "<64sii")]
  planes = records(2, "<4f")
  return [(*shaders[shader], planes[plane][:3], planes[plane][3], flags)
          for plane, shader, flags in records(9, "<iii")]


def check_bsp(sides):
  """The plinth's top face alone of the test/wall faces is buildable, every
  face of the canbuild shader is, and the npcclip brush compiled as such."""
  plinth_top = [s for s in sides if s[0] == "textures/test/wall" and s[2] == (0, 0, 1) and s[3] == 32]
  if len(plinth_top) != 1 or not plinth_top[0][4] & CANBUILD:
    raise TestFailure(f"the plinth's top face is not buildable: {plinth_top}")
  other_wall = [s for s in sides if s[0] == "textures/test/wall" and s not in plinth_top and s[4] & CANBUILD]
  if other_wall:
    raise TestFailure(f"unmarked faces are buildable: {other_wall}")
  ground = [s for s in sides if s[0] == "textures/test/ground"]
  if len(ground) != 6 or not all(s[4] & CANBUILD and s[1] & CANBUILD for s in ground):
    raise TestFailure(f"the canbuild shader's faces are not all buildable: {ground}")
  # npcclip: only contents 0x800, not solid, and compiled translucent like
  # the clip brushes (translucent 0x2, not solid 0x1)
  npcclip = [s for s in sides if s[0] == "textures/test/npcclip"]
  if len(npcclip) != 6 or not all(s[1] == 0x800 and s[4] & 0x3 == 0x2 for s in npcclip):
    raise TestFailure(f"the npcclip brush is not compiled as such: {npcclip}")


def check_decompiled(map_text):
  """Decompiling writes canbuild as the face's content flags only where the
  shader does not imply it: on the plinth's top face alone."""
  faces = re.findall(r"^\s*\(.*\) (\S+) \S+ \S+ \S+ \S+ \S+ (\d+) 0 0$", map_text, re.MULTILINE)
  marked = [f for f in faces if int(f[1]) & CANBUILD]
  if not faces or marked != [("test/wall", str(CANBUILD))]:
    raise TestFailure(f"expected the plinth's top face alone marked buildable, got {marked}")


def run(args, workdir):
  shutil.copytree(FIXTURE_DIR, workdir / "base")
  q3map2(args, workdir, "-meta", Path("base", "maps", "build.map"))
  check_bsp(read_brush_sides(workdir / "base" / "maps" / "build.bsp"))
  q3map2(args, workdir, "-convert", "-format", "map", Path("base", "maps", "build.bsp"))
  check_decompiled((workdir / "base" / "maps" / "build_converted.map").read_text())


def main():
  parser = argparse.ArgumentParser(description="Test q3map2's Iron Grip: Warlord game.")
  parser.add_argument("--q3map2", required=True, type=Path, help="the q3map2 binary to test")
  parser.add_argument("--workdir", type=Path,
                      help="empty directory to work in (default: a temporary one, removed afterwards)")
  parser.add_argument("-v", "--verbose", action="store_true", help="print q3map2's output")
  args = parser.parse_args()
  # q3map2 runs in the working directory, so a relative path would miss
  args.q3map2 = args.q3map2.resolve()

  try:
    if args.workdir:
      args.workdir.mkdir(parents=True, exist_ok=True)
      run(args, args.workdir.resolve())
    else:
      with tempfile.TemporaryDirectory(prefix="q3map2-igw-") as tmp:
        run(args, Path(tmp))
  except TestFailure as e:
    print(f"FAIL: {e}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
