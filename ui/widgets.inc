// Widgets de apresentacao. Sem consultas periodicas em repouso.
class Popover : public Fl_Double_Window {
public:
    inline static Popover *active = nullptr;
    unsigned long draw_count = 0;
    bool hint = false;

    static int dispatch(int event, Fl_Window *window) {
        if (event == FL_PUSH) {
            // Confirmar antes de despachar o clique evita iniciar com um valor antigo.
            auto *focus = Fl::focus();
            if (auto *input = dynamic_cast<Committable *>(focus))
                if (window != focus->top_window() || !Fl::event_inside(focus))
                    input->commit();
        }
        if (active &&
            ((event == FL_PUSH && window != active) ||
             (event == FL_UNFOCUS && window == active && !active->hint) || event == FL_HIDE))
            dismiss();
        return Fl::handle_(event, window);
    }

    Popover(int w, int h) : Fl_Double_Window(w, h) {
        border(0);
        set_non_modal();
    }

    ~Popover() {
        if (active == this)
            active = nullptr;
    }

    static void dismiss() {
        if (active) {
            auto *p = active;
            active = nullptr;
            p->hide();
        }
    }

    void open_at(int x, int y) {
        if (active == this && shown())
            return;
        dismiss();
        position(x, y);
        active = this;
        show();
    }

    void draw() override {
        ++draw_count;
        Fl_Double_Window::draw();
        fl_color(p.line);
        fl_rect(0, 0, w(), h());
    }

    int handle(int e) override {
        if (e == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
            dismiss();
            return 1;
        }
        return Fl_Double_Window::handle(e);
    }
};

class HelpButton : public Fl_Button {
    std::string title, text;
    Fl_Group *container = Fl_Group::current();
    Popover tip{360, 190};
    Fl_Box *heading, *detail;

    static void reveal(void *data) {
        auto *self = static_cast<HelpButton *>(data);
        Fl::remove_timeout(reveal, self);
        if (!self->visible_r() || self->tip.shown())
            return;
        auto *top = self->top_window();
        int mx, my;
        Fl::get_mouse(mx, my);
        if (mx < top->x() + self->x() || mx >= top->x() + self->x() + self->w() ||
            my < top->y() + self->y() || my >= top->y() + self->y() + self->h())
            return;
        self->heading->copy_label(self->title.c_str());
        self->detail->copy_label(self->text.c_str());
        skin(&self->tip);
        int sx, sy, sw, sh;
        Fl::screen_work_area(sx, sy, sw, sh, top->x() + self->x(), top->y() + self->y());
        self->tip.open_at(std::clamp(top->x() + self->x() - 320, sx, sx + sw - 360),
                          std::clamp(top->y() + self->y() + 28, sy, sy + sh - 190));
    }

public:
    HelpButton(const std::string &name, const std::string &info)
        : Fl_Button(0, 0, 24, 24), title(name), text(info) {
        if (text.empty())
            text = "Parâmetro usado na configuração e na previsão deste teste. Consulte as "
                   "orientações do pacote.";
        auto *parent = container;
        tip.hint = true;
        tip.set_tooltip_window();
        tip.set_override();
        tip.begin();
        heading = ::label(title.c_str(), 15, true);
        heading->resize(18, 12, 324, 42);
        detail = ::label(text.c_str(), 14);
        detail->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        detail->resize(18, 60, 324, 114);
        tip.end();
        Fl_Group::current(parent);
        callback([](Fl_Widget *w, void *) {
            reveal(w);
        });
    }

    ~HelpButton() {
        Fl::remove_timeout(reveal, this);
    }

    void draw() override {
        fl_color(p.muted);
        fl_circle(x() + w() / 2, y() + h() / 2, 9);
        fl_font(FL_HELVETICA_BOLD, 13);
        fl_draw("?", x(), y(), w(), h(), FL_ALIGN_CENTER);
    }

    int handle(int e) override {
        if (e == FL_ENTER) {
            if (!tip.shown() && !Fl::has_timeout(reveal, this))
                Fl::add_timeout(0.35, reveal, this);
            return 1;
        }
        if (e == FL_LEAVE) {
            auto *top = top_window();
            int mx, my;
            Fl::get_mouse(mx, my);
            if (mx >= top->x() + x() && mx < top->x() + x() + w() && my >= top->y() + y() &&
                my < top->y() + y() + h())
                return 1;
            Fl::remove_timeout(reveal, this);
            if (Popover::active == &tip)
                Popover::dismiss();
            return 1;
        }
        return Fl_Button::handle(e);
    }
};

