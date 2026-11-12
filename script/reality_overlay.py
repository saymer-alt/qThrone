"""Prints a -modfile flag whose sing-box keeps X25519MLKEM768 in the REALITY ClientHello.

REALITY servers since Xray 26.7 refuse a ClientHello without an X25519MLKEM768 key share before the
X25519 one; sing-box's client removed it. With both shares, as the uTLS fingerprints send them, old
servers take the X25519 share and new ones accept the hello. The module cache and core/go.mod stay
untouched: a patched copy of the module goes to core/.rawline and a copy of go.mod points at it.
Run from core/:  GOFLAGS="$(python3 ../script/reality_overlay.py)" go build ...
"""
import os
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


def main():
    go = os.environ.get('GOCMD', 'go')
    source = Path(subprocess.check_output([go, 'list', '-m', '-f', '{{.Dir}}', MODULE], text=True).strip())
    work = Path('.rawline').resolve()
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
    # gomobile builds outside the module and refuses -modfile: the Android build (a CI checkout) edits go.mod itself.
    if sys.argv[1:] == ['--in-place']:
        subprocess.check_call([go, 'mod', 'edit', f'-replace={MODULE}=./.rawline/sing-box'])
        return
    modfile = work / 'go.mod'
    shutil.copyfile('go.mod', modfile)
    shutil.copyfile('go.sum', work / 'go.sum')
    subprocess.check_call([go, 'mod', 'edit', f'-replace={MODULE}={copy.as_posix()}', str(modfile)])
    print(f'-modfile={modfile.as_posix()}')


if __name__ == '__main__':
    main()
