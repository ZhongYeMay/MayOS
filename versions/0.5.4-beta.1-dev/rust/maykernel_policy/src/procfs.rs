use core::fmt::{self, Write};

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct Snapshot {
    pub total_ticks: u64,
    pub idle_ticks: u64,
    pub frames: u64,
    pub disk_reads: u64,
    pub disk_writes: u64,
    pub rx_bytes: u64,
    pub tx_bytes: u64,
    pub managed_pages: u32,
    pub free_pages: u32,
    pub firmware_mb: u32,
    pub rx_packets: u32,
    pub tx_packets: u32,
    pub rx_errors: u32,
    pub disk_present: u32,
    pub net_present: u32,
    pub cpu: [u8; 64],
    pub version: [u8; 32],
}

impl Default for Snapshot {
    fn default() -> Self {
        // Every field is an integer or a byte array.
        unsafe { core::mem::zeroed() }
    }
}

#[cfg(target_os = "none")]
extern "C" {
    fn mayos_proc_snapshot(snapshot: *mut Snapshot);
}

#[cfg(target_os = "none")]
pub(crate) fn snapshot() -> Snapshot {
    let mut value = Snapshot::default();
    unsafe { mayos_proc_snapshot(&mut value) };
    value
}

#[repr(i32)]
#[derive(Clone, Copy, PartialEq, Eq)]
enum Node {
    Root = 256,
    CpuInfo,
    MemInfo,
    Uptime,
    Stat,
    Version,
    Mounts,
    DiskStats,
    Net,
    NetDev,
    Sys,
    Kernel,
    OsType,
    OsRelease,
}

#[repr(C)]
struct Entry {
    id: i32,
    parent: i32,
    directory: i32,
    name: [u8; 16],
}

const fn entry(node: Node, parent: i32, directory: bool, name: &[u8]) -> Entry {
    let mut fixed = [0; 16];
    let mut i = 0;
    while i < name.len() {
        fixed[i] = name[i];
        i += 1;
    }
    Entry {
        id: node as i32,
        parent,
        directory: directory as i32,
        name: fixed,
    }
}

static ENTRIES: [Entry; 14] = [
    entry(Node::Root, 0, true, b"proc"),
    entry(Node::CpuInfo, Node::Root as i32, false, b"cpuinfo"),
    entry(Node::MemInfo, Node::Root as i32, false, b"meminfo"),
    entry(Node::Uptime, Node::Root as i32, false, b"uptime"),
    entry(Node::Stat, Node::Root as i32, false, b"stat"),
    entry(Node::Version, Node::Root as i32, false, b"version"),
    entry(Node::Mounts, Node::Root as i32, false, b"mounts"),
    entry(Node::DiskStats, Node::Root as i32, false, b"diskstats"),
    entry(Node::Net, Node::Root as i32, true, b"net"),
    entry(Node::NetDev, Node::Net as i32, false, b"dev"),
    entry(Node::Sys, Node::Root as i32, true, b"sys"),
    entry(Node::Kernel, Node::Sys as i32, true, b"kernel"),
    entry(Node::OsType, Node::Kernel as i32, false, b"ostype"),
    entry(Node::OsRelease, Node::Kernel as i32, false, b"osrelease"),
];

#[no_mangle]
extern "C" fn mayos_proc_entry(id: i32) -> *const Entry {
    ENTRIES
        .iter()
        .find(|entry| entry.id == id)
        .map_or(core::ptr::null(), core::ptr::from_ref)
}

#[no_mangle]
extern "C" fn mayos_proc_next(parent: i32, after: i32) -> i32 {
    ENTRIES
        .iter()
        .find(|entry| entry.parent == parent && entry.id > after)
        .map_or(-1, |entry| entry.id)
}

struct Buffer<'a> {
    bytes: &'a mut [u8],
    len: usize,
}

impl Write for Buffer<'_> {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let end = self.len.checked_add(text.len()).ok_or(fmt::Error)?;
        self.bytes
            .get_mut(self.len..end)
            .ok_or(fmt::Error)?
            .copy_from_slice(text.as_bytes());
        self.len = end;
        Ok(())
    }
}

fn label<'a>(bytes: &'a [u8], fallback: &'a str) -> &'a str {
    let count = bytes
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(bytes.len());
    let text = &bytes[..count];
    if text.is_empty() || text.iter().any(|&byte| !(32..=126).contains(&byte)) {
        return fallback;
    }
    core::str::from_utf8(text).unwrap_or(fallback)
}

