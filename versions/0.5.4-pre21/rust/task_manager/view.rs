use core::ffi::c_char;
use core::fmt::{self, Write};
use core::ptr::addr_of_mut;

#[cfg(target_os = "linux")]
#[path = "linux.rs"]
mod linux;

extern "C" {
    fn may_task_selected_device(index: u32);
    fn may_task_heading(x: i32, y: i32, text: *const c_char, color: u32);
    fn may_task_cpu_name(destination: *mut c_char, size: u32);
    fn may_task_gpu_name(destination: *mut c_char, size: u32);
    fn may_task_text(x: i32, y: i32, text: *const c_char, color: u32);
    fn may_task_rect(x: i32, y: i32, w: i32, h: i32, color: u32);
    fn may_task_card(x: i32, y: i32, w: i32, h: i32, radius: i32, color: u32);
}

struct Text([u8; 128], usize);

impl Write for Text {
    fn write_str(&mut self, value: &str) -> fmt::Result {
        let end = self.1 + value.len();
        if end >= self.0.len() {
            return Err(fmt::Error);
        }
        self.0[self.1..end].copy_from_slice(value.as_bytes());
        self.1 = end;
        Ok(())
    }
}

struct Colors {
    bg: u32,
    card: u32,
    border: u32,
    text: u32,
    muted: u32,
    active: u32,
}

impl Colors {
    fn new(dark: bool) -> Self {
        if dark {
            Self {
                bg: 0x222226,
                card: 0x2c2c30,
                border: 0x37373b,
                text: 0xf4f4f6,
                muted: 0xa6a6b0,
                active: 0x3f3f42,
            }
        } else {
            Self {
                bg: 0xf7f7f8,
                card: 0xeaeaec,
                border: 0xd9d9df,
                text: 0x1c1c22,
                muted: 0x555562,
                active: 0xd9d9db,
            }
        }
    }
}

struct Canvas {
    x: i32,
    y: i32,
    colors: Colors,
}

impl Canvas {
    fn text(&self, x: i32, y: i32, value: fmt::Arguments<'_>, color: u32) {
        let mut text = Text([0; 128], 0);
        let _ = text.write_fmt(value);
        unsafe { may_task_text(self.x + x, self.y + y, text.0.as_ptr().cast(), color) }
    }

    fn heading(&self, x: i32, y: i32, value: fmt::Arguments<'_>, color: u32) {
        let mut text = Text([0; 128], 0);
        let _ = text.write_fmt(value);
        unsafe { may_task_heading(self.x + x, self.y + y, text.0.as_ptr().cast(), color) }
    }

    fn rect(&self, x: i32, y: i32, w: i32, h: i32, color: u32) {
        unsafe { may_task_rect(self.x + x, self.y + y, w, h, color) }
    }

    fn rounded(&self, x: i32, y: i32, w: i32, h: i32, radius: i32, color: u32) {
        unsafe { may_task_card(self.x + x, self.y + y, w, h, radius, color) }
    }

    #[cfg(target_os = "linux")]
    fn card(&self, x: i32, y: i32, w: i32, h: i32) {
        self.rounded(x, y, w, h, 13, self.colors.border);
        self.rounded(x + 1, y + 1, w - 2, h - 2, 12, self.colors.card);
    }
}

const NAV: i32 = 274;
const START: i32 = NAV + 12;
const DEVICE_TOP: i32 = 49;
const DEVICE_HEIGHT: i32 = 76;
const HISTORY: usize = 60;

#[derive(Clone, Copy)]
enum Device {
    Cpu,
    Memory,
    Disk,
    Network,
    Gpu,
}

impl Device {
    const LIST: [Self; 5] = [
        Self::Cpu,
        Self::Memory,
        Self::Disk,
        Self::Network,
        Self::Gpu,
    ];
    fn label(self) -> &'static str {
        ["CPU", "Memory", "Disk", "Network", "GPU"][self as usize]
    }
    fn color(self) -> u32 {
        [0x3584e4, 0x62a0ea, 0x2ec27e, 0xdc8add, 0xff6654][self as usize]
    }
    fn percent(self) -> bool {
        matches!(self, Self::Cpu | Self::Memory | Self::Gpu)
    }
}

