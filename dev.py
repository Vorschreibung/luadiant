#!/usr/bin/env python3
"""Developer shim around the build system and tools of this repository.

Every command is a thin wrapper around what you would otherwise type by
hand (mostly 'make'), so that common development tasks are one short
command away:

  ./dev.py build [TARGET...] [VAR=VALUE...]  build (default: everything)
  ./dev.py test [-k FILTER]                  build q3map2 and test it
  ./dev.py run [ARG...]                      run the built radiant
  ./dev.py q3map2 [ARG...]                   run the built q3map2
  ./dev.py format [FILE...]                  uncrustify C/C++ sources
  ./dev.py clean                             remove build artifacts

Run './dev.py <command> --help' for the details of each command.
"""

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# Repository root, all commands run relative to it regardless of the
# current working directory
ROOT = Path(__file__).resolve().parent

# Where the Makefile puts the built binaries (its INSTALLDIR default)
INSTALL_DIR = ROOT / "install"

# Short names for the Makefile targets most commonly built during
# development; anything else is passed through to make as is
TARGET_ALIASES = {
  "all": "all",
  "radiant": "binaries-radiant",
  "tools": "binaries-tools",
  "q3map2": "binaries-q3map2",
  "mbspc": "binaries-mbspc",
}

# Self-contained q3map2 smoke test (a sealed room with a light and an OBJ
# model) and the q3map2 regression test maps
SMOKE_TEST_DIR = ROOT / "tests" / "q3map2" / "smoke"
REGRESSION_TESTS_DIR = ROOT / "regression_tests" / "q3map2"

# Generous upper limit for a single q3map2 invocation in the tests, so a
# hanging compile fails the test instead of blocking forever
Q3MAP2_TIMEOUT_SEC = 600

# File extensions considered C/C++ sources by 'format'
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}


def log(msg):
  """Print a status message, set apart from the output of called tools."""
  print(f"dev.py: {msg}", file=sys.stderr, flush=True)


def die(msg):
  log(f"error: {msg}")
  sys.exit(1)


def run(cmd, **kwargs):
  """Run a command in the repository root, echoing it first; exits with
  the command's exit code if it fails."""
  log("$ " + shlex.join(str(c) for c in cmd))
  try:
    proc = subprocess.run(cmd, cwd=kwargs.pop("cwd", ROOT), **kwargs)
  except FileNotFoundError:
    die(f"'{cmd[0]}' not found, please install it or fix your PATH")
  if proc.returncode != 0:
    sys.exit(proc.returncode)
  return proc


def make(targets, variables=(), jobs=None):
  """Run make with the given targets and VAR=VALUE assignments."""
  cmd = [os.environ.get("MAKE", "make"), f"-j{jobs or os.cpu_count() or 1}"]
  cmd += list(variables) + list(targets)
  run(cmd)


def split_make_args(args):
  """Split positional arguments into make targets (resolving aliases) and
  VAR=VALUE variable assignments."""
  targets, variables = [], []
  for arg in args:
    if re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", arg):
      variables.append(arg)
    else:
      targets.append(TARGET_ALIASES.get(arg, arg))
  return targets, variables


def find_binary(name):
  """Locate a binary built by the Makefile in the install directory.

  On unix-likes it is named '<name>.<machine>' plus a '<name>' symlink,
  on Windows '<name>.exe'."""
  for candidate in (INSTALL_DIR / name, INSTALL_DIR / f"{name}.exe"):
    if candidate.is_file():
      return candidate
  die(f"{name} has not been built yet, run './dev.py build' first")


# ---------------------------------------------------------------------------
# build / clean
# ---------------------------------------------------------------------------

def cmd_build(args):
  targets, variables = split_make_args(args.targets)
  if args.no_gamepacks:
    variables.append("DOWNLOAD_GAMEPACKS=no")
  make(targets or ["all"], variables, args.jobs)


def cmd_clean(args):
  make(["clean"], jobs=1)


# ---------------------------------------------------------------------------
# run / q3map2
# ---------------------------------------------------------------------------

def exec_binary(name, argv, cwd=None):
  """Replace this process with a built binary, so signals and the exit
  code are passed through unchanged."""
  binary = find_binary(name)
  if cwd:
    os.chdir(cwd)
  log("$ " + shlex.join([str(binary)] + argv))
  if os.name == "nt":
    # No real exec() on Windows, emulate it
    sys.exit(subprocess.run([str(binary)] + argv).returncode)
  os.execv(binary, [str(binary)] + argv)


