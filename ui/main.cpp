#include "bancada.h"
#include "communication.h"
#include "lab.h"
#include "workbench.h"
#include "capture_buffer.h"
#include <FL/Fl_Tabs.H>
#include <array>
#include <FL/Fl.H>
#include <FL/platform.H>
#ifndef _WIN32
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#endif
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Tooltip.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#ifndef _WIN32
#include <fontconfig/fontconfig.h>
#endif
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Table.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Check_Browser.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <cerrno>
#include <cctype>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <clocale>
#include <chrono>
#include <ctime>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
extern char **environ;
#include "frame_editor.h"
namespace fs = std::filesystem;

namespace {
#include "job_watch.inc"

    struct Palette {
        Fl_Color bg, panel, text, muted, line, accent, tint, danger;
    } p;

    bool dark = false;
    constexpr auto card_box = static_cast<Fl_Boxtype>(FL_FREE_BOXTYPE);
    constexpr auto input_box = static_cast<Fl_Boxtype>(FL_FREE_BOXTYPE + 1);
    constexpr auto table_frame = static_cast<Fl_Boxtype>(FL_FREE_BOXTYPE + 2);

    // Fl_Table desenha a moldura tambem no repaint parcial: ela nao pode apagar celulas.
    void table_border(int x, int y, int w, int h, Fl_Color) {
        fl_color(p.line);
        fl_rect(x, y, w, h);
    }

    void rounded(int x, int y, int w, int h, Fl_Color c) {
        fl_color(c);
        fl_rounded_rectf(x, y, w, h, 8);
        fl_color(p.line);
        fl_rounded_rect(x, y, w, h, 8);
    }

    void set_palette(bool value) {
        dark = value;
        p = dark ? Palette{fl_rgb_color(22, 25, 29),    fl_rgb_color(30, 34, 39),
                           fl_rgb_color(232, 234, 237), fl_rgb_color(160, 169, 181),
                           fl_rgb_color(58, 65, 74),    fl_rgb_color(107, 215, 174),
                           fl_rgb_color(38, 66, 58),    fl_rgb_color(235, 112, 117)}
                 : Palette{fl_rgb_color(244, 245, 247), FL_WHITE,
                           fl_rgb_color(30, 34, 40),    fl_rgb_color(99, 108, 121),
                           fl_rgb_color(219, 224, 230), fl_rgb_color(25, 112, 88),
                           fl_rgb_color(228, 242, 235), fl_rgb_color(171, 45, 57)};
        unsigned char r, g, b;
        Fl::get_color(p.bg, r, g, b);
        Fl::background(r, g, b);
        Fl::get_color(p.panel, r, g, b);
        Fl::background2(r, g, b);
        Fl::get_color(p.text, r, g, b);
        Fl::foreground(r, g, b);
        Fl::set_boxtype(card_box, rounded, 1, 1, 2, 2);
        Fl::set_boxtype(input_box, rounded, 8, 5, 16, 10);
        Fl::set_boxtype(table_frame, table_border, 1, 1, 2, 2);
    }

    class Button : public Fl_Button {
    public:
        int kind = 0;
        bool selected = false;
        // user_data pertence ao callback; o indice precisa de armazenamento proprio.
        std::size_t item_index = 0;

        Button(const char *name, int style = 0) : Fl_Button(0, 0, 100, 38), kind(style) {
            copy_label(name);
            labelsize(14);
            box(FL_FLAT_BOX);
        }

        void draw() override {
            Fl_Color bg = kind == 1 ? p.accent : kind == 2 ? p.danger : selected ? p.tint : p.panel;
            Fl_Color fg = (kind == 1 && !dark) || kind == 2 ? FL_WHITE
                          : kind == 1                       ? p.bg
                          : selected                        ? p.accent
                                                            : p.text;
            if (!active_r()) {
                bg = fl_color_average(p.panel, p.bg, 0.5f);
                fg = p.muted;
            }
            if (value())
                bg = fl_color_average(bg, p.text, 0.85f);
            fl_color(bg);
            fl_rounded_rectf(x(), y(), w(), h(), 7);
            if (kind == 0 && !selected) {
                fl_color(p.line);
                fl_rounded_rect(x(), y(), w(), h(), 7);
            }
            labelcolor(fg);
            if (kind == 0) {
                fl_font(labelfont(), labelsize());
                fl_color(fg);
                fl_draw(label(), x() + 12, y(), w() - 24, h(), align());
            } else {
                fl_font(labelfont(), labelsize());
                fl_color(fg);
                fl_draw(label(), x() + 8, y(), w() - 16, h(), align());
            }
            if (Fl::focus() == this) {
                fl_color(p.accent);
                fl_rounded_rect(x() + 3, y() + 3, w() - 6, h() - 6, 5);
            }
        }
    };

    class Choice : public Fl_Choice {
    public:
        Choice() : Fl_Choice(0, 0, 100, 40) {
        }

