// Edicao local nao modifica o modelo. Enter ou saida do campo confirmam uma vez.
class StableInput : public Fl_Input {
    bool restore_pointer = false;

public:
    using Fl_Input::Fl_Input;

    int handle(int event) override {
        // FL_ENTER ja define o cursor; movimentos internos nao mudam sua forma.
        if (event == FL_MOVE) {
            if (restore_pointer && active_r() && window())
                window()->cursor(FL_CURSOR_INSERT);
            restore_pointer = false;
            return 1;
        }
        if (event == FL_KEYDOWN)
            restore_pointer = true;
        if (event == FL_ENTER || event == FL_LEAVE)
            restore_pointer = false;
        return Fl_Input::handle(event);
    }
};

struct Committable {
    virtual void commit() = 0;
    virtual ~Committable() = default;
};

class CommitInput : public StableInput, public Committable {
    std::string committed;

public:
    unsigned long commits = 0;

    CommitInput(int x, int y, int w, int h) : StableInput(x, y, w, h) {
        when(FL_WHEN_NEVER);
    }

    const char *value() const {
        return Fl_Input::value();
    }

    int value(const char *text) {
        committed = text ? text : "";
        return Fl_Input::value(committed.c_str());
    }

    void commit() override {
        if (committed == value())
            return;
        committed = value();
        clear_changed();
        ++commits;
        do_callback();
    }

    int handle(int e) override {
        if (e == FL_KEYDOWN && (Fl::event_key() == FL_Enter || Fl::event_key() == FL_KP_Enter)) {
            if (input_type() == FL_MULTILINE_INPUT && (Fl::event_state() & FL_SHIFT)) {
                replace(insert_position(), mark(), "\n");
                return 1;
            }
            commit();
            return 1;
        }
        int handled = StableInput::handle(e);
        if (e == FL_UNFOCUS)
            commit();
        return handled;
    }
};

class CommitEditor : public Fl_Text_Editor, public Committable {
    Fl_Text_Buffer text_buffer;
    std::string committed;
    mutable std::string cached;
    bool restore_pointer = false;
    int limit = 1000;

public:
    unsigned long commits = 0;

    CommitEditor(int x, int y, int w, int h) : Fl_Text_Editor(x, y, w, h) {
        buffer(&text_buffer);
        wrap_mode(WRAP_AT_BOUNDS, 0);
        tab_nav(1);
        textfont(FL_HELVETICA);
        scrollbar_width(12);
        when(FL_WHEN_NEVER);
    }

    ~CommitEditor() {
        buffer(nullptr);
    }

    void maximum_size(int bytes) {
        limit = bytes;
    }

    const char *value() const {
        char *text = text_buffer.text();
        cached = text ? text : "";
        free(text);
        return cached.c_str();
    }

    void value(const char *text) {
        committed = text ? text : "";
        text_buffer.text(committed.c_str());
        insert_position(0);
        scroll(0, 0);
        clear_changed();
    }

    void commit() override {
        if (committed == value())
            return;
        committed = value();
        clear_changed();
        ++commits;
        do_callback();
    }

    int handle(int e) override {
        if (e == FL_MOVE) {
            if (restore_pointer && active_r() && window())
                window()->cursor(FL_CURSOR_INSERT);
            restore_pointer = false;
            return 1;
        }
        if (e == FL_ENTER || e == FL_LEAVE)
            restore_pointer = false;
        if (e == FL_KEYDOWN) {
            restore_pointer = true;
            if ((Fl::event_key() == FL_Enter || Fl::event_key() == FL_KP_Enter) &&
                !(Fl::event_state() & FL_SHIFT)) {
                commit();
                return 1;
            }
        }
        int handled = Fl_Text_Editor::handle(e);
        if ((e == FL_KEYDOWN || e == FL_PASTE) && text_buffer.length() > limit) {
            int end = limit;
            const char *text = value();
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
                --end;
            text_buffer.remove(end, text_buffer.length());
            insert_position(std::min(insert_position(), end));
        }
        if (e == FL_UNFOCUS)
            commit();
        return handled;
    }
};

void set_label(Fl_Widget *w, const char *text) {
    if (!w->label() || std::string(w->label()) != text)
        w->copy_label(text);
}

void set_enabled(Fl_Widget *w, bool on) {
    if (on == static_cast<bool>(w->active()))
        return;
    if (on)
        w->activate();
    else
        w->deactivate();
}

void set_visible(Fl_Widget *w, bool on) {
    if (on == static_cast<bool>(w->visible()))
        return;
    if (on)
        w->show();
    else
        w->hide();
}
