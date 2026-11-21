use core::ffi::c_char;
use core::fmt::{self, Write};

extern "C" {
    fn may_task_read(path: *const c_char, buffer: *mut u8, size: u32) -> u32;
    fn may_task_pids(pids: *mut u32, size: u32) -> u32;
    fn may_task_pidfd(pid: u32) -> i32;
    fn may_task_signal(fd: i32, terminate: i32) -> i32;
    fn may_task_page_size() -> u32;
    fn may_task_hardware_disk(major: u32, minor: u32) -> i32;
}

const LIMIT: usize = 128;
const STAT_BUFFER: usize = 8192;

fn fields(bytes: &[u8]) -> impl Iterator<Item = &[u8]> {
    bytes
        .split(|b| b.is_ascii_whitespace())
        .filter(|s| !s.is_empty())
}

fn decimal(bytes: &[u8]) -> Option<u64> {
    if bytes.is_empty() {
        return None;
    }
    bytes.iter().try_fold(0u64, |number, &byte| {
        if !byte.is_ascii_digit() {
            return None;
        }
        number.checked_mul(10)?.checked_add(u64::from(byte - b'0'))
    })
}

fn read<'a>(path: &core::ffi::CStr, buffer: &'a mut [u8]) -> &'a [u8] {
    let size = unsafe { may_task_read(path.as_ptr(), buffer.as_mut_ptr(), buffer.len() as u32) };
    &buffer[..(size as usize).min(buffer.len())]
}

#[derive(Clone, Copy)]
pub(super) struct Memory {
    pub used_kib: u64,
    pub total_kib: u64,
}

fn memory(data: &[u8]) -> Option<Memory> {
    let mut total = None;
    let mut available = None;
    for line in data.split(|&b| b == b'\n') {
        if let Some(value) = line.strip_prefix(b"MemTotal:") {
            total = fields(value).next().and_then(decimal);
        } else if let Some(value) = line.strip_prefix(b"MemAvailable:") {
            available = fields(value).next().and_then(decimal);
        }
    }
    let total_kib = total.filter(|&v| v > 0)?;
    Some(Memory {
        used_kib: total_kib.saturating_sub(available?),
        total_kib,
    })
}

fn cpu(data: &[u8]) -> Option<(u64, u64)> {
    let mut values = fields(data.split(|&b| b == b'\n').next()?.strip_prefix(b"cpu ")?);
    let mut total = 0u64;
    let mut idle = 0u64;
    // Guest times are already included in user and nice.
    for index in 0..8 {
        let value = decimal(values.next()?)?;
        total = total.checked_add(value)?;
        if index == 3 || index == 4 {
            idle = idle.checked_add(value)?;
        }
    }
    Some((total, idle))
}

fn network(data: &[u8]) -> Option<(u64, u64)> {
    let mut result = None;
    for row in data.split(|&b| b == b'\n') {
        let Some(colon) = row.iter().rposition(|&b| b == b':') else {
            continue;
        };
        let name = fields(&row[..colon]).next();
        if name == Some(b"lo") || name.is_none() {
            continue;
        }
        let mut values = fields(&row[colon + 1..]);
        let Some(rx) = values.next().and_then(decimal) else {
            continue;
        };
        let Some(tx) = values.nth(7).and_then(decimal) else {
            continue;
        };
        let (previous_rx, previous_tx) = result.unwrap_or((0u64, 0u64));
        result = Some((
            previous_rx.saturating_add(rx),
            previous_tx.saturating_add(tx),
        ));
    }
    result
}

fn disk_row(row: &[u8]) -> Option<(u32, u32, u64, u64)> {
    let mut values = fields(row);
    let major = u32::try_from(decimal(values.next()?)?).ok()?;
    let minor = u32::try_from(decimal(values.next()?)?).ok()?;
    values.next()?; // kernel device name
    let read = decimal(values.nth(2)?)?;
    let written = decimal(values.nth(3)?)?;
    Some((major, minor, read, written))
}

