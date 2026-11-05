    // Parte visual do seletor; leitura de pastas executada pelo nucleo C.
    Dialog *picker = nullptr;
    Button *picker_back;
    Fl_Box *picker_title, *picker_description, *picker_path_label, *picker_name_label, *picker_note,
        *picker_card, *picker_summary;
    CommitInput *picker_location, *picker_filename;
    Fl_Box *picker_places_title, *picker_places_card;
    std::vector<std::string> picker_history;
    std::string picker_layout_stage;
    bool browse_remember = true;
    LocationMenu *picker_places;
    std::vector<std::string> location_paths;
    int picker_origin_x = 0, picker_origin_y = 0;
    Button *picker_up, *picker_home, *picker_project, *picker_build, *picker_go, *picker_cancel,
        *picker_accept;
    Grid *picker_files;

    struct FileEntry {
        std::string name;
        bool directory;
    };

    std::vector<FileEntry> picker_entries;
    bc_job *browse_job = nullptr;
    std::string picker_directory, browse_path, browse_pending, picker_stage = "browse",
                                                               export_destination;
    bool picker_export = false, picker_from_home = true, picker_selected_directory = false;

    std::string home_directory() const {
        const char *h = getenv("HOME");
        return h ? h : "/";
    }

    void make_picker() {
        picker = new Dialog(960, 680, "Instalar bancada");
        picker->set_modal();
        picker->on_layout = [this] {
            picker_layout();
        };
        picker->on_close = [this] {
            cancel_picker();
        };
        picker->callback(
            [](Fl_Widget *, void *data) {
                static_cast<App *>(data)->cancel_picker();
            },
            this);
        picker_back = new Button("←");
        picker_back->labelsize(23);
        picker_back->callback(
            [](Fl_Widget *, void *data) {
                static_cast<App *>(data)->picker_previous();
            },
            this);
        picker_title = label("", 27, true);
        picker_description = label("", 14);
        picker_path_label = label("PASTA", 12, true);
        picker_location = new CommitInput(0, 0, 100, 38);
        picker_location->box(input_box);
        picker_location->textsize(14);
        picker_location->maximum_size(4095);
        picker_location->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->browse_directory(a->picker_location->value());
            },
            this);
        picker_places = new LocationMenu();
        picker_places->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                int i = a->picker_places->value();
                if (i >= 0 && i < static_cast<int>(a->location_paths.size()))
                    a->browse_directory(a->location_paths[static_cast<std::size_t>(i)]);
            },
            this);
        picker_go = new Button("Ir");
        picker_go->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->browse_directory(a->picker_location->value());
            },
            this);
        picker_up = new Button("↑");
        picker_up->labelsize(23);
        picker_places_card = card();
        picker_places_title = label("LOCAIS", 12, true);
        picker_home = new Button("Pasta pessoal");
        picker_project = new Button("Pasta do aplicativo");
        picker_build = new Button("Pacotes gerados");
        picker_up->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->browse_directory(fs::path(a->picker_directory).parent_path().string());
            },
            this);
        picker_home->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->browse_directory(a->home_directory());
            },
            this);
        picker_project->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->browse_directory(fs::path(a->bindir).parent_path().string());
            },
            this);
        picker_build->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->browse_directory(a->bindir);
            },
            this);
        picker_files = new Grid();
        picker_files->headers = {"NOME", "TIPO"};
        picker_files->name_first = true;
        picker_files->update();
        picker_files->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (a->picker_files->callback_context() != Fl_Table::CONTEXT_CELL ||
                    Fl::event() != FL_RELEASE)
                    return;
                a->pick_row(a->picker_files->callback_row());
                if (Fl::event_clicks())
                    a->accept_picker();
            },
            this);
        picker_note = label("", 13);
        picker_name_label = label("ARQUIVO", 12, true);
        picker_filename = new CommitInput(0, 0, 100, 38);
        picker_filename->box(input_box);
        picker_filename->textsize(14);
        picker_filename->maximum_size(4095);
        picker_filename->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->picker_selected_directory = false;
                a->picker_files->selected = -1;
                a->picker_files->redraw();
                a->picker_controls();
            },
            this);
        picker_card = card();
        picker_summary = label("", 16);
        picker_summary->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        picker_cancel = new Button("Cancelar");
        picker_cancel->shortcut(FL_Escape);
        picker_accept = new Button("Abrir pacote", 1);
        picker_cancel->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->cancel_picker();
            },
            this);
        picker_accept->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->accept_picker();
            },
            this);
        picker->end();
        picker->hide();
    }

    bool picking() const {
        return picker && picker->visible();
    }

    void picker_layout() {
        if (!picking())
            return;
        const int w = picker->w(), h = picker->h(), x = 24, width = w - 48;
        rect(picker_back, x, 20, 38, 38);
        rect(picker_title, x + 52, 20, width - 52, 38);
        rect(picker_description, x, 67, width, 40);
        rect(picker_path_label, x, 116, 54, 38);
        rect(picker_up, x + 56, 116, 40, 38);
        rect(picker_location, x + 106, 116, width - 222, 38);
        rect(picker_places, x + width - 108, 116, 38, 38);
        rect(picker_go, x + width - 60, 116, 60, 38);
        rect(picker_places_card, x, 172, 186, h - 326);
        rect(picker_places_title, x + 14, 186, 158, 24);
        rect(picker_home, x + 10, 220, 166, 38);
        rect(picker_build, x + 10, 267, 166, 38);
        rect(picker_project, x + 10, 314, 166, 38);
        rect(picker_files, x + 202, 172, width - 202, h - 326);
        rect(picker_note, x + 202, h - 146, width - 202, 30);
        rect(picker_name_label, x, h - 104, 116, 38);
        rect(picker_filename, x + 122, h - 104, width - 122, 38);
        rect(picker_card, x, 116, width, h - 193);
        rect(picker_summary, x + 24, 140, width - 48, h - 240);
        rect(picker_cancel, w - 334, h - 53, 126, 38);
        rect(picker_accept, w - 194, h - 53, 170, 38);
        bool listing = picker_stage == "browse";
        for (Fl_Widget *item : std::vector<Fl_Widget *>{
                 picker_path_label, picker_location, picker_places, picker_go, picker_up,
                 picker_home, picker_project, picker_build, picker_places_card, picker_places_title,
                 picker_files, picker_note, picker_name_label, picker_filename})
            set_visible(item, listing);
        set_visible(picker_card, !listing);
        set_visible(picker_summary, !listing);
        picker_layout_stage = picker_stage;
    }

    void picker_previous() {
        if (busy) {
            if (operation == "inspect")
                cancel_picker();
            return;
        }
        if (picker_stage != "browse") {
            picker_stage = "browse";
            picker_controls();
            return;
        }
        if (browse_job)
            return;
        if (picker_history.empty()) {
            cancel_picker();
            return;
        }
        auto path = picker_history.back();
        picker_history.pop_back();
        browse_directory(path, false);
    }

    void update_locations(json_object *locations) {
        picker_places->clear();
        location_paths.clear();
        auto add = [&](const std::string &name, const std::string &path) {
            if (path.empty() || std::find(location_paths.begin(), location_paths.end(), path) !=
                                    location_paths.end())
                return;
            picker_places->add(name.c_str());
            location_paths.push_back(path);
        };
        for (std::size_t i = 0; i < json_object_array_length(locations); i++) {
            auto *item = json_object_array_get_idx(locations, i);
            add(bc_str(item, "label"), bc_str(item, "path"));
        }
        add("Pasta do aplicativo", fs::path(bindir).parent_path().string());
        add("Pacotes gerados", bindir);
        fs::path parent = picker_directory;
        while (!parent.empty()) {
            add(parent.string(), parent.string());
            auto next = parent.parent_path();
            if (parent == next)
                break;
            parent = next;
        }
        picker_places->value(-1);
    }

    void picker_controls() {
        if (!picker)
            return;
        if (!picking()) {
            picker_cancel->hide();
            picker_accept->hide();
            return;
        }
        picker_cancel->show();
        picker_accept->show();
        auto enable = set_enabled;
        const bool idle = !busy && !browse_job && browse_pending.empty();
        for (Fl_Widget *w :
             std::vector<Fl_Widget *>{picker_location, picker_places, picker_go, picker_up,
                                      picker_home, picker_project, picker_build})
            enable(w, !busy);
        enable(picker_files, idle);
        enable(picker_filename, idle);
        enable(picker_accept, idle && (picker_stage != "browse" || picker_selected_directory ||
                                       *picker_filename->value()));
        enable(picker_cancel, !busy || operation == "inspect");
        set_label(picker_accept, picker_stage == "confirm"     ? "Instalar bancada"
                                 : picker_stage == "overwrite" ? "Substituir arquivo"
                                 : picker_selected_directory   ? "Abrir pasta"
                                 : picker_export               ? "Salvar pacote"
                                                               : "Abrir pacote");
        set_label(
            picker_description,
            picker_stage == "confirm"     ? "2 de 2 · Confira a placa e clique em Instalar bancada."
            : picker_stage == "overwrite" ? "O destino já contém um arquivo com o mesmo nome."
            : picker_export ? "Escolha a pasta, informe o nome e clique em Salvar pacote."
                            : "1 de 2 · Escolha o arquivo .bancada e clique em Abrir pacote.");
        if (picker_layout_stage != picker_stage)
            picker_layout();
    }

    void open_picker(bool exporting) {
        if (busy || picking() || (exporting && !profile))
            return;
        picker_export = exporting;
        picker_from_home = home->visible();
        picker_stage = "browse";
        picker_history.clear();
        picker_origin_x = viewport->xposition();
        picker_origin_y = viewport->yposition();
        picker->copy_label(exporting ? "BPCB · Exportar bancada" : "BPCB · Instalar bancada");
        picker->position(win.x() + std::max(0, (win.w() - picker->w()) / 2),
                         win.y() + std::max(0, (win.h() - picker->h()) / 2));
        picker->show();
        picker_title->copy_label(exporting ? "Exportar bancada" : "Instalar bancada");
        picker_description->copy_label(exporting
                                           ? "Escolha a pasta e o nome do arquivo. O pacote reúne "
                                             "a montagem, os testes, as instruções e os documentos."
                                           : "Selecione o arquivo .bancada recebido. Você poderá "
                                             "conferir a placa e a versão antes de instalar.");
        picker_filename->value(exporting ? (board_id + ".bancada").c_str() : "");
        picker_selected_directory = false;
        set_label(picker_name_label, exporting ? "Salvar como" : "Nome do arquivo");
        browse_directory(picker_directory.empty() ? home_directory() : picker_directory);
        set_status(exporting ? "Escolha onde salvar o pacote."
                             : "Escolha uma pasta e selecione o pacote.");
        skin(picker);
        picker_layout();
        refresh();
    }

    void restore_picker_origin() {
        Popover::dismiss();
        picker->hide();
        refresh();
    }

    void cancel_picker() {
        if (busy && operation != "inspect")
            return;
        if (browse_job)
            bc_job_cancel(browse_job);
        browse_pending.clear();
        if (busy)
            bc_job_cancel(job);
        restore_picker_origin();
        set_status("Seleção de pacote cancelada.");
    }

    void browse_directory(const std::string &requested, bool remember = true) {
        if (busy || !picking())
            return;
        Popover::dismiss();
        fs::path path = requested.empty() ? picker_directory : requested;
        if (path.empty())
            path = home_directory();
        if (path.is_relative())
            path = fs::path(picker_directory.empty() ? home_directory() : picker_directory) / path;
        const auto destination = path.lexically_normal().string();
        if (browse_job && (destination == browse_pending ||
                           (browse_pending.empty() && destination == browse_path)))
            return;
        browse_pending = destination;
        picker_stage = "browse";
        browse_remember = remember;

        picker_note->copy_label("Carregando pastas e arquivos .bancada…");
        picker_selected_directory = false;
        picker_files->selected = -1;
        if (!picker_export)
            picker_filename->value("");
        if (browse_job) {
            bc_job_cancel(browse_job);
            picker_controls();
            return;
        }
        begin_browse();
    }

    void begin_browse() {
        browse_path = std::exchange(browse_pending, {});
        browse_job = bc_job_start("browse", root.c_str(), "", browse_path.c_str(), "", nullptr);
        if (!browse_job) {
            picker_note->copy_label("Não foi possível carregar a pasta.");
            picker_controls();
            return;
        }
        picker_controls();
        watch_job(browse_job, browse_timer, this);
    }

    static void browse_timer(int, void *v) {
        static_cast<App *>(v)->poll_browse();
    }

    void poll_browse() {
        if (!browse_job)
            return;
        bc_job_acknowledge(browse_job);
        if (!bc_job_finished(browse_job))
            return;
        json_object *event = nullptr, *end = nullptr;
        while ((event = bc_job_next(browse_job))) {
            if (!strcmp(bc_str(event, "type"), "finished")) {
                if (end)
                    json_object_put(end);
                end = json_object_get(event);
            }
            json_object_put(event);
        }
        unwatch_job(browse_job);
        bc_job_free(browse_job);
        browse_job = nullptr;
        if (picking() && browse_pending.empty()) {
            if (end && !strcmp(bc_str(end, "status"), "ok")) {
                auto *data = bc_get(end, "data"), *entries = bc_get(data, "entries");
                std::string previous = picker_directory;
                picker_directory = bc_str(data, "path");
                if (browse_remember && !previous.empty() && previous != picker_directory)
                    picker_history.push_back(previous);
                picker_location->value(picker_directory.c_str());
                update_locations(bc_get(data, "locations"));
                picker_entries.clear();
                picker_files->data.clear();
                picker_files->selected = -1;
                for (std::size_t i = 0; i < json_object_array_length(entries); i++) {
                    auto *entry = json_object_array_get_idx(entries, i);
                    bool directory = json_object_get_boolean(bc_get(entry, "directory"));
                    std::string name = bc_str(entry, "name");
                    picker_entries.push_back({name, directory});
                    picker_files->data.push_back({name, directory ? "Pasta" : "Bancada"});
                }
                picker_files->update();
                picker_files->reset_scroll();
                picker_note->copy_label(
                    json_object_get_boolean(bc_get(data, "truncated"))
                        ? "Lista limitada a 20.000 itens. Informe uma subpasta no campo Pasta."
                    : picker_entries.empty() ? "Nenhuma subpasta ou arquivo .bancada nesta pasta."
                                             : "Pastas: abra com dois cliques. Arquivos: selecione "
                                               "e use o botão abaixo.");
            } else if (end && !strcmp(bc_str(end, "status"), "error")) {
                picker_note->copy_label(bc_str(end, "message"));
                set_status(bc_str(end, "message"));
                picker_location->value(picker_directory.c_str());
            }
        }
        if (end)
            json_object_put(end);
        if (picking() && !browse_pending.empty())
            begin_browse();
        else
            picker_controls();
    }

    void pick_row(int row) {
        if (busy || browse_job || row < 0 || row >= static_cast<int>(picker_entries.size()))
            return;
        picker_files->selected = row;
        picker_files->redraw();
        const auto &entry = picker_entries[static_cast<std::size_t>(row)];
        picker_selected_directory = entry.directory;
        if (!entry.directory)
            picker_filename->value(entry.name.c_str());
        else if (!picker_export)
            picker_filename->value("");
        picker_controls();
    }

    void accept_picker() {
        if (!picking() || busy || browse_job || !browse_pending.empty())
            return;
        if (picker_stage == "confirm") {
            launch("install", package_path, nullptr, pending_digest);
            return;
        }
        if (picker_stage == "overwrite") {
            export_selected();
            return;
        }
        if (picker_selected_directory) {
            int row = picker_files->selected;
            if (row >= 0 && row < static_cast<int>(picker_entries.size()))
                browse_directory((fs::path(picker_directory) /
                                  picker_entries[static_cast<std::size_t>(row)].name)
                                     .string());
            return;
        }
        const char *name = picker_filename->value();
        if (!*name)
            return;
        fs::path path(name);
        if (path.is_relative())
            path = fs::path(picker_directory) / path;
        path = path.lexically_normal();
        if (picker_export) {
            if (path.extension() != ".bancada")
                path += ".bancada";
            export_destination = path.string();
            std::error_code ec;
            bool exists = fs::exists(path, ec);
            if (ec) {
                picker_note->copy_label(ec.message().c_str());
                return;
            }
            if (exists) {
                picker_stage = "overwrite";
                picker_summary->copy_label(
                    ("Já existe um arquivo com este nome:\n\n" + export_destination +
                     "\n\nSubstituir este arquivo pelo pacote da bancada atual?")
                        .c_str());
                picker_controls();
                picker->redraw();
            } else
                export_selected();
        } else {
            if (path.extension() != ".bancada") {
                picker_note->copy_label("Selecione um arquivo com a extensão .bancada.");
                set_status(picker_note->label());
                return;
            }
            package_path = path.string();
            launch("inspect", package_path);
        }
    }

    void export_selected() {
        auto *v = values();
        launch("export", export_destination, v);
        json_object_put(v);
    }

    void show_package_review(json_object *data) {
        if (!picking()) {
            set_status("Instalação cancelada.");
            return;
        }
        if (pending_profile)
            json_object_put(pending_profile);
        pending_profile = bc_clone(data);
        picker_stage = "confirm";
        pending_digest = bc_str(data, "package_digest");
        std::string message =
            std::string(bc_str(data, "pcb")) + " · " + bc_str(data, "revision") + "\nPacote " +
            bc_str(data, "version") + "\nAutor: " + bc_str(data, "author") +
            "\n\nO pacote contém rotinas executáveis. Instale apenas arquivos de origem "
            "conhecida.\n\nA instalação adiciona a bancada ao programa e não aciona a placa.";
        picker_summary->copy_label(message.c_str());
        picker_controls();
        set_status("Pacote verificado. Confira os dados antes de instalar.");
        picker->redraw();
    }