class LocationMenu : public Button {
    Fl_Group *container = Fl_Group::current();
    Popover popup{460, 320};
    Fl_Hold_Browser *list;
    Fl_Box *title;
    std::vector<std::string> names;
    int chosen = -1;

public:
    LocationMenu() : Button("") {
        auto *parent = container;
        popup.set_menu_window();
        popup.set_override();
        popup.set_modal();
        popup.begin();
        title = ::label("Escolher uma pasta", 14, true);
        title->resize(16, 10, 428, 28);
        list = new Fl_Hold_Browser(10, 46, 440, 264);
        list->box(FL_NO_BOX);
        list->textsize(14);
        list->textfont(FL_HELVETICA);
        list->linespacing(14);
        list->when(FL_WHEN_RELEASE_ALWAYS);
        list->callback(
            [](Fl_Widget *, void *data) {
                auto *self = static_cast<LocationMenu *>(data);
                int n = self->list->value() - 1;
                if (n < 0 || n >= static_cast<int>(self->names.size()))
                    return;
                self->chosen = n;
                Popover::dismiss();
                self->do_callback();
            },
            this);
        popup.end();
        Fl_Group::current(parent);
    }

    void clear() {
        names.clear();
        list->clear();
        chosen = -1;
    }

    void add(const char *name) {
        names.emplace_back(name);
        list->add(name);
    }

    int value() const {
        return chosen;
    }

    void value(int v) {
        chosen = v;
    }

    int size() const {
        return static_cast<int>(names.size());
    }

    bool opened() {
        return popup.shown();
    }

    void draw() override {
        rounded(x(), y(), w(), h(), p.panel);
        fl_color(p.text);
        int cx = x() + w() / 2, cy = y() + h() / 2;
        fl_polygon(cx - 5, cy - 2, cx + 5, cy - 2, cx, cy + 4);
    }

    void open() {
        auto *top = top_window();
        int px = top->x() + x() + w() - popup.w(), py = top->y() + y() + h() + 5;
        int sx, sy, sw, sh;
        Fl::screen_work_area(sx, sy, sw, sh, px, py);
        skin(&popup);
        popup.open_at(std::clamp(px, sx, sx + sw - popup.w()),
                      std::clamp(py, sy, sy + sh - popup.h()));
        list->value(0);
        list->take_focus();
    }

    int handle(int e) override {
        if (e == FL_PUSH) {
            if (opened())
                Popover::dismiss();
            else
                open();
            return 1;
        }
        if (e == FL_RELEASE)
            return 1;
        if (e == FL_KEYDOWN && (Fl::event_key() == FL_Enter || Fl::event_key() == ' ')) {
            open();
            return 1;
        }
        return Button::handle(e);
    }
};

class Guide : public Fl_Scroll {
    struct Step {
        Fl_Box *box, *number, *title, *body;
        std::string text;
    };

    std::vector<Step> steps;

    void arrange() {
        int old = yposition();
        scroll_to(0, 0);
        int yy = y() + 2;
        int width = std::max(220, w() - 18);
        for (auto &step : steps) {
            fl_font(FL_HELVETICA, 14);
            int tw = width - 68, th = 0;
            fl_measure(step.text.c_str(), tw, th, 0);
            int height = std::max(96, th + 65);
            rect(step.box, x() + 2, yy, width, height);
            rect(step.number, x() + 14, yy + 12, 24, 24);
            rect(step.title, x() + 47, yy + 8, width - 58, 35);
            rect(step.body, x() + 47, yy + 45, width - 65, height - 54);
            yy += height + 12;
        }
        scroll_to(0, std::clamp(old, 0, std::max(0, yy - y() - h())));
    }

public:
    Guide() : Fl_Scroll(0, 0, 10, 10) {
        type(VERTICAL);
        box(FL_NO_BOX);
        scrollbar_size(12);
        end();
    }

    void set(const std::string &text) {
        if (text == last)
            return;
        last = text;
        scroll_to(0, 0);
        clear();
        steps.clear();
        begin();
        std::size_t at = 0;
        unsigned index = 1;
        while (at < text.size()) {
            auto end = text.find("\n\n", at);
            std::string block = text.substr(at, end == std::string::npos ? end : end - at);
            auto nl = block.find('\n');
            std::string title = nl == std::string::npos ? "Orientação" : block.substr(0, nl);
            std::string content = nl == std::string::npos ? block : block.substr(nl + 1);
            auto *bg = card();
            auto *num = ::label(std::to_string(index++).c_str(), 13, true);
            num->align(FL_ALIGN_CENTER);
            auto *head = ::label(title.c_str(), 14, true);
            auto *body = ::label(content.c_str(), 14);
            body->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
            steps.push_back({bg, num, head, body, content});
            if (end == std::string::npos)
                break;
            at = end + 2;
        }
        end();
        arrange();
        skin(this);
    }

