"""Run the real private frame receiver classifier against a configured engine."""
import argparse
import json
from pathlib import Path
import resource
import shlex
import subprocess


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-build', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    build = args.engine_build.resolve(strict=True)
    source = Path(__file__).resolve().parents[1] / 'mozjs/js/src'
    backend = (build / 'js/src/backend.mk').read_text()
    flags, = [line.split('+=', 1)[1].strip() for line in backend.splitlines()
              if line.startswith('COMPUTED_CXXFLAGS +=')]
    # Match this engine configuration, but put the current source first so the
    # production inline classifier under test cannot come from an older cache.
    command = ['/usr/bin/c++', '-std=c++17', '-I', str(source), *shlex.split(flags),
               '-Wno-invalid-offsetof', str(Path(__file__).with_name('native_frame_receiver.cpp')),
               str(build / 'js/src/build/libjs_static.a'), '-lpthread', '-ldl',
               '-o', str(args.output)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(args.output.resolve(strict=True))], preexec_fn=no_core)
    print(json.dumps(dict(returnCode=result.returncode, passed=result.returncode == 0,
                          engineBuild=str(build), executable=str(args.output),
                          realProductionClassifier=True, accountDataUsed=False)))
    return 0 if result.returncode == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
