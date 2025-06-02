import os
import sys
import argparse
from collections import defaultdict


def human_readable(size, decimal_places=1):
    for unit in ["B", "K", "M", "G", "T", "P", "E"]:
        if size < 1024:
            return f"{size:.{decimal_places}f}{unit}"
        size /= 1024
    return f"{size:.{decimal_places}f}Z"


def gather_stats(root, ignore_list):
    stats = defaultdict(lambda: {"size": 0, "count": 0})
    root = os.path.abspath(root)
    for dirpath, dirnames, filenames in os.walk(root, topdown=True):
        # modify dirnames in-place to skip ignored
        dirnames[:] = [d for d in dirnames if d not in ignore_list]
        for fname in filenames:
            fpath = os.path.join(dirpath, fname)
            try:
                sz = os.path.getsize(fpath)
            except OSError:
                continue
            curr = dirpath
            while True:
                stats[curr]["size"] += sz
                stats[curr]["count"] += 1
                if curr == root:
                    break
                curr = os.path.dirname(curr)
    return stats


def print_tree(path, stats, max_files, ignore_list, prefix=""):
    # skip if ignored
    if os.path.basename(path) in ignore_list and prefix:
        return
    count = stats.get(path, {}).get("count", 0)
    size = stats.get(path, {}).get("size", 0)
    name = os.path.basename(path) or path
    line = f"{prefix}{name}/  [{human_readable(size)}, {count} files]"
    if prefix and count > max_files:
        print(line + f"  (skipped >{max_files} files)")
        return
    print(line)

    try:
        entries = os.listdir(path)
    except OSError:
        return
    # filter out ignored dirs here as well
    dirs = [
        e
        for e in entries
        if os.path.isdir(os.path.join(path, e)) and e not in ignore_list
    ]
    files = [e for e in entries if os.path.isfile(os.path.join(path, e))]

    dirs.sort(
        key=lambda d: stats.get(os.path.join(path, d), {}).get("size", 0), reverse=True
    )
    files.sort(key=lambda f: os.path.getsize(os.path.join(path, f)), reverse=True)

    for i, d in enumerate(dirs):
        is_last = (i == len(dirs) - 1) and not files
        joint = "└── " if is_last else "├── "
        new_prefix = prefix + ("    " if is_last else "│   ")
        print_tree(os.path.join(path, d), stats, max_files, ignore_list, prefix + joint)

    for i, f in enumerate(files):
        is_last = i == len(files) - 1
        joint = "└── " if is_last else "├── "
        try:
            fsize = os.path.getsize(os.path.join(path, f))
        except OSError:
            fsize = 0
        print(f"{prefix}{joint}{f}  [{human_readable(fsize)}]")


def main():
    parser = argparse.ArgumentParser(
        description="Tree view with sizes, listing files, skipping dirs over N files and ignore list."
    )
    parser.add_argument("root", nargs="?", default=".", help="Root directory to scan")
    parser.add_argument(
        "--max-files",
        "-m",
        type=int,
        default=1000,
        help="Skip dirs with more than this many files",
    )
    parser.add_argument(
        "--ignore",
        "-i",
        nargs="+",
        default=[],
        help="List of directory names to ignore",
    )
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    ignore_list = args.ignore
    print(
        f"Scanning: {root} (skipping dirs > {args.max_files} files; ignoring: {ignore_list})\n"
    )
    stats = gather_stats(root, ignore_list)
    print_tree(root, stats, args.max_files, ignore_list)


if __name__ == "__main__":
    main()
