#!/usr/bin/env python3
"""Lists every text the UI looks up with T() (and the static name tables), as the C compiler sees the strings.

  python tools/lang/scan_keys.py            prints the keys, one per line
  python tools/lang/scan_keys.py --check    also checks that tools/lang/strings.py has an entry for each (and no stale ones)
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIT = r'"(?:[^"\\\n]|\\.)*"'


def unescape(lit):
    s = lit[1:-1]
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            n = s[i + 1]
            if n == 'n': out.append('\n'); i += 2; continue
            if n == '"': out.append('"'); i += 2; continue
            if n == '\\': out.append('\\'); i += 2; continue
            if n == "'": out.append("'"); i += 2; continue
            if n == 'x':
                bs = bytearray()
                while s[i:i + 2] == '\\x':
                    bs.append(int(s[i + 2:i + 4], 16)); i += 4
                out.append(bs.decode('utf-8', 'replace')); continue
        out.append(c); i += 1
    return ''.join(out)


def source_files():
    fs = [f for f in glob.glob(ROOT + '/src/**/*.h', recursive=True) + [ROOT + '/src/main.c']
          if not any(x in f for x in ('cacerts', 'sky_tex', 'swave_data', 'wave_stock'))]
    return sorted(fs)


def balanced(src, i):
    """the text inside the parentheses opening at src[i] == '('"""
    depth, j, instr = 0, i, False
    while j < len(src):
        c = src[j]
        if instr:
            if c == '\\': j += 1
            elif c == '"': instr = False
        else:
            if c == '"': instr = True
            elif c == '(': depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0: return src[i + 1:j]
        j += 1
    return ''


def literals_in(text):
    """string literals of a C expression, adjacent ones concatenated"""
    out, cur = [], None
    for m in re.finditer(LIT + r'|[^\s"]+|\s+', text):
        t = m.group(0)
        if t.startswith('"'):
            cur = (cur or '') + unescape(t)
        elif t.strip() and t != '\\':
            if cur is not None: out.append(cur); cur = None
    if cur is not None: out.append(cur)
    return out


def rd(path):
    return open(ROOT + '/' + path, encoding='utf-8', errors='replace').read()


def scan():
    keys = {}
    macros = {}
    for f in source_files():
        src = open(f, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'#define\s+(\w+)\s+((?:' + LIT + r'\s*\\?\s*\n?\s*)+)', src):
            macros[m.group(1)] = ''.join(literals_in(m.group(2)))
    for f in source_files():
        src = open(f, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'\bT\(', src):
            inner = balanced(src, m.end() - 1)
            mm = re.fullmatch(r'\s*(\w+)\s*', inner)
            if mm and mm.group(1) in macros:
                keys.setdefault(macros[mm.group(1)], os.path.relpath(f, ROOT)); continue
            for s in literals_in(inner):
                if re.search(r'[A-Za-z]', s): keys.setdefault(s, os.path.relpath(f, ROOT))

    def table(path, start_re):
        m = re.search(start_re, rd(path), re.S)
        if m:
            for s in literals_in(m.group(1)):
                keys.setdefault(s, path)

    # the static tables and the items the menus are built from (their words are looked up where they are drawn)
    table('src/core/data.h', r'cat_names\[CAT_COUNT\]\s*=\s*\{(.*?)\};')
    table('src/ui/options.h', r'opt_names\[8\]\s*=\s*\{(.*?)\};')
    table('src/core/settings.h', r'theme_names\[13\]\s*=\s*\{(.*?)\};')
    table('src/core/settings.h', r'launch_names\[4\]\s*=\s*\{(.*?)\};')
    table('src/ui/videoui.h', r'vui_mode_name\[VM_COUNT\]\s*=\s*\{(.*?)\};')
    table('src/ui/videoui.h', r'static const char \*r\[3\]\s*=\s*\{(.*?)\};')
    table('src/ui/videoui.h', r'static const char \*rows\[\]\[2\]\s*=\s*\{(.*?)\};')
    src = rd('src/ui/videoui.h')
    a, b = src.index('static const char *vui_item_label'), src.index('static const char *vui_item_icon')
    for s in re.findall(r'return T\((' + LIT + r')\)', src[a:b]): keys.setdefault(unescape(s), 'src/ui/videoui.h')
    src = rd('src/core/menus.h')
    for m in re.finditer(r'add_(?:item|value|uri|folder)\(\s*\w+\s*(?:,\s*KIND_\w+)?\s*,\s*(' + LIT + r')(?:\s*,\s*(' + LIT + r'))?', src):
        for g in m.groups():
            if g:
                s = unescape(g)
                if re.search(r'[A-Za-z]', s) and not re.match(r'^(settings_dlg|photo:|wbapp|psns|pspy|psnmsg|near)', s):
                    keys.setdefault(s, 'src/core/menus.h')
    for m in re.finditer(r'snprintf\(ph->sub, sizeof\(ph->sub\), (' + LIT + r')', src): keys.setdefault(unescape(m.group(1)), 'src/core/menus.h')
    for path in ('src/core/folders.h', 'src/core/scan.h', 'src/media/media_scan.h', 'src/media/music.h'):
        for m in re.finditer(r'add_item\(M_\w+, KIND_INFO, (' + LIT + r'), (' + LIT + r')', rd(path)):
            for g in m.groups(): keys.setdefault(unescape(g), path)
    keys.setdefault('Last Played', 'src/core/lastplayed.h')
    for k in list(keys):                                           # button names, symbols and numbers are the same in every language
        if k in ('L / R', 'START', 'SELECT', '4 : 3') or not re.search(r'[A-Za-z]{2}', k): del keys[k]
    return keys


if __name__ == '__main__':
    keys = scan()
    if '--check' in sys.argv:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from strings import ENTRIES
        have = {e[0] for e in ENTRIES}
        for k, f in sorted(keys.items()):
            if k not in have: print('MISSING in strings.py:', repr(k), f)
        for k in sorted(have - set(keys)): print('STALE in strings.py:', repr(k))
        print(len(keys), 'keys in the source,', len(have), 'in strings.py')
    else:
        for k, f in sorted(keys.items()): print('%-30s %r' % (f, k))