def cmd_run(argv):
  # radiant finds its data relative to its own location, but run it from
  # the install directory anyway, like users of a release would
  exec_binary("radiant", argv, cwd=INSTALL_DIR)


def cmd_q3map2(argv):
  exec_binary("q3map2", argv)


# Commands which take no options of their own, but pass all their
# arguments on to the binary they run
PASSTHROUGH_COMMANDS = {
  "run": cmd_run,
  "q3map2": cmd_q3map2,
}


# ---------------------------------------------------------------------------
# test
# ---------------------------------------------------------------------------

class TestFailure(Exception):
  pass


class Q3Map2Runner:
  """Runs q3map2 on maps below a temporary copy of a game base directory,
  so the compile output never ends up in the source tree."""

  def __init__(self, q3map2, basepath, verbose):
    self.q3map2 = q3map2
    self.basepath = basepath
    self.verbose = verbose
    self.output = []

  def __call__(self, *args):
    """Run q3map2 for the quake3 game with the given arguments, returns
    its combined stdout and stderr; raises TestFailure if it fails."""
    cmd = [str(self.q3map2), "-fs_basepath", str(self.basepath), "-game", "quake3"]
    cmd += [str(a) for a in args]
    self.output.append("$ " + shlex.join(cmd))
    try:
      proc = subprocess.run(
        cmd, cwd=self.basepath, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace", timeout=Q3MAP2_TIMEOUT_SEC,
      )
    except subprocess.TimeoutExpired:
      raise TestFailure(f"timed out after {Q3MAP2_TIMEOUT_SEC}s: {shlex.join(cmd)}")
    self.output.append(proc.stdout)
    if self.verbose:
      print(self.output[-2] + "\n" + proc.stdout, flush=True)
    if proc.returncode != 0:
      raise TestFailure(f"exit code {proc.returncode}: {shlex.join(cmd)}")
    return proc.stdout


def test_smoke(q3map2):
  """Compile, vis and light a small sealed room which also contains an OBJ
  model, then read the BSP back in: 6 brush faces plus 1 model surface are
  expected, i.e. the model must have been loaded via assimp."""
  bsp = Path("baseq3", "maps", "box.bsp")
  q3map2("-meta", Path("baseq3", "maps", "box.map"))
  q3map2("-vis", "-fast", bsp)
  q3map2("-light", "-fast", bsp)
  info = q3map2("-info", bsp)
  match = re.search(r"^\s*(\d+) drawsurfaces", info, re.MULTILINE)
  if not match:
    raise TestFailure("no drawsurface count in the -info output")
  if int(match.group(1)) != 7:
    raise TestFailure(f"expected 7 drawsurfaces, got {match.group(1)}")


def make_regression_test(name):
  """The regression test maps are meant for manual inspection of specific
  issues, there is no reference output to compare with; so just make sure
  they still compile."""
  def test(q3map2):
    q3map2("-meta", Path("baseq3", "maps", f"{name}.map"))
  return test


def collect_tests():
  """Returns (name, source game directory, test function) of all tests."""
  tests = [("smoke", SMOKE_TEST_DIR / "baseq3", test_smoke)]
  for test_dir in sorted(p for p in REGRESSION_TESTS_DIR.iterdir() if p.is_dir()):
    tests.append((f"regression/{test_dir.name}", test_dir, make_regression_test(test_dir.name)))
  return tests


def cmd_test(args):
  tests = [t for t in collect_tests() if not args.filter or any(f in t[0] for f in args.filter)]
  if not tests:
    die("no tests match the given filter")

  if not args.no_build:
    make(["binaries-q3map2"], jobs=args.jobs)
  q3map2 = find_binary("q3map2")

  failures = []
  workdir = Path(tempfile.mkdtemp(prefix="luadiant-test-"))
  try:
    for name, game_dir, test in tests:
      # Each test gets its own base directory, holding a copy of the test's
      # game directory as 'baseq3'
      basepath = workdir / name.replace("/", "_")
      shutil.copytree(game_dir, basepath / "baseq3")
      runner = Q3Map2Runner(q3map2, basepath, args.verbose)

      start = time.monotonic()
      try:
        test(runner)
      except TestFailure as e:
        failures.append(name)
        print(f"FAIL  {name}: {e}", flush=True)
        if not args.verbose:
          # Show the tail of the output, which usually holds the error
          lines = "\n".join(runner.output).splitlines()
          print("\n".join("      " + line for line in lines[-30:]), flush=True)
      else:
        print(f"PASS  {name} ({time.monotonic() - start:.1f}s)", flush=True)
  finally:
    if args.keep:
      log(f"test output kept in {workdir}")
    else:
      shutil.rmtree(workdir, ignore_errors=True)

  print(f"\n{len(tests) - len(failures)} passed, {len(failures)} failed")
  sys.exit(1 if failures else 0)


