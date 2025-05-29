import ast, sys


def check_imports(filename):
    with open(filename, "r") as f:
        lines = f.readlines()
    tree = ast.parse("".join(lines), filename=filename)
    body = tree.body

    # 1) Skip any leading module docstring / standalone strings
    i = 0
    while (
        i < len(body)
        and isinstance(body[i], ast.Expr)
        and isinstance(body[i].value, ast.Constant)
        and isinstance(body[i].value.value, str)
    ):
        i += 1

    # 2) Collect top-level imports
    importfrom_nodes = []
    import_nodes = []
    while i < len(body):
        node = body[i]
        if isinstance(node, ast.ImportFrom):
            importfrom_nodes.append(node)
        elif isinstance(node, ast.Import):
            import_nodes.append(node)
        else:
            break
        i += 1

    # 3) No imports at all → OK
    if not importfrom_nodes and not import_nodes:
        print("✅ No imports found. Checks result are fine.")
        return

    # Helper to extract source lines
    def extract(ns):
        return [lines[n.lineno - 1].rstrip("") for n in ns]

    from_lines = extract(importfrom_nodes)
    imp_lines = extract(import_nodes)

    # Helper to ensure contiguous lines (no blank lines within block)
    def check_contiguous(nodes, kind):
        if len(nodes) <= 1:
            return
        linenos = [n.lineno for n in nodes]
        for prev, curr in zip(linenos, linenos[1:]):
            if curr - prev != 1:
                print(
                    f"❌ `{kind}` block contains blank lines or interruptions between imports."
                )
                print(f"    Found gap between lines {prev} and {curr}.")
                sys.exit(1)

    # 4) Check sorted only if >1 lines, descending by length
    def check_sorted(block_lines, kind):
        if len(block_lines) <= 1:
            return
        sorted_block = sorted(block_lines, key=len, reverse=True)
        if block_lines != sorted_block:
            print(f"❌ `{kind}` block is not sorted by line-length (longest first):")
            for l in block_lines:
                print(f"    {l}")
            print("  should be:")
            for l in sorted_block:
                print(f"    {l}")
            sys.exit(1)

    # 5) Single-type imports
    if not importfrom_nodes:
        # ensure no blank lines among imports
        check_contiguous(import_nodes, "import")
        check_sorted(imp_lines, "import")
        print("✅ `import` block sorted by column size (longest first) and contiguous.")
        return
    if not import_nodes:
        check_contiguous(importfrom_nodes, "from ... import")
        check_sorted(from_lines, "from ... import")
        print(
            "✅ `from ... import` block sorted by column size (longest first) and contiguous."
        )
        return

    # 6) Both present → enforce grouping + single blank line
    if importfrom_nodes[-1].lineno > import_nodes[0].lineno:
        print(
            "❌ Mixed import types: `import` found before end of `from ... import` block."
        )
        sys.exit(1)

    last_from = importfrom_nodes[-1].lineno
    first_imp = import_nodes[0].lineno
    if first_imp - last_from != 2 or lines[last_from].strip() != "":
        print(
            f"❌ Expected exactly one blank line between line {last_from} and {first_imp}:"
        )
        print(f"    line {last_from + 1!r} → {lines[last_from]!r}")
        sys.exit(1)

    # 7) Check contiguous within each block
    check_contiguous(importfrom_nodes, "from ... import")
    check_contiguous(import_nodes, "import")

    # 8) Finally, check each block descending
    check_sorted(from_lines, "from ... import")
    check_sorted(imp_lines, "import")

    print(
        "✅ Imports are correctly grouped, separated by one blank line, contiguous, and sorted by column size (longest first)."
    )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <python_file.py>")
        sys.exit(1)
    check_imports(sys.argv[1])
