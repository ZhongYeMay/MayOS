/* Native MayDOS v1.0.0 app surfaces shared by MayKernel and Linux. */
static char md_expression[48] = "", md_result[64] = "0";
static MayDosMines md_game;
static int md_game_mode = 1, md_selected_extension = -1;

static void md_calc_key(char k) {
    int n = (int)strlen(md_expression);
    if (k == 'C') { md_expression[0] = 0; copy(md_result, "0", sizeof(md_result)); }
    else if (k == 'B') { if (n) md_expression[n - 1] = 0; }
    else if (k == '=' || k == '\n') {
        if (!md_expression[0]) copy(md_result, "0", sizeof(md_result));
        else md_calc_eval(md_expression, md_result, sizeof(md_result));
    } else if (n < 36 && ((k >= '0' && k <= '9') ||
               k == '+' || k == '-' || k == '*' || k == '/' ||
               k == '.' || k == '(' || k == ')')) {
        md_expression[n] = k; md_expression[n + 1] = 0;
    }
}
static void md_calc_draw(Window *w) {
    int x = w->x, y = w->y;
    ui_card(x + 22, y + 60, w->w - 44, 104, 17, surface_high);
    text(x + 41, y + 77, md_expression[0] ? md_expression : "0", muted);
    heading_text(x + 41, y + 116, md_result, ink);
    const char *keys[] = { "7", "8", "9", "/", "(",
                           "4", "5", "6", "*", ")",
                           "1", "2", "3", "-", "C",
                           "0", ".", "+", "=", "Del" };
    for (int i = 0; i < 20; ++i) {
        int px = x + 22 + i % 5 * 81, py = y + 184 + i / 5 * 66;
        button(px, py, 72, keys[i], i == 18);
    }
    text(x + 24, y + w->h - 43, "MayOS Calculator  ·  +  -  *  /  ( )", muted);
}
static void md_calc_click(int rx, int ry) {
    if (ry < 184) return;
    int col = (rx - 22) / 81, row = (ry - 184) / 66;
    if (rx < 22 || col < 0 || col >= 5 || row < 0 || row >= 4 ||
        (rx - 22) % 81 >= 72 || (ry - 184) % 66 >= 30) return;
    const char key[] = "789/(456*)123-C0.+=B";
    md_calc_key(key[row * 5 + col]);
}
static void md_mines_start(int mode, int rows, int cols, int mines) {
    if (md_mines_new(&md_game, mode, rows, cols, mines, ticks + 0x4387u))
        md_game_mode = mode;
}
static void md_mines_geometry(Window *w, int *gx, int *gy, int *size) {
    int available_w = w->w - 64, available_h = w->h - 200;
    int cell = available_w / md_game.cols;
    if (cell > available_h / md_game.rows) cell = available_h / md_game.rows;
    if (cell > 36) cell = 36;
    if (cell < 16) cell = 16;
    *size = cell;
    *gx = w->x + (w->w - md_game.cols * cell) / 2;
    *gy = w->y + 144 + (available_h - md_game.rows * cell) / 2;
}
static void md_mines_draw(Window *w) {
    int x = w->x, y = w->y;
    button(x + 25, y + 53, 116, "初级  9×9", md_game_mode == 1);
    button(x + 151, y + 53, 128, "中级 16×16", md_game_mode == 2);
    button(x + 289, y + 53, 127, "高级 16×30", md_game_mode == 3);
    button(x + w->w - 124, y + 53, 100, "重新开始", 0);
    text(x + 27, y + 102, "MINESWEEPER", ink);
    numtext(x + 194, y + 102, (unsigned)md_game.flags, accent);
    text(x + 222, y + 102, "flags", muted);
    numtext(x + 309, y + 102, (unsigned)md_game.mines, ink);
    text(x + 340, y + 102, "mines", muted);
    if (md_game.finished) text(x + 457, y + 102, md_game.won ? "已完成" : "游戏结束", ink);
    else text(x + 457, y + 102, "左键翻开 / 右键标记", muted);
    int gx, gy, cell;
    md_mines_geometry(w, &gx, &gy, &cell);
    for (int r = 0; r < md_game.rows; ++r)
        for (int c = 0; c < md_game.cols; ++c) {
            int i = r * md_game.cols + c;
            int px = gx + c * cell, py = gy + r * cell;
            int visible = md_game.states[i] == 1 || (md_game.finished && md_game.values[i] == 9);
            u32 fill = visible ? panel : surface_high;
            rect(px, py, cell - 2, cell - 2, fill);
            if (md_game.states[i] == 2) text(px + (cell - 8) / 2, py - 1, "F", accent);
            else if (visible) {
                if (md_game.values[i] == 9) text(px + (cell - 8) / 2, py - 1, "*", ink);
                else if (md_game.values[i]) {
                    char digit[2] = { (char)('0' + md_game.values[i]), 0 };
                    text(px + (cell - 8) / 2, py - 1, digit, ink);
                }
            }
        }
    text(x + 26, y + w->h - 31, "MayOS Minesweeper  /  first click safe", muted);
}
static void md_mines_click(Window *w, int mouse_x, int mouse_y, int right) {
    int rx = mouse_x - w->x, ry = mouse_y - w->y;
    if (!right && ry >= 53 && ry < 85) {
        if (rx >= 25 && rx < 141) md_mines_start(1, 0, 0, 0);
        if (rx >= 151 && rx < 279) md_mines_start(2, 0, 0, 0);
        if (rx >= 289 && rx < 416) md_mines_start(3, 0, 0, 0);
        if (rx >= w->w - 124 && rx < w->w - 24)
            md_mines_start(md_game_mode, md_game.rows, md_game.cols, md_game.mines);
        return;
    }
    int gx, gy, cell;
    md_mines_geometry(w, &gx, &gy, &cell);
    int ox = mouse_x - gx, oy = mouse_y - gy;
    if (ox < 0 || oy < 0 || ox >= md_game.cols * cell || oy >= md_game.rows * cell) return;
    int c = ox / cell, r = oy / cell;
    if (ox % cell >= cell - 2 || oy % cell >= cell - 2) return;
    if (right) md_mines_flag(&md_game, r, c);
    else md_mines_reveal(&md_game, r, c, ticks);
}
/* The Windows AddFiles directory is represented by a MayFS staging folder.
   Imported data are never executed as arbitrary .exe, .lnk, or .py files. */