# ---------------------------------------------------------------------------
# format
# ---------------------------------------------------------------------------

def changed_sources():
  """C/C++ sources which are modified, staged or untracked in git."""
  out = subprocess.run(
    ["git", "status", "--porcelain", "-z", "--untracked-files=all"],
    cwd=ROOT, stdout=subprocess.PIPE, check=True, text=True,
  ).stdout
  files = []
  entries = iter(out.split("\0"))
  for entry in entries:
    if not entry:
      continue
    status, path = entry[:2], entry[3:]
    if "R" in status or "C" in status:
      # Renames/copies are followed by the original path, skip it
      next(entries, None)
    if "D" in status:
      continue
    if Path(path).suffix in SOURCE_SUFFIXES:
      files.append(path)
  return files


def cmd_format(args):
  files = args.files or changed_sources()
  if not files:
    log("no changed C/C++ sources to format")
    return
  # Formatting the whole tree at once would rewrite large parts of the
  # upstream code base and cause merge conflicts, so only ever format
  # specific files
  cmd = ["uncrustify", "-c", ROOT / "uncrustify.cfg"]
  cmd.append("--check" if args.check else "--no-backup")
  run(cmd + [str(Path(f).resolve()) for f in files], cwd=Path.cwd())


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
  parser = argparse.ArgumentParser(
    description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
  )
  sub = parser.add_subparsers(dest="command", metavar="COMMAND", required=True)

  def add_jobs(p):
    p.add_argument("-j", "--jobs", type=int, help="parallel make jobs (default: number of CPUs)")

  p = sub.add_parser(
    "build", help="build via make",
    description="Build via make. TARGETs may be one of the aliases "
    + ", ".join(f"'{k}' ({v})" for k, v in TARGET_ALIASES.items())
    + " or any other Makefile target; VAR=VALUE arguments are passed to make, "
    "e.g. 'BUILD=debug' (see the top of the Makefile for all variables).",
  )
  p.add_argument("targets", nargs="*", metavar="TARGET|VAR=VALUE", help="default: all")
  p.add_argument("--no-gamepacks", action="store_true", help="don't download the gamepacks")
  add_jobs(p)
  p.set_defaults(func=cmd_build)

  p = sub.add_parser(
    "test", help="build q3map2 and run its tests",
    description="Build q3map2 and run its tests: a smoke test which compiles, "
    "vises and lights a small map containing a model, and a compile of every "
    "map in regression_tests/q3map2. Tests run in a temporary directory.",
  )
  p.add_argument("-k", dest="filter", action="append", metavar="FILTER",
                 help="only run tests whose name contains FILTER (repeatable)")
  p.add_argument("-v", "--verbose", action="store_true", help="show the q3map2 output")
  p.add_argument("--keep", action="store_true", help="keep the temporary test directory")
  p.add_argument("--no-build", action="store_true", help="test the already built q3map2")
  add_jobs(p)
  p.set_defaults(func=cmd_test)

  # Only registered for the help output, these commands are dispatched
  # before parsing, see PASSTHROUGH_COMMANDS
  sub.add_parser("run", help="run the built radiant (needs './dev.py build'), "
                 "all arguments are passed on")
  sub.add_parser("q3map2", help="run the built q3map2, all arguments are passed on")

  p = sub.add_parser(
    "format", help="format C/C++ sources with uncrustify",
    description="Format C/C++ sources in place with uncrustify and the "
    "repository's uncrustify.cfg. Without FILEs, all changed (modified, "
    "staged or untracked) C/C++ sources are formatted.",
  )
  p.add_argument("files", nargs="*", metavar="FILE")
  p.add_argument("--check", action="store_true", help="only check, don't modify files")
  p.set_defaults(func=cmd_format)

  p = sub.add_parser("clean", help="remove build artifacts (make clean)")
  p.set_defaults(func=cmd_clean)

  # Pass all arguments of the passthrough commands on verbatim; argparse
  # would otherwise try to interpret the ones starting with '-' itself
  if len(sys.argv) > 1 and sys.argv[1] in PASSTHROUGH_COMMANDS:
    PASSTHROUGH_COMMANDS[sys.argv[1]](sys.argv[2:])
    return

  args = parser.parse_args()
  args.func(args)


if __name__ == "__main__":
  main()
