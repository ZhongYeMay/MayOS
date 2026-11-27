//! MayOS A/B boot-slot *policy* and crash-consistent journal validation.
//! No filesystem writes, bootloader selection or installer in this module.
//! Data must be committed atomically by a future bootloader-compatible store.
//! FNV checksums detect accidental corruption, NOT malicious modification.
#[cfg(any(test, feature = "ab-disk-diagnostic"))]
mod disk;

const MAGIC: u32 = 0x4d41_4231; // MAB1
const FORMAT: u32 = 1;
const NONE: u32 = 2;
const MAX_ATTEMPTS: u32 = 3;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct MayAbJournal {
    magic: u32,
    generation: u32,
    active_slot: u32,
    trial_slot: u32,
    attempts_left: u32,
    reserved: u32,
    accepted_epoch: u64,
    trial_epoch: u64,
    checksum: u32,
    format: u32,
}

fn hash(j: &MayAbJournal) -> u32 {
    let mut h: u32 = 2166136261;
    let mut feed = |bytes: &[u8]| {
        for &b in bytes {
            h = (h ^ u32::from(b)).wrapping_mul(16777619);
        }
    };
    for word in [
        j.magic, j.generation, j.active_slot, j.trial_slot,
        j.attempts_left, j.reserved, j.format,
    ] {
        feed(&word.to_le_bytes());
    }
    feed(&j.accepted_epoch.to_le_bytes());
    feed(&j.trial_epoch.to_le_bytes());
    h
}
fn seal(j: &mut MayAbJournal) { j.checksum = hash(j); }
fn valid(j: &MayAbJournal) -> bool {
    if j.magic != MAGIC || j.format != FORMAT ||
       j.generation == 0 || j.reserved != 0 ||
       j.active_slot > 1 || j.checksum != hash(j) {
        return false;
    }
    if j.trial_slot == NONE {
        return j.attempts_left == 0 && j.trial_epoch == 0;
    }
    j.trial_slot <= 1 && j.trial_slot != j.active_slot &&
    j.attempts_left <= MAX_ATTEMPTS &&
    j.trial_epoch > j.accepted_epoch
}
fn bump(j: &mut MayAbJournal) -> bool {
    let Some(next) = j.generation.checked_add(1) else { return false };
    j.generation = next;
    seal(j);
    true
}
fn new(active_slot: u32, accepted_epoch: u64) -> Option<MayAbJournal> {
    if active_slot > 1 { return None; }
    let mut j = MayAbJournal {
        magic: MAGIC, generation: 1, active_slot, trial_slot: NONE,
        attempts_left: 0, reserved: 0, accepted_epoch, trial_epoch: 0,
        checksum: 0, format: FORMAT,
    };
    seal(&mut j);
    Some(j)
}
fn stage(j: &mut MayAbJournal, trial: u32, authenticated: bool, epoch: u64) -> bool {
    if !valid(j) || !authenticated || trial > 1 ||
       trial == j.active_slot || j.trial_slot != NONE ||
       epoch <= j.accepted_epoch || j.generation == u32::MAX {
        return false;
    }
    j.trial_slot = trial;
    j.attempts_left = MAX_ATTEMPTS;
    j.trial_epoch = epoch;
    bump(j)
}
fn start_attempt(j: &mut MayAbJournal) -> Option<u32> {
    if !valid(j) || j.trial_slot == NONE ||
       j.attempts_left == 0 || j.generation == u32::MAX {
        return None;
    }
    j.attempts_left -= 1;
    let slot = j.trial_slot;
    if !bump(j) { return None; }
    Some(slot)
}
fn confirm(j: &mut MayAbJournal, healthy: bool) -> bool {
    if !valid(j) || !healthy || j.trial_slot == NONE ||
       j.generation == u32::MAX {
        return false;
    }
    j.active_slot = j.trial_slot;
    j.accepted_epoch = j.trial_epoch;
    j.trial_slot = NONE;
    j.trial_epoch = 0;
    j.attempts_left = 0;
    bump(j)
}
fn rollback_exhausted(j: &mut MayAbJournal) -> bool {
    if !valid(j) || j.trial_slot == NONE ||
       j.attempts_left != 0 || j.generation == u32::MAX {
        return false;
    }
    // The last confirmed active slot remains untouched until a healthy boot.
    j.trial_slot = NONE;
    j.trial_epoch = 0;
    j.attempts_left = 0;
    bump(j)
}
fn select_latest(a: &MayAbJournal, b: &MayAbJournal) -> Option<MayAbJournal> {
    match (valid(a), valid(b)) {
        (true, false) => Some(*a),
        (false, true) => Some(*b),
        (false, false) => None,
        (true, true) if a.generation > b.generation => Some(*a),
        (true, true) if b.generation > a.generation => Some(*b),
        (true, true) if a == b => Some(*a),
        _ => None, // mismatched mirrors with equal generation: fail closed
    }
}