fn blend(a: u32, b: u32, weight: u32) -> u32 {
    let channel = |shift: u32| {
        ((((a >> shift) & 255u32) * (256 - weight) + ((b >> shift) & 255u32) * weight) / 256u32)
            << shift
    };
    channel(16) | channel(8) | channel(0)
}

struct Trend {
    primary: [Option<u64>; HISTORY],
    secondary: [Option<u64>; HISTORY],
}

impl Trend {
    const EMPTY: Self = Self {
        primary: [None; HISTORY],
        secondary: [None; HISTORY],
    };
    fn add(&mut self, values: (Option<u64>, Option<u64>)) {
        self.primary.rotate_left(1);
        self.secondary.rotate_left(1);
        self.primary[HISTORY - 1] = values.0;
        self.secondary[HISTORY - 1] = values.1;
    }
    fn limit(&self, minimum: u64) -> u64 {
        let peak = self
            .primary
            .iter()
            .chain(&self.secondary)
            .flatten()
            .copied()
            .max()
            .unwrap_or(0);
        peak.max(minimum)
            .checked_next_power_of_two()
            .unwrap_or(u64::MAX)
    }
}

fn chart(
    canvas: &Canvas,
    area: (i32, i32, i32, i32),
    trend: &Trend,
    color: u32,
    limit: u64,
    mini: bool,
) {
    let (x, y, w, h) = area;
    if w < 4 || h < 4 {
        return;
    }
    let c = &canvas.colors;
    let grid = blend(c.bg, color, 55);
    canvas.rounded(x, y, w, h, 6, color);
    canvas.rounded(x + 1, y + 1, w - 2, h - 2, 5, c.bg);
    if !mini {
        for col in 1..10 {
            canvas.rect(x + col * w / 10, y + 2, 1, h - 4, grid);
        }
        for row in 1..10 {
            canvas.rect(x + 2, y + row * h / 10, w - 4, 1, grid);
        }
    }
    for (series, tint, solid) in [
        (&trend.primary, color, true),
        (&trend.secondary, blend(color, c.text, 105), false),
    ] {
        for index in 1..HISTORY {
            let Some(next) = series[index] else {
                continue;
            };
            let current = series[index - 1].unwrap_or(next);
            let first_x = (index - 1) as i32 * (w - 5) / (HISTORY as i32 - 1);
            let last_x = index as i32 * (w - 5) / (HISTORY as i32 - 1);
            let to_y =
                |value: u64| (value.min(limit).saturating_mul((h - 6) as u64) / limit) as i32;
            let from = to_y(current);
            let to = to_y(next);
            let span = (last_x - first_x).max(1);
            for dx in 0..span {
                let level = from + (to - from) * (dx + 1) / span;
                let at = y + h - 3 - level;
                if solid && level > 0 {
                    canvas.rect(
                        x + 2 + first_x + dx,
                        at + 1,
                        1,
                        level,
                        blend(c.bg, color, 76),
                    );
                }
                canvas.rect(x + 2 + first_x + dx, at, 1, 1, tint);
            }
        }
    }
}

#[derive(Default)]
struct Performance {
    cpu: Option<u32>,
    ram: Option<(u64, u64)>,
    network: Option<(u64, u64)>,
    disk: Option<(u64, u64)>,
    gpu: Option<u32>,
    frame_rate: Option<u64>,
}

struct State {
    sampled: Option<u32>,
    window_count: u32,
    performance: Performance,
    device: usize,
    trends: [Trend; 5],
    frames: Trend,
    #[cfg(target_os = "linux")]
    core_trends: [Trend; 32],
    #[cfg(target_os = "none")]
    previous: Option<crate::procfs::Snapshot>,
    #[cfg(target_os = "linux")]
    linux: linux::Linux,
    #[cfg(target_os = "linux")]
    page: ProcessesPage,
}

