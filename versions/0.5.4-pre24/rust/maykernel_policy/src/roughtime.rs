//! IETF Roughtime draft-11 request encoder and authenticated reply parser.
use ed25519_dalek::{Signature, VerifyingKey};
use sha2::{Digest, Sha512};

const DRAFT_11: u32 = 0x8000_000b;
const DRAFT_08: u32 = 0x8000_0008;
const REQUEST_LEN: usize = 1024;
const MAX_DATAGRAM: usize = 1232;
const FRAME: &[u8; 8] = b"ROUGHTIM";
const DELEGATION_CONTEXT: &[u8] = b"RoughTime v1 delegation signature--\0";
const RESPONSE_CONTEXT: &[u8] = b"RoughTime v1 response signature\0";

fn hash(parts: &[&[u8]]) -> [u8; 32] {
    let mut state = Sha512::new();
    for part in parts {
        state.update(part);
    }
    let mut result = [0; 32];
    result.copy_from_slice(&state.finalize()[..32]);
    result
}

fn request(nonce: &[u8; 32], key: &[u8; 32]) -> [u8; REQUEST_LEN] {
    let mut packet = [0; REQUEST_LEN];
    packet[..8].copy_from_slice(FRAME);
    let fields = [4u32, 8, 40, 72];
    packet[8..12].copy_from_slice(&((REQUEST_LEN as u32 - 12).to_le_bytes()));
    for (wire, word) in packet[12..28].chunks_exact_mut(4).zip(fields) {
        wire.copy_from_slice(&word.to_le_bytes());
    }
    packet[28..44].copy_from_slice(b"VER\0SRV\0NONCZZZZ");
    packet[44..48].copy_from_slice(&DRAFT_11.to_le_bytes());
    packet[48..52].copy_from_slice(&DRAFT_08.to_le_bytes());
    packet[52..84].copy_from_slice(&hash(&[&[0xff], key]));
    packet[84..116].copy_from_slice(nonce);
    packet
}

fn word(bytes: &[u8], at: usize) -> Option<u32> {
    Some(u32::from_le_bytes(bytes.get(at..at + 4)?.try_into().ok()?))
}

struct Message<'a> {
    data: &'a [u8],
    count: usize,
}

impl<'a> Message<'a> {
    fn parse(data: &'a [u8]) -> Option<Self> {
        let count = word(data, 0)? as usize;
        if count == 0 || count > 32 || data.len() % 4 != 0 || data.len() < 8 * count {
            return None;
        }
        let mut prior_tag = None;
        let mut prior_offset = 0;
        for index in 0..count {
            let tag = word(data, 4 * (count + index))?;
            let offset = if index + 1 == count {
                data.len() - 8 * count
            } else {
                word(data, (index + 1) * 4)? as usize
            };
            if prior_tag.is_some_and(|previous| previous >= tag)
                || offset % 4 != 0
                || offset < prior_offset
                || offset > data.len() - 8 * count
            {
                return None;
            }
            prior_tag = Some(tag);
            prior_offset = offset;
        }
        Some(Self { data, count })
    }

    fn get(&self, tag: &[u8; 4]) -> Option<&'a [u8]> {
        let search = u32::from_le_bytes(*tag);
        let mut start = 0;
        let payload = &self.data[self.count * 8..];
        for index in 0..self.count {
            let end = if index + 1 == self.count {
                payload.len()
            } else {
                word(self.data, (index + 1) * 4)? as usize
            };
            if word(self.data, 4 * (self.count + index))? == search {
                return payload.get(start..end);
            }
            start = end;
        }
        None
    }

    fn u32(&self, tag: &[u8; 4]) -> Option<u32> {
        Some(u32::from_le_bytes(self.get(tag)?.try_into().ok()?))
    }

    fn u64(&self, tag: &[u8; 4]) -> Option<u64> {
        Some(u64::from_le_bytes(self.get(tag)?.try_into().ok()?))
    }
}

fn signed(key: &[u8], signature: &[u8], context: &[u8], body: &[u8]) -> Option<()> {
    let mut message = [0; 256];
    let size = context.len() + body.len();
    message.get_mut(..size)?;
    message[..context.len()].copy_from_slice(context);
    message[context.len()..size].copy_from_slice(body);
    let key = VerifyingKey::from_bytes(key.try_into().ok()?).ok()?;
    let signature = Signature::from_slice(signature).ok()?;
    key.verify_strict(&message[..size], &signature).ok()
}

fn verify(packet: &[u8], nonce: &[u8; 32], key: &[u8; 32]) -> Option<(u64, u32)> {
    if packet.len() > MAX_DATAGRAM
        || packet.get(..8)? != FRAME
        || word(packet, 8)? as usize != packet.len() - 12
    {
        return None;
    }
    let reply = Message::parse(&packet[12..])?;
    if !matches!(reply.u32(b"VER\0")?, DRAFT_11 | DRAFT_08) || reply.get(b"NONC")? != nonce {
        return None;
    }
    let certificate = Message::parse(reply.get(b"CERT")?)?;
    let delegation = certificate.get(b"DELE")?;
    let response = reply.get(b"SREP")?;
    let bounds = Message::parse(delegation)?;
    let time = Message::parse(response)?;
    let midpoint = time.u64(b"MIDP")?;
    let radius = time.u32(b"RADI")?;
    if !(1_577_836_800..4_102_444_800).contains(&midpoint)
        || radius > 10
        || midpoint.checked_sub(radius as u64)? < bounds.u64(b"MINT")?
        || midpoint.checked_add(radius as u64)? > bounds.u64(b"MAXT")?
    {
        return None;
    }

    let path = reply.get(b"PATH")?;
    let mut index = reply.u32(b"INDX")?;
    if path.len() % 32 != 0 {
        return None;
    }
    let mut root = hash(&[&[0], nonce]);
    for sibling in path.chunks_exact(32) {
        root = if index & 1 == 0 {
            hash(&[&[1], &root, sibling])
        } else {
            hash(&[&[1], sibling, &root])
        };
        index >>= 1;
    }
    if index != 0 || time.get(b"ROOT")? != root {
        return None;
    }
    signed(
        key,
        certificate.get(b"SIG\0")?,
        DELEGATION_CONTEXT,
        delegation,
    )?;
    signed(
        bounds.get(b"PUBK")?,
        reply.get(b"SIG\0")?,
        RESPONSE_CONTEXT,
        response,
    )?;
    Some((midpoint, radius))
}

/// SAFETY: the kernel supplies separate buffers of 32, 32, and 1024 bytes.
#[no_mangle]
pub unsafe extern "C" fn mayrs_roughtime_request(
    nonce: *const [u8; 32],
    key: *const [u8; 32],
    output: *mut [u8; REQUEST_LEN],
) {
    unsafe { output.write(request(&*nonce, &*key)) };
}

/// SAFETY: the kernel supplies a datagram of length bytes, two 32-byte
/// inputs, and a separate writable radius output.
#[no_mangle]
pub unsafe extern "C" fn mayrs_roughtime_verify(
    data: *const u8,
    length: u32,
    nonce: *const [u8; 32],
    key: *const [u8; 32],
    radius: *mut u32,
) -> u64 {
    if length as usize > MAX_DATAGRAM {
        return 0;
    }
    let packet = unsafe { core::slice::from_raw_parts(data, length as usize) };
    match unsafe { verify(packet, &*nonce, &*key) } {
        Some((midpoint, bound)) => {
            unsafe { radius.write(bound) };
            midpoint
        }
        None => 0,
    }
}