fn disks(data: &[u8]) -> Option<(u64, u64)> {
    let mut result = None;
    for (major, minor, read, written) in data.split(|&byte| byte == b'\n').filter_map(disk_row) {
        if unsafe { may_task_hardware_disk(major, minor) } == 0 {
            continue;
        }
        let (previous_read, previous_written) = result.unwrap_or((0u64, 0u64));
        result = Some((
            previous_read.saturating_add(read.saturating_mul(512)),
            previous_written.saturating_add(written.saturating_mul(512)),
        ));
    }
    result
}

fn delta(previous: Option<(u64, u64)>, current: Option<(u64, u64)>) -> Option<(u64, u64)> {
    let ((old_a, old_b), (new_a, new_b)) = (previous?, current?);
    Some((new_a.checked_sub(old_a)?, new_b.checked_sub(old_b)?))
}

fn rates(
    previous: Option<(u64, u64)>,
    current: Option<(u64, u64)>,
    elapsed: u32,
) -> Option<(u64, u64)> {
    let (read, written) = delta(previous, current)?;
    if elapsed == 0 {
        return None;
    }
    let scale = |bytes: u64| bytes.saturating_mul(100) / u64::from(elapsed);
    Some((scale(read), scale(written)))
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) struct Identity {
    pub pid: u32,
    started: u64,
}

#[derive(Clone, Copy)]
pub(super) struct Process {
    pub id: Identity,
    pub name: [u8; 32],
    pub state: u8,
    pub rss_kib: u64,
}

impl Process {
    const EMPTY: Self = Self {
        id: Identity { pid: 0, started: 0 },
        name: [0; 32],
        state: b'?',
        rss_kib: 0,
    };
    pub fn name(&self) -> &str {
        let length = self
            .name
            .iter()
            .position(|&byte| byte == 0)
            .unwrap_or(self.name.len());
        core::str::from_utf8(&self.name[..length]).unwrap_or("?")
    }
}

fn parse_process(pid: u32, data: &[u8], page_size: u32) -> Option<Process> {
    let begin = data.iter().position(|&b| b == b'(')?;
    let end = data.iter().rposition(|&b| b == b')')?;
    let command = data.get(begin + 1..end)?;
    let mut values = fields(data.get(end + 1..)?);
    let mut process = Process::EMPTY;
    process.id.pid = pid;
    process.state = *values.next()?.first()?;
    process.id.started = decimal(values.nth(18)?)?;
    let resident = values.nth(1)?;
    let pages = if resident.starts_with(b"-") {
        0
    } else {
        decimal(resident)?
    };
    process.rss_kib = pages.saturating_mul(u64::from(page_size)) / 1024;
    for (out, &b) in process.name.iter_mut().zip(command) {
        *out = if b.is_ascii_graphic() || b == b' ' {
            b
        } else {
            b'?'
        };
    }
    Some(process)
}

struct PathBuffer([u8; 32], usize);

impl Write for PathBuffer {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let end = self.1 + text.len();
        if end >= self.0.len() {
            return Err(fmt::Error);
        }
        self.0[self.1..end].copy_from_slice(text.as_bytes());
        self.1 = end;
        Ok(())
    }
}

fn process(pid: u32, buffer: &mut [u8], page_size: u32) -> Option<Process> {
    let mut path = PathBuffer([0; 32], 0);
    write!(path, "/proc/{pid}/stat").ok()?;
    let path = core::ffi::CStr::from_bytes_until_nul(&path.0).ok()?;
    parse_process(pid, read(path, buffer), page_size)
}

pub(super) struct Linux {
    cpu_counters: Option<(u64, u64)>,
    network_counters: Option<(u64, u64)>,
    disk_counters: Option<(u64, u64)>,
    pub cpu_percent: Option<u32>,
    pub rates: Option<(u64, u64)>,
    pub disk_rates: Option<(u64, u64)>,
    pub disk_present: bool,
    pub ram: Option<Memory>,
    processes: [Process; LIMIT],
    shown: usize,
    pub total_processes: u32,
}

impl Linux {
    pub const fn new() -> Self {
        Self {
            cpu_counters: None,
            network_counters: None,
            disk_counters: None,
            cpu_percent: None,
            rates: None,
            disk_rates: None,
            disk_present: false,
            ram: None,
            processes: [Process::EMPTY; LIMIT],
            shown: 0,
            total_processes: 0,
        }
    }