#[cfg(target_os = "none")]
fn native_performance(state: &mut State) -> Performance {
    let data = crate::procfs::snapshot();
    let mut performance = Performance {
        ram: Some((
            u64::from(data.managed_pages.saturating_sub(data.free_pages)) * 4,
            u64::from(data.managed_pages) * 4,
        )),
        ..Performance::default()
    };
    if let Some(old) = state.previous.replace(data) {
        let time = data.total_ticks.saturating_sub(old.total_ticks);
        let idle = data.idle_ticks.saturating_sub(old.idle_ticks);
        let rate = |value: u64, old: u64, scale: u64| {
            value
                .saturating_sub(old)
                .saturating_mul(scale)
                .checked_div(time)
        };
        performance.cpu = time
            .saturating_sub(idle)
            .saturating_mul(100)
            .checked_div(time)
            .map(|percent| percent as u32);
        performance.frame_rate = rate(data.frames, old.frames, 100);
        if data.disk_present != 0 {
            performance.disk = rate(data.disk_read_bytes, old.disk_read_bytes, 100).zip(rate(
                data.disk_write_bytes,
                old.disk_write_bytes,
                100,
            ));
        }
        if data.net_present != 0 {
            performance.network =
                rate(data.rx_bytes, old.rx_bytes, 100).zip(rate(data.tx_bytes, old.tx_bytes, 100));
        }
    }
    performance
}

#[cfg(target_os = "linux")]
struct ProcessesPage {
    open: bool,
    query: [u8; 32],
    length: usize,
    selected: Option<linux::Identity>,
    offset: usize,
    pending: Option<linux::Identity>,
    result: Option<bool>,
}

#[cfg(target_os = "linux")]
impl ProcessesPage {
    const fn new() -> Self {
        Self {
            open: false,
            query: [0; 32],
            length: 0,
            selected: None,
            offset: 0,
            pending: None,
            result: None,
        }
    }

    fn query(&self) -> &[u8] {
        &self.query[..self.length]
    }

    fn visible(&self, process: &linux::Process) -> bool {
        self.length == 0
            || process
                .name()
                .as_bytes()
                .windows(self.length)
                .any(|part| part.eq_ignore_ascii_case(self.query()))
    }

    fn reset_selection(&mut self) {
        self.selected = None;
        self.offset = 0;
        self.result = None;
    }
}

static mut STATE: State = State {
    sampled: None,
    window_count: 0,
    performance: Performance {
        cpu: None,
        ram: None,
        network: None,
        disk: None,
        gpu: None,
        frame_rate: None,
    },
    device: 0,
    trends: [const { Trend::EMPTY }; 5],
    frames: Trend::EMPTY,
    #[cfg(target_os = "linux")]
    core_trends: [const { Trend::EMPTY }; 32],
    #[cfg(target_os = "none")]
    previous: None,
    #[cfg(target_os = "linux")]
    linux: linux::Linux::new(),
    #[cfg(target_os = "linux")]
    page: ProcessesPage::new(),
};

