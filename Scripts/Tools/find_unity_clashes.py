#!/usr/bin/env python3
"""Finds latent unity-build name clashes in the project's C++ modules.

Unreal's unity build pastes several .cpp files of one module into one translation unit ("blob"). File-local names that are
fine on their own then collide: two anonymous namespaces in one blob are the SAME namespace, two `static` functions with
the same name are a redefinition, two `#define X` without `#undef X` redefine a macro. Which files share a blob changes with
the file list and with adaptive unity (files changed in git are compiled alone), so a branch can build while main fails.

This script lists every name that two .cpp files of the same module define at namespace scope (token level, no compiler,
a few seconds):
  - functions (anonymous namespace, `static`, `inline`, `constexpr`, or plain definitions in a named namespace),
  - variables / constants (`constexpr`, `const`, `static`, `TAutoConsoleVariable`, `FAutoConsoleCommand`, ...),
  - structs / classes / unions / enums defined in a .cpp,
  - `using X = ...;` / `typedef` aliases,
  - names made by macros at namespace scope (`IMPLEMENT_*_AUTOMATION_TEST(F...)`, `DEFINE_LOG_CATEGORY_STATIC(Log...)`,
    project wrappers like `GAMEFLOW_TEST(F...)`),
  - macros `#define`d in two files when at least one of them does not `#undef` it (e.g. LOCTEXT_NAMESPACE),
  - a file-local (internal) name of a .cpp that equals a namespace-scope name declared in a project header (a later file
    of the blob includes that header: the call becomes ambiguous).
Anonymous namespaces are transparent (their names live in the enclosing namespace); a `using namespace N;` anywhere in a
file of the module (namespace scope or a function body) also makes N's names visible next to the outer ones (it leaks
into the following files of a blob / makes their unqualified uses ambiguous).
Names inside class bodies and function bodies are ignored. Out-of-line member definitions (`AFoo::Bar`) are ignored.
Overloads in different files are still reported (rename them: a unity blob makes calls ambiguous easily).

Usage:
  python Scripts/Tools/find_unity_clashes.py [--root <repo>] [--module CodexTactics] [--warn-only] [--quiet] [--leaks]
  python Scripts/Tools/find_unity_clashes.py --dump <file.cpp>     every name the scanner sees in one file
  python Scripts/Tools/find_unity_clashes.py --self-test           checks the scanner on built-in samples
Exit code: 1 when a clash was found (0 with --warn-only), 0 otherwise.
"""

import argparse
import os
import re
import shutil
import sys
import tempfile
from collections import defaultdict

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT_DEFAULT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BOM = chr(0xFEFF)
IDENT_RE = re.compile(r"^[A-Za-z_]\w*$")

TYPE_KEYWORDS = {"struct", "class", "union", "enum"}
DECL_SPECIFIERS = {
    "static", "inline", "constexpr", "consteval", "constinit", "const", "volatile", "mutable", "thread_local",
    "FORCEINLINE", "FORCENOINLINE", "extern", "unsigned", "signed", "typename", "virtual", "explicit",
}
TRAILING_FUNC_QUALIFIERS = {"const", "noexcept", "override", "final", "volatile", "&", "&&", "throw"}
# Macro invocations at namespace scope whose first argument is the name they define.
DEFINING_MACRO_RE = re.compile(r"^(IMPLEMENT_\w+|DEFINE_\w+|DECLARE_\w+|BEGIN_DEFINE_SPEC|\w+_TEST|\w+_SPEC)$")
MACRO_STATEMENT_RE = re.compile(r"^[A-Z][A-Z0-9_]{2,}$")
API_MACRO_RE = re.compile(r"^[A-Z][A-Z0-9_]*_API$")

TOKEN_RE = re.compile(
    r"""
    (?P<ws>[ \t\r\f\v]+)
  | (?P<nl>\n)
  | (?P<ident>[A-Za-z_]\w*)
  | (?P<num>\.?\d[\w.']*(?:[eEpP][+-]\d+)?)
  | (?P<op>::|->|&&|\|\||\.\.\.|[{}()\[\];,<>=&*~!%^|?:+\-/.#])
  | (?P<other>.)
    """,
    re.X,
)


