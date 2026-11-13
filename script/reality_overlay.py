"""Prints a -modfile flag whose sing-box keeps X25519MLKEM768 in the REALITY ClientHello.

REALITY servers since Xray 26.7 refuse a ClientHello without an X25519MLKEM768 key share before the
X25519 one; sing-box's client removed it. With both shares, as the uTLS fingerprints send them, old
servers take the X25519 share and new ones accept the hello. The module cache and core/go.mod stay
untouched: a patched copy of the module goes to core/.rawline and a copy of go.mod points at it.
Run from core/:  GOFLAGS="$(python3 ../script/reality_overlay.py)" go build ...
"""
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path

MODULE = 'github.com/sagernet/sing-box'
FILTER = '''	for _, extension := range uConn.Extensions {
		if ce, ok := extension.(*utls.SupportedCurvesExtension); ok {
			ce.Curves = common.Filter(ce.Curves, func(curveID utls.CurveID) bool {
				return curveID != utls.X25519MLKEM768
			})
		}
		if ks, ok := extension.(*utls.KeyShareExtension); ok {
			ks.KeyShares = common.Filter(ks.KeyShares, func(share utls.KeyShare) bool {
				return share.Group != utls.X25519MLKEM768
			})
		}
	}
'''


# The uTLS of Xray 26.11 needs Go 1.27; the patched Go 1.26 of the Windows 7 / macOS 10.15 builds keeps the previous one.
LEGACY_UTLS = 'github.com/refraction-networking/utls@v1.8.3-0.20260301010127-aa6edf4b11af'


def legacy_go(go):
    version = re.match(r'go(\d+)\.(\d+)', subprocess.check_output([go, 'env', 'GOVERSION'], text=True).strip())
    assert version, 'unknown Go version'
    return (int(version[1]), int(version[2])) < (1, 27)


def main():
    go = os.environ.get('GOCMD', 'go')
    work = Path('.rawline').resolve()
    work.mkdir(exist_ok=True)
    modfile = work / 'go.mod'
    shutil.copyfile('go.mod', modfile)
    shutil.copyfile('go.sum', work / 'go.sum')
    legacy = legacy_go(go)
    flags = [f'-modfile={modfile.as_posix()}']
    if legacy:
        subprocess.check_call([go, 'mod', 'edit', '-go=1.26.0', f'-replace={LEGACY_UTLS.split("@")[0]}={LEGACY_UTLS}', str(modfile)])
        flags.append('-mod=mod')
    # .Dir is empty until the module is in the cache.
    subprocess.check_call([go, 'mod', 'download', flags[0], MODULE])
    source = subprocess.check_output([go, 'list', *flags, '-m', '-f', '{{.Dir}}', MODULE], text=True).strip()
    assert source, 'sing-box module not found'
    source = Path(source)
    copy = work / 'sing-box'
    if copy.exists():
        shutil.rmtree(copy, onerror=lambda f, p, _: (os.chmod(p, stat.S_IWRITE), f(p)))
    shutil.copytree(source, copy)
    for root, dirs, files in os.walk(copy):
        for name in dirs + files:
            os.chmod(os.path.join(root, name), stat.S_IREAD | stat.S_IWRITE | (stat.S_IEXEC if name in dirs else 0))
    client = copy / 'common/tls/reality_client.go'
    text = client.read_text(encoding='utf-8')
    assert text.count(FILTER) == 1, 'sing-box REALITY client changed; review script/reality_overlay.py'
    text = text.replace(FILTER, '')
    if 'common.' not in text:
        text = text.replace('\t"github.com/sagernet/sing/common"\n', '')
    client.write_text(text, encoding='utf-8', newline='\n')
    # A WireGuard endpoint behind a detour (a chain hop, WARP over a proxy) does not use the default network: rebinding
    # it there dropped the flow to the next hop, and the tunnel stalled until the 15 s handshake retry.
    endpoint = copy / 'protocol/wireguard/endpoint.go'
    text = endpoint.read_text(encoding='utf-8')
    for old, new in [
        ('\tonDemand       bool\n', '\tonDemand       bool\n\tdetoured       bool\n'),
        ('\t\tonDemand:       options.OnDemand,\n', '\t\tonDemand:       options.OnDemand,\n\t\tdetoured:       options.Detour != "",\n'),
        ('func (w *Endpoint) InterfaceUpdated(ctx context.Context) {\n\tif !w.started.Load() {',
         'func (w *Endpoint) InterfaceUpdated(ctx context.Context) {\n\tif !w.started.Load() || w.detoured {'),
    ]:
        assert text.count(old) == 1, 'sing-box WireGuard endpoint changed; review script/reality_overlay.py'
        text = text.replace(old, new)
    endpoint.write_text(text, encoding='utf-8', newline='\n')
    # gomobile builds outside the module and refuses -modfile: the Android build (a CI checkout) edits go.mod itself.
    if sys.argv[1:] == ['--in-place']:
        assert not legacy, 'the Android build needs Go 1.27'
        subprocess.check_call([go, 'mod', 'edit', f'-replace={MODULE}=./.rawline/sing-box'])
        return
    subprocess.check_call([go, 'mod', 'edit', f'-replace={MODULE}={copy.as_posix()}', str(modfile)])
    print(' '.join(flags))


if __name__ == '__main__':
    main()
