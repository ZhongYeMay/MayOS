#![cfg_attr(not(test), no_std)]

#[cfg(feature = "release-signature")]
mod release_signature;

// MayKernel Rust + C migration: independent second validation of the
// UNTRUSTED local MayFS update manifest. No heap, no disk writes, no network,
// no cryptographic authentication and no permission to install updates.
#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop { core::hint::spin_loop() }
}

const MAX_MANIFEST: usize = 2048;
const ALL_KEYS: u16 = 0x01ff;

fn decimal(data: &[u8], max: u32) -> Option<u32> {
    if data.is_empty() || data.len() > 10 { return None; }
    let mut result = 0u32;
    for &ch in data {
        if !ch.is_ascii_digit() { return None; }
        result = result.checked_mul(10)?.checked_add(u32::from(ch - b'0'))?;
        if result > max { return None; }
    }
    Some(result)
}

fn version_valid(v: &[u8]) -> bool {
    if v.is_empty() || v.len() > 32 { return false; }
    let mut pos = 0usize;
    for field in 0..3 {
        let first = pos;
        while pos < v.len() && v[pos].is_ascii_digit() { pos += 1; }
        if decimal(&v[first..pos], 1_000_000).is_none() { return false; }
        if field < 2 {
            if v.get(pos) != Some(&b'.') { return false; }
            pos += 1;
        }
    }
    if pos == v.len() { return true; }
    if !v[pos..].starts_with(b"-pre") { return false; }
    pos += 4;
    let first = pos;
    while pos < v.len() && v[pos].is_ascii_digit() { pos += 1; }
    if decimal(&v[first..pos], 1_000_000).is_none() { return false; }
    pos == v.len() || &v[pos..] == b"-dev"
}

fn date_valid(d: &[u8]) -> bool {
    if d.len() != 10 || d[4] != b'-' || d[7] != b'-' { return false; }
    let Some(year) = decimal(&d[..4], 9999) else { return false; };
    let Some(month) = decimal(&d[5..7], 12) else { return false; };
    let Some(day) = decimal(&d[8..10], 31) else { return false; };
    if year < 2023 || month == 0 || day == 0 { return false; }
    let days = [31u32, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    let mut max_day = days[(month - 1) as usize];
    if month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) {
        max_day = 29;
    }
    day <= max_day
}

fn pair(line: &[u8], seen: &mut u16) -> bool {
    let Some(equal) = line.iter().position(|c| *c == b'=') else { return false; };
    if equal == 0 || equal + 1 >= line.len() { return false; }
    let key = &line[..equal];
    let val = &line[equal + 1..];
    if val.iter().any(|c| !(32..=126).contains(c)) { return false; }

    let (flag, valid) = if key == b"name" {
        (1u16 << 0, val == b"MayOS")
    } else if key == b"version" {
        (1u16 << 1, version_valid(val))
    } else if key == b"architecture" {
        (1u16 << 2, val == b"x86_64")
    } else if key == b"kernel" {
        (1u16 << 3, val == b"MayKernel")
    } else if key == b"release_date" {
        (1u16 << 4, date_valid(val))
    } else if key == b"size_bytes" {
        (1u16 << 5, matches!(decimal(val, u32::MAX), Some(n) if n > 0))
    } else if key == b"sha256" {
        (1u16 << 6, val.len() == 64 && val.iter().all(|c| c.is_ascii_hexdigit()))
    } else if key == b"eta_minutes" {
        (1u16 << 7, matches!(decimal(val, 1440), Some(n) if n > 0))
    } else if key == b"changelog" {
        (1u16 << 8, !val.is_empty() && val.len() <= 48)
    } else { return false; };
    if !valid || (*seen & flag) != 0 { return false; }
    *seen |= flag;
    true
}

fn validate(data: &[u8]) -> bool {
    if data.is_empty() || data.len() > MAX_MANIFEST { return false; }
    let mut seen = 0u16;
    let mut pos = 0;
    while pos < data.len() {
        let start = pos;
        while pos < data.len() && data[pos] != b'\n' {
            if data[pos] == 0 { return false; }
            pos += 1;
            if pos - start >= 160 { return false; }
        }
        let mut end = pos;
        if pos < data.len() { pos += 1; }
        if end > start && data[end - 1] == b'\r' { end -= 1; }
        let line = &data[start..end];
        if line.is_empty() || line[0] == b'#' { continue; }
        if !pair(line, &mut seen) { return false; }
    }
    seen == ALL_KEYS
}

