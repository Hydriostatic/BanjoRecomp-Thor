#!/usr/bin/env python3
"""Turns the end of a failed build log into GitHub annotations.

Annotations show up on the run's summary page and through the API, so the errors can be read
without downloading the full log.
"""

import re
import sys

MAX_CHARS = 3500


def emit(title: str, text: str) -> None:
    text = text[-MAX_CHARS:]
    text = text.replace('%', '%25').replace('\r', '').replace('\n', '%0A')
    print(f'::error title={title}::{text}')


def main() -> None:
    lines = open(sys.argv[1], errors='replace').read().splitlines()

    errors = [i for i, line in enumerate(lines) if re.search(r'error:|FAILED:|fatal:|CMake Error', line)]
    shown = 0
    last_end = -1
    for i in errors:
        if i <= last_end or i >= len(lines) - 60:
            continue  # already shown, or part of the log's end, which is shown below
        emit(f'Build error {shown + 1}', '\n'.join(lines[max(0, i - 5):i + 30]))
        last_end = i + 30
        shown += 1
        if shown == 4:
            break

    emit('Build log (end)', '\n'.join(lines[-60:]))


if __name__ == '__main__':
    main()
