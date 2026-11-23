//! The Rust mirror of kernel/procfs.h. Both desktops consume identical C ABI
//! snapshots. Unavailable GPU utilization must never be replaced by a guessed
//! framebuffer percentage; actual display submissions are a separate metric.
#![allow(dead_code)]
#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct PerfSnapshot {
    pub cpu_valid: u32,
    pub cpu_percent: u32,
    pub ram_valid: u32,
    pub ram_used_mib: u32,
    pub ram_total_mib: u32,
    pub gpu_valid: u32,
    pub gpu_percent: u32,
    pub framebuffer_valid: u32,
    pub framebuffer_submits_s: u32,
    pub disk_valid: u32,
    pub disk_busy_valid: u32,
    pub disk_busy_percent: u32,
    pub disk_read_kib_s: u32,
    pub disk_write_kib_s: u32,
    pub net_valid: u32,
    pub net_link: u32,
    pub net_rx_kib_s: u32,
    pub net_tx_kib_s: u32,
    pub sample_ready: u32,
    pub tick: u32,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GpuActivity {
    HardwarePercent(u32),
    FramebufferSubmissionsPerSecond(u32),
    Unavailable,
}
impl PerfSnapshot {
    pub const fn gpu_activity(&self) -> GpuActivity {
        if self.gpu_valid != 0 {
            GpuActivity::HardwarePercent(self.gpu_percent)
        } else if self.framebuffer_valid != 0 {
            GpuActivity::FramebufferSubmissionsPerSecond(self.framebuffer_submits_s)
        } else {
            GpuActivity::Unavailable
        }
    }
    pub const fn cpu_activity(&self) -> Option<u32> {
        if self.cpu_valid == 0 { None } else { Some(self.cpu_percent) }
    }
    pub const fn ram_usage(&self) -> Option<(u32, u32)> {
        if self.ram_valid == 0 || self.ram_total_mib == 0 {
            None
        } else {
            Some((self.ram_used_mib, self.ram_total_mib))
        }
    }
}
// The C platform provides the actual counters; this Rust type has no cached
// fake workload or synthetic process/temperature information.
unsafe extern "C" {
    pub fn mayperf_sample(output: *mut PerfSnapshot, ticks_100hz: u32);
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn layout_matches_c_20_u32_fields() {
        assert_eq!(core::mem::size_of::<PerfSnapshot>(), 80);
        assert_eq!(core::mem::align_of::<PerfSnapshot>(), 4);
    }
    #[test]
    fn gpu_fallback_is_not_hardware_percent() {
        let mut state = PerfSnapshot::default();
        assert_eq!(state.gpu_activity(), GpuActivity::Unavailable);
        state.framebuffer_valid = 1;
        state.framebuffer_submits_s = 19;
        assert_eq!(state.gpu_activity(), GpuActivity::FramebufferSubmissionsPerSecond(19));
        state.gpu_valid = 1;
        state.gpu_percent = 3;
        assert_eq!(state.gpu_activity(), GpuActivity::HardwarePercent(3));
    }
    #[test]
    fn missing_metrics_remain_missing() {
        let mut state = PerfSnapshot::default();
        assert_eq!(state.cpu_activity(), None);
        assert_eq!(state.ram_usage(), None);
        state.cpu_valid = 1; state.cpu_percent = 0;
        state.ram_valid = 1; state.ram_total_mib = 512; state.ram_used_mib = 2;
        assert_eq!(state.cpu_activity(), Some(0));
        assert_eq!(state.ram_usage(), Some((2, 512)));
    }
}