/// ABI v1: C passes a valid, immutable MayFS buffer for exactly length bytes.
/// Only the C kernel caller has authority to establish pointer validity.
/// Returns 1 for a syntactically valid but UNTRUSTED manifest, else 0.
///
/// # Safety
/// data must refer to at least length initialized, readable bytes.
#[no_mangle]
pub unsafe extern "C" fn mayrs_manifest_gate(data: *const u8, length: u32) -> i32 {
    if data.is_null() || length == 0 || (length as usize) > MAX_MANIFEST {
        return 0;
    }
    // SAFETY: this is an FFI precondition, checked by the calling C kernel.
    let input = unsafe { core::slice::from_raw_parts(data, length as usize) };
    i32::from(validate(input))
}

#[no_mangle]
pub extern "C" fn mayrs_abi_version() -> u32 { 1 }

#[cfg(test)]
mod tests {
    use super::*;
    const MANIFEST: &[u8] = b"name=MayOS\nversion=0.5.0-pre3\narchitecture=x86_64\nkernel=MayKernel\nrelease_date=2026-11-18\nsize_bytes=3\nsha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\neta_minutes=1\nchangelog=Test payload\n";

    fn replace(old: &[u8], new: &[u8]) -> Vec<u8> {
        let at = MANIFEST.windows(old.len()).position(|w| w == old).unwrap();
        let mut out = MANIFEST.to_vec();
        out.splice(at..at + old.len(), new.iter().copied());
        out
    }

    #[test]
    fn valid_and_ffi() {
        assert!(validate(MANIFEST));
        assert_eq!(mayrs_abi_version(), 1);
        // SAFETY: pointer is valid for this call.
        assert_eq!(unsafe { mayrs_manifest_gate(MANIFEST.as_ptr(), MANIFEST.len() as u32) }, 1);
    }
    #[test]
    fn duplicates_unknown_and_missing() {
        let mut input = MANIFEST.to_vec();
        input.extend_from_slice(b"name=MayOS\n");
        assert!(!validate(&input));
        assert!(!validate(&replace(b"kernel=MayKernel", b"unknown=MayKernel")));
        assert!(!validate(&MANIFEST[..30]));
    }
    #[test]
    fn rejects_wrong_kernel_architecture_product() {
        assert!(!validate(&replace(b"x86_64", b"arm64")));
        assert!(!validate(&replace(b"MayKernel", b"Linux")));
        assert!(!validate(&replace(b"name=MayOS", b"name=FakeOS")));
    }
    #[test]
    fn rejects_calendar_and_version_abuse() {
        assert!(!validate(&replace(b"2026-11-18", b"2026-02-30")));
        assert!(!validate(&replace(b"2026-11-18", b"2100-02-29")));
        assert!(validate(&replace(b"2026-11-18", b"2028-02-29")));
        assert!(!validate(&replace(b"0.5.0-pre3", b"0.5.0-pre3x")));
    }
    #[test]
    fn rejects_overflow_and_corruption() {
        assert!(!validate(&replace(b"size_bytes=3", b"size_bytes=4294967296")));
        assert!(!validate(&replace(b"eta_minutes=1", b"eta_minutes=1441")));
        assert!(!validate(&replace(b"ba7816bf", b"ba7816bg")));
        assert!(!validate(&replace(b"Test payload", b"Unsafe\ttext")));
    }
    #[test]
    fn bounded_and_compatible_crlf() {
        assert!(!validate(&[MANIFEST, b"\0"].concat()));
        assert!(!validate(&vec![b'#'; 2049]));
        let mut crlf = Vec::new();
        for &b in MANIFEST {
            if b == b'\n' { crlf.push(b'\r'); }
            crlf.push(b);
        }
        assert!(validate(&crlf));
        // SAFETY: null pointer is detected before dereference.
        assert_eq!(unsafe { mayrs_manifest_gate(core::ptr::null(), 10) }, 0);
    }
    #[test]
    fn versions() {
        assert!(version_valid(b"0.5.0-pre3-dev"));
        assert!(version_valid(b"1.0.0"));
        assert!(!version_valid(b"1.0"));
        assert!(!version_valid(b"1.0.0-preview"));
        assert!(!version_valid(b"1.0.0-pre1000001"));
    }
}
