import os

def print_tree(root_path, prefix=""):
    entries = sorted(os.listdir(root_path))
    entries_count = len(entries)

    for i, entry in enumerate(entries):
        path = os.path.join(root_path, entry)
        connector = "└── " if i == entries_count - 1 else "├── "
        print(prefix + connector + entry)

        if os.path.isdir(path):
            extension = "    " if i == entries_count - 1 else "│   "
            print_tree(path, prefix + extension)

if __name__ == "__main__":
    import sys
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    print(directory)
    print_tree(directory)