def strip_comments_and_strings(text):
    """Blanks comments and the contents of string / char literals (keeps newlines so line numbers stay right)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if m:
                end = text.find(")" + m.group(1) + '"', i)
                end = n if end < 0 else end + len(m.group(1)) + 2
                out.append('""' + "".join("\n" if ch == "\n" else " " for ch in text[i + 2:end]))
                i = end
            else:
                out.append(c)
                i += 1
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * (j - i - 2) + c if j - i >= 2 else c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def split_preprocessor(text):
    """Returns (code with directive lines blanked, list of (line, 'define' | 'undef', name))."""
    lines = text.split("\n")
    directives = []
    i = 0
    while i < len(lines):
        stripped = lines[i].lstrip()
        if stripped.startswith("#"):
            start = i
            full = stripped
            while lines[i].rstrip().endswith("\\") and i + 1 < len(lines):
                lines[i] = ""
                i += 1
                full += " " + lines[i]
            lines[i] = ""
            m = re.match(r"#\s*(define|undef)\s+([A-Za-z_]\w*)", full)
            if m:
                directives.append((start + 1, m.group(1), m.group(2)))
        i += 1
    return "\n".join(lines), directives


def tokenize(code):
    tokens = []
    line = 1
    for m in TOKEN_RE.finditer(code):
        kind = m.lastgroup
        if kind == "nl":
            line += 1
        elif kind == "other" and m.group() not in "\"'":
            continue  # stray characters (`@`, `$`, `\`, non-ASCII) carry no names
        elif kind != "ws":
            tokens.append((m.group(), line))
    return tokens


def skip_balanced(tokens, i, open_tok, close_tok):
    """tokens[i] == open_tok; returns the index after the matching close_tok."""
    depth = 0
    n = len(tokens)
    while i < n:
        t = tokens[i][0]
        if t == open_tok:
            depth += 1
        elif t == close_tok:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return n


def strip_template_prefix(toks):
    """Drops leading `template <...>` groups and [[attributes]]."""
    changed = True
    while changed and toks:
        changed = False
        if toks[0] == "template" and len(toks) > 1 and toks[1] == "<":
            depth, j = 0, 1
            while j < len(toks):
                if toks[j] == "<":
                    depth += 1
                elif toks[j] == ">":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            toks = toks[j + 1:]
            changed = True
        elif toks[0] == "[" and len(toks) > 1 and toks[1] == "[":
            j = 2
            while j + 1 < len(toks) and not (toks[j] == "]" and toks[j + 1] == "]"):
                j += 1
            toks = toks[j + 2:]
            changed = True
    return toks


def top_level_split(toks, sep):
    """Splits a token list at `sep` outside (), [], {}, <> (angle depth is a heuristic)."""
    parts, cur = [], []
    depth = 0
    angle = 0
    for t in toks:
        if t in ("(", "[", "{"):
            depth += 1
        elif t in (")", "]", "}"):
            depth -= 1
        elif t == "<" and depth == 0:
            angle += 1
        elif t == ">" and depth == 0 and angle > 0:
            angle -= 1
        if t == sep and depth == 0 and angle == 0:
            parts.append(cur)
            cur = []
        else:
            cur.append(t)
    parts.append(cur)
    return parts


def top_level_tokens(toks):
    """Top-level tokens only (contents of (), [], {} removed)."""
    out = []
    depth = 0
    for t in toks:
        if t in ("(", "[", "{"):
            depth += 1
            continue
        if t in (")", "]", "}"):
            depth -= 1
            continue
        if depth == 0:
            out.append(t)
    return out


def last_paren_group(toks):
    """Index range (open, close) of the last top-level (...) group, or None."""
    depth = 0
    close = None
    for j in range(len(toks) - 1, -1, -1):
        t = toks[j]
        if t == ")":
            if depth == 0:
                close = j
            depth += 1
        elif t == "(":
            depth -= 1
            if depth == 0 and close is not None:
                return j, close
    return None


def first_paren_group(toks):
    depth = 0
    open_ = None
    for j, t in enumerate(toks):
        if t == "(":
            if depth == 0:
                open_ = j
            depth += 1
        elif t == ")":
            depth -= 1
            if depth == 0 and open_ is not None:
                return open_, j
    return None


def name_before(toks, j):
    """The identifier right before index j and whether it is qualified (`X::Name`) / special (operator, destructor)."""
    k = j - 1
    if k < 0 or not IDENT_RE.match(toks[k]):
        return None, True  # `operator==(`, `operator()(` ...
    name = toks[k]
    if name == "operator" or (k >= 1 and toks[k - 1] in ("operator", "~")):
        return None, True
    qualified = k >= 1 and toks[k - 1] == "::"
    return name, qualified


def is_function_head(toks):
    """`... Name(params) [const noexcept override] [-> Ret]` -> (open, close) of the parameter list, else None."""
    if "=" in top_level_tokens(toks):
        return None
    grp = last_paren_group(toks)
    if grp is None:
        return None
    _, close = grp
    tail = toks[close + 1:]
    if "->" in tail:
        # trailing return type: the parameter list is the paren group right before `->`
        arrow = close + 1 + tail.index("->")
        grp2 = last_paren_group(toks[:arrow])
        if grp2 is None or grp2[1] != arrow - 1:
            return None
        return grp2
    for t in tail:
        if t not in TRAILING_FUNC_QUALIFIERS and t not in ("(", ")") and not re.match(r"^[A-Z_][A-Z0-9_]*$", t):
            return None
    if tail and tail[0] == "(":
        return None
    return grp


def cut_at_ctor_init(toks):
    """`A::A(args) : Super(x), B(y)` -> `A::A(args)` (head ends at the top-level `:`)."""
    depth = 0
    for j, t in enumerate(toks):
        if t in ("(", "[", "{"):
            depth += 1
        elif t in (")", "]", "}"):
            depth -= 1
        elif t == ":" and depth == 0:
            return toks[:j]
    return toks


class Record:
    __slots__ = ("name", "kind", "ns", "file", "line", "internal")

    def __init__(self, name, kind, ns, file, line, internal):
        self.name, self.kind, self.ns, self.file, self.line, self.internal = name, kind, ns, file, line, internal


class FileScan:
    def __init__(self, path, rel, is_header):
        self.path = path
        self.rel = rel
        self.is_header = is_header
        self.records = []
        self.using_namespaces = []  # (scope tuple, namespace tuple)
        self.macro_defines = defaultdict(list)  # name -> [line]
        self.macro_undefs = defaultdict(list)


def scan_file(path, rel, is_header=False):
    """Collects the namespace-scope names of one file (see the module doc)."""
    scan = FileScan(path, rel, is_header)
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read().replace(BOM, "")  # a UTF-8 BOM would glue onto the first token
    text = strip_comments_and_strings(text)
    code, directives = split_preprocessor(text)
    for line, kind, name in directives:
        (scan.macro_defines if kind == "define" else scan.macro_undefs)[name].append(line)
    tokens = tokenize(code)

    # Only namespaces are pushed (None = anonymous / extern "C": transparent); class, function and initializer bodies
    # are skipped wholesale.
    ns_stack = []
    stmt = []  # list of (tok, line)
    namespace_scope_usings = set()
    i = 0
    n = len(tokens)

    def ns_path():
        return tuple(part for x in ns_stack if x for part in x.split("::"))

    def in_anonymous():
        return any(x is None for x in ns_stack)

    def record(name, kind, line, internal):
        if name and IDENT_RE.match(name) and name not in DECL_SPECIFIERS and name not in TYPE_KEYWORDS:
            scan.records.append(Record(name, kind, ns_path(), rel, line, internal or in_anonymous()))

    def process_statement(st):
        """A namespace-scope statement that ended with `;` (st holds its tokens without the `;`)."""
        toks = [t for t, _ in st]
        lines = [l for _, l in st]
        stripped = strip_template_prefix(toks)
        lines = lines[len(toks) - len(stripped):]
        toks = stripped
        if not toks:
            return
        first, line0 = toks[0], lines[0]
        if first == "__type__":
            # `struct FX {...} Var, *Ptr;` - variables of the type just defined
            for part in top_level_split(toks[1:], ","):
                idents = [t for t in top_level_split(part, "=")[0] if IDENT_RE.match(t)]
                if idents:
                    record(idents[-1], "variable", line0, not is_header)
            return
        if first == "using":
            if len(toks) > 1 and toks[1] == "namespace":
                target = tuple(t for t in toks[2:] if t != "::")
                scan.using_namespaces.append((ns_path(), target))
                namespace_scope_usings.add(line0)
            elif len(toks) > 2 and toks[2] == "=":
                record(toks[1], "alias", line0, not is_header)
            return
        if first == "typedef":
            grp = first_paren_group(toks)
            if grp and grp[0] + 2 < len(toks) and toks[grp[0] + 1] == "*":
                record(toks[grp[0] + 2], "alias", line0, not is_header)  # `typedef R (*Name)(...)`
            else:
                idents = [t for t in toks if IDENT_RE.match(t)]
                if idents:
                    record(idents[-1], "alias", line0, not is_header)
            return
        if first in ("static_assert", "friend", "namespace", "return", "public", "private", "protected"):
            return
        top = top_level_tokens(toks)
        if first == "extern" and "=" not in top:
            return  # a declaration of something defined elsewhere
        core = [t for t in toks if t not in DECL_SPECIFIERS]
        if core and core[0] in TYPE_KEYWORDS and "=" not in top and "(" not in core and len(top) <= 6:
            return  # forward declaration `class FFoo;` / `enum class EFoo : uint8;`
        is_static = "static" in toks
        is_const = ("const" in top or "constexpr" in top) and first != "extern"
        grp = is_function_head(cut_at_ctor_init(toks))
        if grp is not None:
            name, qualified = name_before(toks, grp[0])
            if qualified or name is None:
                return
            # A variable built with constructor arguments (`static FAutoConsoleCommand Cmd(TEXT(..), ..);`) looks like a
            # function declaration: its "parameters" are expressions (strings, numbers, literals).
            params = toks[grp[0] + 1:grp[1]]
            looks_like_ctor = any(t.startswith('"') or t.startswith("'") for t in params) or any(
                re.match(r"^\d", t) for t in params) or any(t in ("TEXT", "nullptr", "true", "false") for t in params)
            if looks_like_ctor:
                record(name, "variable", line0, is_static or not is_header)
            elif is_header:
                record(name, "function", line0, is_static)  # header declaration: visible to every includer
            return  # a .cpp forward declaration: its definition records the name
        # variables: one record per declarator (`const float A = 1.f, B = 2.f;`)
        for part in top_level_split(toks, ","):
            decl = top_level_split(part, "=")[0]
            # brace / paren / array init `T Name{...}` / `T Name(...)` / `T Name[N]`
            while decl and decl[-1] in (")", "]", "}"):
                closer = decl[-1]
                opener = {")": "(", "]": "[", "}": "{"}[closer]
                depth, j = 0, len(decl) - 1
                while j >= 0:
                    if decl[j] == closer:
                        depth += 1
                    elif decl[j] == opener:
                        depth -= 1
                        if depth == 0:
                            break
                    j -= 1
                decl = decl[:max(j, 0)]
            idents = [(k, t) for k, t in enumerate(decl) if IDENT_RE.match(t)]
            if not idents:
                continue
            k, name = idents[-1]
            if k >= 1 and decl[k - 1] == "::":
                continue  # out-of-line static member definition `float AFoo::Bar = 1.f;`
            if part is toks or len(idents) >= 2 or decl[-1:] == [name]:
                record(name, "variable", line0, is_static or is_const or not is_header)

    while i < n:
        tok, line = tokens[i]
        if not stmt and MACRO_STATEMENT_RE.match(tok) and tok not in DECL_SPECIFIERS:
            # A macro invocation at statement start (`IMPLEMENT_SIMPLE_AUTOMATION_TEST(...)` has no `;`).
            j = i + 1
            if j < n and tokens[j][0] == "(":
                end = skip_balanced(tokens, j, "(", ")")
                args = [t for t, _ in tokens[j + 1:end - 1]]
                if DEFINING_MACRO_RE.match(tok) and args and IDENT_RE.match(args[0]) and (
                        len(args) == 1 or args[1] == ","):
                    # `DEFINE_LOG_CATEGORY(X)` in a .cpp defines the header's `DECLARE_LOG_CATEGORY_EXTERN(X)`: external
                    external = tok.startswith("DEFINE_") and not tok.endswith("_STATIC")
                    record(args[0], "macro " + tok, line, tok.endswith("_STATIC") or (not is_header and not external))
                i = end
                if i < n and tokens[i][0] == ";":
                    i += 1
                continue
            i += 1
            continue
        if tok == ";":
            process_statement(stmt)
            stmt = []
            i += 1
            continue
        if tok == "}":
            if ns_stack:
                ns_stack.pop()
            stmt = []
            i += 1
            continue
        if tok in ("(", "["):
            end = skip_balanced(tokens, i, tok, ")" if tok == "(" else "]")
            stmt.extend(tokens[i:end])
            i = end
            continue
        if tok == "{":
            toks = [t for t, _ in stmt]
            if toks and (toks[0] == "namespace" or toks[:2] == ["inline", "namespace"]):
                names = [t for t in toks if t not in ("namespace", "inline", "::") and IDENT_RE.match(t)]
                ns_stack.append("::".join(names) if names else None)  # `namespace A::B {` is one entry (one `}`)
                stmt = []
                i += 1
                continue
            if toks[:1] == ["extern"] and len(toks) <= 2:
                ns_stack.append(None)  # extern "C" { ... }
                stmt = []
                i += 1
                continue
            end = skip_balanced(tokens, i, "{", "}")
            head = strip_template_prefix(toks)
            top = top_level_tokens(head)
            core = [t for t in head if t not in DECL_SPECIFIERS]
            if "=" in top:
                stmt.extend(tokens[i:end])  # an initializer (`= {...}`, a lambda): the statement goes on to its `;`
                i = end
                continue
            if core and core[0] in TYPE_KEYWORDS:
                kw_idx = 2 if core[0] == "enum" and len(core) > 1 and core[1] in ("class", "struct") else 1
                name = None
                for t in core[kw_idx:]:
                    if t in (":", "final"):
                        break
                    if IDENT_RE.match(t) and not API_MACRO_RE.match(t) and t != "alignas":
                        name = t
                if name:
                    record(name, "type", stmt[0][1], not is_header)
                # `struct X {...} Var;`: the rest of the statement is a variable of that type
                stmt = [("__type__", line)]
                i = end
                continue
            fhead = cut_at_ctor_init(head)
            grp = is_function_head(fhead)
            if grp is not None:
                name, qualified = name_before(fhead, grp[0])
                if name and not qualified:
                    internal = "static" in head or "inline" in head or "constexpr" in head or not is_header
                    # a plain function DEFINED in a .cpp may be the definition of a header declaration: not internal
                    if not is_header and not any(t in head for t in ("static", "inline", "constexpr")):
                        internal = False
                    record(name, "function", stmt[0][1] if stmt else line, internal)
                stmt = []
                i = end
                continue
            stmt.extend(tokens[i:end])  # `T Name{...}` brace-initialised variable
            i = end
            continue
        stmt.append((tok, line))
        i += 1
    # `using namespace N;` inside function bodies (skipped above): N's names become visible next to the outer ones.
    for m in re.finditer(r"\busing\s+namespace\s+([A-Za-z_][\w:]*)\s*;", code):
        line = code.count("\n", 0, m.start()) + 1
        if line not in namespace_scope_usings:
            scan.using_namespaces.append(((), tuple(x for x in m.group(1).split("::") if x)))
    return scan


def module_of(rel):
    parts = rel.replace("\\", "/").split("/")
    return parts[1] if len(parts) > 2 and parts[0] == "Source" else parts[0]


def collect(root, modules=None):
    src = os.path.join(root, "Source")
    cpps, headers = [], []
    for dirpath, _, files in os.walk(src):
        for fn in sorted(files):
            if not (fn.endswith(".cpp") or fn.endswith(".h")):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root).replace("\\", "/")
            if fn.endswith(".h"):
                headers.append(scan_file(full, rel, True))
            elif not modules or module_of(rel) in modules:
                cpps.append(scan_file(full, rel, False))
    return cpps, headers


def find_clashes(root, modules=None):
    cpps, headers = collect(root, modules)
    by_module = defaultdict(list)
    for s in cpps:
        by_module[module_of(s.rel)].append(s)

    clashes = []  # (module, key, [records])
    macro_clashes = []  # (module, name, [(file, line, leaks)])
    leaks = []  # (module, name, file, line)
    for module, mscans in sorted(by_module.items()):
        usings = {(scope, target) for s in mscans for scope, target in s.using_namespaces if target}

        def keys_of(r):
            keys = {(r.ns, r.name)}
            for scope, target in usings:
                # `using namespace N` at scope P makes P::N::x (or N::x) visible as P::x
                if r.ns and r.ns != scope and r.ns[-len(target):] == target:
                    keys.add((scope, r.name))
            return keys

        visible = defaultdict(list)  # (scope, name) -> .cpp records
        for s in mscans:
            seen = set()
            for r in s.records:
                for key in keys_of(r):
                    if key not in seen:
                        seen.add(key)
                        visible[key].append(r)
        header_visible = defaultdict(list)
        for h in headers:
            seen = set()
            for r in h.records:
                for key in keys_of(r):
                    if key not in seen:
                        seen.add(key)
                        header_visible[key].append(r)
        for key in sorted(set(visible) | set(header_visible)):
            recs = visible.get(key, [])
            if len({r.file for r in recs}) > 1:
                clashes.append((module, key, recs))
                continue
            internal = [r for r in recs if r.internal]
            if internal and key in header_visible:
                clashes.append((module, key, internal + header_visible[key]))
        defs = defaultdict(list)
        for s in mscans:
            for name, def_lines in s.macro_defines.items():
                undefs = s.macro_undefs.get(name, [])
                leaks_here = any(not any(u > d for u in undefs) for d in def_lines)
                defs[name].append((s.rel, def_lines[0], leaks_here))
                if leaks_here:
                    leaks.append((module, name, s.rel, def_lines[0]))
        for name, entries in sorted(defs.items()):
            if len(entries) > 1 and any(e[2] for e in entries):
                macro_clashes.append((module, name, entries))
    return cpps, clashes, macro_clashes, leaks


SELF_TEST_FILES = {
    "Source/ModA/Private/One.cpp": """\
