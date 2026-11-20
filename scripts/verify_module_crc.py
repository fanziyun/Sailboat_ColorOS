import pathlib
import re
import sys

mod_c, symvers, imports = map(pathlib.Path, sys.argv[1:4])
text = mod_c.read_text()
versions = re.search(r'____versions\[\].*?=\s*\{(.*?)\};', text, re.S)
if not versions:
    raise SystemExit('missing symbol version table')
entries = {name: int(crc, 16) for crc, name in
           re.findall(r'\{\s*(0x[0-9a-fA-F]+),\s*"([^"]+)"\s*\}', versions[1])}
expected = {}
for line in symvers.read_text().splitlines():
    fields = line.split()
    if len(fields) >= 2:
        expected[fields[1]] = int(fields[0], 16)
used = {line.split()[-1] for line in imports.read_text().splitlines()
        if re.search(r'\bU\s+\S+$', line)}
missing = (used | {'module_layout'}) - entries.keys()
bad = {name for name, crc in entries.items() if expected.get(name) != crc}
print('versions', len(entries), 'imports', len(used))
print('missing', sorted(missing))
print('mismatched', sorted(bad))
raise SystemExit(1 if missing or bad else 0)
