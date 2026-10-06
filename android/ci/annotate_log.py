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

    errors = [i for i, line in enumerate(lines) if re.search(r'\berror\b|FAILED:|fatal:', line, re.IGNORECASE)]
    shown = 0
    for i in errors[:4]:
        emit(f'Build error {shown + 1}', '\n'.join(lines[max(0, i - 15):i + 25]))
        shown += 1

    emit('Build log (end)', '\n'.join(lines[-80:]))


if __name__ == '__main__':
    main()