static int md_directory(const char *name, int create) {
    int i = fs_find(1, name);
    if (i >= 0) return files[i].dir ? i : -1;
    if (!create) return -1;
    return fs_create(1, name, 1);
}
static int md_import_one(const char *name) {
    int src = md_directory("AddFiles", 0), dst = md_directory("ThirdPartyFile", 1);
    if (src < 0 || dst < 0 || !name) return 0;
    int i = fs_find(src, name);
    if (i < 0 || files[i].dir || fs_find(dst, name) >= 0) return 0;
    int j = fs_create(dst, name, 0);
    if (j < 0) return 0;
    files[j].size = files[i].size;
    memcpy(files[j].data, files[i].data, FILE_CAP);
    sync_notice();
    return 1;
}
static int md_import_all(void) {
    int src = md_directory("AddFiles", 1), added = 0;
    if (src < 0) return 0;
    for (int i = 4; i < FS_MAX; ++i)
        if (files[i].used && !files[i].dir && files[i].parent == (u32)src)
            added += md_import_one(files[i].name);
    return added;
}
static void md_library_draw(Window *w) {
    int x = w->x, y = w->y;
    text(x + 28, y + 58, "Extensions", ink);
    text(x + 28, y + 89, "数据文件导入 · 不执行 Windows / Python 程序", muted);
    button(x + 28, y + 130, 140, "打开导入目录", 0);
    button(x + 182, y + 130, 116, "导入全部", 1);
    button(x + 312, y + 130, 124, "移到回收站", 0);
    rect(x + 28, y + 178, w->w - 56, 1, line);
    int dst = md_directory("ThirdPartyFile", 0), row = 0;
    if (dst >= 0)
        for (int i = 4; i < FS_MAX && row < 8; ++i) {
            if (!files[i].used || files[i].parent != (u32)dst || files[i].dir) continue;
            int yy = y + 194 + row * 32;
            if (md_selected_extension == i) rect(x + 28, yy, w->w - 56, 30, selected_fill);
            text(x + 42, yy + 3, files[i].name, ink);
            ++row;
        }
    if (!row) text(x + 42, y + 205, "尚未导入文件。先复制到 Documents/AddFiles。", muted);
    text(x + 30, y + w->h - 36, "单个文件上限 4095 bytes · MayFS / RAM", muted);
}
static void md_library_click(Window *w, int rx, int ry) {
    (void)w;
    if (ry >= 130 && ry < 160) {
        if (rx >= 28 && rx < 168) {
            int src = md_directory("AddFiles", 1);
            if (src >= 0) { folder = src; selected = -1; file_scroll = 0; raise_window(0); }
            else notify("无法创建 AddFiles 目录");
        } else if (rx >= 182 && rx < 298) {
            notify(md_import_all() ? "已导入文件到 ThirdPartyFile" : "没有可导入的新文件");
        } else if (rx >= 312 && rx < 436) {
            int dst = md_directory("ThirdPartyFile", 0);
            if (dst >= 0 && md_selected_extension >= 4 &&
                files[md_selected_extension].used && files[md_selected_extension].parent == (u32)dst &&
                fs_move(md_selected_extension, 3)) {
                md_selected_extension = -1;
                sync_notice();
                notify("已移入回收站");
            } else notify("请选择一个扩展文件");
        }
        return;
    }
    if (ry < 194 || ry >= 450) return;
    int dst = md_directory("ThirdPartyFile", 0), row = 0;
    if (dst < 0) return;
    for (int i = 4; i < FS_MAX && row < 8; ++i) {
        if (!files[i].used || files[i].parent != (u32)dst || files[i].dir) continue;
        if (ry >= 194 + row * 32 && ry < 226 + row * 32) { md_selected_extension = i; return; }
        ++row;
    }
}
