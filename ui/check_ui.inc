    void qa_idle_report(const char *name) {
        Fl::focus(nullptr);
        auto wait_for = [](double seconds) {
            auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
            while (std::chrono::steady_clock::now() < end)
                Fl::wait(0.02);
        };
        wait_for(0.15);
        auto before = win.draw_count, resize_before = win.resize_count;
        wait_for(0.5);
        if (win.draw_count - before > 1 || win.resize_count - resize_before > 1)
            throw std::runtime_error("Interface redesenhando em repouso");
        std::printf("IDLE %s: %lu desenhos, %lu chamadas de resize em 0.5 s\n", name,
                    win.draw_count - before, win.resize_count - resize_before);
    }

    // Regressao grafica executada somente com --ui-check em Xvfb.
    struct QaCursorMonitor {
        void *library = nullptr;
        Display *display = nullptr;
        int event_base = 0;

        QaCursorMonitor() {
            library = dlopen("libXfixes.so.3", RTLD_NOW);
            if (!library)
                throw std::runtime_error("Xfixes necessario para a regressao de cursor");
            auto query = reinterpret_cast<Bool (*)(Display *, int *, int *)>(
                dlsym(library, "XFixesQueryExtension"));
            auto select = reinterpret_cast<void (*)(Display *, ::Window, unsigned long)>(
                dlsym(library, "XFixesSelectCursorInput"));
            display = XOpenDisplay(nullptr);
            int error_base = 0;
            if (!display || !query || !select || !query(display, &event_base, &error_base))
                throw std::runtime_error("Observador de cursor indisponivel");
            select(display, DefaultRootWindow(display), XFixesDisplayCursorNotifyMask);
            drain();
        }

        unsigned drain() {
            XSync(display, False);
            unsigned count = 0;
            while (XPending(display)) {
                XEvent e{};
                XNextEvent(display, &e);
                if (e.type == event_base + XFixesCursorNotify)
                    ++count;
            }
            return count;
        }

        ~QaCursorMonitor() {
            if (display)
                XCloseDisplay(display);
            if (library)
                dlclose(library);
        }
    };

    std::vector<unsigned long> qa_pixels(Fl_Window *target, int x, int y, int width, int height) {
        Fl::check();
        XSync(fl_display, False);
        const double scale = Fl::screen_scale(target->screen_num());
        int px = static_cast<int>(x * scale), py = static_cast<int>(y * scale);
        int pw = static_cast<int>(width * scale), ph = static_cast<int>(height * scale);
        XImage *shot = XGetImage(fl_display, fl_xid(target), px, py, static_cast<unsigned>(pw),
                                 static_cast<unsigned>(ph), AllPlanes, ZPixmap);
        if (!shot)
            throw std::runtime_error("Falha na leitura dos pixels reais");
        std::vector<unsigned long> pixels;
        pixels.reserve(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph));
        for (int yy = 0; yy < ph; ++yy)
            for (int xx = 0; xx < pw; ++xx)
                pixels.push_back(XGetPixel(shot, xx, yy));
        XDestroyImage(shot);
        return pixels;
    }

    void qa_grid_repaint(Grid *grid, const char *name) {
        if (grid->data.size() < 3)
            throw std::runtime_error("Regressao exige tres linhas");
        grid->row_position(0);
        Fl::check();
        Fl::focus(nullptr);
        Fl::check();
        int hx, hy, hw, hh, x, y, w, h;
        grid->header_rect(hx, hy, hw, hh);
        grid->cell_rect(2, x, y, w, h);
        auto header = qa_pixels(grid->top_window(), hx, hy, hw, hh);
        auto other = qa_pixels(grid->top_window(), x, y, w, h);
        for (int row : {0, 1, 0, 1}) {
            int ax, ay, aw, ah;
            grid->cell_rect(row, ax, ay, aw, ah);
            auto native_id = std::to_string(static_cast<unsigned long>(fl_xid(grid->top_window())));
            qa_xdotool({"xdotool", "windowfocus", native_id, "mousemove", "--window", native_id,
                        std::to_string(ax + 30), std::to_string(ay + ah / 2), "mousedown", "1"});
            if (qa_pixels(grid->top_window(), hx, hy, hw, hh) != header)
                throw std::runtime_error(std::string("Cabecalho apagou no mouse-down: ") + name);
            if (qa_pixels(grid->top_window(), x, y, w, h) != other)
                throw std::runtime_error(std::string("Celula apagou no mouse-down: ") + name);
            qa_xdotool({"xdotool", "mouseup", "1"});
            if (qa_pixels(grid->top_window(), hx, hy, hw, hh) != header)
                throw std::runtime_error(std::string("Cabecalho apagou: ") + name);
            if (qa_pixels(grid->top_window(), x, y, w, h) != other)
                throw std::runtime_error(std::string("Celula nao selecionada apagou: ") + name);
        }
        std::printf(
            "PIXELS %s: cabecalho e linha nao selecionada preservados apos quatro cliques\n", name);
    }

    void qa_field_moves(CommitInput *input) {
        auto *window = input->top_window();
        auto id = std::to_string(static_cast<unsigned long>(fl_xid(window)));
        qa_xdotool({"xdotool", "mousemove", "--window", id, std::to_string(input->x() + 20),
                    std::to_string(input->y() + 18)});
        Fl::check();
        auto draws = win.draw_count, changes = win.corner_cursor_changes, commits = input->commits;
        QaCursorMonitor cursor_events;
        cursor_events.drain();
        std::vector<std::string> args = {"xdotool"};
        for (int n = 0; n < 30; n++) {
            args.insert(args.end(),
                        {"mousemove", "--window", id, std::to_string(input->x() + 20 + n * 2),
                         std::to_string(input->y() + 18)});
        }
        qa_xdotool(args);
        if (win.corner_cursor_changes != changes || input->commits != commits ||
            win.draw_count != draws || cursor_events.drain())
            throw std::runtime_error("Mover no campo redesenhou, alterou cursor ou aplicou valor");
        std::puts("CURSOR: 30 movimentos no campo sem troca de cursor, redraw ou recalculo");
    }

    void qa_dialog_idle(Dialog *dialog, const char *name) {
        Fl::check();
        auto draws = dialog->draw_count;
        for (int i = 0; i < 20; i++)
            Fl::wait(0.02);
        if (dialog->draw_count != draws)
            throw std::runtime_error(std::string("Dialogo redesenhando em repouso: ") + name);
        std::printf("IDLE %s: nenhum redesenho\n", name);
    }

    void qa_xdotool(std::vector<std::string> args) {
        std::vector<char *> argv;
        for (auto &arg : args)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        pid_t pid = -1;
        int rc = posix_spawnp(&pid, "xdotool", nullptr, nullptr, argv.data(), environ);
        if (rc)
            throw std::runtime_error("xdotool necessario para a verificacao de cliques");
        int status = 0;
        for (int i = 0; i < 500; i++) {
            pid_t ended = waitpid(pid, &status, WNOHANG);
            if (ended == pid) {
                Fl::wait(0.025);
                if (!WIFEXITED(status) || WEXITSTATUS(status))
                    throw std::runtime_error("Falha no evento de mouse");
                return;
            }
            if (ended < 0)
                throw std::runtime_error("Falha ao aguardar evento de mouse");
            Fl::wait(0.01);
        }
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        throw std::runtime_error("Evento de mouse excedeu o tempo");
    }

    void qa_mouse(int x, int y, Fl_Window *target = nullptr) {
        if (!target)
            target = &win;
        Fl::check();
        auto id = std::to_string(static_cast<unsigned long>(fl_xid(target)));
        qa_xdotool({"xdotool", "windowfocus", id, "mousemove", "--window", id, std::to_string(x),
                    std::to_string(y), "click", "1"});
    }

    void qa_click(Fl_Widget *widget) {
        if (widget->parent() == tests) {
            if (widget->y() < tests->y() || widget->y() + widget->h() > tests->y() + tests->h()) {
                int position = tests->yposition() + widget->y() - tests->y();
                tests->scroll_to(0, std::max(0, position));
                Fl::check();
            }
        }
        if (!widget->visible_r() || !widget->active_r())
            throw std::runtime_error(std::string("Botao indisponivel: ") +
                                     (widget->label() ? widget->label() : "campo"));
        qa_mouse(widget->x() + widget->w() / 2, widget->y() + widget->h() / 2,
                 widget->top_window());
    }

    void qa_capture(Fl_Window *window, const fs::path &path) {
        // XGetImage devolve pixels indefinidos em áreas encobertas. Aguarde os
        // eventos reais de exposição antes de ler a janela solicitada.
        XRaiseWindow(fl_display, fl_xid(window));
        XSync(fl_display, False);
        auto settled = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (std::chrono::steady_clock::now() < settled)
            Fl::wait(0.01);
        Fl::check();
        XSync(fl_display, False);
        XWindowAttributes info{};
        XGetWindowAttributes(fl_display, fl_xid(window), &info);
        XImage *native =
            XGetImage(fl_display, fl_xid(window), 0, 0, static_cast<unsigned>(info.width),
                      static_cast<unsigned>(info.height), AllPlanes, ZPixmap);
        if (!native)
            throw std::runtime_error("Captura real falhou");
        std::vector<unsigned char> bytes(static_cast<std::size_t>(info.width) *
                                         static_cast<std::size_t>(info.height) * 3);
        auto component = [](unsigned long pixel, unsigned long mask) {
            if (!mask)
                return static_cast<unsigned char>(0);
            while (!(mask & 1)) {
                mask >>= 1;
                pixel >>= 1;
            }
            return static_cast<unsigned char>((pixel & mask) * 255 / mask);
        };
        std::size_t i = 0;
        for (int y = 0; y < info.height; y++)
            for (int x = 0; x < info.width; x++) {
                unsigned long pixel = XGetPixel(native, x, y);
                bytes[i++] = component(pixel, native->red_mask);
                bytes[i++] = component(pixel, native->green_mask);
                bytes[i++] = component(pixel, native->blue_mask);
            }
        XDestroyImage(native);
        Fl_RGB_Image shot(bytes.data(), info.width, info.height, 3);
        if (fl_write_png(path.c_str(), &shot))
            throw std::runtime_error("Falha ao salvar captura real");
    }

    void wait_browse() {
        for (int i = 0; browse_job && i < 1000; i++)
            Fl::wait(0.01);
        if (browse_job)
            throw std::runtime_error("Listagem nao terminou");
    }

    void qa_select_file(const std::string &name) {
        auto entry =
            std::find_if(picker_entries.begin(), picker_entries.end(), [&](const FileEntry &e) {
                return e.name == name;
            });
        if (entry == picker_entries.end())
            throw std::runtime_error("Arquivo nao listado: " + name);
        int row = static_cast<int>(entry - picker_entries.begin());
        picker_files->row_position(row);
        Fl::check();
        int x, y, w, h;
        picker_files->cell_rect(row, x, y, w, h);
        qa_mouse(x + 36, y + h / 2, picker);
        if (picker_files->selected != row)
            throw std::runtime_error("Clique nao selecionou o arquivo");
    }

    void qa_install(const std::string &name) {
        qa_click(install);
        wait_browse();
        qa_click(picker_build);
        wait_browse();
        qa_select_file(name);
        qa_click(picker_accept);
        wait_job();
        if (picker_stage != "confirm")
            throw std::runtime_error("Revisao do pacote nao abriu");
        qa_click(picker_accept);
        wait_job();
        if (picking())
            throw std::runtime_error("Instalacao nao fechou o dialogo");
    }

    void check_ui(const std::string &dir) {
        setvbuf(stdout, nullptr, _IOLBF, 0);
        if (!getenv("BC_DOCUMENT_LOG"))
            throw std::runtime_error("Execute tests/ui_test.sh");
        fs::create_directories(dir);
        wait_job();
        if (dark)
            toggle_theme();
        if (json_object_array_length(boards) || open_board->active_r())
            throw std::runtime_error("Início deveria estar vazio");
        qa_capture(&win, fs::path(dir) / "inicio-vazio.png");
        qa_idle_report("início vazio");
        qa_install("Demonstracao-V1.bancada");
        if (json_object_array_length(boards) != 1)
            throw std::runtime_error("Demonstração não instalada");
        qa_click(open_board);
        if (record_enabled->value())
            throw std::runtime_error("Histórico habilitado sem escolha do usuário");
        qa_click(start);
        wait_job();
        if (measurements->data.empty() || !current_record)
            throw std::runtime_error("Simulação sem resultado em memória");
        qa_capture(&win, fs::path(dir) / "demonstracao-claro.png");
        qa_idle_report("resultado da demonstração");
        record_board->value("PCB-DEMO-1");
        record_device->value("Carga simulada");
        qa_click(record_enabled);
        qa_click(start);
        wait_job();
        show_history();
        wait_job();
        if (history_ids.size() != 1)
            throw std::runtime_error("Execução sem opt-in foi registrada");
        qa_capture(history_dialog, fs::path(dir) / "historico-opcional.png");
        history_dialog->hide();
        qa_click(record_enabled);
        qa_click(start);
        wait_job();
        show_history();
        wait_job();
        if (json_object_array_length(history_items) != 1)
            throw std::runtime_error("Opt-out não respeitado");
        history_dialog->hide();
        for (int tab = 0; tab < 6; tab++) {
            show_lab(tab);
            qa_capture(lx_dialog, fs::path(dir) / ("laboratorio-" + std::to_string(tab) + ".png"));
            lx_dialog->hide();
        }
        show_lab(5);
        lx_dialog->resize(20, 20, 1020, 700);
        lx_dialog->on_layout();
        qa_capture(lx_dialog, fs::path(dir) / "laboratorio-minimo.png");
        if (lx_stop->y() + lx_stop->h() > lx_dialog->h())
            throw std::runtime_error("Controles fora da altura mínima");
        lx_dialog->hide();
        toggle_theme();
        qa_capture(&win, fs::path(dir) / "demonstracao-escuro.png");
        std::puts("PUBLIC_UI_OK: demonstração, histórico opcional e laboratório");
        win.hide();
    }
