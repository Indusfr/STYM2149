//! STYM2149 — C ABI layer around ym2149::Ym2149 (secondary oracle S3, tests only).
//! Programming: Pascal "Indus" Nowak (with Claude's help).
//! Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
use ym2149::Ym2149;

#[no_mangle]
pub extern "C" fn ymffi_new(master_clock: u32, sample_rate: u32) -> *mut Ym2149 {
    Box::into_raw(Box::new(Ym2149::with_clocks(master_clock, sample_rate)))
}

/// # Safety
/// `p` comes from `ymffi_new` and has not been freed.
#[no_mangle]
pub unsafe extern "C" fn ymffi_free(p: *mut Ym2149) {
    if !p.is_null() {
        drop(Box::from_raw(p));
    }
}

/// # Safety
/// `p` valid.
#[no_mangle]
pub unsafe extern "C" fn ymffi_write(p: *mut Ym2149, reg: u8, value: u8) {
    (*p).write_register(reg, value)
}

/// Output of the chip (after the crate's DC filter), i16.
/// # Safety
/// `p` valid, `out` points to `n` i16.
#[no_mangle]
pub unsafe extern "C" fn ymffi_render_i16(p: *mut Ym2149, out: *mut i16, n: usize) {
    let chip = &mut *p;
    let buf = std::slice::from_raw_parts_mut(out, n);
    for s in buf.iter_mut() {
        *s = chip.compute_next_sample();
    }
}