// The desktop invokes these entry points on its single UI thread.
#[no_mangle]
pub extern "C" fn may_task_poll(ticks: u32, windows: u32) -> i32 {
    let state = unsafe { &mut *addr_of_mut!(STATE) };
    let elapsed = state.sampled.map_or(100, |last| ticks.wrapping_sub(last));
    if elapsed < 100 {
        return 0;
    }
    state.sampled = Some(ticks);
    state.window_count = windows;
    #[cfg(target_os = "none")]
    {
        state.performance = native_performance(state);
    }
    #[cfg(target_os = "linux")]
    {
        state.linux.sample(elapsed, state.page.open);
        for (trend, percent) in state.core_trends.iter_mut().zip(state.linux.cores) {
            trend.add((percent.map(u64::from), None));
        }
        state.performance = Performance {
            cpu: state.linux.cpu_percent,
            ram: state.linux.ram.map(|ram| (ram.used_kib, ram.total_kib)),
            network: state.linux.rates,
            disk: state.linux.disk_rates,
            gpu: state.linux.gpu,
            frame_rate: state.linux.frame_rate,
        };
        let count = state
            .linux
            .processes()
            .iter()
            .filter(|p| state.page.visible(p))
            .count();
        state.page.offset = state.page.offset.min(count.saturating_sub(1));
    }
    let perf = &state.performance;
    state.trends[0].add((perf.cpu.map(u64::from), None));
    state.trends[1].add((
        perf.ram
            .and_then(|(used, total)| usage(used, total))
            .map(u64::from),
        None,
    ));
    state.trends[2].add(perf.disk.map_or((None, None), |(a, b)| (Some(a), Some(b))));
    state.trends[3].add(
        perf.network
            .map_or((None, None), |(a, b)| (Some(a), Some(b))),
    );
    state.trends[4].add((perf.gpu.map(u64::from), None));
    state.frames.add((perf.frame_rate, None));
    1
}

#[no_mangle]
pub extern "C" fn may_task_draw(x: i32, y: i32, w: i32, h: i32, dark: i32) {
    let state = unsafe { &*addr_of_mut!(STATE) };
    let canvas = Canvas {
        x,
        y,
        colors: Colors::new(dark != 0),
    };
    let c = &canvas.colors;
    canvas.rect(0, 0, w, h, c.bg);
    canvas.rect(0, 0, NAV, h, c.card);
    canvas.rect(NAV, 0, 1, h, c.border);
    canvas.text(111, 13, format_args!("Devices"), c.text);

    let process_page = {
        #[cfg(target_os = "linux")]
        {
            state.page.open
        }
        #[cfg(not(target_os = "linux"))]
        {
            false
        }
    };
    let header = (START + w) / 2 - 135;
    canvas.rounded(
        header,
        5,
        142,
        32,
        8,
        if process_page { c.bg } else { c.active },
    );
    canvas.text(header + 13, 11, format_args!("Performance"), c.text);
    #[cfg(target_os = "linux")]
    {
        if process_page {
            canvas.rounded(header + 156, 5, 100, 32, 8, c.active);
        }
        canvas.text(header + 172, 11, format_args!("Apps"), c.text);
    }
    let perf = &state.performance;
    for (index, device) in Device::LIST.into_iter().enumerate() {
        let y = DEVICE_TOP + index as i32 * DEVICE_HEIGHT;
        if index == state.device && !process_page {
            canvas.rounded(6, y, NAV - 12, 69, 8, c.active);
        }
        chart(
            &canvas,
            (14, y + 8, 80, 53),
            if matches!(device, Device::Gpu) && perf.gpu.is_none() {
                &state.frames
            } else {
                &state.trends[index]
            },
            device.color(),
            if matches!(device, Device::Gpu) && perf.gpu.is_none() {
                state.frames.limit(8)
            } else if device.percent() {
                100
            } else {
                state.trends[index].limit(1024)
            },
            true,
        );
        canvas.text(105, y + 5, format_args!("{}", device.label()), c.text);
        match device {
            Device::Cpu => {
                let processor = cpu_name();
                let title = core::str::from_utf8(&processor).unwrap_or("Processor");
                let title = title.split('\0').next().unwrap_or("");
                let title = shorten(title, 17);
                canvas.text(105, y + 24, format_args!("{title}"), c.muted);
                percent(&canvas, 105, y + 43, perf.cpu);
            }
            Device::Memory => {
                if let Some((used, total)) = perf.ram {
                    canvas.text(
                        105,
                        y + 24,
                        format_args!("{} / {} MiB", used / 1024, total / 1024),
                        c.muted,
                    );
                    percent(&canvas, 105, y + 43, usage(used, total));
                }
            }
            Device::Disk | Device::Network => {
                let (rates, labels) = if matches!(device, Device::Disk) {
                    (perf.disk, ("R", "W"))
                } else {
                    (perf.network, ("R", "S"))
                };
                if let Some((first, second)) = rates {
                    rate(&canvas, 105, y + 24, labels.0, first);
                    rate(&canvas, 105, y + 43, labels.1, second);
                } else {
                    canvas.text(105, y + 24, format_args!("Unavailable"), c.muted);
                }
            }
            Device::Gpu => {
                let mut gpu = [0u8; 64];
                unsafe { may_task_gpu_name(gpu.as_mut_ptr().cast(), gpu.len() as u32) };
                let gpu = core::str::from_utf8(&gpu).unwrap_or("Graphics");
                let gpu = shorten(gpu.split('\0').next().unwrap_or(""), 17);
                canvas.text(105, y + 24, format_args!("{gpu}"), c.muted);
                percent(&canvas, 105, y + 43, perf.gpu);
            }
        }
    }
    #[cfg(target_os = "linux")]
    if process_page {
        draw_processes(&canvas, state, w, h);
        return;
    }
    draw_performance(&canvas, state, w, h);
}

