import os
import sys
import argparse
from collections import defaultdict

def human_readable(size, decimal_places=1):
    for unit in ['B','K','M','G','T','P']:
        if size < 1024:
            return f"{size:.{decimal_places}f}{unit}"
        size /= 1024
    return f"{size:.{decimal_places}f}E"

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
            # propagate up
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
    if count > max_files:
        return
    size = stats[path]['size']
    name = os.path.basename(path) or path
    print(f"{prefix}{name}/  [{human_readable(size)}, {count} files]")
    try:
        children = [os.path.join(path, d) for d in os.listdir(path)
                    if os.path.isdir(os.path.join(path, d))]
    except OSError:
        return
    # sort by size desc
    children.sort(key=lambda p: stats[p]['size'], reverse=True)
    for i, child in enumerate(children):
        joint = '└── ' if i == len(children)-1 else '├── '
        new_prefix = prefix + ('    ' if i == len(children)-1 else '│   ')
        print_tree(child, stats, max_files, prefix + joint)

def main():
    p = argparse.ArgumentParser(description="Tree view of file system showing largest folders, skipping those with >N files.")
    p.add_argument('root', nargs='?', default='.', help='Root directory to scan')
    p.add_argument('--max-files', '-m', type=int, default=1000,
                   help='Ignore directories with more than this many files')
    args = p.parse_args()
    root = os.path.abspath(args.root)
    stats = gather_stats(root)
    print_tree(root, stats, args.max_files)

if __name__ == '__main__':
    main()