        void draw() override {
            rounded(x(), y(), w(), h(), p.panel);
            fl_push_clip(x() + 12, y() + 2, w() - 42, h() - 4);
            fl_font(textfont(), textsize());
            fl_color(active_r() ? p.text : p.muted);
            if (mvalue())
                fl_draw(mvalue()->label(), x() + 12, y(), w() - 42, h(),
                        FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            fl_pop_clip();
            fl_color(p.muted);
            int ax = x() + w() - 21, ay = y() + h() / 2;
            fl_polygon(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        }
    };

#include "inputs.inc"

    class SearchInput : public CommitInput {
    public:
        SearchInput() : CommitInput(0, 0, 100, 38) {
        }

        void draw() override {
            Fl_Input::draw();
            if (!*value() && Fl::focus() != this) {
                fl_font(FL_HELVETICA, 14);
                fl_color(p.muted);
                fl_draw("Buscar componente ou valor...", x() + 12, y(), w() - 24, h(),
                        FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            }
        }
    };

    Fl_Box *label(const char *text, int size = 14, bool bold = false) {
        auto *b = new Fl_Box(0, 0, 10, 10);
        b->copy_label(text);
        b->labelsize(size);
        b->labelfont(bold ? FL_HELVETICA_BOLD : FL_HELVETICA);
        b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        return b;
    }

    Fl_Box *card() {
        auto *b = new Fl_Box(0, 0, 10, 10);
        b->box(card_box);
        return b;
    }

    Fl_Text_Display *display(Fl_Text_Buffer &buf) {
        auto *v = new Fl_Text_Display(0, 0, 10, 10);
        v->buffer(buf);
        v->box(FL_NO_BOX);
        v->textfont(FL_HELVETICA);
        v->textsize(14);
        v->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
        v->scrollbar_width(12);
        return v;
    }

    void skin(Fl_Widget *w) {
        w->color(w->box() == card_box || w->box() == input_box || w->box() == table_frame ? p.panel
                                                                                          : p.bg);
        w->labelcolor(w->labelsize() <= 13 ? p.muted : p.text);
        w->selection_color(p.tint);
        if (auto *v = dynamic_cast<Fl_Input_ *>(w)) {
            v->color(p.panel);
            v->textcolor(p.text);
            v->cursor_color(p.accent);
            v->selection_color(p.tint);
        }
        if (auto *v = dynamic_cast<Fl_Choice *>(w)) {
            v->color(p.panel);
            v->textcolor(p.text);
        }
        if (auto *v = dynamic_cast<Fl_Hold_Browser *>(w)) {
            v->color(p.panel);
            v->textcolor(p.text);
            v->selection_color(p.tint);
        }
        if (auto *v = dynamic_cast<Fl_Check_Browser *>(w)) {
            v->color(p.panel);
            v->textcolor(p.text);
            v->selection_color(p.tint);
        }
        if (auto *v = dynamic_cast<Fl_Text_Display *>(w)) {
            v->color(p.panel);
            v->textcolor(p.text);
            v->cursor_color(p.accent);
        }
        if (auto *g = w->as_group())
            for (int i = 0; i < g->children(); i++)
                skin(g->child(i));
        if (auto *bar = dynamic_cast<Fl_Scrollbar *>(w)) {
            bar->box(FL_FLAT_BOX);
            bar->slider(FL_FLAT_BOX);
            bar->color(p.bg);
            bar->selection_color(p.line);
        }
        if (auto *s = dynamic_cast<Fl_Scroll *>(w)) {
            s->scrollbar.box(FL_FLAT_BOX);
            s->scrollbar.slider(FL_FLAT_BOX);
            s->scrollbar.color(p.bg);
            s->scrollbar.selection_color(p.line);
            s->hscrollbar.box(FL_FLAT_BOX);
            s->hscrollbar.slider(FL_FLAT_BOX);
            s->hscrollbar.color(p.bg);
            s->hscrollbar.selection_color(p.line);
        }
        w->redraw();
    }

    void rect(Fl_Widget *w, int x, int y, int width, int height) {
        width = std::max(width, 1);
        height = std::max(height, 1);
        if (w->x() != x || w->y() != y || w->w() != width || w->h() != height)
            w->resize(x, y, width, height);
    }

#include "grid.inc"
#include "widgets.inc"
#include "chart.inc"
    struct App;

    class Window : public Fl_Double_Window {
    public:
        App *app = nullptr;
        unsigned long draw_count = 0, resize_count = 0;
        int cursor_corner = 0;
        unsigned long corner_cursor_changes = 0;
        int edge_drag = 0, drag_x = 0, drag_y = 0, initial_x = 0, initial_y = 0, initial_w = 0,
            initial_h = 0, snap_side = 0;
        bool snap_pending = false, placing = false;
        int handle(int event) override;
        int corner(int x, int y) const;
        void tile(int side);
        static void finish_move(void *data);
        void close_transients();
        static constexpr int minimum_width = 640, minimum_height = 520;

        ~Window() {
            Fl::remove_timeout(finish_move, this);
        }

        void draw() override {
            ++draw_count;
            Fl_Double_Window::draw();
        }

        Window() : Fl_Double_Window(1180, 800, "BPCB · Bancada de PCBs") {
        }

        void resize(int x, int y, int w, int h) override;
    };

    struct Field {
        json_object *node;
        Fl_Box *label;
        CommitInput *input;
        HelpButton *help;
        Choice *choice = nullptr;
    };

    struct App {
        Window win;
        std::string root, bindir, board_id, operation, package_path, pending_digest, edit_ref,
            view = "test";
        std::string result_snapshot, result_board, result_test, reload_after_install;
        json_object *boards = nullptr, *profile = nullptr, *test = nullptr,
                    *last_snapshot = nullptr, *pending_profile = nullptr, *finished_event = nullptr;
        bc_job *job = nullptr;
        bool busy = false, running = false, close_after = false, valid = false, stale = false,
             mount_changed = false, had_result = false;
        Fl_Box *brand, *app_label, *footer_bg, *status, *sidebar_card, *board_name, *board_version;
        Button *theme, *install, *export_btn, *home_btn, *start, *stop, *mount_btn, *docs_btn,
            *results_btn, *back_btn;
        Fl_Group *home, *work, *sidebar, *content, *test_page, *mount_page, *docs_page,
            *results_page;
        Fl_Scroll *viewport;
        Fl_Group *canvas;

        struct Route {
            std::string page, test_id;
        };

        std::vector<Route> navigation;
        Fl_Box *home_title, *home_description, *home_card, *home_card_title, *pcb_label,
            *revision_label, *home_note;
        Fl_Choice *pcb, *revision;
        Button *open_board, *home_history, *home_rigol, *home_lab, *lab_work, *guide_work;
        std::vector<std::string> pcb_names, revision_ids;
        Fl_Scroll *tests;
        std::vector<Button *> test_buttons;
        std::vector<json_object *> listed_tests;
        Fl_Box *test_title, *purpose, *test_badge, *config_card, *instructions_card, *expected_card,
            *config_title, *instructions_title, *expected_title, *availability;
        Fl_Scroll *body, *fields_scroll;
        Fl_Text_Buffer instructions, expected;
        Guide *instructions_display;
        Fl_Text_Display *expected_display;
        std::vector<Field> fields;
        Fl_Box *mount_title, *mount_note, *edit_label;
        CommitInput *filter, *edit;
        CommitEditor *description;
        Fl_Box *description_label;
        Grid *components;
        Button *save_mount;
        Fl_Box *docs_title, *docs_note;
        Fl_Scroll *docs_scroll;
        std::vector<Button *> doc_buttons;
        Fl_Box *results_title, *results_note, *result_details;
        Grid *measurements;
#include "package_picker.inc"
#include "confirmation.inc"
#include "document.inc"

#include "calibration.inc"
#include "history.inc"
#include "rigol.inc"
#include "communication.inc"
#include "communication_tools.inc"
#include "lab.inc"
#include "workspace.inc"
#include "support.inc"
#include "workbench.inc"

        App() {
            win.xclass("bpcb");
            const char *data = getenv("BANCADA_HOME");
            const char *xdg = getenv("XDG_DATA_HOME");
            const char *user = getenv("HOME");
            root =
                data ? data
                     : (xdg ? std::string(xdg) : std::string(user ? user : ".") + "/.local/share") +
                           "/bpcb";
#ifdef _WIN32
            if (!data)
                root = std::string(g_get_user_data_dir()) + "/bpcb";
            wchar_t module[32768];
            DWORD count = GetModuleFileNameW(nullptr, module, 32768);
            if (!count || count >= 32768)
                throw std::runtime_error("Caminho do aplicativo indisponível");
            char *encoded = g_utf16_to_utf8(reinterpret_cast<gunichar2 *>(module), -1, nullptr,
                                            nullptr, nullptr);
            if (!encoded)
                throw std::runtime_error("Caminho inválido");
            bindir = fs::u8path(encoded).parent_path().u8string();
            g_free(encoded);
            if (fs::exists(fs::u8path(bindir + "/../fonts/fonts.conf")))
                _putenv_s("FONTCONFIG_FILE", (bindir + "/../fonts/fonts.conf").c_str());
            _putenv_s("PATH", (bindir + ";" + (getenv("PATH") ? getenv("PATH") : "")).c_str());
#else
            bindir = fs::read_symlink("/proc/self/exe").parent_path().string();
#endif
            bc_error e = {{0}};
            bc_mkdirs(root.c_str(), &e);
            auto pref = bc_read_json((root + "/ui.json").c_str(), &e);
            bool is_dark = pref && json_object_get_boolean(bc_get(pref, "dark"));
            if (pref)
                json_object_put(pref);
            Fl_Tooltip::disable();
            Fl::event_dispatch(Popover::dispatch);
            Fl::scheme("base");
            Fl::set_font(FL_HELVETICA, "DejaVu Sans");
            Fl::set_font(FL_HELVETICA_BOLD, "DejaVu Sans Bold");
            set_palette(is_dark);
#ifdef _WIN32
            Fl::set_font(FL_HELVETICA, "Segoe UI");
            Fl::set_font(FL_HELVETICA_BOLD, "Segoe UI Bold");
#endif
            win.begin();
            back_btn = new Button("←");
            back_btn->labelsize(23);
            back_btn->shortcut(FL_ALT | FL_Left);
            back_btn->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->go_back();
                },
                this);
            brand = label("BPCB", 21, true);
            app_label = label("BANCADA DE PCBs", 12);
            theme = new Button(dark ? "Tema: escuro" : "Tema: claro");
            install = new Button("Instalar bancada");
            export_btn = new Button("Exportar");
            home_btn = new Button("Bancadas");
            theme->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->toggle_theme();
                },
                this);
            install->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->install_package();
                },
                this);
            export_btn->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->export_package();
                },
                this);
            home_btn->callback(
                [](Fl_Widget *, void *v) {
                    auto *a = static_cast<App *>(v);
                    a->home->show();
                    a->work->hide();
                    a->layout();
                    a->refresh();
                },
                this);
            viewport = new Fl_Scroll(0, 78, 1180, 642);
            viewport->type(Fl_Scroll::BOTH);
            viewport->box(FL_FLAT_BOX);
            viewport->scrollbar_size(16);
            canvas = new Fl_Group(0, 78, 1180, 642);
            canvas->box(FL_FLAT_BOX);
            canvas->resizable(nullptr);
            home = new Fl_Group(0, 0, 10, 10);
            home_title = label("Sua bancada, pronta para trabalhar.", 30, true);
            home_description = label(
                "Escolha a placa e a revisão para acessar os testes, as conexões e os documentos.",
                15);
            home_card = card();
            home_card_title = label("Abrir uma bancada", 20, true);
            pcb_label = label("PLACA", 12, true);
            revision_label = label("REVISÃO", 12, true);
            pcb = new Choice();
            pcb->box(input_box);
            pcb->textsize(15);
            revision = new Choice();
            revision->box(input_box);
            revision->textsize(15);
            pcb->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->revisions();
                },
                this);
            open_board = new Button("Abrir bancada", 1);
            open_board->callback(
                [](Fl_Widget *, void *v) {
                    auto *a = static_cast<App *>(v);
                    int n = a->revision->value();
                    if (n >= 0 && n < static_cast<int>(a->revision_ids.size()))
                        a->load_board(a->revision_ids[static_cast<std::size_t>(n)]);
                },
                this);
            auto *history_home = new Button("Histórico");
            history_home->resize(0, 0, 125, 36);
            history_home->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->show_history();
                },
                this);
            home_history = history_home;
            auto *rigol_home = new Button("Rigol");
            rigol_home->callback(
                [](Fl_Widget *, void *v) {
                    auto *a = static_cast<App *>(v);
                    skin(a->rigol_dialog);
                    a->rigol_dialog->show();
                },
                this);
            home_rigol = rigol_home;
            tools_open = new Button("Ferramentas");
            tools_open->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->show_tools();
                },
                this);
            home_lab = new Button("Laboratório");
            home_lab->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->show_lab();
                },
                this);
            home_note = label("Recebeu uma bancada de um colega? Use “Instalar bancada” para "
                              "adicionar o arquivo .bancada.",
                              13);
            home->end();
            work = new Fl_Group(0, 0, 10, 10);
            sidebar = new Fl_Group(0, 0, 10, 10);
            sidebar_card = card();
            board_name = label("", 18, true);
            board_version = label("", 13);
            tests = new Fl_Scroll(0, 0, 10, 10);
            tests->type(Fl_Scroll::VERTICAL);
            tests->box(FL_NO_BOX);
            tests->end();
            mount_btn = new Button("Componentes");
            docs_btn = new Button("Documentação");
            results_btn = new Button("Resultados");
            mount_btn->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->page("mount");
                },
                this);
            docs_btn->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->page("docs");
                },
                this);
            results_btn->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->page("results");
                },
                this);
            cal_btn = new Button("Calibração");
            cal_btn->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->show_calibration();
                },
                this);
            history_open = new Button("Histórico");
            history_open->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->show_history();
                },
                this);
            rigol_open = new Button("Ferramentas");
            rigol_open->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->show_tools();
                },
                this);
            for (auto *button : {cal_btn, rigol_open, results_btn, history_open})
                button->labelsize(12);
            lab_work = new Button("Laboratório");
            lab_work->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->show_lab();
                },
                this);
            guide_work = new Button("Preparar teste…");
            guide_work->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->show_preparation_guide();
                },
                this);
            sidebar->end();
            content = new Fl_Group(0, 0, 10, 10);
            test_page = new Fl_Group(0, 0, 10, 10);
            test_badge = label("ENSAIO", 12, true);
            test_title = label("", 27, true);
            purpose = label("", 14);
            body = new Fl_Scroll(0, 0, 10, 10);
            body->type(Fl_Scroll::VERTICAL);
            body->box(FL_NO_BOX);
            config_card = card();
            config_title = label("Configuração", 17, true);
            fields_scroll = new Fl_Scroll(0, 0, 10, 10);
            fields_scroll->type(Fl_Scroll::VERTICAL);
            fields_scroll->end();
            expected_card = card();
            expected_title = label("Valores esperados", 17, true);
            expected_display = display(expected);
            instructions_card = card();
            instructions_title = label("Conexões e orientações", 17, true);
            instructions_display = new Guide();
            availability = label("", 13);
            body->end();
            test_page->end();
            mount_page = new Fl_Group(0, 0, 10, 10);
            mount_title = label("Valores dos componentes", 27, true);
            mount_note = label("Informe o valor realmente montado. Resistores e capacitores "
                               "aparecem aqui; os cálculos usam os novos valores.",
                               14);
            filter = new SearchInput();
            filter->box(input_box);
            filter->textsize(14);
            filter->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->filter_components();
                },
                this);
            components = new Grid();
            components->headers = {"COMPONENTE", "VALOR MONTADO", "DESCRIÇÃO"};
            components->wrapped_column = 2;
            components->update();
            components->callback(
                [](Fl_Widget *, void *v) {
                    auto *a = static_cast<App *>(v);
                    if (a->components->callback_context() == Fl_Table::CONTEXT_CELL &&
                        Fl::event() == FL_RELEASE)
                        a->select_component(a->components->callback_row());
                },
                this);
            edit_label = label("Selecione um componente", 13);
            edit = new CommitInput(0, 0, 100, 38);
            edit->box(input_box);
            edit->textsize(15);
            edit->maximum_size(180);
            edit->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->edit_component();
                },
                this);
            description_label = label("Descrição · Shift+Enter para nova linha", 13);
            description = new CommitEditor(0, 0, 100, 80);
            description->box(input_box);
            description->textsize(14);
            description->maximum_size(1000);
            description->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->edit_component();
                },
                this);
            save_mount = new Button("Salvar valores", 1);
            save_mount->callback(
                [](Fl_Widget *, void *v) {
                    auto *a = static_cast<App *>(v);
                    auto *values = a->values();
                    a->launch("save", "", values);
                    json_object_put(values);
                },
                this);
            mount_page->end();
            docs_page = new Fl_Group(0, 0, 10, 10);
            docs_title = label("Documentação", 27, true);
            docs_note = label("Esquemático e desenhos da PCB incluídos nesta bancada.", 14);
            docs_scroll = new Fl_Scroll(0, 0, 10, 10);
            docs_scroll->type(Fl_Scroll::VERTICAL);
            docs_scroll->end();
            docs_page->end();
            results_page = new Fl_Group(0, 0, 10, 10);
            results_title = label("Resultados", 27, true);
            results_note = label("Nenhuma medição realizada.", 14);
            result_details = label("", 13);
            measurements = new Grid();
            measurements->headers = {"SINAL / RESULTADO", "VALOR", "UNIDADE", "COMPARAÇÃO",
                                     "CRITÉRIO"};
            measurements->update();
            results_page->end();
            content->end();
            work->end();
            work->hide();
            canvas->end();
            viewport->end();
            footer_bg = new Fl_Box(0, 0, 10, 10);
            footer_bg->box(FL_FLAT_BOX);
            status = label("Carregando suas bancadas…", 13);
            status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
            record_enabled = new Fl_Check_Button(0, 0, 200, 24, "Guardar resultado");
            record_enabled->labelsize(13);
            record_enabled->value(0);
            record_enabled->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->history_controls();
                },
                this);
            record_setup = new Button("Identificação");
            record_setup->labelsize(11);
            record_setup->callback(
                [](Fl_Widget *, void *v) {
                    auto *a = static_cast<App *>(v);
                    skin(a->record_dialog);
                    a->record_dialog->show();
                },
                this);
            start = new Button("Iniciar teste", 1);
            stop = new Button("Parar teste", 2);
            start->callback(
                [](Fl_Widget *, void *v) {
                    auto *a = static_cast<App *>(v);
                    if (a->view == "results" && a->result_board == a->board_id) {
                        auto *t = bc_test(a->profile, a->result_test.c_str());
                        if (t)
                            a->select_test(t);
                    }
                    a->execute();
                },
                this);
            stop->callback(
                [](Fl_Widget *, void *v) {
                    auto *a = static_cast<App *>(v);
                    bc_job_cancel(a->job);
                    a->set_status("Parada solicitada. Estado físico não confirmado.");
                },
                this);
            win.end();
            win.app = this;
            win.resizable(&win);
            win.size_range(Window::minimum_width, Window::minimum_height);
            win.callback(
                [](Fl_Widget *, void *v) {
                    auto *a = static_cast<App *>(v);
                    if (a->running) {
                        a->close_after = true;
                        bc_job_cancel(a->job);
                        a->set_status("Encerrando a rotina. Estado físico não confirmado.");
                    } else
                        a->close_windows();
                },
                this);
            make_picker();
            make_confirmation();
            make_document();
            make_calibration();
            make_history();
            make_rigol();
            make_workspace();
            make_support();
            make_communication();
            make_communication_tools();
            make_lab();
            layout();
            skin(&win);
            fields_scroll->color(p.panel);
            tests->color(p.panel);
            refresh();
            launch("boot", "");
        }

        ~App() {
            destroy_support();
            destroy_lab();
            destroy_communication_tools();
            destroy_communication();
            destroy_workspace();
            if (rigol_job) {
                unwatch_job(rigol_job);
                bc_job_free(rigol_job);
            }
            if (rigol_acquisition)
                json_object_put(rigol_acquisition);
            if (rigol_snapshot)
                json_object_put(rigol_snapshot);
            rigol_details_view->buffer(nullptr);
            delete rigol_details_dialog;
            delete rigol_dialog;
            if (job)
                unwatch_job(job);
            if (browse_job)
                unwatch_job(browse_job);
            if (browse_job)
                bc_job_free(browse_job);
            if (job)
                bc_job_free(job);
            if (boards)
                json_object_put(boards);
            if (profile)
                json_object_put(profile);
            if (last_snapshot)
                json_object_put(last_snapshot);
            if (pending_profile)
                json_object_put(pending_profile);
            if (finished_event)
                json_object_put(finished_event);
            /* Os buffers permanecem vivos ate os widgets serem destruidos. */
            expected_display->buffer(nullptr);
            if (cal_library)
                json_object_put(cal_library);
            if (history_items)
                json_object_put(history_items);
            history_detail->buffer(nullptr);
            delete history_dialog;
            delete record_dialog;
            delete cal_dialog;
            delete picker;
            delete confirmation;
            document_image->image(nullptr);
            delete document;
        }

        void close_windows() {
            Popover::dismiss();
            communication_dialog->hide();
            ct_dialog->hide();
            lx_dialog->hide();
            close_workspace();
            diagnostic_dialog->hide();
            recovery_dialog->hide();
            rigol_details_dialog->hide();
            picker->hide();
            confirmation->hide();
            document->hide();
            cal_dialog->hide();
            history_dialog->hide();
            record_dialog->hide();
            rigol_dialog->hide();
            win.hide();
        }

        void set_status(const std::string &s) {
            if (!status->label() || s != status->label())
                status->copy_label(s.c_str());
        }

        void toggle_theme() {
            set_palette(!dark);
            theme->copy_label(dark ? "Tema: escuro" : "Tema: claro");
            skin(&win);
            skin(picker);
            skin(document);
            skin(cal_dialog);
            skin(history_dialog);
            skin(record_dialog);
            components->style_scrollbars();
            fields_scroll->color(p.panel);
            tests->color(p.panel);
            for (auto *d : {tools_dialog, prepare_dialog, backup_dialog, chart_dialog,
                            session_dialog, comparison_dialog, rigol_dialog, rigol_details_dialog,
                            diagnostic_dialog, recovery_dialog, communication_dialog})
                skin(d);
            if (ct_dialog)
                skin(ct_dialog);
            if (lx_dialog)
                skin(lx_dialog);
            json_object *v = json_object_new_object();
            json_object_object_add(v, "dark", json_object_new_boolean(dark));
            bc_error e = {{0}};
            if (bc_write_json((root + "/ui.json").c_str(), v, &e) < 0)
                set_status(e.text);
            json_object_put(v);
        }

        void refresh() {
            auto enable = set_enabled;
            enable(back_btn, (!busy || operation == "inspect") && (picking() || work->visible()));
            enable(install, !busy && !picking());
            enable(export_btn, !busy && profile && work->visible());
            enable(home_btn, !busy && work->visible());
            enable(pcb, !busy);
            enable(revision, !busy);
            enable(open_board, !busy && !revision_ids.empty());
            enable(tests, !busy);
            enable(mount_btn, !busy);
            enable(fields_scroll, !busy);
            enable(save_mount, !busy && mount_changed);
            enable(edit, !busy && !edit_ref.empty());
            enable(filter, !busy);
            enable(description, !busy && !edit_ref.empty());
            enable(start, !busy && test && valid &&
                              (view == "test" || (view == "results" && result_board == board_id &&
                                                  !result_test.empty())) &&
                              work->visible() && !picking() &&
                              json_object_get_boolean(bc_get(test, "enabled")));
            set_label(start, view == "results" ? "Repetir teste" : "Iniciar teste");
            enable(stop, running);
            if (home->visible()) {
                export_btn->hide();
                home_btn->hide();
                start->hide();
                stop->hide();
            } else {
                export_btn->show();
                home_btn->show();
                start->show();
                stop->show();
            }
            if (home->visible()) {
                record_enabled->hide();
                record_setup->hide();
            } else {
                record_enabled->show();
                record_setup->show();
            }
            picker_controls();
            calibration_controls();
            history_controls();
            rigol_controls();
            enable(home_lab, !busy && !session_active);
            enable(lab_work, !busy && !session_active);
            enable(guide_work, !busy && test && !session_active);
            enable(home_history, !busy);
            enable(home_rigol, !busy);
            enable(cal_btn, !busy && profile && bc_get(profile, "calibration"));
            if (session_active) {
                enable(home_btn, false);
                enable(install, false);
                enable(open_board, false);
                enable(pcb, false);
                enable(revision, false);
            }
            enable(rigol_open, !busy || running);
            enable(tools_open, !busy || running);
            workspace_controls();
            support_controls();
            communication_controls();
            lx_controls();
            if (recovery_blocked) {
                enable(start, false);
                enable(save_mount, false);
                enable(install, false);
                enable(theme, false);
            } else
                enable(theme, true);
        }

        void reveal_selected() {
            if (view != "test" || !work->visible())
                return;
            for (std::size_t i = 0; i < test_buttons.size(); i++)
                if (test_buttons[i]->selected) {
                    int top = static_cast<int>(i) * 53, offset = tests->yposition();
                    if (top < offset)
                        tests->scroll_to(0, top);
                    else if (top + 49 > offset + tests->h())
                        tests->scroll_to(0, std::max(0, top + 49 - tests->h()));
                    break;
                }
        }

        void layout() {
            const int page_x = viewport->xposition(), page_y = viewport->yposition();
            viewport->scroll_to(0, 0);
            int physical_w = std::max(win.w(), Window::minimum_width),
                physical_h = std::max(win.h(), Window::minimum_height);
            bool horizontal = physical_w < 1180, vertical = physical_h < 800;
            for (int i = 0; i < 2; i++) {
                horizontal = physical_w - (vertical ? 16 : 0) < 1180;
                vertical = physical_h - 158 - (horizontal ? 16 : 0) < 642;
            }
            int w = std::max(1180, physical_w - (vertical ? 16 : 0)),
                h = std::max(800, physical_h - (horizontal ? 16 : 0));
            rect(viewport, 0, 78, physical_w, physical_h - 158);
            rect(canvas, 0, 78, w, h - 158);
            int nav_y = tests->yposition(), body_y = body->yposition(),
                fields_y = fields_scroll->yposition(), docs_y = docs_scroll->yposition();
            tests->scroll_to(0, 0);
            body->scroll_to(0, 0);
            fields_scroll->scroll_to(0, 0);
            docs_scroll->scroll_to(0, 0);
            int pad = 24;
            rect(back_btn, pad, 16, 38, 36);
            rect(brand, pad + 52, 17, 76, 27);
            rect(app_label, pad + 140, 22, 160, 20);
            int button_y = 16;
            rect(theme, physical_w - pad - 138, button_y, 138, 36);
            rect(install, physical_w - pad - 310, button_y, 160, 36);
            rect(export_btn, physical_w - pad - 416, button_y, 94, 36);
            rect(home_btn, physical_w - pad - 526, button_y, 98, 36);
            if (physical_w < 1060)
                app_label->hide();
            else
                app_label->show();
            if (physical_w < 720)
                brand->hide();
            else
                brand->show();
            rect(footer_bg, 0, physical_h - 66, physical_w, 66);
            rect(status, pad, physical_h - 34, physical_w - 360, 30);
            rect(record_enabled, pad, physical_h - 64, 174, 25);
            rect(record_setup, pad + 180, physical_h - 62, 98, 24);
            rect(start, physical_w - pad - 304, physical_h - 52, 164, 39);
            rect(stop, physical_w - pad - 128, physical_h - 52, 128, 39);
            if (home->visible())
                rect(status, pad, physical_h - 60, physical_w - 2 * pad, 49);
            rect(home, pad, 78, w - 2 * pad, h - 158);
            int cw = std::min(820, w - 2 * pad - 32), cx = (w - cw) / 2,
                hy = std::max(90, std::min(156, h / 5));
            rect(home_title, cx, hy, cw, 48);
            home_title->labelsize(w < 900 ? 26 : 30);
            rect(home_description, cx, hy + 55, cw, 50);
            int cy = hy + 122, ch = std::min(290, h - 82 - cy);
            rect(home_card, cx, cy, cw, ch);
            rect(home_card_title, cx + 24, cy + 20, cw - 48, 32);
            int split = (cw - 60) * 2 / 3;
            rect(pcb_label, cx + 24, cy + 70, split, 22);
            rect(revision_label, cx + 36 + split, cy + 70, cw - split - 60, 22);
            rect(pcb, cx + 24, cy + 97, split, 42);
            rect(revision, cx + 36 + split, cy + 97, cw - split - 60, 42);
            rect(open_board, cx + cw - 208, cy + ch - 64, 184, 40);
            rect(home_history, cx + 24, cy + ch - 64, 125, 40);
            rect(home_rigol, cx + 162, cy + ch - 64, 100, 40);
            rect(home_note, cx + 24, cy + 154, cw - 48, std::max(40, ch - 234));
            rect(tools_open, cx + 275, cy + ch - 64, 145, 40);
            rect(home_lab, cx + 432, cy + ch - 64, 160, 40);
            if (ch < 235)
                home_note->hide();
            else
                home_note->show();
            rect(work, pad, 78, w - 2 * pad, h - 158);
            int sw = w < 920 ? 178 : 216, sx = pad, sy = 78, sh = h - 158;
            rect(sidebar, sx, sy, sw, sh);
            rect(sidebar_card, sx, sy, sw, sh);
            rect(board_name, sx + 16, sy + 12, sw - 32, 48);
            rect(board_version, sx + 16, sy + 66, sw - 32, 35);
            rect(tests, sx + 12, sy + 109, sw - 24, std::max(70, sh - 396));
            tests->scroll_to(0, 0);
            for (std::size_t i = 0; i < test_buttons.size(); i++)
                rect(test_buttons[i], sx + 14, sy + 111 + static_cast<int>(i) * 53, sw - 42, 47);
            rect(guide_work, sx + 12, sy + sh - 270, sw - 24, 36);
            rect(lab_work, sx + 12, sy + sh - 226, sw - 24, 36);
            rect(cal_btn, sx + 12, sy + sh - 182, (sw - 30) / 2, 36);
            rect(rigol_open, sx + 18 + (sw - 30) / 2, sy + sh - 182, (sw - 30) / 2, 36);
            rect(mount_btn, sx + 12, sy + sh - 138, sw - 24, 36);
            rect(docs_btn, sx + 12, sy + sh - 94, sw - 24, 36);
            rect(results_btn, sx + 12, sy + sh - 50, (sw - 30) / 2, 36);
            rect(history_open, sx + 18 + (sw - 30) / 2, sy + sh - 50, (sw - 30) / 2, 36);
            int x = sx + sw + 24, y = sy, cwidth = w - pad - x, cheight = sh;
            rect(content, x, y, cwidth, cheight);
            for (auto *g : {test_page, mount_page, docs_page, results_page})
                rect(g, x, y, cwidth, cheight);
            rect(test_badge, x, y, cwidth, 20);
            rect(test_title, x, y + 27, cwidth, 36);
            rect(purpose, x, y + 69, cwidth, 48);
            rect(body, x, y + 132, cwidth, std::max(120, cheight - 132));
            body->scroll_to(0, 0);
            int bx = x, by = y + 132, bw = cwidth - 16,
                gh = std::min(260, std::max(96, static_cast<int>(fields.size()) * 76));
            if (bw >= 710) {
                int right = std::max(296, bw * 40 / 100), left = bw - right - 20,
                    rx = bx + left + 20;
                int ih = std::max(360, cheight - 132);
                rect(instructions_card, bx, by, left, ih);
                rect(instructions_title, bx + 20, by + 16, left - 40, 28);
                rect(instructions_display, bx + 16, by + 58, left - 32, ih - 80);
                rect(config_card, rx, by, right, gh + 64);
                rect(config_title, rx + 20, by + 16, right - 40, 28);
                rect(fields_scroll, rx + 18, by + 57, right - 36, gh);
                rect(expected_card, rx, by + gh + 82, right, 192);
                rect(expected_title, rx + 20, by + gh + 98, right - 40, 28);
                rect(expected_display, rx + 16, by + gh + 140, right - 32, 116);
                rect(availability, rx, by + gh + 286, right, 64);
            } else {
                rect(config_card, bx, by, bw, gh + 64);
                rect(config_title, bx + 20, by + 16, bw - 40, 28);
                rect(fields_scroll, bx + 18, by + 57, bw - 36, gh);
                int ey = by + gh + 82;
                rect(expected_card, bx, ey, bw, 190);
                rect(expected_title, bx + 20, ey + 16, bw - 40, 28);
                rect(expected_display, bx + 16, ey + 58, bw - 32, 112);
                int iy = ey + 210;
                rect(instructions_card, bx, iy, bw, 360);
                rect(instructions_title, bx + 20, iy + 16, bw - 40, 28);
                rect(instructions_display, bx + 16, iy + 58, bw - 32, 284);
                rect(availability, bx, iy + 376, bw, 58);
            }
            fields_scroll->scroll_to(0, 0);
            int fy = fields_scroll->y() + 2;
            for (auto &f : fields) {
                rect(f.label, fields_scroll->x() + 2, fy, fields_scroll->w() - 51, 27);
                rect(f.help, fields_scroll->x() + fields_scroll->w() - 46, fy, 24, 24);
                rect(f.choice ? static_cast<Fl_Widget *>(f.choice)
                              : static_cast<Fl_Widget *>(f.input),
                     fields_scroll->x() + 2, fy + 31, fields_scroll->w() - 22, 36);
                fy += 76;
            }
            rect(mount_title, x, y, cwidth, 39);
            rect(mount_note, x, y + 49, cwidth, 50);
            rect(filter, x, y + 113, cwidth, 36);
            rect(components, x, y + 165, cwidth, std::max(88, cheight - 340));
            rect(edit_label, x, y + cheight - 161, cwidth, 22);
            rect(edit, x, y + cheight - 129, 200, 40);
            rect(description_label, x + 218, y + cheight - 161, cwidth - 404, 22);
            rect(description, x + 218, y + cheight - 129, std::max(150, cwidth - 402), 92);
            rect(save_mount, x + cwidth - 172, y + cheight - 129, 172, 40);
            rect(docs_title, x, y, cwidth, 40);
            rect(docs_note, x, y + 54, cwidth, 48);
            rect(docs_scroll, x, y + 126, cwidth, cheight - 126);
            for (std::size_t i = 0; i < doc_buttons.size(); i++)
                rect(doc_buttons[i], x + 1, y + 128 + static_cast<int>(i) * 62, cwidth - 22, 48);
            rect(results_title, x, y, cwidth, 40);
            rect(results_note, x, y + 52, cwidth, 52);
            rect(result_details, x, y + 116, cwidth, 79);
            rect(measurements, x, y + 214, cwidth, std::max(90, cheight - 214));
            tests->scroll_to(0, std::clamp(nav_y, 0,
                                           std::max(0, static_cast<int>(test_buttons.size()) * 53 -
                                                           tests->h() + 4)));
            fields_scroll->scroll_to(0,
                                     std::clamp(fields_y, 0,
                                                std::max(0, static_cast<int>(fields.size()) * 76 -
                                                                fields_scroll->h() + 4)));
            docs_scroll->scroll_to(
                0, std::clamp(docs_y, 0,
                              std::max(0, static_cast<int>(doc_buttons.size()) * 62 -
                                              docs_scroll->h() + 4)));
            body->scroll_to(0, std::clamp(body_y, 0,
                                          std::max(0, availability->y() + availability->h() -
                                                          body->y() - body->h() + 4)));
            components->style_scrollbars();
            viewport->scroll_to(std::clamp(page_x, 0, w - physical_w + (vertical ? 16 : 0)),
                                std::clamp(page_y, 0, h - physical_h + (horizontal ? 16 : 0)));
            win.redraw();
        }

        void launch(const std::string &op, const std::string &path, json_object *values = nullptr,
                    const std::string &extra = "") {
            if (job)
                return;
            operation = op;
            busy = true;
            job = bc_job_start(op.c_str(), root.c_str(), board_id.c_str(), path.c_str(),
                               extra.c_str(), values);
            if (!job) {
                busy = running = false;
                set_status("Não foi possível iniciar a operação.");
                refresh();
                return;
            }
            if (op != "run")
                set_status(op == "boot"   ? "Carregando bancadas…"
                           : op == "save" ? "Salvando componentes…"
                                          : "Processando pacote…");
            watch_job(job, timer, this);
            refresh();
        }

        static void timer(int, void *v) {
            static_cast<App *>(v)->poll();
        }

        void poll() {
            if (!job)
                return;
            bc_job_acknowledge(job);
            json_object *event;
            while (job && (event = bc_job_next(job))) {
                std::string kind = bc_str(event, "type");
                plot_event(event);
                ct_event(event);
                if (kind == "finished") {
                    if (finished_event)
                        json_object_put(finished_event);
                    finished_event = json_object_get(event);
                } else if (kind == "status")
                    set_status(bc_str(event, "message"));
                else if (kind == "snapshot") {
                    if (last_snapshot)
                        json_object_put(last_snapshot);
                    last_snapshot = bc_clone(bc_get(event, "data"));
                    result_board = board_id;
                    result_test = bc_str(test, "id");
                    result_snapshot =
                        json_object_to_json_string_ext(profile, JSON_C_TO_STRING_PLAIN);
                    std::ostringstream s;
                    s << bc_str(profile, "pcb") << " · " << bc_str(profile, "revision")
                      << " · pacote " << bc_str(profile, "version") << "\n"
                      << bc_str(event, "message");
                    json_object *params = bc_get(last_snapshot, "parameters");
                    auto *parameter_nodes = bc_get(test, "parameters");
                    bool first = true;
                    for (std::size_t i = 0; i < json_object_array_length(parameter_nodes); i++) {
                        auto *node = json_object_array_get_idx(parameter_nodes, i);
                        auto *value = bc_get(params, bc_str(node, "id"));
                        if (!value)
                            continue;
                        s << (first ? "\n" : " · ") << bc_str(node, "label") << ": "
                          << json_object_get_double(value) << " " << bc_str(node, "unit");
                        first = false;
                    }
                    if (*bc_str(bc_get(last_snapshot, "calibration"), "physical_id"))
                        s << "\nCalibração da placa: "
                          << bc_str(bc_get(last_snapshot, "calibration"), "physical_id");
                    auto *connection = bc_get(last_snapshot, "communication");
                    if (connection) {
                        if (bc_num(connection, "api") == 2) {
                            s << "\nComunicação: "
                              << json_object_object_length(bc_get(connection, "channels"))
                              << " canais (detalhes no relatório/JSON)";
                        } else
                            s << "\nComunicação: " << bc_str(connection, "adapter") << " · "
                              << bc_str(connection, "bus") << " · " << bc_str(connection, "device");
                    }
                    result_details->copy_label(s.str().c_str());
                } else if (kind == "measurement") {
                    std::ostringstream s;
                    s.precision(8);
                    s << bc_num(event, "value");
                    std::string stamp = bc_str(event, "timestamp");
                    std::string verdict = bc_str(event, "verdict");
                    measurements->data.push_back({bc_str(event, "label"), s.str(),
                                                  bc_str(event, "unit"),
                                                  verdict == "pass"   ? "✓ Dentro da faixa"
                                                  : verdict == "fail" ? "✕ Fora da faixa"
                                                                      : "— Sem comparação",
                                                  bc_str(event, "criterion")});
                    measurements->update();
                    had_result = true;
                } else if (kind == "confirm") {
                    show_confirmation(event);
                }
                json_object_put(event);
            }
            if (job && !bc_job_finished(job))
                return;
            if (job) {
                while ((event = bc_job_next(job))) {
                    if (!strcmp(bc_str(event, "type"), "finished")) {
                        if (finished_event)
                            json_object_put(finished_event);
                        finished_event = json_object_get(event);
                    }
                    json_object_put(event);
                }
                unwatch_job(job);
                bc_job_free(job);
                job = nullptr;
            }
            busy = false;
            auto *end = finished_event;
            finished_event = nullptr;
            finish(end);
            if (end)
                json_object_put(end);
            refresh();
        }

        void finish(json_object *end) {
            const auto op = operation;
            bool ok = end && !strcmp(bc_str(end, "status"), "ok");
            std::string message = end ? bc_str(end, "message") : "Operação incompleta.";
            support_log(op, end);
            if (lx_finished(op, end, ok, message))
                return;
            if (communication_finished(op, end, ok, message))
                return;
            if (support_finished(op, end, ok, message))
                return;
            if (workspace_finished(op, end, ok, message))
                return;
            if (op.rfind("history_", 0) == 0) {
                history_finished(op, end, ok, message);
                return;
            }
            if (op == "render_document") {
                document_rendered(bc_get(end, "data"), ok, message);
                return;
            }
            if (op == "run") {
                if (current_record)
                    json_object_put(current_record);
                current_record = bc_clone(bc_get(end, "record"));
                if (current_record && !chart->series.empty())
                    json_object_object_add(current_record, "plot", chart->snapshot());
                if (rigol_job)
                    bc_job_cancel(rigol_job);
                confirmation->hide();
                running = false;
                set_status(ok ? bc_str(bc_get(end, "data"), "message") : message);
                results_note->copy_label(
                    ok ? "Leituras desta execução. ✓/✕ comparam os valores; não aprovam a PCB "
                         "inteira."
                       : "Execução interrompida ou falhou. Leituras parciais não validam o teste.");
                if (*bc_str(end, "history_error"))
                    set_status(std::string("ATENÇÃO: resultado não foi salvo. ") +
                               bc_str(end, "history_error"));
                else if (*bc_str(end, "history_id"))
                    set_status(std::string(status->label()) + " · Histórico salvo.");
                page("results");
                session_finished(end);
                if (close_after)
                    close_windows();
                return;
            }
            if (!ok) {
                if (op.rfind("calibration_", 0) == 0)
                    set_label(cal_note, message.c_str());
                set_status(message);
                if (picking())
                    picker_note->copy_label(message.c_str());
                return;
            }
            if (op == "calibration_list") {
                calibration_list_received(bc_get(end, "data"));
                set_status(cal_active_id.empty()
                               ? "Calibrações carregadas. Selecione a placa física."
                               : "Calibração em uso: " + cal_active_id);
                return;
            }
            if (op == "calibration_save") {
                auto *cal = bc_get(end, "data");
                cal_active_id = bc_str(cal, "physical_id");
                set_label(cal_note,
                          ("Calibração salva e em uso nesta sessão: " + cal_active_id).c_str());
                update_preview();
                launch("calibration_list", "");
                return;
            }
            if (op == "boot" || op == "list") {
                if (boards)
                    json_object_put(boards);
                boards = bc_clone(bc_get(end, "data"));
                rebuild_boards();
                set_status("Bancadas carregadas. Nenhum teste em execução.");
                recovery_guard();
                if (recovery_blocked) {
                    set_status("Restauração pendente. Confira Ferramentas > Recuperação antes de "
                               "ensaiar.");
                    show_recovery();
                    return;
                }
                if (!reload_after_install.empty()) {
                    auto id = std::exchange(reload_after_install, {});
                    load_board(id);
                }
            } else if (op == "inspect") {
                show_package_review(bc_get(end, "data"));
            } else if (op == "install") {
                if (work->visible() && board_id == bc_str(pending_profile, "id"))
                    reload_after_install = board_id;
                restore_picker_origin();
                launch("list", "");
            } else if (op == "save") {
                mount_changed = false;
                set_status("Componentes e parâmetros salvos.");
            } else if (op == "export") {
                if (picking())
                    restore_picker_origin();
                set_status(
                    "Bancada exportada com montagem, parâmetros, documentos e rotinas nativas.");
            } else if (op == "document")
                set_status("Documento enviado ao leitor de PDF.");
        }

        void rebuild_boards() {
            pcb->clear();
            pcb_names.clear();
            for (std::size_t i = 0; i < json_object_array_length(boards); i++) {
                auto *m = json_object_array_get_idx(boards, i);
                std::string name = bc_str(m, "pcb");
                if (std::find(pcb_names.begin(), pcb_names.end(), name) == pcb_names.end()) {
                    pcb_names.push_back(name);
                    pcb->add(name.c_str());
                }
            }
            bool empty = pcb_names.empty();
            home_card_title->copy_label(empty ? "Nenhuma bancada instalada" : "Abrir uma bancada");
            home_description->copy_label(
                empty ? "Comece instalando um arquivo .bancada. Cada pacote inclui os testes, as "
                        "conexões e os documentos de uma PCB."
                      : "Escolha a placa e a revisão para acessar os testes, as conexões e os "
                        "documentos.");
            pcb->value(empty ? -1 : 0);
            revisions();
        }

        void revisions() {
            revision->clear();
            revision_ids.clear();
            int n = pcb->value();
            if (n < 0 || n >= static_cast<int>(pcb_names.size())) {
                refresh();
                return;
            }
            for (std::size_t i = 0; i < json_object_array_length(boards); i++) {
                auto *m = json_object_array_get_idx(boards, i);
                if (pcb_names[static_cast<std::size_t>(n)] == bc_str(m, "pcb")) {
                    revision_ids.emplace_back(bc_str(m, "id"));
                    revision->add(bc_str(m, "revision"));
                }
            }
            revision->value(0);
            refresh();
        }

        void load_board(const std::string &id) {
            if (busy)
                return;
            bc_error e = {{0}};
            char folder[4096];
            auto *m = bc_profile(root.c_str(), id.c_str(), folder, &e);
            if (!m) {
                set_status(e.text);
                return;
            }
            if (profile)
                json_object_put(profile);
            cal_dialog->hide();
            cal_active_id.clear();
            cal_profiles->clear();
            if (cal_library) {
                json_object_put(cal_library);
                cal_library = nullptr;
            }

            cal_unit->value("");
            for (auto *check : cal_checks)
                check->value(0);
            for (auto &row : cal_points)
                for (auto *i : row)
                    i->value("");
            set_label(
                cal_note,
                "Nenhum perfil selecionado. Selecione a placa física para usar uma calibração.");
            profile = m;
            test = nullptr;
            navigation.clear();
            view = "test";
            viewport->scroll_to(0, 0);
            board_id = id;
            mount_changed = false;
            edit_ref.clear();
            edit->value("");
            edit_label->copy_label("Selecione um componente");
            board_name->copy_label(bc_str(m, "pcb"));
            board_name->labelsize(std::string(bc_str(m, "pcb")).size() > 22 ? 14 : 18);
            std::string rev =
                std::string(bc_str(m, "revision")) + "\nPacote " + bc_str(m, "version");
            board_version->copy_label(rev.c_str());
            tests->clear();
            test_buttons.clear();
            listed_tests.clear();
            auto *a = bc_get(m, "tests");
            tests->begin();
            for (std::size_t i = 0; i < json_object_array_length(a); i++) {
                auto *t = json_object_array_get_idx(a, i);
                listed_tests.push_back(t);
                auto *button = new Button(!strcmp(bc_str(t, "id"), "adc") ? "Leitura dos canais ADC"
                                                                          : bc_str(t, "title"));
                button->labelsize(14);
                button->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
                button->item_index = i;
                button->callback(
                    [](Fl_Widget *w, void *v) {
                        auto *self = static_cast<App *>(v);
                        const auto index = static_cast<Button *>(w)->item_index;
                        if (index < self->listed_tests.size())
                            self->select_test(self->listed_tests[index]);
                    },
                    this);
                test_buttons.push_back(button);
            }
            tests->end();

            filter->value("");
            filter_components();
            docs_scroll->clear();
            doc_buttons.clear();
            docs_scroll->begin();
            auto *docs = bc_get(m, "documents");
            if (docs)
                for (std::size_t i = 0; i < json_object_array_length(docs); i++) {
                    auto *d = json_object_array_get_idx(docs, i);
                    auto *b = new Button(bc_str(d, "title"));
                    b->item_index = i;
                    doc_buttons.push_back(b);
                    b->callback(
                        [](Fl_Widget *w, void *v) {
                            auto *a = static_cast<App *>(v);
                            if (a->busy)
                                return;
                            auto *docs = bc_get(a->profile, "documents");
                            auto index = static_cast<Button *>(w)->item_index;
                            if (docs && index < json_object_array_length(docs)) {
                                auto *d = json_object_array_get_idx(docs, index);
                                a->show_document(bc_str(d, "file"));
                            }
                        },
                        this);
                }
            docs_scroll->end();
            home->hide();
            work->show();
            if (!listed_tests.empty())
                select_test(listed_tests[0]);
            else {
                test = nullptr;
                page("docs");
            }
            skin(&win);
            fields_scroll->color(p.panel);
            tests->color(p.panel);
            layout();
            refresh();
            set_status("Bancada aberta. Escolha um teste para preparar a execução.");
        }

        void remember_page() {
            if (work->visible() && test)
                navigation.push_back({view, bc_str(test, "id")});
        }

        void go_back() {
            if (picking()) {
                picker_previous();
                return;
            }
            if (busy || !work->visible())
                return;
            if (navigation.empty()) {
                home->show();
                work->hide();
                viewport->scroll_to(0, 0);
                layout();
                refresh();
                return;
            }
            Route route = navigation.back();
            navigation.pop_back();
            if (route.page == "test") {
                if (auto *t = bc_test(profile, route.test_id.c_str()))
                    select_test(t, false);
            } else
                page(route.page, false);
        }

        void select_test(json_object *t, bool remember = true) {
            if (busy)
                return;
            if (remember && (test != t || view != "test"))
                remember_page();
            viewport->scroll_to(0, 0);
            body->scroll_to(0, 0);
            fields_scroll->scroll_to(0, 0);
            test = t;
            test_title->copy_label(bc_str(t, "title"));
            purpose->copy_label(bc_str(t, "purpose"));
            test_badge->copy_label(!strcmp(bc_str(t, "mode"), "simulation")
                                       ? "SIMULAÇÃO · SEM HARDWARE"
                                   : !strcmp(bc_str(t, "group"), "diagnostics") ? "DIAGNÓSTICO"
                                                                                : "ENSAIO");
            for (std::size_t i = 0; i < listed_tests.size(); i++)
                test_buttons[i]->selected = listed_tests[i] == t;
            fields_scroll->clear();
            fields.clear();
            fields_scroll->begin();
            auto *a = bc_get(t, "parameters");
            for (std::size_t i = 0; i < json_object_array_length(a); i++) {
                auto *n = json_object_array_get_idx(a, i);
                std::string text = bc_str(n, "label");
                if (*bc_str(n, "unit"))
                    text += " (" + std::string(bc_str(n, "unit")) + ")";
                auto *l = label(text.c_str(), 13);
                if (auto *choices = bc_get(n, "choices")) {
                    auto *choice = new Choice();
                    choice->textsize(14);
                    choice->box(input_box);
                    for (std::size_t k = 0; k < json_object_array_length(choices); k++) {
                        auto *option = json_object_array_get_idx(choices, k);
                        choice->add(bc_str(option, "label"));
                        if (bc_num(option, "value") == bc_num(n, "default"))
                            choice->value(static_cast<int>(k));
                    }
                    auto *hint = new HelpButton(text, bc_str(n, "help"));
                    choice->callback(
                        [](Fl_Widget *, void *v) {
                            static_cast<App *>(v)->change_parameters();
                        },
                        this);
                    fields.push_back({n, l, nullptr, hint, choice});
                    continue;
                }
                auto *input = new CommitInput(0, 0, 100, 36);
                input->box(input_box);
                char number[64];
                std::snprintf(number, sizeof number, "%.9g", bc_num(n, "default"));
                input->value(bc_get(n, "default") ? number : "");
                input->textsize(15);
                input->maximum_size(40);
                auto *hint = new HelpButton(text, bc_str(n, "help"));
                input->callback(
                    [](Fl_Widget *, void *v) {
                        static_cast<App *>(v)->change_parameters();
                    },
                    this);
                fields.push_back({n, l, input, hint});
            }
            fields_scroll->end();
            page("test", false);
            update_preview();
            skin(&win);
            fields_scroll->color(p.panel);
            tests->color(p.panel);
            layout();
            reveal_selected();
            refresh();
        }

        void change_parameters() {
            if (busy)
                return;
            std::vector<double> parsed;
            for (auto &f : fields) {
                if (f.choice) {
                    auto *choices = bc_get(f.node, "choices");
                    int index = f.choice->value();
                    if (index < 0 ||
                        static_cast<std::size_t>(index) >= json_object_array_length(choices)) {
                        valid = false;
                        return;
                    }
                    parsed.push_back(
                        bc_num(json_object_array_get_idx(choices, static_cast<std::size_t>(index)),
                               "value"));
                    continue;
                }
                std::string text = f.input->value();
                std::replace(text.begin(), text.end(), ',', '.');
                if (text.empty() && json_object_get_boolean(bc_get(f.node, "optional"))) {
                    parsed.push_back(NAN);
                    continue;
                }
                char *end = nullptr;
                errno = 0;
                double value = std::strtod(text.c_str(), &end);
                if (text.empty() || end == text.c_str() || *end || errno || !std::isfinite(value) ||
                    value < bc_num(f.node, "min") || value > bc_num(f.node, "max") ||
                    (json_object_get_boolean(bc_get(f.node, "integer")) &&
                     std::floor(value) != value)) {
                    valid = false;
                    expected.text(
                        "Valor inválido. Digite um número dentro da faixa indicada no botão ?.");
                    availability->copy_label(
                        ("Confira: " + std::string(bc_str(f.node, "label"))).c_str());
                    if (had_result) {
                        stale = true;
                        results_note->copy_label(
                            "Parâmetros em edição. Resultados da execução anterior.");
                    }
                    refresh();
                    return;
                }
                parsed.push_back(value);
            }
            for (std::size_t i = 0; i < fields.size(); i++)
                json_object_object_add(fields[i].node, "default",
                                       std::isnan(parsed[i]) ? nullptr
                                                             : json_object_new_double(parsed[i]));
            update_preview();
        }

        void update_preview() {
            valid = false;
            if (!test)
                return;
            bc_error e = {{0}};
            auto *v = bc_preview(profile, test, &e);
            if (v) {
                instructions.text(bc_str(v, "instructions"));
                instructions_display->set(bc_str(v, "instructions"));
                expected.text(bc_str(v, "expected"));
                json_object_put(v);
                valid = true;
            } else {
                instructions_display->set("CONFIGURAÇÃO\nCorrija os valores antes de iniciar.");
                instructions.text("Corrija a configuração antes de iniciar.");
                expected.text(e.text);
            }
            availability->copy_label(
                json_object_get_boolean(bc_get(test, "enabled"))
                    ? "As leituras aparecem após executar o teste."
                    : "Preparação disponível. A rotina elétrica ainda não está integrada.");
            stale = had_result && (result_board != board_id ||
                                   result_snapshot != json_object_to_json_string_ext(
                                                          profile, JSON_C_TO_STRING_PLAIN));
            if (stale)
                results_note->copy_label(
                    "Configuração alterada. As leituras abaixo pertencem à execução anterior.");
            if (!cal_active_id.empty())
                availability->copy_label(("Calibração selecionada: " + cal_active_id +
                                          ". A compatibilidade é conferida antes de executar.")
                                             .c_str());
            refresh();
        }

        void page(const std::string &name, bool remember = true) {
            if (remember && view != name)
                remember_page();
            viewport->scroll_to(0, 0);
            view = name;
            test_page->hide();
            mount_page->hide();
            docs_page->hide();
            results_page->hide();
            if (name == "test")
                test_page->show();
            else if (name == "mount")
                mount_page->show();
            else if (name == "docs")
                docs_page->show();
            else
                results_page->show();
            mount_btn->selected = name == "mount";
            docs_btn->selected = name == "docs";
            results_btn->selected = name == "results";
            layout();
            refresh();
        }

        json_object *values() {
            auto *v = json_object_new_object();
            auto *binding = lx_binding_values();
            if (binding)
                json_object_object_add(v, "communication", binding);
            else if (communication_applied)
                json_object_object_add(v, "communication", bc_clone(communication_applied));
            else if (communication_edited)
                json_object_object_add(
                    v, "communication",
                    json_tokener_parse(
                        "{\"adapter\":\"ch341t\",\"bus\":\"i2c\",\"connections_checked\":false}"));
            if (!profile)
                return v;
            json_object_object_add(v, "components", json_object_get(bc_get(profile, "components")));
            json_object_object_add(v, "component_descriptions",
                                   json_object_get(bc_get(profile, "component_descriptions")));
            auto *params = json_object_new_object();
            auto *a = bc_get(profile, "tests");
            for (std::size_t i = 0; i < json_object_array_length(a); i++) {
                auto *t = json_object_array_get_idx(a, i), *x = json_object_new_object(),
                     *b = bc_get(t, "parameters");
                for (std::size_t k = 0; k < json_object_array_length(b); k++) {
                    auto *n = json_object_array_get_idx(b, k);
                    json_object_object_add(x, bc_str(n, "id"),
                                           json_object_get(bc_get(n, "default")));
                }
                json_object_object_add(params, bc_str(t, "id"), x);
            }
            json_object_object_add(v, "parameters", params);
            return v;
        }

        void execute(json_object *reviewed = nullptr) {
            if (busy || !valid || !test || !json_object_get_boolean(bc_get(test, "enabled")))
                return;
            if (session_active && (!session_waiting || session_index >= session_queue.size() ||
                                   session_queue[session_index] != bc_str(test, "id"))) {
                set_status("Prepare a etapa correta pelo diálogo Sessão guiada.");
                return;
            }
            auto *v = reviewed ? bc_clone(reviewed) : values();
            auto *identity = json_object_new_object();
            json_object_object_add(identity, "physical_board",
                                   json_object_new_string(record_board->value()));
            json_object_object_add(identity, "device",
                                   json_object_new_string(record_device->value()));
            json_object_object_add(identity, "operator",
                                   json_object_new_string(record_operator->value()));
            json_object_object_add(v, "identity", identity);
            chart->clear();
            chart_signal->clear();
            if (record_enabled->value()) {
                auto *record = json_object_new_object();
                json_object_object_add(record, "enabled", json_object_new_boolean(1));
                json_object_object_add(record, "physical_board",
                                       json_object_new_string(record_board->value()));
                json_object_object_add(record, "device",
                                       json_object_new_string(record_device->value()));
                json_object_object_add(record, "operator",
                                       json_object_new_string(record_operator->value()));
                json_object_object_add(v, "history", record);
            }
            running = true;
            had_result = stale = false;
            measurements->data.clear();
            measurements->update();
            results_note->copy_label("Teste em execução. Leituras parciais.");
            set_status("Executando. Parar teste permanece disponível.");
            launch("run", bc_str(test, "id"), v, cal_active_id);
            json_object_put(v);
            refresh();
        }

        void filter_components() {
            components->data.clear();
            components->selected = -1;
            if (!profile)
                return;
            std::string query = filter->value();
            std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            auto *c = bc_get(profile, "components");
            json_object_object_foreach(c, k, v) {
                std::string key = k;
                bool passive = key.size() > 1 && (key[0] == 'R' || key[0] == 'C') &&
                               std::all_of(key.begin() + 1, key.end(), [](unsigned char ch) {
                                   return std::isdigit(ch);
                               });
                auto *extra = bc_get(profile, "editable_components");
                for (std::size_t i = 0; extra && i < json_object_array_length(extra); i++)
                    if (key == json_object_get_string(json_object_array_get_idx(extra, i)))
                        passive = true;
                if (!passive)
                    continue;
                std::string value = json_object_get_string(v),
                            desc = bc_str(bc_get(profile, "component_descriptions"), k),
                            hay = key + " " + value + " " + desc;
                std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char x) {
                    return static_cast<char>(std::tolower(x));
                });
                if (query.empty() || hay.find(query) != std::string::npos)
                    components->data.push_back({key, value, desc});
            }
            auto natural = [](const auto &a, const auto &b) {
                auto split = [](const std::string &s) {
                    std::size_t n = s.find_first_of("0123456789");
                    return std::make_pair(s.substr(0, n),
                                          n == std::string::npos ? 0 : std::atoi(s.c_str() + n));
                };
                return split(a[0]) < split(b[0]);
            };
            std::sort(components->data.begin(), components->data.end(), natural);
            edit_ref.clear();
            edit->value("");
            description->value("");
            edit_label->copy_label("Selecione um componente");
            components->update();
            layout();
            refresh();
        }

        void select_component(int row) {
            if (row < 0 || row >= static_cast<int>(components->data.size()) || busy)
                return;
            components->selected = row;
            components->redraw();
            auto &r = components->data[static_cast<std::size_t>(row)];
            edit_ref = r[0];
            edit_label->copy_label(("Valor montado em " + edit_ref).c_str());
            edit->value(r[1].c_str());
            description->value(r[2].c_str());
            refresh();
        }

        void edit_component() {
            if (edit_ref.empty() || busy)
                return;
            auto *mounted = bc_get(profile, "components");
            auto *descs = bc_get(profile, "component_descriptions");
            bool value_changed = std::string(bc_str(mounted, edit_ref.c_str())) != edit->value();
            bool description_changed =
                std::string(bc_str(descs, edit_ref.c_str())) != description->value();
            if (!value_changed && !description_changed)
                return;
            json_object_object_add(mounted, edit_ref.c_str(),
                                   json_object_new_string(edit->value()));
            if (!descs) {
                descs = json_object_new_object();
                json_object_object_add(profile, "component_descriptions", descs);
            }
            json_object_object_add(descs, edit_ref.c_str(),
                                   json_object_new_string(description->value()));
            for (auto &row : components->data)
                if (row[0] == edit_ref) {
                    row[1] = edit->value();
                    row[2] = description->value();
                }
            components->update();
            mount_changed = true;
            if (value_changed)
                update_preview();
            else
                refresh();
            set_status(
                value_changed
                    ? "Valor aplicado às previsões. Clique em Salvar valores para manter a "
                      "alteração."
                    : "Descrição aplicada. Clique em Salvar valores para manter a alteração.");
        }

        void install_package() {
            open_picker(false);
        }

        void export_package() {
            open_picker(true);
        }

        void capture(const fs::path &path) {
            Fl::check();
            Fl_Image_Surface surface(win.w(), win.h());
            Fl_Surface_Device::push_current(&surface);
            surface.draw(&win);
            Fl_Surface_Device::pop_current();
            std::unique_ptr<Fl_RGB_Image> image(surface.image());
            if (!image || image->d() != 3)
                throw std::runtime_error("Captura falhou");
            if (fl_write_png(path.u8string().c_str(), image.get()) != 0)
                throw std::runtime_error("Falha ao salvar PNG");
        }

        void wait_job() {
            for (int i = 0; busy && i < 3000; i++)
                Fl::wait(0.01);
            if (busy)
                throw std::runtime_error("Tempo limite da verificacao da interface");
        }
#ifndef _WIN32
#include "check_ui.inc"
#endif
    };

#include "window.inc"
}

int main(int argc, char **argv) {
    std::setlocale(LC_NUMERIC, "C");
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
    if (!FcInit()) {
        std::fputs("Falha ao inicializar as fontes.\n", stderr);
        return 1;
    }
#endif
    try {
        App app;
        app.win.show();
#ifndef _WIN32
        if (argc == 3 && std::string(argv[1]) == "--ui-check") {
            app.check_ui(argv[2]);
            return 0;
        }
#else
        (void)argc;
        (void)argv;
#endif
        return Fl::run();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Falha na interface: %s\n", e.what());
        return 1;
    }
}
