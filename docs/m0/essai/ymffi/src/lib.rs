//! Minimal C ABI layer around ym2149::Ym2149 (M0 trial, option A).
use ym2149::Ym2149;

#[unsafe(no_mangle)]
pub extern "C" fn ymffi_new(master_clock: u32, sample_rate: u32) -> *mut Ym2149 {
    Box::into_raw(Box::new(Ym2149::with_clocks(master_clock, sample_rate)))
}

/// # Safety
/// `p` must come from `ymffi_new` and must not have been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ymffi_free(p: *mut Ym2149) {
    if !p.is_null() {
        drop(unsafe { Box::from_raw(p) });
    }
}

/// # Safety
/// `p` must be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ymffi_write(p: *mut Ym2149, reg: u8, value: u8) {
    unsafe { (*p).write_register(reg, value) }
}

/// Raw chip output (after the crate's DC filter), i16.
/// # Safety
/// `p` must be valid and `out` must point to `n` i16.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ymffi_render_i16(p: *mut Ym2149, out: *mut i16, n: usize) {
    let chip = unsafe { &mut *p };
    let buf = unsafe { std::slice::from_raw_parts_mut(out, n) };
    for s in buf.iter_mut() {
        *s = chip.compute_next_sample();
    }
}
