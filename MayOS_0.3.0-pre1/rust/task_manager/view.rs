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

struct State {
    sampled: Option<u32>,
    ram_mb: u32,
    free_pages: u32,
    window_count: u32,
    #[cfg(target_os = "linux")]
    linux: linux::Linux,
    #[cfg(target_os = "linux")]
    cpu_graph: [Option<u32>; 48],
    #[cfg(target_os = "linux")]
    page: ProcessesPage,
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
    ram_mb: 0,
    free_pages: 0,
    window_count: 0,
    #[cfg(target_os = "linux")]
    linux: linux::Linux::new(),
    #[cfg(target_os = "linux")]
    cpu_graph: [None; 48],
    #[cfg(target_os = "linux")]
    page: ProcessesPage::new(),
};

// The desktop invokes these entry points on its single UI thread.
#[no_mangle]
pub extern "C" fn may_task_poll(ticks: u32, ram_mb: u32, free_pages: u32, windows: u32) -> i32 {
    let state = unsafe { &mut *addr_of_mut!(STATE) };
    let elapsed = state.sampled.map_or(100, |last| ticks.wrapping_sub(last));
    if elapsed < 100 {
        return 0;
    }
    state.sampled = Some(ticks);
    state.ram_mb = ram_mb;
    state.free_pages = free_pages;
    state.window_count = windows;
    #[cfg(target_os = "linux")]
    {
        state.linux.sample(elapsed, state.page.open);
        state.cpu_graph.rotate_left(1);
        state.cpu_graph[47] = state.linux.cpu_percent;
        let count = state
            .linux
            .processes()
            .iter()
            .filter(|p| state.page.visible(p))
            .count();
        state.page.offset = state.page.offset.min(count.saturating_sub(1));
    }
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
    #[cfg(not(target_os = "linux"))]
    {
        canvas.card(START, 65, content, 128);
        canvas.text(START + 18, 82, format_args!("Physical memory"), c.text);
        canvas.text(
            START + 18,
            114,
            format_args!("Firmware usable RAM      {} MiB", state.ram_mb),
            c.text,
        );
        canvas.text(
            START + 18,
            147,
            format_args!(
                "Free allocator frames    {} ({} MiB)",
                state.free_pages,
                state.free_pages / 256
            ),
            c.muted,
        );
        canvas.card(START, 211, content, 114);
        canvas.text(
            START + 18,
            230,
            format_args!("MayKernel desktop session"),
            c.text,
        );
        canvas.text(
            START + 18,
            261,
            format_args!("Visible windows   {}", state.window_count),
            c.text,
        );
        canvas.text(
            START + 18,
            290,
            format_args!("CPU load and process accounting are unavailable."),
            c.muted,
        );
    }
    #[cfg(target_os = "linux")]
    {
        canvas.card(START, 62, content, 196);
        canvas.text(START + 18, 78, format_args!("CPU utilization"), c.text);
        if let Some(percent) = state.linux.cpu_percent {
            canvas.text(
                START + content - 62,
                78,
                format_args!("{percent:>3}%"),
                c.text,
            );
        } else {
            canvas.text(START + content - 90, 78, format_args!("Sampling"), c.muted);
        }
        let chart_w = content - 36;
        for y in [121, 159, 197, 235] {
            canvas.rect(START + 18, y, chart_w, 1, c.border);
        }
        let column = chart_w / 48;
        for (index, point) in state.cpu_graph.iter().enumerate() {
            if let Some(point) = point {
                let height = ((*point).min(100) as i32 * 114 / 100).max(2);
                canvas.rect(
                    START + 18 + index as i32 * column,
                    235 - height,
                    column - 2,
                    height,
                    c.fill,
                );
            }
        }
        let tile = (content - 28) / 3;
        let network = START + tile + 14;
        let disk = network + tile + 14;
        canvas.card(START, 274, tile, 128);
        canvas.text(START + 18, 291, format_args!("Memory"), c.text);
        if let Some(ram) = state.linux.ram {
            canvas.text(
                START + 18,
                321,
                format_args!(
                    "{} / {} MiB used",
                    ram.used_kib / 1024,
                    ram.total_kib / 1024
                ),
                c.text,
            );
            if ram.total_kib != 0 {
                let width = tile - 36;
                let used = ram.used_kib.saturating_mul(width as u64) / ram.total_kib;
                canvas.rounded(START + 18, 358, width, 8, 4, c.border);
                canvas.rect(START + 18, 358, used as i32, 8, c.fill);
            }
        } else {
            canvas.text(START + 18, 321, format_args!("Unavailable"), c.muted);
        }
        canvas.card(network, 274, tile, 128);
        canvas.text(network + 18, 291, format_args!("Network"), c.text);
        if let Some((rx, tx)) = state.linux.rates {
            canvas.text(
                network + 18,
                321,
                format_args!("Receive {} KiB/s", rx / 1024),
                c.text,
            );
            canvas.text(
                network + 18,
                351,
                format_args!("Send    {} KiB/s", tx / 1024),
                c.text,
            );
        } else {
            canvas.text(network + 18, 321, format_args!("Waiting for data"), c.muted);
        }
        canvas.card(disk, 274, tile, 128);
        canvas.text(disk + 18, 291, format_args!("Disk I/O"), c.text);
        if let Some((read, written)) = state.linux.disk_rates {
            canvas.text(
                disk + 18,
                321,
                format_args!("Read  {} KiB/s", read / 1024),
                c.text,
            );
            canvas.text(
                disk + 18,
                351,
                format_args!("Write {} KiB/s", written / 1024),
                c.text,
            );
        } else {
            let message = if state.linux.disk_present {
                "Waiting for data"
            } else {
                "No visible disks"
            };
            canvas.text(disk + 18, 321, format_args!("{message}"), c.muted);
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
