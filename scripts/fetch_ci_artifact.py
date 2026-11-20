import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import urllib.error
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('artifact_id')
parser.add_argument('output', type=Path)
args = parser.parse_args()
if args.output.exists():
    raise SystemExit('Use a fresh output path')
API = 'https://api.github.com/repos/fanziyun/ABK/actions/artifacts/' + args.artifact_id
token = os.environ.get('SAILBOAT_GH_TOKEN', '').strip()
if not token:
    token = subprocess.check_output(['gh', 'auth', 'token'], text=True).strip()
if not token:
    raise SystemExit('Set SAILBOAT_GH_TOKEN or sign in with gh')


def api(path):
    request = urllib.request.Request(API + path, headers={'Authorization': 'Bearer ' + token,
        'Accept': 'application/vnd.github+json', 'User-Agent': 'Sailboat-local-artifact-download'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read())


meta = api('')
expected = meta['size_in_bytes']
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        return None
opener = urllib.request.build_opener(NoRedirect())
def signed_url():
    request = urllib.request.Request('https://api.github.com/repos/fanziyun/ABK/actions/artifacts/' + args.artifact_id + '/zip',
        headers={'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json', 'User-Agent': 'Sailboat-local-artifact-download'})
    try:
        response = opener.open(request, timeout=60)
    except urllib.error.HTTPError as error:
        if error.code not in (301, 302, 303, 307, 308):
            raise RuntimeError('GitHub artifact redirect failed: HTTP ' + str(error.code)) from None
        location = error.headers['Location']
        if not location.startswith('https://'):
            raise RuntimeError('Unexpected download protocol')
        return location
    response.close()
    raise RuntimeError('Expected GitHub signed artifact redirect')
args.output.parent.mkdir(parents=True, exist_ok=True)
written = 0
last_report = 0
for attempt in range(1, 7):
    try:
        url = signed_url()
        headers = {'User-Agent': 'Sailboat-local-artifact-download'}
        if written:
            headers['Range'] = 'bytes=' + str(written) + '-'
        request = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(request, timeout=90) as response:
            if written and (response.status != 206 or not response.headers.get('Content-Range', '').startswith('bytes ' + str(written) + '-')):
                raise RuntimeError('Server did not honor exact resume range')
            with args.output.open('ab' if written else 'wb') as stream:
                while True:
                    block = response.read(2 * 1024 * 1024)
                    if not block:
                        break
                    stream.write(block)
                    written += len(block)
                    if written - last_report >= 128 * 1024 * 1024:
                        print(f'{written}/{expected} bytes', flush=True)
                        last_report = written
        if written == expected:
            break
        raise RuntimeError('Transfer ended before expected size')
    except Exception as error:
        written = args.output.stat().st_size if args.output.exists() else 0
        print(f'Download attempt {attempt} interrupted ({type(error).__name__}); preserved {written} bytes', flush=True)
        if attempt == 6 or written > expected:
            raise SystemExit('Download failed; partial file retained') from None
        time.sleep(2 * attempt)
if written != expected:
    raise SystemExit('Artifact size mismatch')
hasher = hashlib.sha256()
with args.output.open('rb') as stream:
    for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
        hasher.update(block)
digest = hasher.hexdigest()
if meta.get('digest') and meta['digest'] != 'sha256:' + digest:
    raise SystemExit('Artifact digest mismatch')
print('Download complete:', args.output, flush=True)
print('SHA256:', digest, flush=True)
args.output.with_suffix(args.output.suffix + '.metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
