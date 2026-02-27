#!/usr/bin/env python3
"""
Builds multiple Zephyr apps and collect firmware artifacts in one directory.
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


def sanitize_name(raw: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", raw.strip())
    cleaned = re.sub(r"_+", "_", cleaned).strip("_.-")
    return cleaned or "unnamed"


def run_command(command: list[str], cwd: Path) -> None:
    print(f"Running command in {cwd}:\n  {' '.join(command)}")
    result = subprocess.run(command, cwd=cwd)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed in {cwd}: {' '.join(command)} (exit {result.returncode})"
        )


def resolve_input_dir(input_dir: str) -> Path:
    path = Path(input_dir).expanduser()
    if not path.is_absolute():
        path = Path.cwd() / path
    path = path.resolve()

    if not path.exists():
        raise FileNotFoundError(f"Directory does not exist: {input_dir}")
    if not path.is_dir():
        raise NotADirectoryError(f"Not a directory: {input_dir}")
    return path


def build_and_collect(
    project: str, board: str, app_dirs: list[str], use_hex: bool = False
) -> int:
    project_safe = sanitize_name(project)
    board_safe = sanitize_name(board)
    output_dir = Path.cwd() / f"{project_safe}_{board_safe}_build"
    artifact_ext = "hex" if use_hex else "elf"
    artifact_name = f"zephyr.{artifact_ext}"

    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Output directory: {output_dir}")

    for app_dir_raw in app_dirs:
        app_dir = resolve_input_dir(app_dir_raw)
        app_name = sanitize_name(app_dir.name)
        build_dir = app_dir / "build"
        overlay_config = app_dir / "overlay-802154.conf"

        if build_dir.exists():
            print(f"[{app_name}] Cleaning {build_dir}")
            shutil.rmtree(build_dir)

        print(f"[{app_name}] Building for board '{board}'")
        command = [
            "west",
            "build",
            "-p",
            "always",
            "-b",
            board,
            "-s",
            str(app_dir),
            "-d",
            str(build_dir),
        ]

        if overlay_config.exists():
            command.extend(["--", f"-DOVERLAY_CONFIG={overlay_config.name}"])

        run_command(command, cwd=app_dir)

        artifact_path = build_dir / "zephyr" / artifact_name
        if not artifact_path.exists():
            raise FileNotFoundError(
                f"[{app_name}] Build finished but {artifact_ext.upper()} not found: {artifact_path}"
            )

        destination = output_dir / f"{app_name}_zephyr.{artifact_ext}"
        shutil.copy2(artifact_path, destination)
        print(f"[{app_name}] Collected -> {destination}")

    print(
        f"\nDone. Collected {len(app_dirs)} {artifact_ext.upper()} file(s) in {output_dir}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Clean, build, and collect Zephyr firmware files from multiple app directories"
        ),
        epilog=(
            "Example:\n"
            "  ./py/multi_west_build.py myproj nrf52840dk/nrf52840 "
            "apps/simple_client apps/simple_server"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("project_name", help="Project name used for output directory naming")
    parser.add_argument("board", help="Zephyr board target passed to west build -b")
    parser.add_argument(
        "directories",
        nargs="+",
        help="One or more Zephyr app directories to build",
    )
    parser.add_argument(
        "--hex",
        action="store_true",
        help="Collect HEX artifacts instead of ELF (default: ELF)",
    )

    args = parser.parse_args()

    try:
        return build_and_collect(
            args.project_name,
            args.board,
            args.directories,
            use_hex=args.hex,
        )
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())