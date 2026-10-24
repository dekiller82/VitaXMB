#!/usr/bin/env python3
"""Builds assets/lang/<code>.lng, the translations VitaXMB shows, from the PSP firmware's own texts and VitaXMB's hand-made ones.

  python tools/lang/make_lang.py <resource dir of the decrypted 6.60 firmware>        (F0/vsh/resource)

The decrypted firmware comes from pspdecrypt (see README, "Languages"). Each line of a language file is "English<TAB>translation"
with a line break written as \\n; the app looks the English text up (src/core/lang.h). tools/lang/strings.py says where each
translation comes from.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from fwtext import load
from strings import ENTRIES, LANGS

ROOT = os.path.dirname(os.path.dirname(HERE))
PUA = re.compile('[\ue000-\uf8ff]')


def clean(s):
    s = PUA.sub('', s).replace('\r', '')
    return s.strip()


def esc(s):
    return s.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', ' ')


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    res = sys.argv[1]
    cache = {}

    def fw(ref, lang):
        f, label = ref.split(':')
        if f not in cache:
            cache[f] = load(os.path.join(res, f + '.rco'))
        try:
            return clean(cache[f][lang][label])
        except KeyError:
            raise SystemExit('no firmware text %s in %s (%s)' % (label, f, lang))

    out = {l: [] for l in LANGS if l != 'en'}
    for key, src in ENTRIES:
        for lang in out:
            if isinstance(src, str):
                v = fw(src, lang)
            elif isinstance(src, dict):
                v = src[lang]
            elif src[0] == 'fw+':
                v = fw(src[1], lang) + src[2]
            elif src[0] == 'tmpl':
                v = src[1][lang].format(c=fw(src[2], lang))
            else:
                raise SystemExit('bad source for %r' % key)
            if v and v != key:
                out[lang].append('%s\t%s' % (esc(key), esc(v)))
    d = os.path.join(ROOT, 'assets', 'lang')
    os.makedirs(d, exist_ok=True)
    for lang, lines in out.items():
        with open(os.path.join(d, lang + '.lng'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        print(lang, len(lines), 'texts')


if __name__ == '__main__':
    main()