fn cpu_name() -> [u8; 64] {
    let mut name = [0u8; 64];
    unsafe { may_task_cpu_name(name.as_mut_ptr().cast(), name.len() as u32) };
    name
}

fn shorten(value: &str, max: usize) -> &str {
    let mut end = value.len().min(max);
    while !value.is_char_boundary(end) {
        end -= 1;
    }
    &value[..end]
}

fn usage(used: u64, total: u64) -> Option<u32> {
    used.saturating_mul(100)
        .checked_div(total)
        .map(|value| value.min(100) as u32)
}

fn percent(canvas: &Canvas, x: i32, y: i32, value: Option<u32>) {
    if let Some(value) = value {
        canvas.text(x, y, format_args!("{value}%"), canvas.colors.text);
    } else {
        canvas.text(x, y, format_args!("Unavailable"), canvas.colors.muted);
    }
}

fn rate(canvas: &Canvas, x: i32, y: i32, label: &str, bytes: u64) {
    let (value, unit) = if bytes >= 1024 * 1024 {
        (bytes / (1024 * 1024), "MiB/s")
    } else if bytes >= 1024 {
        (bytes / 1024, "KiB/s")
    } else {
        (bytes, "B/s")
    };
    canvas.text(
        x,
        y,
        format_args!("{label}: {value} {unit}"),
        canvas.colors.text,
    );
}

