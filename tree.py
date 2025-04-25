import os
import sys
import argparse
from collections import defaultdict

def human_readable(size, decimal_places=1):
    for unit in ['B','K','M','G','T','P','E']:
        if size < 1024:
            return f"{size:.{decimal_places}f}{unit}"
        size /= 1024
    return f"{size:.{decimal_places}f}Z"

def gather_stats(root):
    stats = defaultdict(lambda: {'size': 0, 'count': 0})
    root = os.path.abspath(root)
    for dirpath, _, filenames in os.walk(root):
        for fname in filenames:
            fpath = os.path.join(dirpath, fname)
            try:
                sz = os.path.getsize(fpath)
            except OSError:
                continue
            curr = dirpath
            while True:
                stats[curr]['size'] += sz
                stats[curr]['count'] += 1
                if curr == root:
                    break
                curr = os.path.dirname(curr)
    return stats

def print_tree(path, stats, max_files, prefix=''):
    count = stats[path]['count']
    size = stats[path]['size']
    name = os.path.basename(path) or path
    line = f"{prefix}{name}/  [{human_readable(size)}, {count} files]"
    if prefix and count > max_files:
        print(line + f"  (skipped >{max_files} files)")
        return
    print(line)

    # get subdirectories and files
    try:
        entries = os.listdir(path)
    except OSError:
        return
    dirs = [e for e in entries if os.path.isdir(os.path.join(path, e))]
    files = [e for e in entries if os.path.isfile(os.path.join(path, e))]

    # sort dirs by size desc, files by size desc
    dirs.sort(key=lambda d: stats.get(os.path.join(path, d), {}).get('size', 0), reverse=True)
    files.sort(key=lambda f: os.path.getsize(os.path.join(path, f)), reverse=True)

    # print directories
    for i, d in enumerate(dirs):
        is_last = (i == len(dirs) - 1) and not files
        joint = '└── ' if is_last else '├── '
        new_prefix = prefix + ('    ' if is_last else '│   ')
        print_tree(os.path.join(path, d), stats, max_files, prefix + joint)

    # print files
    for i, f in enumerate(files):
        is_last = (i == len(files) - 1)
        joint = '└── ' if is_last else '├── '
        try:
            fsize = os.path.getsize(os.path.join(path, f))
        except OSError:
            fsize = 0
        print(f"{prefix}{joint}{f}  [{human_readable(fsize)}]")

def main():
    parser = argparse.ArgumentParser(
        description="Tree view with sizes, listing files, skipping dirs over N files.")
    parser.add_argument('root', nargs='?', default='.', help='Root directory to scan')
    parser.add_argument('--max-files', '-m', type=int, default=20000,
                        help='Skip dirs with more than this many files')
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    print(f"Scanning: {root} (skipping dirs > {args.max_files} files)\n")
    stats = gather_stats(root)
    print_tree(root, stats, args.max_files)

if __name__ == '__main__':
    main()
