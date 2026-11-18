//! Ed25519 signed-release metadata verification, no_std, no allocator.
 //! Cryptographic correctness is delegated to maintained RustCrypto.
 //! Signature authenticity also requires a separately provisioned, trusted
 //! publisher key; accepting a key supplied by an untrusted manifest is unsafe.
use ed25519_dalek::{Signature, VerifyingKey};

pub(crate) fn verify(public_key: &[u8; 32], signature: &[u8; 64], payload: &[u8]) -> bool {
    let Ok(key) = VerifyingKey::from_bytes(public_key) else { return false; };
    let sig = Signature::from_bytes(signature);
    key.verify_strict(payload, &sig).is_ok()
}

/// Verify a detached Ed25519 signature over exact manifest bytes.
/// Only a separately pinned publisher public key establishes release trust.
/// No I/O, allocation or installation. Returns 1 on valid signature, else 0.
///
/// # Safety
/// public_key must point to 32 readable bytes, signature to 64 readable bytes,
/// and manifest to length readable bytes if length != 0. These immutable
/// pointers must not alias concurrently mutable kernel memory.
#[no_mangle]
pub unsafe extern "C" fn mayrs_ed25519_verify(
    public_key: *const u8,
    signature: *const u8,
    manifest: *const u8,
    length: u32,
) -> i32 {
    if public_key.is_null() || signature.is_null() || length > 2048 ||
       (length != 0 && manifest.is_null()) {
        return 0;
    }
    // SAFETY: kernel ABI caller guarantees bounds and read-only memory.
    let key: &[u8; 32] = unsafe { &*(public_key as *const [u8; 32]) };
    let sig: &[u8; 64] = unsafe { &*(signature as *const [u8; 64]) };
    let data: &[u8] = if length == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(manifest, length as usize) }
    };
    i32::from(verify(key, sig, data))
}

#[cfg(test)]
mod tests {
    use super::*;

    // RFC 8032 Ed25519 published test vector 1; the corresponding private
    // key is PUBLIC TEST DATA and must NEVER be trusted for MayOS releases.
    const PUBKEY: &str =
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
    const SIGNATURE: &str = concat!(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901",
        "55fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100"
    );
    fn hex<const N: usize>(s: &str) -> [u8; N] {
        assert_eq!(s.len(), N * 2);
        let mut out = [0u8; N];
        for i in 0..N {
            out[i] = u8::from_str_radix(&s[2*i..2*i+2], 16).unwrap();
        }
        out
    }
    #[test]
    fn published_rfc8032_vector() {
        let key = hex::<32>(PUBKEY);
        let sig = hex::<64>(SIGNATURE);
        assert!(verify(&key,&sig,b""));
        // SAFETY: buffers are correctly sized and initialized.
        assert_eq!(unsafe { mayrs_ed25519_verify(
            key.as_ptr(),sig.as_ptr(),core::ptr::null(),0) },1);
    }
    #[test]
    fn rejects_modified_message_signature_key_and_missing_buffers() {
        let key = hex::<32>(PUBKEY);
        let sig = hex::<64>(SIGNATURE);
        assert!(!verify(&key,&sig,b"MayOS"));
        let mut corrupt=sig;
        corrupt[5]^=1;
        assert!(!verify(&key,&corrupt,b""));
        let mut fake=key;
        fake[0]^=1;
        assert!(!verify(&fake,&sig,b""));
        // SAFETY: null pointers are rejected before dereference.
        assert_eq!(unsafe { mayrs_ed25519_verify(core::ptr::null(),sig.as_ptr(),core::ptr::null(),0)},0);
        assert_eq!(unsafe { mayrs_ed25519_verify(key.as_ptr(),sig.as_ptr(),core::ptr::null(),3)},0);
        assert_eq!(unsafe { mayrs_ed25519_verify(key.as_ptr(),sig.as_ptr(),core::ptr::null(),2049)},0);
    }
    #[test]
    fn refuses_random_key_and_invalid_scalars() {
        let invalid=[0xffu8;32];
        let sig=hex::<64>(SIGNATURE);
        assert!(!verify(&invalid,&sig,b""));
        let key=hex::<32>(PUBKEY);
        let invalid_sig=[0xffu8;64];
        assert!(!verify(&key,&invalid_sig,b""));
    }
}
