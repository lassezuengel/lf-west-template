#!/usr/bin/env python3
"""
flash-multi.py - Flash multiple firmware files using flash-ocd.py

This script runs flash-ocd.py for up to four ELF files, each assigned to a unique board ID.
Board IDs are assigned in order: 684, 513, 689, 664
"""

import argparse
import subprocess
import sys
from pathlib import Path

# Board IDs in order of assignment
BOARD_IDS = ["684", "513", "689", "664"]

# ANSI color codes
RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
BOLD = "\033[1m"
RESET = "\033[0m"

def flash_elf(flash_script, elf_path, board_id, index, total):
    """Flash a single ELF file to a board"""
    print(f"\n{BOLD}{BLUE}{'='*70}{RESET}")
    print(f"{BOLD}{BLUE}Flashing [{index}/{total}]: {elf_path} -> Board {board_id}{RESET}")
    print(f"{BOLD}{BLUE}{'='*70}{RESET}\n")

    try:
        result = subprocess.run(
            [sys.executable, flash_script, elf_path, board_id],
            check=True
        )
        print(f"{BOLD}{GREEN}Successfully flashed {elf_path} to board {board_id}{RESET}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"{BOLD}{RED}Failed to flash {elf_path} to board {board_id}{RESET}")
        return False
    except Exception as e:
        print(f"{BOLD}{RED}Error: {e}{RESET}")
        return False

def main():
    parser = argparse.ArgumentParser(
        description="Flash multiple ELF files to different boards",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s file1.elf file2.elf file3.elf
  %(prog)s firmware/*.elf
  %(prog)s --script ./flash-ocd.py app1.elf app2.elf

Board assignment order: 684, 513, 689, 664
        """
    )
    parser.add_argument(
        "elfs",
        nargs="+",
        help="ELF files to flash (1-4 files)"
    )
    parser.add_argument(
        "--script",
        default="./flash-ocd.py",
        help="Path to flash-ocd.py script (default: ./flash-ocd.py)"
    )
    args = parser.parse_args()

    # Validate number of ELF files
    if len(args.elfs) > 4:
        print(f"{BOLD}{RED}Error: Maximum 4 ELF files allowed, got {len(args.elfs)}{RESET}")
        sys.exit(1)

    # Check if flash script exists
    flash_script = Path(args.script)
    if not flash_script.exists():
        print(f"{BOLD}{RED}Error: Flash script not found: {flash_script}{RESET}")
        sys.exit(1)

    # Flash each ELF file
    total = len(args.elfs)
    results = []

    print(f"{BOLD}{BLUE}Starting flash sequence for {total} file(s)...{RESET}")

    for i, elf_path in enumerate(args.elfs, 1):
        board_id = BOARD_IDS[i - 1]
        success = flash_elf(str(flash_script), elf_path, board_id, i, total)
        results.append((elf_path, board_id, success))

    # Summary
    print(f"\n{BOLD}{BLUE}{'='*70}{RESET}")
    print(f"{BOLD}{BLUE}SUMMARY{RESET}")
    print(f"{BOLD}{BLUE}{'='*70}{RESET}")

    for elf_path, board_id, success in results:
        status = f"{GREEN}SUCCESS" if success else f"{RED}FAILED"
        print(f"{status}{RESET} - {elf_path} (Board {board_id})")

    # Exit with appropriate code
    all_success = all(success for _, _, success in results)
    if all_success:
        print(f"\n{BOLD}{GREEN}All flashing operations completed successfully!{RESET}")
        sys.exit(0)
    else:
        print(f"\n{BOLD}{YELLOW}Some flashing operations failed.{RESET}")
        sys.exit(1)

if __name__ == "__main__":
    main()