fn draw_performance(canvas: &Canvas, state: &State, w: i32, h: i32) {
    let c = &canvas.colors;
    let device = Device::LIST[state.device];
    let graph_y = 105;
    let graph_h = h - graph_y - 34;
    let detail = w - 230;
    let graph_w = detail - START - 15;
    let perf = &state.performance;
    let frame_chart = matches!(device, Device::Gpu) && perf.gpu.is_none();
    let trend = if frame_chart {
        &state.frames
    } else {
        &state.trends[state.device]
    };
    canvas.heading(START, 52, format_args!("{}", device.label()), c.text);
    if matches!(device, Device::Cpu) {
        let name = cpu_name();
        let name = core::str::from_utf8(&name).unwrap_or("Processor");
        let name = shorten(name.split('\0').next().unwrap_or(""), 43);
        canvas.text(START + 82, 56, format_args!("{name}"), c.text);
    }
    let legend = match device {
        Device::Cpu => "Utilization over 1 minute",
        Device::Memory => "Memory usage over 1 minute",
        Device::Disk => "Read and write over 1 minute",
        Device::Network => "Receive and send over 1 minute",
        Device::Gpu if frame_chart => "Desktop frame submits over 1 minute",
        Device::Gpu => "Hardware utilization over 1 minute",
    };
    canvas.text(START, 83, format_args!("{legend}"), c.muted);
    let bound = if frame_chart {
        trend.limit(8)
    } else if device.percent() {
        100
    } else {
        trend.limit(1024)
    };
    if frame_chart {
        canvas.text(detail - 130, 83, format_args!("{bound} frames/s"), c.muted);
    } else if device.percent() {
        canvas.text(detail - 58, 83, format_args!("100%"), c.muted);
    } else {
        rate(canvas, detail - 174, 83, "Max", bound);
    }
    let area = (START, graph_y, graph_w, graph_h);
    #[cfg(target_os = "linux")]
    if matches!(device, Device::Cpu) && state.linux.online_cores > 1 {
        let count = state.linux.online_cores.min(state.core_trends.len() as u32) as i32;
        let columns = count.min(4);
        let rows = (count + columns - 1) / columns;
        let width = (graph_w - (columns - 1) * 7) / columns;
        let height = (graph_h - (rows - 1) * 7) / rows;
        for index in 0..count {
            let x = START + (index % columns) * (width + 7);
            let y = graph_y + (index / columns) * (height + 7);
            chart(
                canvas,
                (x, y, width, height),
                &state.core_trends[index as usize],
                device.color(),
                100,
                false,
            );
        }
    } else {
        chart(canvas, area, trend, device.color(), bound, false);
    }
    #[cfg(target_os = "none")]
    chart(canvas, area, trend, device.color(), bound, false);
    canvas.text(START, h - 27, format_args!("60 seconds"), c.muted);
    canvas.text(detail - 42, h - 27, format_args!("0"), c.muted);

    match device {
        Device::Cpu => {
            canvas.text(detail, 106, format_args!("Utilization"), c.muted);
            percent(canvas, detail, 127, perf.cpu);
            let seconds = state.sampled.unwrap_or(0) / 100;
            canvas.text(detail, 167, format_args!("Up time"), c.muted);
            canvas.text(
                detail,
                188,
                format_args!(
                    "{:02}:{:02}:{:02}",
                    seconds / 3600,
                    seconds / 60 % 60,
                    seconds % 60
                ),
                c.text,
            );
            #[cfg(target_os = "linux")]
            canvas.text(
                detail,
                228,
                format_args!("Processes: {}", state.linux.total_processes),
                c.text,
            );
            #[cfg(target_os = "linux")]
            canvas.text(
                detail,
                256,
                format_args!("Logical CPUs: {}", state.linux.online_cores),
                c.text,
            );
            #[cfg(target_os = "none")]
            canvas.text(
                detail,
                228,
                format_args!("Open windows: {}", state.window_count),
                c.text,
            );
        }
        Device::Memory => {
            if let Some((used, total)) = perf.ram {
                canvas.text(detail, 106, format_args!("In use"), c.muted);
                canvas.text(detail, 128, format_args!("{} MiB", used / 1024), c.text);
                canvas.text(detail, 167, format_args!("Available"), c.muted);
                canvas.text(
                    detail,
                    189,
                    format_args!("{} MiB", (total - used) / 1024),
                    c.text,
                );
                canvas.text(
                    detail,
                    231,
                    format_args!("Capacity: {} MiB", total / 1024),
                    c.muted,
                );
            }
        }
        Device::Disk | Device::Network => {
            let (rates, labels) = if matches!(device, Device::Disk) {
                (perf.disk, ("Read", "Write"))
            } else {
                (perf.network, ("Receive", "Send"))
            };
            if let Some((a, b)) = rates {
                rate(canvas, detail, 108, labels.0, a);
                rate(canvas, detail, 141, labels.1, b);
            } else {
                canvas.text(detail, 108, format_args!("Unavailable"), c.muted);
            }
        }
        Device::Gpu => {
            canvas.text(detail, 106, format_args!("Hardware utilization"), c.muted);
            percent(canvas, detail, 128, perf.gpu);
            if let Some(fps) = perf.frame_rate {
                canvas.text(detail, 167, format_args!("Desktop frame submits"), c.muted);
                canvas.text(detail, 189, format_args!("{fps} frames/s"), c.text);
            }
        }
    }
}

