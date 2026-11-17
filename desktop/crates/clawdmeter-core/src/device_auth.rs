//! What the device needs to read usage on its own over WiFi while the
//! computer is away (this fork): each account's current OAuth *access*
//! token, never the refresh token, and the WiFi network.
//!
//! The computer stays the only one that renews tokens; the device uses each
//! one until it expires and then shows Offline. Both go out on the AUTH
//! characteristic (`…-0005`), which the firmware accepts only over the
//! owner's bonded, encrypted link, one JSON object per write:
//!
//! ```text
//! {"i":0,"n":2,"l":"Trabajo","tk":"<access token>","x":1794342805,"tz":-18000,"tf":24}
//! {"wifi":{"s":"<ssid>","p":"<password>"}}
//! ```
//!
//! `i` is the account's position in `config_dirs`, `n` how many there are,
//! `l` its label, `x` when the token expires (unix seconds, 0 = unknown),
//! `tz` the computer's UTC offset (s) and `tf` its 12/24-hour clock, so the
//! device can show local times while it is on its own.
//!
//! The WiFi network lives in its own file, `<state>/device-wifi`: the SSID on
//! the first line, the password on the second, both taken verbatim (the
//! config file would cut a password at a `#`).

use std::collections::HashMap;
use std::path::Path;

use serde_json::json;
use sha2::{Digest, Sha256};

use crate::oauth::Token;

/// The device's WiFi network.
#[derive(Clone, PartialEq, Eq)]
pub struct Wifi {
    /// The network name.
    pub ssid: String,
    /// Its password (empty for an open network).
    pub password: String,
}

impl std::fmt::Debug for Wifi {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Wifi").field("ssid", &self.ssid).field("password", &"<redacted>").finish()
    }
}

impl Wifi {
    /// Parse `device-wifi`: the SSID, then the password, one per line. `None`
    /// without an SSID.
    pub fn parse(text: &str) -> Option<Self> {
        let mut lines = text.lines().map(|l| l.trim_end_matches('\r'));
        let ssid = lines.next()?.trim().to_owned();
        if ssid.is_empty() {
            return None;
        }
        let password = lines.next().unwrap_or("").to_owned();
        Some(Wifi { ssid, password })
    }

    /// Read `<state>/device-wifi`; `None` when it's missing or has no SSID.
    pub fn load(path: &Path) -> Option<Self> {
        Self::parse(&std::fs::read_to_string(path).ok()?)
    }
}

/// One account the device may poll on its own.
#[derive(Debug, Clone)]
pub struct AccountToken {
    /// Its position in `config_dirs` (`i`).
    pub idx: u8,
    /// Its label (`l`, [`crate::Settings::account_label`]).
    pub label: String,
    /// Its current access token.
    pub token: Token,
}

/// Host facts every token message carries.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HostTime {
    /// The computer's UTC offset (s).
    pub utc_offset_secs: i64,
    /// 12 or 24.
    pub hour_format: u8,
}

/// One AUTH write.
#[derive(Clone)]
pub struct AuthWrite {
    /// What it is, for the logs: never the token or the password.
    pub what: String,
    /// The JSON to write.
    pub bytes: Vec<u8>,
    key: String,
    digest: [u8; 32],
}

impl std::fmt::Debug for AuthWrite {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("AuthWrite").field("what", &self.what).field("len", &self.bytes.len()).finish()
    }
}

/// Which AUTH messages the device already has, since the last connect.
#[derive(Debug, Default)]
pub struct AuthSync {
    sent: HashMap<String, [u8; 32]>,
}

fn digest(bytes: &[u8]) -> [u8; 32] {
    Sha256::digest(bytes).into()
}

impl AuthSync {
    /// A new link: the device may have lost what it had, so send everything.
    pub fn reset(&mut self) {
        self.sent.clear();
    }

    /// The writes the device doesn't have yet: the WiFi network, then each
    /// account's token. An expired token isn't sent (the device would only
    /// drop it); one too long for a write is skipped.
    pub fn pending(
        &self,
        wifi: Option<&Wifi>,
        accounts: &[AccountToken],
        count: u8,
        host: HostTime,
        now: i64,
        limit: usize,
    ) -> Vec<AuthWrite> {
        let mut out = Vec::new();
        if let Some(w) = wifi {
            let bytes = json!({"wifi": {"s": w.ssid, "p": w.password}}).to_string().into_bytes();
            out.push(self.write("wifi".into(), format!("WiFi network {:?}", w.ssid), bytes));
        }
        for a in accounts.iter().filter(|a| !a.token.expired(now)) {
            let expires = a.token.expires_at_ms.map_or(0, |ms| ms / 1000);
            let bytes = json!({
                "i": a.idx,
                "n": count,
                "l": a.label,
                "tk": a.token.access,
                "x": expires,
                "tz": host.utc_offset_secs,
                "tf": host.hour_format,
            })
            .to_string()
            .into_bytes();
            out.push(self.write(format!("token-{}", a.idx), format!("account {} token", a.idx), bytes));
        }
        out.retain(|w| {
            let fits = w.bytes.len() <= limit;
            if !fits {
                log::warn!("{} is {} bytes, over the {limit}-byte write; not sent", w.what, w.bytes.len());
            }
            fits && self.sent.get(&w.key) != Some(&w.digest)
        });
        out
    }