// Only a diagnostic entrypoint is wired into MayKernel today. Caller cannot
// authorize installation by changing an input flag; real signed-image
// authentication must be checked at a future write boundary.
#[no_mangle]
pub extern "C" fn mayrs_ab_selftest() -> i32 {
    let Some(mut state) = new(0, 7) else { return 0 };
    if !valid(&state) || stage(&mut state, 1, false, 8) ||
       !stage(&mut state, 1, true, 8) {
        return 0;
    }
    let backup = state;
    if start_attempt(&mut state) != Some(1) ||
       state.attempts_left != 2 ||
       select_latest(&backup, &state) != Some(state) {
        return 0;
    }
    let mut damaged = state;
    damaged.checksum ^= 1;
    if select_latest(&damaged, &backup) != Some(backup) ||
       confirm(&mut state, false) || !confirm(&mut state, true) ||
       state.active_slot != 1 || state.accepted_epoch != 8 {
        return 0;
    }
    let Some(mut failing) = new(0, 5) else { return 0 };
    if !stage(&mut failing, 1, true, 6) { return 0; }
    for _ in 0..3 {
        if start_attempt(&mut failing) != Some(1) { return 0; }
    }
    if start_attempt(&mut failing).is_some() ||
       !rollback_exhausted(&mut failing) || failing.active_slot != 0 ||
       failing.trial_slot != NONE {
        return 0;
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_unsigned_staging_and_downgrades() {
        let mut s = new(0, 10).unwrap();
        assert!(!stage(&mut s, 1, false, 11));
        assert!(!stage(&mut s, 0, true, 11));
        assert!(!stage(&mut s, 1, true, 10));
        assert!(!stage(&mut s, 2, true, 11));
        assert!(stage(&mut s, 1, true, 11));
        assert!(!stage(&mut s, 1, true, 12));
    }
    #[test]
    fn confirm_is_health_gated_and_monotonic() {
        let mut s = new(0, 50).unwrap();
        assert!(stage(&mut s, 1, true, 51));
        assert_eq!(start_attempt(&mut s),Some(1));
        assert!(!confirm(&mut s,false));
        assert_eq!(s.active_slot,0);
        assert!(confirm(&mut s,true));
        assert_eq!(s.active_slot,1);
        assert_eq!(s.accepted_epoch,51);
        assert!(!stage(&mut s,0,true,50));
    }
    #[test]
    fn three_failures_restore_old_boot_slot() {
        let mut s = new(0,1).unwrap();
        assert!(stage(&mut s,1,true,2));
        for expected in [2,1,0] {
            assert_eq!(start_attempt(&mut s),Some(1));
            assert_eq!(s.attempts_left,expected);
        }
        assert_eq!(start_attempt(&mut s),None);
        assert!(rollback_exhausted(&mut s));
        assert_eq!(s.active_slot,0);
        assert_eq!(s.accepted_epoch,1);
        assert!(!rollback_exhausted(&mut s));
    }
    #[test]
    fn corruption_and_torn_mirror_are_rejected() {
        let a=new(0,5).unwrap();
        let mut b=a;
        assert!(stage(&mut b,1,true,6));
        assert_eq!(select_latest(&a,&b),Some(b));
        b.checksum ^= 0x1234;
        assert_eq!(select_latest(&a,&b),Some(a));
        let mut bad=a;
        bad.format=99;
        assert_eq!(select_latest(&bad,&b),None);
        assert!(!stage(&mut bad,1,true,6));
    }
    #[test]
    fn rejects_ambiguous_generation_and_overflow() {
        let a=new(0,5).unwrap();
        let mut b=new(1,5).unwrap();
        assert_eq!(select_latest(&a,&b),None);
        b.generation=u32::MAX;
        seal(&mut b);
        assert!(!stage(&mut b,0,true,6));
        assert_eq!(new(2,0),None);
    }
    #[test]
    fn native_diagnostic_covers_rollback() {
        assert_eq!(mayrs_ab_selftest(),1);
        assert_eq!(core::mem::size_of::<MayAbJournal>(),48);
    }
}