#[cfg(target_os = "linux")]
const ROW_TOP: i32 = 115;
#[cfg(target_os = "linux")]
const ROW_HEIGHT: i32 = 27;

#[cfg(target_os = "linux")]
fn visible_rows(h: i32) -> usize {
    ((h - ROW_TOP - 58) / ROW_HEIGHT).max(1) as usize
}

#[cfg(target_os = "linux")]
fn draw_processes(canvas: &Canvas, state: &State, w: i32, h: i32) {
    let c = &canvas.colors;
    let page = &state.page;
    let query = core::str::from_utf8(page.query()).unwrap_or("");
    let filtered = state.linux.processes().iter().filter(|p| page.visible(p));
    let filtered_count = filtered.clone().count();
    canvas.text(START, 21, format_args!("Processes"), c.text);
    canvas.text(
        w - 230,
        21,
        format_args!("{filtered_count} / {} tasks", state.linux.total_processes),
        c.muted,
    );
    canvas.card(START, 51, w - START - 24, 30);
    if query.is_empty() {
        canvas.text(
            START + 13,
            56,
            format_args!("Type to filter by process name"),
            c.muted,
        );
    } else {
        canvas.text(START + 13, 56, format_args!("Filter  {query}"), c.text);
    }
    canvas.text(START + 12, 90, format_args!("PID"), c.muted);
    canvas.text(START + 104, 90, format_args!("Process"), c.muted);
    canvas.text(w - 260, 90, format_args!("State"), c.muted);
    canvas.text(w - 153, 90, format_args!("Memory (RSS)"), c.muted);
    for (index, process) in filtered.skip(page.offset).take(visible_rows(h)).enumerate() {
        let y = ROW_TOP + index as i32 * ROW_HEIGHT;
        if page.selected == Some(process.id) {
            canvas.rounded(START, y, w - START - 24, 25, 7, c.active);
        } else if index % 2 == 0 {
            canvas.rounded(START, y, w - START - 24, 25, 7, c.card);
        }
        canvas.text(
            START + 12,
            y + 3,
            format_args!("{}", process.id.pid),
            c.text,
        );
        canvas.text(
            START + 104,
            y + 3,
            format_args!("{}", process.name()),
            c.text,
        );
        canvas.text(
            w - 260,
            y + 3,
            format_args!("{}", char::from(process.state)),
            c.muted,
        );
        canvas.text(
            w - 153,
            y + 3,
            format_args!("{} KiB", process.rss_kib),
            c.text,
        );
    }
    if filtered_count == 0 {
        canvas.text(
            START + 12,
            129,
            format_args!("No matching processes"),
            c.muted,
        );
    }
    canvas.rect(START, h - 53, w - START - 24, 1, c.border);
    if let Some(id) = page.pending {
        canvas.text(
            START,
            h - 41,
            format_args!("End PID {}?   Enter confirms   Esc cancels", id.pid),
            c.text,
        );
    } else {
        let hint = match page.result {
            Some(true) => "Termination requested",
            Some(false) => "Process changed, exited, or permission was denied",
            None => "Up/Down select    Delete ends task",
        };
        canvas.text(START, h - 41, format_args!("{hint}"), c.muted);
        if page.selected.is_some() {
            canvas.rounded(w - 140, h - 46, 114, 31, 8, c.active);
            canvas.text(w - 126, h - 40, format_args!("End task"), c.text);
        }
    }
}

#[cfg(target_os = "linux")]
fn switch_page(state: &mut State, processes: bool) {
    state.page.open = processes;
    state.page.pending = None;
    if processes {
        state.linux.sample_process_list();
    }
}