    fn write(&self, key: String, what: String, bytes: Vec<u8>) -> AuthWrite {
        let digest = digest(&bytes);
        AuthWrite { what, bytes, key, digest }
    }

    /// The device has `w`.
    pub fn mark_sent(&mut self, w: &AuthWrite) {
        self.sent.insert(w.key.clone(), w.digest);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const NOW: i64 = 1_794_320_000;
    const HOST: HostTime = HostTime { utc_offset_secs: -18_000, hour_format: 24 };

    fn token(access: &str, expires_s: Option<i64>) -> Token {
        Token { access: access.into(), expires_at_ms: expires_s.map(|s| s * 1000) }
    }

    fn acct(idx: u8, label: &str, access: &str) -> AccountToken {
        AccountToken { idx, label: label.into(), token: token(access, Some(NOW + 8 * 3600)) }
    }

    fn json(w: &AuthWrite) -> serde_json::Value {
        serde_json::from_slice(&w.bytes).unwrap()
    }

    #[test]
    fn wifi_file() {
        let w = Wifi::parse("Casa 5G\r\npa#ss word \n").unwrap();
        assert_eq!((w.ssid.as_str(), w.password.as_str()), ("Casa 5G", "pa#ss word "), "verbatim, # included");
        assert_eq!(Wifi::parse("Open\n").unwrap().password, "");
        assert!(Wifi::parse("").is_none());
        assert!(Wifi::parse("  \nsecret\n").is_none());
        assert!(!format!("{w:?}").contains("pa#ss"), "the password never reaches a log");
    }

    #[test]
    fn messages_and_their_fields() {
        let sync = AuthSync::default();
        let wifi = Wifi { ssid: "Casa".into(), password: "secret".into() };
        let accounts = [acct(0, "Trabajo", "sk-ant-oat01-aaa"), acct(1, "Personal", "sk-ant-oat01-bbb")];
        let w = sync.pending(Some(&wifi), &accounts, 2, HOST, NOW, 500);
        assert_eq!(w.len(), 3);
        assert_eq!(json(&w[0]), serde_json::json!({"wifi": {"s": "Casa", "p": "secret"}}));
        assert_eq!(
            json(&w[2]),
            serde_json::json!({"i": 1, "n": 2, "l": "Personal", "tk": "sk-ant-oat01-bbb",
                               "x": NOW + 8 * 3600, "tz": -18000, "tf": 24})
        );
        for m in &w {
            assert!(!m.what.contains("secret") && !m.what.contains("sk-ant"), "{}", m.what);
            assert!(!format!("{m:?}").contains("sk-ant"));
        }
    }

    #[test]
    fn only_what_changed_goes_out_until_a_reconnect() {
        let mut sync = AuthSync::default();
        let mut accounts = vec![acct(0, "Trabajo", "tok-a"), acct(1, "Personal", "tok-b")];
        for w in sync.pending(None, &accounts, 2, HOST, NOW, 500) {
            sync.mark_sent(&w);
        }
        assert!(sync.pending(None, &accounts, 2, HOST, NOW, 500).is_empty(), "nothing new");
        // Claude Code renewed account 1's token: only that one.
        accounts[1].token = token("tok-b2", Some(NOW + 9 * 3600));
        let w = sync.pending(None, &accounts, 2, HOST, NOW, 500);
        assert_eq!(w.iter().map(|w| w.what.as_str()).collect::<Vec<_>>(), ["account 1 token"]);
        // A label or the clock changing is a change too.
        sync.mark_sent(&w[0]);
        let w = sync.pending(None, &accounts, 2, HostTime { hour_format: 12, ..HOST }, NOW, 500);
        assert_eq!(w.len(), 2);
        // A new link sends everything again.
        for w in &w {
            sync.mark_sent(w);
        }
        sync.reset();
        assert_eq!(sync.pending(None, &accounts, 2, HOST, NOW, 500).len(), 2);
    }

    #[test]
    fn expired_and_oversized_tokens_stay_home() {
        let sync = AuthSync::default();
        let accounts = [
            AccountToken { idx: 0, label: "A".into(), token: token("old", Some(NOW - 10)) },
            AccountToken { idx: 1, label: "B".into(), token: token("near", Some(NOW + 30)) },
            AccountToken { idx: 2, label: "C".into(), token: token("unknown-expiry", None) },
            AccountToken { idx: 3, label: "D".into(), token: token(&"x".repeat(600), Some(NOW + 3600)) },
        ];
        let w = sync.pending(None, &accounts, 4, HOST, NOW, 500);
        assert_eq!(w.iter().map(|w| w.what.as_str()).collect::<Vec<_>>(), ["account 2 token"]);
        assert_eq!(json(&w[0])["x"], 0, "unknown expiry: 0");
    }
}
