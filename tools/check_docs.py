#!/usr/bin/env python3
"""Check local Markdown link targets in current docs; archive is historical."""
from pathlib import Path
import re
import urllib.parse


def main():
    root = Path(__file__).resolve().parents[1]
    errors, count = [], 0
    for path in root.rglob('*.md'):
        relative = path.relative_to(root)
        if ('archive' in relative.parts or '__pycache__' in relative.parts or
                any(part.startswith('build') for part in relative.parts)):
            continue
        text = re.sub(r'```[\s\S]*?```', '', path.read_text())
        for value in re.findall(r'!?\[[^\]\n]*\]\(([^)\n]+)\)', text):
            target = value.split(' ', 1)[0].strip('<>')
            parsed = urllib.parse.urlsplit(target)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            count += 1
            resolved = (path.parent / urllib.parse.unquote(parsed.path)).resolve()
            if not resolved.is_relative_to(root) or not resolved.exists():
                errors.append(str(relative) + ': ' + target)
    if errors:
        raise SystemExit('Broken/escaped documentation links:\n' + '\n'.join(errors))
    print('PASS: ' + str(count) + ' current local documentation links')


if __name__ == '__main__':
    main()
