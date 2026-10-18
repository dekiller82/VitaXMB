#!/usr/bin/env python3
"""Patches an FFmpeg source tree for the Vita: worker threads get a 1 MB stack.

The Vita's pthread library gives threads made with a NULL attribute a tiny stack. FFmpeg's frame / slice worker threads (H.264, HEVC ...)
overflow it and crash with a data abort in the first big function. Every pthread_create() of the worker pools goes through
ff_vita_pthread_create(), which sets the stack size. Idempotent.

  ffmpeg_vita_patch.py <ffmpeg source dir>
"""
import os, re, sys

src = sys.argv[1]

HELPER = """
static inline int ff_vita_pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg)
{
    pthread_attr_t at;
    int r;
    (void)a;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 1024 * 1024);
    r = pthread_create(t, &at, fn, arg);
    pthread_attr_destroy(&at);
    return r;
}
"""


def rw(path, f):
    p = os.path.join(src, path)
    if not os.path.exists(p):
        return
    s = open(p, encoding='utf-8', newline='').read()
    n = f(s)
    if n != s:
        open(p, 'w', encoding='utf-8', newline='').write(n)
        print('patched', path)


def add_helper(s):
    if 'ff_vita_pthread_create' in s:
        return s
    key = '#include <pthread.h>'                 # inside the HAVE_PTHREADS block
    i = s.index(key)
    return s[:i + len(key)] + '\n' + HELPER + s[i + len(key):]


rw('libavutil/thread.h', add_helper)
for f in ('libavcodec/pthread_frame.c', 'libavutil/slicethread.c', 'libavcodec/executor.c', 'libavutil/executor.c'):
    rw(f, lambda s: re.sub(r'(?<![\w])pthread_create\(', 'ff_vita_pthread_create(', s))
