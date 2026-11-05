void Window::close_transients() {
    Popover::dismiss();
}

int Window::corner(int px, int py) const {
    // Area interna de 24 x 24 px; a decoracao externa pertence ao compositor.
    int h = px < 24 ? 1 : px >= w() - 24 ? 2 : 0;
    int v = py < 24 ? 4 : py >= this->h() - 24 ? 8 : 0;
    return h && v ? h | v : 0;
}

void Window::tile(int side) {
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh, x() + w() / 2, y() + h() / 2);
    if (side != 3 && sw / 2 < minimum_width) {
        if (app)
            app->set_status("A tela precisa comportar duas janelas de pelo menos 640 px.");
        return;
    }
    placing = true;
    close_transients();
    resize(side == 2 ? sx + sw / 2 : sx, sy, side == 3 ? sw : sw / 2, std::max(sh, minimum_height));
    placing = false;
}

void Window::finish_move(void *data) {
    auto *win = static_cast<Window *>(data);
    win->snap_pending = false;
    if (!win->shown())
        return;
    int mx, my;
    Fl::get_mouse(mx, my);
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh, mx, my);
    win->snap_side = mx <= sx + 14 ? 1 : mx >= sx + sw - 15 ? 2 : my <= sy + 10 ? 3 : 0;
#ifdef _WIN32
    unsigned mask = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;
    const unsigned Button1Mask = 1;
#else
    ::Window rr, cr;
    int rx, ry, wx, wy;
    unsigned mask = 0;
    if (fl_display)
        XQueryPointer(fl_display, fl_xid(win), &rr, &cr, &rx, &ry, &wx, &wy, &mask);
#endif
    // Uma verificacao apos o ultimo movimento; nao consultar em loop enquanto arrasta.
    if (!(mask & Button1Mask) && win->snap_side) {
        int side = win->snap_side;
        win->snap_side = 0;
        win->tile(side);
    }
}

int Window::handle(int event) {
    int px = Fl::event_x(), py = Fl::event_y();
    if (event == FL_PUSH || event == FL_HIDE)
        close_transients();
    if (event == FL_KEYDOWN && (Fl::event_state() & (FL_CTRL | FL_ALT)) == (FL_CTRL | FL_ALT)) {
        int key = Fl::event_key();
        if (key == FL_Left || key == FL_Right || key == FL_Up) {
            tile(key == FL_Left ? 1 : key == FL_Right ? 2 : 3);
            return 1;
        }
    }
    if (event == FL_MOVE || event == FL_ENTER || event == FL_LEAVE) {
        int hit = event == FL_LEAVE ? 0 : corner(px, py);
        if (hit != cursor_corner) {
            if (hit)
                Fl::belowmouse(this);
            cursor_corner = hit;
            ++corner_cursor_changes;
            cursor(hit == 5 || hit == 10 ? FL_CURSOR_NWSE
                   : hit                 ? FL_CURSOR_NESW
                                         : FL_CURSOR_DEFAULT);
        }
        if (hit)
            return 1;
    }
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
        edge_drag = corner(px, py);
        // Arraste tambem pelo nome BPCB, alem da barra de titulo do sistema.
        if (!edge_drag && px >= 74 && px <= 210 && py >= 8 && py <= 62)
            edge_drag = 16;
        if (edge_drag) {
            drag_x = Fl::event_x_root();
            drag_y = Fl::event_y_root();
            initial_x = x();
            initial_y = y();
            initial_w = w();
            initial_h = h();
            return 1;
        }
    }
    if (event == FL_DRAG && edge_drag) {
        int dx = Fl::event_x_root() - drag_x, dy = Fl::event_y_root() - drag_y;
        if (edge_drag == 16)
            position(initial_x + dx, initial_y + dy);
        else {
            int nw = std::max(minimum_width, initial_w + (edge_drag & 1 ? -dx : dx));
            int nh = std::max(minimum_height, initial_h + (edge_drag & 4 ? -dy : dy));
            resize(initial_x + (edge_drag & 1 ? initial_w - nw : 0),
                   initial_y + (edge_drag & 4 ? initial_h - nh : 0), nw, nh);
        }
        return 1;
    }
    if (event == FL_RELEASE && edge_drag) {
        bool moving = edge_drag == 16;
        edge_drag = 0;
        cursor_corner = 0;
        cursor(FL_CURSOR_DEFAULT);
        Fl::remove_timeout(finish_move, this);
        snap_pending = false;
        if (moving)
            finish_move(this);
        return 1;
    }
    return Fl_Double_Window::handle(event);
}

void Window::resize(int x, int y, int w, int h) {
    bool undersized = w < minimum_width || h < minimum_height;
    w = std::max(w, minimum_width);
    h = std::max(h, minimum_height);
    ++resize_count;
    bool changed = w != this->w() || h != this->h(), moved = x != this->x() || y != this->y();
    if (changed || moved)
        close_transients();
    Fl_Double_Window::resize(x, y, w, h);
    if (undersized)
        enforce_native_minimum(this, minimum_width, minimum_height);
    if (app && changed) {
        app->layout();
        app->reveal_selected();
    }
    if (moved && !changed && !placing && shown() && edge_drag == 0) {
        Fl::remove_timeout(finish_move, this);
        snap_pending = true;
        Fl::add_timeout(0.12, finish_move, this);
    }
}
