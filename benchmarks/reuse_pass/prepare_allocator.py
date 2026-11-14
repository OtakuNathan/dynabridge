#!/usr/bin/env python3
"""Apply the rejected allocator-policy experiment only to an out-of-tree copy."""
import argparse
from pathlib import Path
import shutil
import subprocess


def prepare(root: Path, output: Path) -> Path:
    root, output = root.resolve(), output.resolve()
    if output == root or output in root.parents or root / 'portal' in output.parents:
        raise ValueError('the experiment output must not overwrite the source checkout')
    output.mkdir(parents=True, exist_ok=True)
    target = output / 'portal'
    marker = output / '.portal-reuse-overlay'
    if target.exists():
        if not marker.is_file():
            raise FileExistsError('refusing to replace an unmarked portal directory: ' + str(target))
        shutil.rmtree(target)
    marker.write_text('Owned by Portal reuse-pass prepare_allocator.py\n')
    shutil.copytree(root / 'portal', target)
    patch = root / 'benchmarks/reuse_pass/experiments/async_allocator.patch'
    # No repository is created or modified; git apply also works outside a tree.
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=output, check=True)
    subprocess.run(['git', 'apply', str(patch)], cwd=output, check=True)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prepare(args.root, args.output)