    pub fn sample(&mut self, elapsed: u32, process_page: bool) {
        let mut buffer = [0u8; STAT_BUFFER];
        let counters = cpu(read(c"/proc/stat", &mut buffer));
        self.cpu_percent = delta(self.cpu_counters, counters).and_then(|(total, idle)| {
            if total == 0 {
                return None;
            }
            Some((total.saturating_sub(idle).saturating_mul(100) / total) as u32)
        });
        self.cpu_counters = counters;
        self.ram = memory(read(c"/proc/meminfo", &mut buffer));
        let network = network(read(c"/proc/net/dev", &mut buffer));
        self.rates = rates(self.network_counters, network, elapsed);
        self.network_counters = network;
        let disks = disks(read(c"/proc/diskstats", &mut buffer));
        self.disk_rates = rates(self.disk_counters, disks, elapsed);
        self.disk_present = disks.is_some();
        self.disk_counters = disks;
        if process_page {
            self.refresh_processes(&mut buffer);
        }
    }

    fn refresh_processes(&mut self, buffer: &mut [u8]) {
        let mut pids = [0u32; LIMIT];
        self.total_processes = unsafe { may_task_pids(pids.as_mut_ptr(), LIMIT as u32) };
        self.shown = 0;
        let page_size = unsafe { may_task_page_size() };
        for pid in pids
            .into_iter()
            .take((self.total_processes as usize).min(LIMIT))
        {
            if let Some(process) = process(pid, buffer, page_size) {
                self.processes[self.shown] = process;
                self.shown += 1;
            }
        }
        self.processes[..self.shown].sort_unstable_by(|a, b| {
            b.rss_kib
                .cmp(&a.rss_kib)
                .then_with(|| a.id.pid.cmp(&b.id.pid))
        });
    }

    pub fn processes(&self) -> &[Process] {
        &self.processes[..self.shown]
    }

    pub fn sample_process_list(&mut self) {
        self.refresh_processes(&mut [0u8; STAT_BUFFER]);
    }
}

pub(super) fn terminate(id: Identity) -> bool {
    let fd = unsafe { may_task_pidfd(id.pid) };
    if fd < 0 {
        return false;
    }
    let mut buffer = [0u8; STAT_BUFFER];
    let matches = process(id.pid, &mut buffer, unsafe { may_task_page_size() })
        .is_some_and(|current| current.id == id);
    // The descriptor refers to the original task even if the numeric PID is reused.
    let result = unsafe { may_task_signal(fd, i32::from(matches)) };
    matches && result == 0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_aggregate_cpu_without_double_counting_guest_time() {
        assert_eq!(cpu(b"cpu  1 2 3 4 5 6 7 8 100 200\ncpu0"), Some((36, 9)));
        assert_eq!(delta(Some((10, 2)), Some((9, 3))), None);
    }

    #[test]
    fn reads_memory_and_excludes_loopback_traffic() {
        assert_eq!(
            memory(b"MemTotal: 2048 kB\nMemAvailable: 512 kB")
                .unwrap()
                .used_kib,
            1536
        );
        assert_eq!(
            network(b"lo: 900 0 0 0 0 0 0 0 900\neth0: 123 0 0 0 0 0 0 0 456"),
            Some((123, 456))
        );
    }

    #[test]
    fn extracts_disk_sectors_and_measures_io_per_second() {
        assert_eq!(
            disk_row(b"8 0 sda 1 2 300 4 5 6 700 8 9"),
            Some((8, 0, 300, 700))
        );
        assert_eq!(
            rates(Some((5, 8)), Some((4005, 1008)), 200),
            Some((2000, 500))
        );
    }

    #[test]
    fn command_parentheses_do_not_shift_process_fields() {
        let row = b"41 (a)b c) S 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21";
        let task = parse_process(41, row, 4096).unwrap();
        assert_eq!(task.name(), "a)b c");
        assert_eq!(task.id.started, 19);
        assert_eq!(task.rss_kib, 84);
        assert!(parse_process(41, b"41 (incomplete", 4096).is_none());
    }
}
