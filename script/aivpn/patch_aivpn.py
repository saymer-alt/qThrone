"""Adds Rawline's private launch file and an authenticated SOCKS5 listener to aivpn-client.

Usage: patch_aivpn.py <aivpn source>. Everything else stays the upstream client.
"""
import shutil
import sys
from pathlib import Path


def replace_once(path: Path, old: str, new: str):
    text = path.read_text(encoding='utf-8')
    assert text.count(old) == 1, (path, old)
    path.write_text(text.replace(old, new), encoding='utf-8')


def patch(root: Path):
    client = root / 'crates/aivpn-client/src'
    shutil.copyfile(Path(__file__).with_name('rawline.rs'), client / 'rawline.rs')
    replace_once(client / 'lib.rs', 'pub mod proxy;\n', 'pub mod proxy;\npub mod rawline;\n')
    replace_once(client / 'run.rs', '    let mut args = ClientArgs::parse();\n',
                 '    let mut args = ClientArgs::parse_from(crate::rawline::argv());\n')
    socks = client / 'proxy/socks5.rs'
    replace_once(socks, 'const METHOD_NO_ACCEPTABLE: u8 = 0xFF;\n', '''const METHOD_NO_ACCEPTABLE: u8 = 0xFF;
const METHOD_USER_PASS: u8 = 2;

static BRIDGE_AUTH: std::sync::OnceLock<(Vec<u8>, Vec<u8>)> = std::sync::OnceLock::new();

/// Rawline's per-launch credentials: once set, the listener accepts only them (RFC 1929).
pub fn set_bridge_auth(user: Vec<u8>, pass: Vec<u8>) {
    let _ = BRIDGE_AUTH.set((user, pass));
}

fn same(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}
''')
    replace_once(socks, '''        if !methods.contains(&METHOD_NO_AUTH) {
            self.stream
                .write_all(&[SOCKS5_VER, METHOD_NO_ACCEPTABLE])
                .await?;
            return Err(std::io::Error::new(
                std::io::ErrorKind::PermissionDenied,
                "no acceptable auth method",
            ));
        }
        self.stream.write_all(&[SOCKS5_VER, METHOD_NO_AUTH]).await?;
''', '''        if let Some((user, pass)) = BRIDGE_AUTH.get() {
            if !methods.contains(&METHOD_USER_PASS) {
                self.stream
                    .write_all(&[SOCKS5_VER, METHOD_NO_ACCEPTABLE])
                    .await?;
                return Err(std::io::Error::new(
                    std::io::ErrorKind::PermissionDenied,
                    "authentication required",
                ));
            }
            self.stream.write_all(&[SOCKS5_VER, METHOD_USER_PASS]).await?;
            let mut head = [0u8; 2];
            self.stream.read_exact(&mut head).await?;
            let mut got_user = vec![0u8; head[1] as usize];
            self.stream.read_exact(&mut got_user).await?;
            let mut length = [0u8; 1];
            self.stream.read_exact(&mut length).await?;
            let mut got_pass = vec![0u8; length[0] as usize];
            self.stream.read_exact(&mut got_pass).await?;
            let ok = head[0] == 1 && same(&got_user, user) & same(&got_pass, pass);
            self.stream.write_all(&[1, if ok { 0 } else { 1 }]).await?;
            if !ok {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::PermissionDenied,
                    "wrong credentials",
                ));
            }
        } else {
            if !methods.contains(&METHOD_NO_AUTH) {
                self.stream
                    .write_all(&[SOCKS5_VER, METHOD_NO_ACCEPTABLE])
                    .await?;
                return Err(std::io::Error::new(
                    std::io::ErrorKind::PermissionDenied,
                    "no acceptable auth method",
                ));
            }
            self.stream.write_all(&[SOCKS5_VER, METHOD_NO_AUTH]).await?;
        }
''')


if __name__ == '__main__':
    patch(Path(sys.argv[1]))