    std::string last;

    void resize(int x, int y, int w, int h) override {
        bool sized = w != this->w() || h != this->h();
        Fl_Scroll::resize(x, y, w, h);
        if (sized)
            arrange();
    }
};

void enforce_native_minimum(Fl_Window *window, int minw, int minh) {
#ifndef _WIN32
    if (!window->shown() || !fl_display)
        return;
    XWindowAttributes geometry{};
    if (!XGetWindowAttributes(fl_display, fl_xid(window), &geometry))
        return;
    const float scale = Fl::screen_scale(window->screen_num());
    unsigned width = static_cast<unsigned>(std::ceil(static_cast<double>(minw) * scale));
    unsigned height = static_cast<unsigned>(std::ceil(static_cast<double>(minh) * scale));
    if (geometry.width < static_cast<int>(width) || geometry.height < static_cast<int>(height))
        XResizeWindow(fl_display, fl_xid(window),
                      std::max(width, static_cast<unsigned>(geometry.width)),
                      std::max(height, static_cast<unsigned>(geometry.height)));

#else
    (void)window;
    (void)minw;
    (void)minh;
#endif
}

class Dialog : public Fl_Double_Window {
public:
    std::function<void()> on_layout, on_close;
    int minw = 760, minh = 560;
    int cursor_corner = 0;
    unsigned long draw_count = 0, corner_cursor_changes = 0;
    int drag = 0, mx = 0, my = 0, ix = 0, iy = 0, iw = 0, ih = 0;

    void draw() override {
        ++draw_count;
        Fl_Double_Window::draw();
    }

    Dialog(int w, int h, const char *name) : Fl_Double_Window(w, h, name) {
        resizable(this);
        size_range(minw, minh);
    }

    void resize(int x, int y, int w, int h) override {
        bool undersized = w < minw || h < minh;
        w = std::max(w, minw);
        h = std::max(h, minh);
        bool sized = w != this->w() || h != this->h(), moved = x != this->x() || y != this->y();
        if (sized || moved)
            Popover::dismiss();
        Fl_Double_Window::resize(x, y, w, h);
        if (undersized)
            enforce_native_minimum(this, minw, minh);
        if (sized && on_layout)
            on_layout();
    }

    int handle(int e) override {
        if (e == FL_PUSH || e == FL_HIDE)
            Popover::dismiss();
        int ex = Fl::event_x(), ey = Fl::event_y();
        int hhit = ex < 24          ? 1
                   : ex >= w() - 24 ? 2
                                    : 0,
            vhit = ey < 24          ? 4
                   : ey >= h() - 24 ? 8
                                    : 0;
        int corner = hhit && vhit ? hhit | vhit : 0;
        if (e == FL_MOVE || e == FL_ENTER || e == FL_LEAVE) {
            int next = e == FL_LEAVE ? 0 : corner;
            if (next != cursor_corner) {
                if (next)
                    Fl::belowmouse(this);
                cursor_corner = next;
                ++corner_cursor_changes;
                cursor(next == 5 || next == 10 ? FL_CURSOR_NWSE
                       : next                  ? FL_CURSOR_NESW
                                               : FL_CURSOR_DEFAULT);
            }
            if (next)
                return 1; // Campos e listas controlam seus proprios cursores.
        }
        if (e == FL_PUSH && corner && Fl::event_button() == FL_LEFT_MOUSE) {
            drag = corner;
            mx = Fl::event_x_root();
            my = Fl::event_y_root();
            ix = x();
            iy = y();
            iw = w();
            ih = h();
            return 1;
        }
        if (e == FL_DRAG && drag) {
            int dx = Fl::event_x_root() - mx, dy = Fl::event_y_root() - my;
            int nw = std::max(minw, iw + (drag & 1 ? -dx : dx)),
                nh = std::max(minh, ih + (drag & 4 ? -dy : dy));
            resize(ix + (drag & 1 ? iw - nw : 0), iy + (drag & 4 ? ih - nh : 0), nw, nh);
            return 1;
        }
        if (e == FL_RELEASE && drag) {
            drag = 0;
            cursor_corner = 0;
            cursor(FL_CURSOR_DEFAULT);
            return 1;
        }
        if (e == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
            if (on_close)
                on_close();
            return 1;
        }
        return Fl_Double_Window::handle(e);
    }
};
