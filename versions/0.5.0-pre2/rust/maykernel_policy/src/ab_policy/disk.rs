//! Diagnostic-only 48-byte little-endian journal codec.
//! The real install/bootloader must not call this. No crypto or trust here.
use super::*;
const BYTES: usize = 48;
fn encode(j: &MayAbJournal, out: &mut [u8; BYTES]) {
    for (i, word) in [j.magic,j.generation,j.active_slot,j.trial_slot,
                       j.attempts_left,j.reserved].iter().enumerate() {
        out[4*i..4*i+4].copy_from_slice(&word.to_le_bytes());
    }
    out[24..32].copy_from_slice(&j.accepted_epoch.to_le_bytes());
    out[32..40].copy_from_slice(&j.trial_epoch.to_le_bytes());
    out[40..44].copy_from_slice(&j.checksum.to_le_bytes());
    out[44..48].copy_from_slice(&j.format.to_le_bytes());
}
fn decode(bytes: &[u8; BYTES]) -> Option<MayAbJournal> {
    let w = |p: usize| u32::from_le_bytes([bytes[p],bytes[p+1],bytes[p+2],bytes[p+3]]);
    let d = |p: usize| u64::from_le_bytes([
        bytes[p],bytes[p+1],bytes[p+2],bytes[p+3],
        bytes[p+4],bytes[p+5],bytes[p+6],bytes[p+7]
    ]);
    let j=MayAbJournal {
        magic:w(0),generation:w(4),active_slot:w(8),trial_slot:w(12),
        attempts_left:w(16),reserved:w(20),
        accepted_epoch:d(24),trial_epoch:d(32),checksum:w(40),format:w(44)
    };
    if valid(&j) { Some(j) } else { None }
}
#[cfg(feature="ab-disk-diagnostic")]
#[no_mangle]
pub unsafe extern "C" fn mayrs_ab_disk_initial(out: *mut u8, cap:u32) -> i32 {
    if out.is_null() || cap != BYTES as u32 { return 0; }
    let Some(j)=new(0,5) else { return 0 };
    let mut buf=[0u8;BYTES];
    encode(&j,&mut buf);
    // SAFETY: C test caller guarantees a writable 48-byte buffer.
    unsafe { core::ptr::copy_nonoverlapping(buf.as_ptr(),out,BYTES); }
    1
}
#[cfg(feature="ab-disk-diagnostic")]
#[no_mangle]
pub unsafe extern "C" fn mayrs_ab_disk_select(
    a:*const u8,b:*const u8,out:*mut u8,cap:u32
)->u32 {
    if a.is_null() || b.is_null() || out.is_null() || cap != BYTES as u32 { return 0; }
    let mut aa=[0u8;BYTES];let mut bb=[0u8;BYTES];
    // SAFETY: C test caller guarantees readable 48-byte stable buffers.
    unsafe {
        core::ptr::copy_nonoverlapping(a,aa.as_mut_ptr(),BYTES);
        core::ptr::copy_nonoverlapping(b,bb.as_mut_ptr(),BYTES);
    }
    let pick=match (decode(&aa),decode(&bb)) {
        (Some(x),Some(y))=>select_latest(&x,&y),
        (Some(x),None)=>Some(x),
        (None,Some(y))=>Some(y),
        (None,None)=>None,
    };
    let Some(j)=pick else { return 0 };
    let mut buf=[0u8;BYTES];encode(&j,&mut buf);
    // SAFETY: output is 48 writable bytes. All input copied first.
    unsafe { core::ptr::copy_nonoverlapping(buf.as_ptr(),out,BYTES); }
    j.generation
}
#[cfg(feature="ab-disk-diagnostic")]
#[no_mangle]
pub unsafe extern "C" fn mayrs_ab_disk_synthetic_stage(
    source:*const u8,out:*mut u8,cap:u32
)->i32 {
    if source.is_null() || out.is_null() || cap != BYTES as u32 { return 0; }
    let mut buf=[0u8;BYTES];
    // SAFETY: C test caller provides readable 48-byte stable record.
    unsafe {core::ptr::copy_nonoverlapping(source,buf.as_mut_ptr(),BYTES);}
    let Some(mut j)=decode(&buf) else {return 0};
    let Some(epoch)=j.accepted_epoch.checked_add(1) else {return 0};
    // TEST ONLY: authenticate=true is synthetic and never available in a
    // production kernel; no image, slot or bootloader gets modified.
    let candidate_slot=1-j.active_slot;
    if !stage(&mut j,candidate_slot,true,epoch) {return 0}
    encode(&j,&mut buf);
    unsafe {core::ptr::copy_nonoverlapping(buf.as_ptr(),out,BYTES);}
    1
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn roundtrip_record_and_stage() {
        let x=new(0,5).unwrap();
        let mut bytes=[0u8;BYTES]; encode(&x,&mut bytes);
        assert_eq!(&bytes[..4],&MAGIC.to_le_bytes());
        assert_eq!(decode(&bytes),Some(x));
        let mut trial=x;
        assert!(stage(&mut trial,1,true,6));
        encode(&trial,&mut bytes);
        assert_eq!(decode(&bytes),Some(trial));
    }
    #[test]
    fn checksum_and_invalid_fields_rejected() {
        let x=new(0,5).unwrap();
        let mut bytes=[0u8;BYTES]; encode(&x,&mut bytes);
        bytes[8]=3;assert!(decode(&bytes).is_none());
        encode(&x,&mut bytes);bytes[40]^=3;assert!(decode(&bytes).is_none());
        encode(&x,&mut bytes);bytes[44]=0xff;assert!(decode(&bytes).is_none());
    }
    #[test]
    fn torn_second_copy_preserves_first_generation() {
        let a=new(0,5).unwrap();
        let mut b=a;
        assert!(stage(&mut b,1,true,6));
        let mut left=[0u8;BYTES];let mut right=[0u8;BYTES];
        encode(&a,&mut left);encode(&b,&mut right);
        assert_eq!(select_latest(&decode(&left).unwrap(),&decode(&right).unwrap()),Some(b));
        right[24]^=0xff;
        assert_eq!(decode(&right),None);
        assert_eq!(decode(&left),Some(a));
    }
}