#include "X.h"
#define LOCTEXT_NAMESPACE "One"
namespace
{
	constexpr float Margin = 16.f;
	const FLinearColor PanelColor(0.f, 0.f, 0.f, 0.5f);
	TAutoConsoleVariable<bool> CVarShared(TEXT("A.B"), true, TEXT("x"));
	struct FHelper { int32 Inner = 0; void Member() {} };
	using FAlias = TArray<int32>;
	float Dist2D(const FVector& A, const FVector& B) { return 0.f; }
	enum class EMode : uint8 { A, B };
	const TArray<FName> Names = { TEXT("a"), TEXT("b") };
	auto Lambda = [](int32 X) { return X; };
	FVector Braced{1.f, 2.f, 3.f};
}
static void StaticHelper() {}
DEFINE_LOG_CATEGORY_STATIC(LogShared, Log, All);
namespace NamedLocal
{
	constexpr int32 Limit = 3;
	template <typename T> T Twice(T V) { return V * 2; }
}
static FAutoConsoleCommand SharedCmd(TEXT("A.Cmd"), TEXT("x"), FConsoleCommandDelegate::CreateLambda([]() { int32 Inside = 0; }));
auto Trailing(int32 X) -> int32 { return X; }
UNIQUE_TEST(FSharedTest, "A.B")
bool FSharedTest::RunTest(const FString&) { return true; }
AFoo::AFoo() : Super() {}
void AFoo::Member() const { static int32 LocalStatic = 0; }
int32 Unique1 = 0;
""",
    "Source/ModA/Private/Two.cpp": """\