#[no_mangle]
pub extern "C" fn may_task_click(x: i32, y: i32, w: i32, h: i32) {
    let state = unsafe { &mut *addr_of_mut!(STATE) };
    if x < NAV && y >= DEVICE_TOP {
        let index = ((y - DEVICE_TOP) / DEVICE_HEIGHT) as usize;
        if index < Device::LIST.len() {
            state.device = index;
            #[cfg(target_os = "linux")]
            switch_page(state, false);
            unsafe {
                may_task_selected_device(index as u32);
            }
        }
        return;
    }
    #[cfg(target_os = "none")]
    let _ = (w, h);
    #[cfg(target_os = "linux")]
    let header = (START + w) / 2 - 135;
    #[cfg(target_os = "linux")]
    if (5..37).contains(&y) && (header..header + 256).contains(&x) {
        switch_page(state, x >= header + 156);
    } else if state.page.open && state.page.pending.is_none() {
        if x >= w - 140 && (h - 46..h - 15).contains(&y) {
            state.page.pending = state.page.selected;
        } else if x >= START && (ROW_TOP..h - 57).contains(&y) {
            let row = ((y - ROW_TOP) / ROW_HEIGHT) as usize;
            if row >= visible_rows(h) {
                return;
            }
            state.page.selected = state
                .linux
                .processes()
                .iter()
                .filter(|p| state.page.visible(p))
                .nth(state.page.offset + row)
                .map(|p| p.id);
            state.page.result = None;
        }
    }
}

#[cfg(target_os = "linux")]
#[no_mangle]
pub extern "C" fn may_task_cancel() {
    let state = unsafe { &mut *addr_of_mut!(STATE) };
    state.page.pending = None;
    state.page.result = None;
}

#[cfg(target_os = "linux")]
#[no_mangle]
pub extern "C" fn may_task_key(key: i32, h: i32) {
    let state = unsafe { &mut *addr_of_mut!(STATE) };
    if let Some(id) = state.page.pending {
        if key == 27 {
            state.page.pending = None;
        }
        if key == 10 {
            state.page.result = Some(linux::terminate(id));
            state.page.pending = None;
            state.page.selected = None;
            state.linux.sample_process_list();
        }
        return;
    }
    if key == 9 {
        switch_page(state, !state.page.open);
        return;
    }
    let page = &mut state.page;
    if !page.open {
        change_device(state, key);
        return;
    }
    match key {
        0x101 | 0x102 => {
            let filtered = state.linux.processes().iter().filter(|p| page.visible(p));
            let selected = filtered.clone().position(|p| Some(p.id) == page.selected);
            let next = selected.map_or(0, |index| {
                if key == 0x101 {
                    index.saturating_sub(1)
                } else {
                    (index + 1).min(filtered.clone().count().saturating_sub(1))
                }
            });
            page.selected = filtered.clone().nth(next).map(|p| p.id);
            if next < page.offset {
                page.offset = next;
            }
            let rows = visible_rows(h);
            if next >= page.offset + rows {
                page.offset = next + 1 - rows;
            }
            page.result = None;
        }
        0x103 => page.pending = page.selected,
        8 if page.length > 0 => {
            page.length -= 1;
            page.reset_selection();
        }
        32..=126 if page.length < page.query.len() => {
            page.query[page.length] = key as u8;
            page.length += 1;
            page.reset_selection();
        }
        _ => {}
    }
}

fn change_device(state: &mut State, key: i32) {
    if matches!(key, 0x101 | 0x102) {
        let shift = if key == 0x101 {
            Device::LIST.len() - 1
        } else {
            1
        };
        state.device = (state.device + shift) % Device::LIST.len();
        unsafe {
            may_task_selected_device(state.device as u32);
        }
    }
}

#[cfg(target_os = "none")]
#[no_mangle]
pub extern "C" fn may_task_key(key: i32, _: i32) {
    change_device(unsafe { &mut *addr_of_mut!(STATE) }, key);
}
