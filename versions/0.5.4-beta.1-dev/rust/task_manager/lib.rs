#![cfg_attr(not(test), no_std)]

mod view;

#[cfg(not(test))]
extern "C" {
    fn abort() -> !;
}

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe { abort() }
}

// The host core library references this symbol in unwind metadata. This
// static library is linked into a C executable and always uses panic=abort.
#[cfg(not(test))]
#[no_mangle]
pub extern "C" fn rust_eh_personality() -> ! {
    unsafe { abort() }
}