#define LOCTEXT_NAMESPACE "Two"
namespace
{
	constexpr float Margin = 8.f;
	const FLinearColor PanelColor = FLinearColor::Black;
	static TAutoConsoleVariable<bool> CVarShared(TEXT("A.C"), true, TEXT("x"));
	class FHelper final : public FOther {};
	typedef TMap<int32, int32> FAlias;
	inline float Dist2D(const FVector& A) { return 0.f; }
	enum EMode { C, D };
	static const TArray<FName> Names{ TEXT("c") };
	TFunction<int32(int32)> Lambda;
	FVector Braced = FVector::ZeroVector;
}
namespace { void StaticHelper(); }
void StaticHelper() {}
DEFINE_LOG_CATEGORY_STATIC(LogShared, Log, All);
namespace NamedLocal
{
	static const int32 Limit = 4;
	int32 Twice(int32 V) { return V; }
}
FAutoConsoleCommand SharedCmd(TEXT("A.Cmd2"), TEXT("y"), FConsoleCommandDelegate());
int32 Trailing(int32 X) { return X; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedTest, "A.C", 0)
void Unique2() {}
#undef LOCTEXT_NAMESPACE
""",
    "Source/ModA/Private/Three.cpp": """\
namespace MinimapLocal { constexpr float HeaderHeight = 22.f; }
void UMinimap::Layout() { using namespace MinimapLocal; float H = HeaderHeight; }
static bool HeaderClash() { return true; }
""",
    "Source/ModA/Private/Four.cpp": """\
