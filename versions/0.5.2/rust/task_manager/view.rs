use core::ffi::c_char;
use core::fmt::{self, Write};
use core::ptr::addr_of_mut;

#[cfg(target_os = "linux")]
#[path = "linux.rs"]
mod linux;

extern "C" {
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
    fill: u32,
}

impl Colors {
    fn new(dark: bool) -> Self {
        if dark {
            Self {
                bg: 0x101013,
                card: 0x19191d,
                border: 0x34343c,
                text: 0xf4f4f6,
                muted: 0xa6a6b0,
                active: 0x303038,
                fill: 0xa4a4b2,
            }
        } else {
            Self {
                bg: 0xf0f0f3,
                card: 0xfafafb,
                border: 0xd9d9df,
                text: 0x1c1c22,
                muted: 0x555562,
                active: 0xe0e0e8,
                fill: 0x515163,
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

    fn rect(&self, x: i32, y: i32, w: i32, h: i32, color: u32) {
        unsafe { may_task_rect(self.x + x, self.y + y, w, h, color) }
    }

    fn rounded(&self, x: i32, y: i32, w: i32, h: i32, radius: i32, color: u32) {
        unsafe { may_task_card(self.x + x, self.y + y, w, h, radius, color) }
    }

    fn card(&self, x: i32, y: i32, w: i32, h: i32) {
        self.rounded(x, y, w, h, 13, self.colors.border);
        self.rounded(x + 1, y + 1, w - 2, h - 2, 12, self.colors.card);
    }
}

const NAV: i32 = 160;
const START: i32 = NAV + 24;

#[derive(Default)]
struct Performance {
    cpu: Option<u32>,
    ram: Option<(u64, u64)>,
    network: Option<(u64, u64)>,
    disk: Option<(u64, u64)>,
    disk_present: bool,
    gpu: Option<u32>,
    frame_rate: Option<u64>,
}

struct State {
    sampled: Option<u32>,
    window_count: u32,
    performance: Performance,
    cpu_graph: [Option<u32>; 48],
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
        disk_present: data.disk_present != 0,
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
            performance.disk = rate(data.disk_reads, old.disk_reads, 51200).zip(rate(
                data.disk_writes,
                old.disk_writes,
                51200,
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
        disk_present: false,
        gpu: None,
        frame_rate: None,
    },
    cpu_graph: [None; 48],
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
        state.performance = Performance {
            cpu: state.linux.cpu_percent,
            ram: state.linux.ram.map(|ram| (ram.used_kib, ram.total_kib)),
            network: state.linux.rates,
            disk: state.linux.disk_rates,
            disk_present: state.linux.disk_present,
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
    state.cpu_graph.rotate_left(1);
    state.cpu_graph[47] = state.performance.cpu;
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
    canvas.rect(NAV, 0, 1, h, c.border);
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
    if !process_page {
        canvas.rounded(12, 20, 136, 32, 8, c.active);
    }
    canvas.text(
        25,
        26,
        format_args!("Performance"),
        if process_page { c.muted } else { c.text },
    );

    #[cfg(target_os = "linux")]
    {
        if process_page {
            canvas.rounded(12, 62, 136, 32, 8, c.active);
        }
        canvas.text(
            25,
            68,
            format_args!("Processes"),
            if process_page { c.text } else { c.muted },
        );
        if process_page {
            draw_processes(&canvas, state, w, h);
            return;
        }
    }
    draw_performance(&canvas, state, w);
}

fn draw_performance(canvas: &Canvas, state: &State, w: i32) {
    let c = &canvas.colors;
    let perf = &state.performance;
    let content = w - START - 24;
    let seconds = state.sampled.unwrap_or(0) / 100;
    canvas.text(START, 21, format_args!("Performance"), c.text);
    canvas.text(
        w - 200,
        21,
        format_args!(
            "Uptime {}:{:02}:{:02}",
            seconds / 3600,
            seconds / 60 % 60,
            seconds % 60
        ),
        c.muted,
    );
    if cfg!(target_os = "none") {
        canvas.text(25, 74, format_args!("MayKernel64"), c.muted);
        canvas.text(
            25,
            102,
            format_args!("Windows {}", state.window_count),
            c.muted,
        );
    }

    canvas.card(START, 62, content, 153);
    let title = if cfg!(target_os = "none") {
        "CPU busy (PIT sample)"
    } else {
        "CPU utilization"
    };
    canvas.text(START + 18, 77, format_args!("{title}"), c.text);
    if let Some(percent) = perf.cpu {
        canvas.text(
            START + content - 62,
            77,
            format_args!("{percent:>3}%"),
            c.text,
        );
    } else {
        canvas.text(START + content - 90, 77, format_args!("Sampling"), c.muted);
    }
    let chart_w = content - 36;
    for y in [112, 140, 168, 196] {
        canvas.rect(START + 18, y, chart_w, 1, c.border);
    }
    let column = chart_w / 48;
    for (index, point) in state.cpu_graph.iter().enumerate() {
        if let Some(point) = point {
            let height = ((*point).min(100) as i32 * 84 / 100).max(2);
            canvas.rect(
                START + 18 + index as i32 * column,
                196 - height,
                column - 2,
                height,
                c.fill,
            );
        }
    }

    let tile = (content - 15) / 2;
    let right = START + tile + 15;
    let label = if cfg!(target_os = "none") {
        "RAM / allocator"
    } else {
        "RAM"
    };
    canvas.card(START, 229, tile, 100);
    canvas.text(START + 18, 242, format_args!("{label}"), c.text);
    if let Some((used, total)) = perf.ram {
        canvas.text(
            START + 18,
            268,
            format_args!("{}/{} MiB used", used / 1024, total / 1024),
            c.text,
        );
        let width = tile - 36;
        if let Some(pixels) = used.saturating_mul(width as u64).checked_div(total) {
            canvas.rounded(START + 18, 303, width, 6, 3, c.border);
            canvas.rect(START + 18, 303, pixels as i32, 6, c.fill);
        }
    } else {
        canvas.text(START + 18, 268, format_args!("Unavailable"), c.muted);
    }

    canvas.card(right, 229, tile, 100);
    canvas.text(right + 18, 242, format_args!("Network"), c.text);
    if let Some((rx, tx)) = perf.network {
        show_rate(canvas, right + 18, 268, "Receive", rx);
        show_rate(canvas, right + 18, 294, "Send", tx);
    } else {
        canvas.text(
            right + 18,
            268,
            format_args!("Waiting for interface data"),
            c.muted,
        );
    }

    canvas.card(START, 343, tile, 100);
    let label = if cfg!(target_os = "none") {
        "Disk I/O / MayFS"
    } else {
        "Disk I/O"
    };
    canvas.text(START + 18, 356, format_args!("{label}"), c.text);
    if let Some((read, written)) = perf.disk {
        show_rate(canvas, START + 18, 382, "Read", read);
        show_rate(canvas, START + 18, 408, "Write", written);
    } else {
        let message = if perf.disk_present {
            "Waiting for data"
        } else {
            "No visible disks"
        };
        canvas.text(START + 18, 382, format_args!("{message}"), c.muted);
    }

    canvas.card(right, 343, tile, 100);
    canvas.text(right + 18, 356, format_args!("GPU"), c.text);
    if let Some(busy) = perf.gpu {
        canvas.text(
            right + 18,
            382,
            format_args!("Hardware utilization   {busy}%"),
            c.text,
        );
    } else {
        canvas.text(
            right + 18,
            382,
            format_args!("Hardware utilization unavailable"),
            c.muted,
        );
    }
    if let Some(fps) = perf.frame_rate {
        canvas.text(
            right + 18,
            408,
            format_args!("Framebuffer submits   {fps}/s"),
            c.text,
        );
    } else {
        canvas.text(
            right + 18,
            408,
            format_args!("Sampling framebuffer"),
            c.muted,
        );
    }
}

fn show_rate(canvas: &Canvas, x: i32, y: i32, name: &str, bytes: u64) {
    let (value, unit) = if bytes >= 1024 {
        (bytes / 1024, "KiB/s")
    } else {
        (bytes, "B/s")
    };
    canvas.text(
        x,
        y,
        format_args!("{name:<8} {value} {unit}"),
        canvas.colors.text,
    );
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

#[cfg(target_os = "linux")]
#[no_mangle]
pub extern "C" fn may_task_click(x: i32, y: i32, w: i32, h: i32) {
    let state = unsafe { &mut *addr_of_mut!(STATE) };
    if x < NAV {
        if (20..52).contains(&y) {
            switch_page(state, false);
        }
        if (62..94).contains(&y) {
            switch_page(state, true);
        }
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
