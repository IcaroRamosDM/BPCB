// A moldura nao preenche o fundo: Fl_Table preserva as celulas nao danificadas.
class Grid : public Fl_Table {
    std::vector<std::vector<std::string>> wrapped;
    int cached_width = 0;

    static std::vector<std::string> wrap_text(const std::string &text, int width) {
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start < text.size()) {
            std::size_t end = start, fit = start, space = std::string::npos;
            while (end < text.size() && text[end] != '\n') {
                std::size_t next = end + 1;
                while (next < text.size() &&
                       (static_cast<unsigned char>(text[next]) & 0xc0) == 0x80)
                    ++next;
                if (fl_width(text.data() + start, static_cast<int>(next - start)) > width &&
                    fit > start)
                    break;
                fit = next;
                if (text[end] == ' ' || text[end] == '\t')
                    space = end;
                end = next;
            }
            if (end < text.size() && text[end] == '\n') {
                lines.push_back(text.substr(start, end - start));
                start = end + 1;
            } else if (end == text.size()) {
                lines.push_back(text.substr(start));
                start = end;
            } else if (space != std::string::npos && space > start) {
                lines.push_back(text.substr(start, space - start));
                start = space + 1;
            } else {
                lines.push_back(text.substr(start, fit - start));
                start = fit;
            }
        }
        if (lines.empty())
            lines.emplace_back("");
        return lines;
    }

    void arrange_columns() {
        int width = std::max(1, w() - 2 * Fl::box_dx(box()) - Fl::scrollbar_size() - 2);
        if (headers.size() == 2) {
            int first = name_first ? std::max(120, width - 130) : 120;
            col_width(0, first);
            col_width(1, std::max(100, width - first));
        }
        if (headers.size() == 3) {
            col_width(0, 120);
            col_width(1, 200);
            col_width(2, std::max(180, width - 320));
        }
        if (headers.size() == 5) {
            int a = width * 26 / 100, b = 100, c = 70, d = width * 23 / 100;
            col_width(0, a);
            col_width(1, b);
            col_width(2, c);
            col_width(3, d);
            col_width(4, std::max(130, width - a - b - c - d));
        }
        if (headers.size() == 4) {
            int a = width * 32 / 100, b = width * 15 / 100, c = (width - a - b) / 2;
            col_width(0, a);
            col_width(1, b);
            col_width(2, c);
            col_width(3, std::max(100, width - a - b - c));
        }
    }

    void arrange_rows(bool changed = false) {
        if (wrapped_column < 0 || wrapped_column >= cols())
            return;
        int width = std::max(20, col_width(wrapped_column) - 24);
        if (!changed && width == cached_width)
            return;
        cached_width = width;
        wrapped.clear();
        wrapped.reserve(data.size());
        fl_font(FL_HELVETICA, 14);
        for (std::size_t i = 0; i < data.size(); ++i) {
            wrapped.push_back(
                wrap_text(data[i].at(static_cast<std::size_t>(wrapped_column)), width));
            row_height(static_cast<int>(i),
                       std::max(36, static_cast<int>(wrapped.back().size()) * 20 + 16));
        }
    }

public:
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> data;
    int selected = -1, wrapped_column = -1;
    bool name_first = false;
    unsigned long draw_count = 0;

    Grid() : Fl_Table(0, 0, 10, 10) {
        end();
        box(table_frame);
        row_header(0);
        col_header(1);
        col_header_height(36);
        row_height_all(36);
        col_resize(0);
        row_resize(0);
        when(FL_WHEN_RELEASE);
    }

    void draw() override {
        ++draw_count;
        Fl_Table::draw();
    }

    void style_scrollbars() {
        for (auto *bar : {vscrollbar, hscrollbar}) {
            bar->box(FL_FLAT_BOX);
            bar->slider(FL_FLAT_BOX);
            bar->color(p.bg);
            bar->selection_color(p.line);
        }
    }

    void reset_scroll() {
        vscrollbar->value(0);
        vscrollbar->do_callback();
        hscrollbar->value(0);
        hscrollbar->do_callback();
    }

    bool scroll_available() const {
        return vscrollbar->visible();
    }

    void cell_rect(int row, int &x, int &y, int &w, int &h) {
        find_cell(CONTEXT_CELL, row, 0, x, y, w, h);
    }

    void header_rect(int &x, int &y, int &w, int &h) {
        find_cell(CONTEXT_COL_HEADER, 0, 0, x, y, w, h);
    }

    const std::vector<std::string> &wrapped_lines(int row) const {
        return wrapped.at(static_cast<std::size_t>(row));
    }

    void update() {
        cols(static_cast<int>(headers.size()));
        rows(static_cast<int>(data.size()));
        arrange_columns();
        if (wrapped_column < 0)
            row_height_all(36);
        else
            arrange_rows(true);
        redraw();
    }

    void resize(int x, int y, int w, int h) override {
        bool width_changed = w != this->w();
        Fl_Table::resize(x, y, w, h);
        if (width_changed) {
            arrange_columns();
            arrange_rows();
        }
    }

    void draw_cell(TableContext ctx, int row, int col, int x, int y, int w, int h) override {
        if (ctx == CONTEXT_STARTPAGE) {
            fl_font(FL_HELVETICA, 14);
            return;
        }
        if (ctx != CONTEXT_CELL && ctx != CONTEXT_COL_HEADER)
            return;
        fl_push_clip(x, y, w, h);
        fl_color(ctx == CONTEXT_COL_HEADER ? p.bg : row == selected ? p.tint : p.panel);
        fl_rectf(x, y, w, h);
        fl_color(p.line);
        fl_line(x, y + h - 1, x + w, y + h - 1);
        fl_color(ctx == CONTEXT_COL_HEADER ? p.muted : p.text);
        fl_font(ctx == CONTEXT_COL_HEADER || col == 0 ? FL_HELVETICA_BOLD : FL_HELVETICA,
                ctx == CONTEXT_COL_HEADER ? 12 : 14);
        const auto &s =
            ctx == CONTEXT_COL_HEADER
                ? headers.at(static_cast<std::size_t>(col))
                : data.at(static_cast<std::size_t>(row)).at(static_cast<std::size_t>(col));
        if (ctx == CONTEXT_CELL && col == 3 && headers.size() == 5) {
            if (s.rfind("✓", 0) == 0)
                fl_color(p.accent);
            else if (s.rfind("✕", 0) == 0)
                fl_color(p.danger);
            else
                fl_color(p.muted);
        }
        if (ctx == CONTEXT_CELL && col == wrapped_column) {
            int yy = y + 8;
            for (const auto &line : wrapped.at(static_cast<std::size_t>(row))) {
                fl_draw(line.c_str(), x + 12, yy, w - 24, 20, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
                yy += 20;
            }
        } else
            fl_draw(s.c_str(), x + 12, y, w - 24, h, FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        fl_pop_clip();
    }
};