namespace { constexpr float HeaderHeight = 20.f; }
""",
    "Source/ModA/Public/Api.h": """\
#pragma once
CODEX_API bool HeaderClash();
namespace Api { void Declared(); }
""",
    "Source/ModA/Private/Api.cpp": """\
namespace Api { void Declared() {} }
""",
    "Source/ModB/Private/Other.cpp": """\
namespace { constexpr float Margin = 1.f; }
""",
}
SELF_TEST_EXPECTED = {
    "Margin", "PanelColor", "CVarShared", "FHelper", "FAlias", "Dist2D", "EMode", "Names", "Lambda", "Braced",
    "StaticHelper", "LogShared", "NamedLocal::Limit", "NamedLocal::Twice", "SharedCmd", "Trailing", "FSharedTest",
    "HeaderHeight", "HeaderClash", "#LOCTEXT_NAMESPACE",
}


def self_test():
    tmp = tempfile.mkdtemp(prefix="unity_clash_selftest_")
    try:
        for rel, body in SELF_TEST_FILES.items():
            path = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(body)
        _, clashes, macro_clashes, _ = find_clashes(tmp)
        found = set()
        for module, (scope, name), _ in clashes:
            if module == "ModA":
                found.add(("::".join(scope) + "::" if scope else "") + name)
            else:
                found.add(f"{module}:{name}")
        found |= {"#" + name for module, name, _ in macro_clashes if module == "ModA"}
        missing = SELF_TEST_EXPECTED - found
        extra = found - SELF_TEST_EXPECTED
        for name in sorted(missing):
            print(f"self-test: MISSED {name}")
        for name in sorted(extra):
            print(f"self-test: FALSE POSITIVE {name}")
        print(f"self-test: {'PASS' if not missing and not extra else 'FAIL'} ({len(found)} clashes found)")
        return 0 if not missing and not extra else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=ROOT_DEFAULT)
    ap.add_argument("--module", action="append", help="limit to these module folder names")
    ap.add_argument("--warn-only", action="store_true", help="exit 0 even when clashes are found")
    ap.add_argument("--quiet", action="store_true", help="print only the summary line when clean")
    ap.add_argument("--leaks", action="store_true", help="also list macros #defined in a .cpp and never #undef'd")
    ap.add_argument("--dump", metavar="FILE", help="debug: print every name the scanner found in this file")
    ap.add_argument("--self-test", action="store_true", help="check the scanner on built-in samples")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if args.dump:
        s = scan_file(args.dump, args.dump, args.dump.endswith(".h"))
        for r in s.records:
            scope = "::".join(r.ns) or "(global)"
            print(f"{r.line:5d} {r.kind:32s} {'internal' if r.internal else 'external'} {scope}::{r.name}")
        for scope, target in s.using_namespaces:
            print(f"using namespace {'::'.join(target)} at {'::'.join(scope) or '(global)'}")
        return 0

    cpps, clashes, macro_clashes, leaks = find_clashes(args.root, args.module)
    total = len(clashes) + len(macro_clashes)
    for module, (scope, name), recs in clashes:
        qual = ("::".join(scope) + "::" if scope else "") + name
        kinds = "/".join(sorted({r.kind for r in recs}))
        print(f"[unity-clash] {module}: {qual} ({kinds})")
        for r in sorted(recs, key=lambda r: (r.file, r.line)):
            print(f"    {r.file}:{r.line}")
    for module, name, entries in macro_clashes:
        print(f"[unity-clash] {module}: #define {name} in {len(entries)} files, not #undef'd in all")
        for rel, line, leak in entries:
            print(f"    {rel}:{line}{'  (no #undef)' if leak else ''}")
    if args.leaks:
        for module, name, rel, line in leaks:
            print(f"[macro-leak] {module}: #define {name} never #undef'd: {rel}:{line}")
    if total or not args.quiet:
        hint = " - rename the file-local names (docs/port/HANDOFF.md §2, unity builds)" if total else ""
        print(f"find_unity_clashes: {len(cpps)} .cpp files, {total} clash(es){hint}")
    return 1 if total and not args.warn_only else 0


if __name__ == "__main__":
    sys.exit(main())