fn render(id: i32, data: &Snapshot, out: &mut Buffer<'_>) -> fmt::Result {
    let idle = data.idle_ticks.min(data.total_ticks);
    let active = data.total_ticks - idle;
    match id {
        n if n == Node::CpuInfo as i32 => {
            writeln!(
                out,
                "processor\t: 0\nmodel name\t: {}",
                label(&data.cpu, "Generic x86_64")
            )
        }
        n if n == Node::MemInfo as i32 => {
            let total = u64::from(data.managed_pages) * 4;
            let free = u64::from(data.free_pages.min(data.managed_pages)) * 4;
            writeln!(out, "MemTotal:       {total:>8} kB\nMemFree:        {free:>8} kB\nMemAvailable:   {free:>8} kB")
        }
        n if n == Node::Uptime as i32 => writeln!(
            out,
            "{}.{:02} {}.{:02}",
            data.total_ticks / 100,
            data.total_ticks % 100,
            idle / 100,
            idle % 100
        ),
        n if n == Node::Stat as i32 => writeln!(
            out,
            "cpu  0 0 {active} {idle} 0 0 0 0 0 0\ncpu0 0 0 {active} {idle} 0 0 0 0 0 0"
        ),
        n if n == Node::Version as i32 => {
            writeln!(
                out,
                "MayKernel version {} (x86_64)",
                label(&data.version, "unknown")
            )
        }
        n if n == Node::Mounts as i32 => {
            let source = if data.disk_present != 0 {
                "mayfs-disk"
            } else {
                "mayfs-ram"
            };
            writeln!(out, "{source} / mayfs rw 0 0\nproc /proc proc ro 0 0")
        }
        n if n == Node::DiskStats as i32 => {
            if data.disk_present == 0 {
                return Ok(());
            }
            writeln!(
                out,
                "   0   0 mayfs0 {} 0 {} 0 {} 0 {} 0 0 0 0",
                data.disk_reads, data.disk_reads, data.disk_writes, data.disk_writes
            )
        }
        n if n == Node::NetDev as i32 => {
            writeln!(
                out,
                "Inter-|   Receive                                                |  Transmit"
            )?;
            writeln!(out, " face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs drop fifo colls carrier compressed")?;
            if data.net_present != 0 {
                writeln!(
                    out,
                    "  eth0: {} {} {} 0 0 0 0 0 {} {} 0 0 0 0 0 0",
                    data.rx_bytes, data.rx_packets, data.rx_errors, data.tx_bytes, data.tx_packets
                )?;
            }
            Ok(())
        }
        n if n == Node::OsType as i32 => writeln!(out, "MayKernel"),
        n if n == Node::OsRelease as i32 => writeln!(out, "{}", label(&data.version, "unknown")),
        _ => Err(fmt::Error),
    }
}

#[cfg(target_os = "none")]
#[no_mangle]
extern "C" fn mayos_proc_render(id: i32, buffer: *mut u8, capacity: u32) -> i32 {
    if buffer.is_null() || capacity == 0 || capacity > 4096 {
        return -1;
    }
    let bytes = unsafe { core::slice::from_raw_parts_mut(buffer, capacity as usize) };
    let mut out = Buffer { bytes, len: 0 };
    if render(id, &snapshot(), &mut out).is_err() {
        return -1;
    }
    out.len as i32
}

#[cfg(test)]
mod tests {
    use super::*;

    fn content(id: Node, data: &Snapshot) -> std::string::String {
        let mut buf = [0; 4096];
        let mut out = Buffer {
            bytes: &mut buf,
            len: 0,
        };
        render(id as i32, data, &mut out).unwrap();
        std::string::String::from_utf8(out.bytes[..out.len].to_vec()).unwrap()
    }

    #[test]
    fn reports_measured_memory_and_cpu_time() {
        let data = Snapshot {
            total_ticks: 1532,
            idle_ticks: 912,
            managed_pages: 4000,
            free_pages: 2000,
            ..Snapshot::default()
        };
        assert_eq!(content(Node::Uptime, &data), "15.32 9.12\n");
        assert!(content(Node::Stat, &data).starts_with("cpu  0 0 620 912 "));
        assert!(content(Node::MemInfo, &data).contains("16000 kB"));
        assert!(content(Node::MemInfo, &data).contains("8000 kB"));
    }

    #[test]
    fn nested_nodes_and_bounded_output() {
        assert_eq!(mayos_proc_next(Node::Net as i32, -1), Node::NetDev as i32);
        assert_eq!(mayos_proc_next(Node::Net as i32, Node::NetDev as i32), -1);
        let mut tiny = [0; 3];
        let mut out = Buffer {
            bytes: &mut tiny,
            len: 0,
        };
        assert!(render(Node::CpuInfo as i32, &Snapshot::default(), &mut out).is_err());
        assert!(content(Node::DiskStats, &Snapshot::default()).is_empty());
    }
}
