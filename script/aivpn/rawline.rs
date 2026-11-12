//! Rawline starts this client with `--rawline-config FILE`: a private JSON file in place of argv, so the
//! connection key stays out of the process list, and the SOCKS5 listener asks for the per-launch
//! credentials. The file becomes the arguments the original GUIs pass; any other launch is unchanged.

use std::ffi::OsString;
use std::net::SocketAddrV4;

#[derive(serde::Deserialize)]
struct Config {
    key: String,
    socks: String,
    socks_user: String,
    socks_pass: String,
    admin: String,
    #[serde(default)]
    dns: Vec<String>,
    #[serde(default)]
    mask: String,
    #[serde(default)]
    polymorphic: bool,
    #[serde(default)]
    adaptive_level: u8,
}

fn fail(message: &str) -> ! {
    eprintln!("aivpn: {message}");
    std::process::exit(2)
}

fn loopback(address: &str) -> bool {
    address.parse::<SocketAddrV4>().is_ok_and(|a| a.ip().is_loopback() && a.port() != 0)
}

pub fn argv() -> Vec<OsString> {
    let args: Vec<OsString> = std::env::args_os().collect();
    if args.len() != 3 || args[1] != "--rawline-config" {
        return args;
    }
    let data = std::fs::read(&args[2]).unwrap_or_else(|_| fail("cannot read the private configuration"));
    let c: Config = serde_json::from_slice(&data).unwrap_or_else(|_| fail("invalid private configuration"));
    if !c.key.trim().starts_with("aivpn://") {
        fail("the connection key must start with aivpn://");
    }
    if !loopback(&c.socks) || !loopback(&c.admin) || c.socks == c.admin {
        fail("listeners must be distinct IPv4 loopback ports");
    }
    if c.socks_user.is_empty() || c.socks_pass.is_empty() || c.socks_user.len() > 255 || c.socks_pass.len() > 255 {
        fail("authenticated bridge required");
    }
    crate::proxy::socks5::set_bridge_auth(c.socks_user.into_bytes(), c.socks_pass.into_bytes());
    let mut out: Vec<OsString> = vec![args[0].clone()];
    for (flag, value) in [("--connection-key", c.key.trim().to_string()), ("--proxy-listen", c.socks), ("--admin-listen", c.admin)] {
        out.push(flag.into());
        out.push(value.into());
    }
    if !c.dns.is_empty() {
        out.push("--proxy-dns".into());
        out.push(c.dns.join(",").into());
    }
    // As aivpn-linux builds its client command: "auto" leaves the choice to the server.
    if !c.mask.is_empty() && c.mask != "auto" {
        out.push(if c.polymorphic { "--polymorphic-base" } else { "--preferred-mask" }.into());
        out.push(c.mask.into());
    }
    if c.adaptive_level > 0 {
        out.push("--adaptive-level".into());
        out.push(c.adaptive_level.to_string().into());
    }
    out
}

#[cfg(test)]
mod tests {
    #[test]
    fn listeners_are_loopback_ports() {
        assert!(super::loopback("127.0.0.1:1080"));
        assert!(!super::loopback("0.0.0.0:1080"));
        assert!(!super::loopback("127.0.0.1:0"));
        assert!(!super::loopback("localhost:1080"));
    }
}
