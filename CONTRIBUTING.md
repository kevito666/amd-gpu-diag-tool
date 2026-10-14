# Contributing

Priorities, in order:

1. Correctness and root-process safety.
2. Low latency impact on games and audio.
3. Simple, readable C.
4. Minimal dependencies.
5. Clear diagnostic value.

Avoid adding a toolkit or library when the required information is already available through libc, Linux kernel interfaces, or Xlib.

Before submitting changes:

```bash
make clean
make check
```
