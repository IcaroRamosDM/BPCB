    Dialog *rigol_dialog = nullptr, *rigol_details_dialog = nullptr;
    Fl_Input *rigol_endpoint;
    Fl_Box *rigol_note;
    Fl_Text_Buffer rigol_details_text;
    Fl_Text_Display *rigol_details_view;
    Button *rigol_open, *rigol_probe, *rigol_close, *rigol_read, *rigol_details,
        *rigol_details_open, *rigol_refresh;
    bc_job *rigol_job = nullptr;
    json_object *rigol_acquisition = nullptr, *rigol_snapshot = nullptr;
    std::string rigol_requested, rigol_prompt_id;
    unsigned rigol_prompt_generation = 0;

    void show_rigol_details() {
        rigol_details_dialog->on_layout();
        skin(rigol_details_dialog);
        rigol_details_dialog->show();
        rigol_controls();
    }

    void render_rigol_details(json_object *data) {
        std::ostringstream text;
        text.precision(9);
        text << bc_str(data, "instrument") << "\nConsulta UTC: " << bc_str(data, "queried_at")
             << "\nEstado da captura: " << bc_str(data, "capture_state") << "\n\n";
        auto *channels = bc_get(data, "channels");
        for (size_t i = 0; i < json_object_array_length(channels); i++) {
            auto *c = json_object_array_get_idx(channels, i);
            text << "CH" << bc_num(c, "channel") << " · Exibição: " << bc_str(c, "display")
                 << " · Unidade: " << bc_str(c, "unit")
                 << "\nAcoplamento: " << bc_str(c, "coupling")
                 << " · Sonda configurada: " << bc_num(c, "probe_ratio")
                 << "x\nEscala: " << bc_num(c, "scale") << " · Offset: " << bc_num(c, "offset")
                 << "\n";
            const char *keys[] = {"VAVG", "VPP", "VMIN", "VMAX"},
                       *names[] = {"Média (VAVG)", "Pico a pico (Vpp)", "Mínimo", "Máximo"};
            for (size_t k = 0; k < 4; k++) {
                text << names[k] << ": ";
                auto *value = bc_get(c, keys[k]);
                if (value) {
                    text << json_object_get_double(value) << " " << bc_str(c, "unit");
                    if (!std::strcmp(bc_str(c, "unit"), "VOLT"))
                        text << " (" << json_object_get_double(value) * 1000 << " mV)";
                } else
                    text << "Indisponível";
                text << "\n";
            }
            text << "\n";
        }
        text << "Consultas sequenciais: em captura contínua, os valores podem mudar entre "
                "leituras. A razão de sonda é a configuração do Rigol; confira também a chave "
                "física. VAVG mede a média; VPP mede a variação pico a pico.";
        rigol_details_text.text(text.str().c_str());
    }

    void make_rigol() {
        rigol_dialog = new Dialog(720, 390, "BPCB · Rigol DHO800/900");
        rigol_dialog->minw = 720;
        rigol_dialog->minh = 390;
        rigol_dialog->size_range(720, 390);
        auto *title = label("Conexão com o Rigol", 23, true);
        rect(title, 24, 20, 672, 36);
        auto *help = label(
            "Informe tcp://IP:porta ou usbtmc:/dev/usbtmc0 (USB no Linux). Consulte a porta SCPI "
            "no instrumento. A leitura utiliza a captura e os cursores preparados por você.",
            14);
        rect(help, 24, 72, 672, 80);
        rigol_endpoint = new StableInput(24, 167, 672, 38);
        rigol_endpoint->box(input_box);
        rigol_endpoint->maximum_size(280);
        rigol_endpoint->value("tcp://");
        rigol_note = label("Nenhum instrumento consultado.", 13);
        rect(rigol_note, 24, 225, 672, 75);
        rigol_probe = new Button("Identificar instrumento", 1);
        rect(rigol_probe, 24, 325, 230, 38);
        rigol_probe->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->start_rigol("identify");
            },
            this);
        rigol_details_open = new Button("Medidas e canais");
        rect(rigol_details_open, 266, 325, 220, 38);
        rigol_details_open->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_rigol_details();
            },
            this);
        rigol_close = new Button("Fechar");
        rect(rigol_close, 566, 325, 130, 38);
        rigol_close->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->rigol_dialog->hide();
            },
            this);
        rigol_dialog->on_layout = [] {};
        rigol_dialog->on_close = [this] {
            rigol_dialog->hide();
        };
        rigol_dialog->end();
        rigol_details_dialog = new Dialog(820, 740, "BPCB · Medidas e canais do Rigol");
        rigol_details_dialog->minw = 700;
        rigol_details_dialog->minh = 600;
        rigol_details_dialog->size_range(700, 600);
        rigol_details_dialog->set_modal();
        rigol_details_view = display(rigol_details_text);
        rigol_details_text.text(
            "Configure a conexão no diálogo Rigol. Atualizar consulta CH1 e CH2 sem alterar a "
            "configuração do instrumento. Nenhuma medição foi consultada nesta sessão.");
        rigol_refresh = new Button("Atualizar leitura", 1);
        rigol_refresh->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->start_rigol("diagnostic");
            },
            this);
        auto *close = new Button("Fechar");
        close->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->rigol_details_dialog->hide();
            },
            this);
        rigol_details_dialog->on_layout = [this, close] {
            int w = rigol_details_dialog->w(), h = rigol_details_dialog->h();
            rect(rigol_details_view, 24, 24, w - 48, h - 108);
            rect(rigol_refresh, 24, h - 58, 200, 38);
            rect(close, w - 154, h - 58, 130, 38);
        };
        rigol_details_dialog->on_close = [this] {
            rigol_details_dialog->hide();
        };
        rigol_details_dialog->end();
    }

    void rigol_controls() {
        if (!rigol_dialog)
            return;
        set_enabled(rigol_probe, !rigol_job && !running);
        set_enabled(rigol_endpoint, !rigol_job && !running);
        set_enabled(rigol_open, !busy);
        set_enabled(rigol_read, !rigol_job && running);
        set_enabled(rigol_details, !rigol_job);
        set_enabled(rigol_refresh, !rigol_job && (!running || confirmation->shown()));
    }

    void start_rigol(const std::string &field) {
        if (rigol_job)
            return;
        rigol_prompt_id = confirmation_id;
        rigol_prompt_generation = confirmation_generation;
        rigol_requested = field;
        rigol_job = bc_job_start("rigol", root.c_str(), "", rigol_endpoint->value(), field.c_str(),
                                 nullptr);
        if (!rigol_job) {
            rigol_note->copy_label("Não foi possível iniciar a leitura.");
            return;
        }
        if (field == "identify")
            rigol_note->copy_label("Consultando identificação…");
        else if (field == "diagnostic")
            rigol_details_text.text("Consultando medidas e configurações dos canais…");
        else
            confirmation_error->copy_label("Consultando o Rigol…");
        watch_job(
            rigol_job,
            [](int, void *v) {
                static_cast<App *>(v)->poll_rigol();
            },
            this);
        rigol_controls();
    }

    void poll_rigol() {
        if (!rigol_job)
            return;
        bc_job_acknowledge(rigol_job);
        json_object *event, *end = nullptr;
        while ((event = bc_job_next(rigol_job))) {
            if (!std::strcmp(bc_str(event, "type"), "finished")) {
                if (end)
                    json_object_put(end);
                end = json_object_get(event);
            }
            json_object_put(event);
        }
        if (!end)
            return;
        unwatch_job(rigol_job);
        bc_job_free(rigol_job);
        rigol_job = nullptr;
        bool ok = !std::strcmp(bc_str(end, "status"), "ok");
        auto *data = bc_get(end, "data");
        support_log("rigol", end);
        if (ok) {
            if (rigol_snapshot)
                json_object_put(rigol_snapshot);
            rigol_snapshot = bc_clone(data);
        }
        if (rigol_requested == "identify")
            rigol_note->copy_label(ok ? bc_str(data, "instrument") : bc_str(end, "message"));
        else if (rigol_requested == "diagnostic") {
            if (ok)
                render_rigol_details(data);
            else
                rigol_details_text.text(bc_str(end, "message"));
        } else if (running && confirmation->shown() && confirmation_id == rigol_prompt_id &&
                   confirmation_generation == rigol_prompt_generation) {
            if (ok) {
                double displayed = bc_num(data, "value");
                const char *unit = confirmation_unit->text();
                if (unit && !std::strcmp(unit, "mV"))
                    displayed *= 1000;
                else if (unit && !std::strcmp(unit, "us"))
                    displayed *= 1000;
                else if (unit && !std::strcmp(unit, "s"))
                    displayed /= 1000;
                char number[64];
                std::snprintf(number, sizeof number, "%.12g", displayed);
                confirmation_value->value(number);
                if (rigol_acquisition)
                    json_object_put(rigol_acquisition);
                rigol_acquisition = bc_clone(data);
                json_object_object_add(rigol_acquisition, "instrument_request",
                                       json_object_new_string(rigol_requested.c_str()));
                json_object_object_add(rigol_acquisition, "field",
                                       json_object_new_string(confirmation_id.c_str()));
                confirmation_error->copy_label(
                    "Leitura recebida. Confira a captura e confirme o valor.");
            } else
                confirmation_error->copy_label(bc_str(end, "message"));
        }
        json_object_put(end);
        rigol_controls();
    }
