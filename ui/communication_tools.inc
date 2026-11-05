    Dialog *ct_dialog = nullptr;
    Fl_Tabs *ct_tabs = nullptr;
    Fl_Group *ct_uart = nullptr, *ct_profiles = nullptr, *ct_i2c = nullptr, *ct_jtag = nullptr;
    Choice *ct_format, *ct_delimiter, *ct_profile, *ct_map, *ct_register, *ct_target, *ct_action;
    StableInput *ct_text, *ct_seconds, *ct_name, *ct_first, *ct_last, *ct_address, *ct_value,
        *ct_exe, *ct_scripts, *ct_firmware, *ct_offset, *ct_speed, *ct_serial;
    Fl_Check_Button *ct_trust;
    Button *ct_start, *ct_send, *ct_stop, *ct_capture_export, *ct_profile_save, *ct_profile_load,
        *ct_profile_delete, *ct_scan, *ct_read, *ct_write, *ct_map_import, *ct_identify,
        *ct_preview, *ct_execute, *ct_target_import;
    Fl_Box *ct_note;
    Fl_Text_Buffer ct_log, ct_preview_buffer;
    Fl_Text_Display *ct_log_view, *ct_preview_view;
    json_object *ct_profile_rows = nullptr, *ct_maps = nullptr, *ct_targets = nullptr;
    CaptureBuffer ct_capture;
    std::string ct_reviewed, ct_sha;
    bool ct_exchange_pending = false;

    std::vector<Fl_Widget *> ct_editable;

    void ct_message(const char *s) {
        ct_note->copy_label(s);
        ct_note->redraw();
    }

    void ct_append(const std::string &s) {
        ct_log.append(s.c_str());
        if (ct_log.length() > 131072) {
            int drop = ct_log.length() - 98304;
            ct_log.remove(0, drop);
            ct_log.insert(0, "[Início do texto removido: limite de memória. Exporte a captura para "
                             "conservar os eventos disponíveis.]\n");
        }
        ct_log_view->insert_position(ct_log.length());
        ct_log_view->show_insert_position();
    }

    std::string ct_menu(const char *s) {
        return s;
    }

    int ct_add_literal(Fl_Choice *menu, const char *text) {
        int index = menu->add("", 0, nullptr);
        menu->replace(index, text);
        return index;
    }

    bool ct_number(StableInput *w, long low, long high, long &out) {
        char *end = nullptr;
        errno = 0;
        out = std::strtol(w->value(), &end, 0);
        if (errno || end == w->value() || !end || *end || out < low || out > high) {
            ct_message("Campo numérico inválido; confira a faixa e use decimal ou 0x hexadecimal.");
            return false;
        }
        return true;
    }

    void ct_update_profiles() {
        bc_error e = {{0}};
        auto *a = bc_comm_profiles(root.c_str(), &e);
        if (!a) {
            ct_message(e.text);
            return;
        }
        json_object_put(ct_profile_rows);
        ct_profile_rows = a;
        ct_profile->clear();
        for (size_t i = 0; i < json_object_array_length(a); i++)
            ct_add_literal(ct_profile,
                           ct_menu(bc_str(json_object_array_get_idx(a, i), "name")).c_str());
        ct_profile->value(json_object_array_length(a) ? 0 : -1);
    }

    void ct_profile_action(int action) {
        if (busy || session_active)
            return;
        bc_error e = {{0}};
        if (action == 0) {
            auto *c = communication_draft();
            if (!c)
                return;
            int rc = bc_comm_profile_save(root.c_str(), ct_name->value(), c, 0, &e);
            if (rc < 0 && std::strstr(e.text, "já existe") &&
                fl_choice("Substituir explicitamente o perfil %s?", "Cancelar", "Substituir",
                          nullptr, ct_name->value()) == 1)
                rc = bc_comm_profile_save(root.c_str(), ct_name->value(), c, 1, &e);
            json_object_put(c);
            ct_message(rc < 0
                           ? e.text
                           : "Perfil salvo. Conexões e liberação do driver não são guardadas como "
                             "autorização.");
            if (!rc)
                ct_update_profiles();
            return;
        }
        int i = ct_profile->value();
        if (i < 0 || static_cast<size_t>(i) >= json_object_array_length(ct_profile_rows)) {
            ct_message("Selecione um perfil salvo.");
            return;
        }
        auto *row = json_object_array_get_idx(ct_profile_rows, static_cast<size_t>(i));
        if (action == 1) {
            communication_assign(bc_get(row, "config"));
            ct_name->value(bc_str(row, "name"));
            ct_message(
                "Perfil carregado para edição. Volte à seleção, confira as conexões e clique "
                "Usar seleção.");
        } else if (fl_choice("Excluir somente o perfil %s?", "Cancelar", "Excluir", nullptr,
                             bc_str(row, "name")) == 1) {
            int rc = bc_comm_profile_delete(root.c_str(), bc_str(row, "name"), &e);
            ct_message(rc < 0 ? e.text : "Perfil excluído.");
            if (!rc)
                ct_update_profiles();
        }
    }

    void ct_controls() {
        if (!ct_dialog)
            return;
        bool enabled = !busy && !session_active && !running;
        for (auto *w : ct_editable)
            set_enabled(w, enabled);
        bool terminal = busy && operation == "communication_terminal";
        set_enabled(ct_send, terminal);
        set_enabled(ct_text, enabled || terminal);
        set_enabled(ct_format, enabled || terminal);
        set_enabled(ct_stop, busy && operation.rfind("communication_", 0) == 0);
        int mi = ct_map->value(), ri = ct_register->value();
        auto *map = mi < 0 ? nullptr : json_object_array_get_idx(ct_maps, static_cast<size_t>(mi));
        auto *entry =
            ri < 0 ? nullptr
                   : json_object_array_get_idx(bc_get(map, "registers"), static_cast<size_t>(ri));
        set_enabled(ct_read, enabled && entry && std::strcmp(bc_str(entry, "access"), "wo"));
        set_enabled(ct_write, enabled && entry && std::strcmp(bc_str(entry, "access"), "ro"));
        set_enabled(ct_capture_export, !busy && ct_capture.size() > 0);
    }

    void ct_launch(const char *op, json_object *request, const char *bus) {
        if (busy || session_active || running) {
            json_object_put(request);
            return;
        }
        auto *config = communication_draft();
        if (!config) {
            json_object_put(request);
            return;
        }
        if (std::strcmp(bc_str(config, "bus"), bus) ||
            !json_object_get_boolean(bc_get(config, "connections_checked"))) {
            ct_message("Escolha o barramento correspondente e confirme as conexões na seleção de "
                       "comunicação.");
            json_object_put(request);
            json_object_put(config);
            return;
        }
        auto *v = json_object_new_object();
        json_object_object_add(v, "communication", config);
        json_object_object_add(v, "operation", request);
        if (!std::strcmp(op, "communication_exchange"))
            ct_exchange_pending = true;
        launch(op, "", v);
        json_object_put(v);
        ct_controls();
    }

    void ct_terminal_start() {
        long seconds;
        if (!ct_number(ct_seconds, 1, 3600, seconds))
            return;
        if (fl_choice("Abrir a UART selecionada por até %ld s? A abertura pode alterar DTR/RTS. A "
                      "captura fica em memória e só é salva ao exportar.",
                      "Cancelar", "Abrir terminal", nullptr, seconds) != 1)
            return;
        auto *o = json_object_new_object();
        const char *ds[] = {"none", "lf", "cr", "crlf"};
        json_object_object_add(o, "delimiter", json_object_new_string(ds[ct_delimiter->value()]));
        json_object_object_add(o, "seconds", json_object_new_int64(seconds));
        ct_launch("communication_terminal", o, "uart");
    }

    void ct_terminal_send() {
        if (!job || operation != "communication_terminal")
            return;
        bc_error e = {{0}};
        const char *ds[] = {"none", "lf", "cr", "crlf"};
        auto *o = bc_comm_text_operation(ct_text->value(), ct_format->value() == 1,
                                         ds[ct_delimiter->value()], 0, &e);
        if (!o) {
            ct_message(e.text);
            return;
        }
        if (bc_job_command(job, o) < 0)
            ct_message(
                "Há um envio pendente ou o terminal está encerrando. Nenhum comando foi repetido.");
        else
            ct_message("Envio enfileirado uma vez.");
        json_object_put(o);
    }

    void ct_event(json_object *ev) {
        const char *type = bc_str(ev, "type");
        if (!std::strcmp(type, "communication_terminal_data")) {
            bool stored = ct_capture.append(ev);
            if (!stored || ct_capture.dropped())
                ct_message(
                    "Limite da captura: eventos descartados são contados. Exporte os eventos "
                    "disponíveis ou limpe explicitamente para iniciar nova captura.");
            std::ostringstream s;
            s << bc_num(ev, "elapsed") << " s · " << bc_str(ev, "direction") << " · "
              << (ct_format->value() ? "HEX " : "ASCII ")
              << bc_str(ev, ct_format->value() ? "hex" : "ascii");
            if (!json_object_get_boolean(bc_get(ev, "complete")))
                s << " [fragmento]";
            s << "\n";
            ct_append(s.str());
        } else if (!std::strcmp(type, "communication_scan_row")) {
            auto *d = bc_get(ev, "data");
            char b[120];
            std::snprintf(
                b, sizeof b, "I²C 0x%02X · %s\n", static_cast<unsigned>(bc_num(d, "address")),
                json_object_get_boolean(bc_get(d, "ack")) ? "respondeu à leitura de 1 byte"
                                                          : "NACK");
            ct_append(b);
        }
    }

    bool ct_finished(const std::string &op, json_object *end, bool ok, const std::string &message) {
        if (ct_exchange_pending && op == "communication_exchange") {
            ct_exchange_pending = false;
            ct_message(ok ? "Transferência concluída; confira os bytes recebidos."
                          : message.c_str());
            auto *data = bc_get(end, "data");
            if (data)
                ct_append(
                    std::string(json_object_to_json_string_ext(data, JSON_C_TO_STRING_PRETTY)) +
                    "\n");
            return false;
        }
        if (op != "communication_terminal" && op != "communication_scan" &&
            op != "communication_identify" && op != "communication_openocd")
            return false;
        auto *d = bc_get(end, "data");
        if (op == "communication_openocd" && d) {
            ct_append(bc_str(d, "log"));
            if (bc_get(d, "ch347_supported")) {
                ct_message(bc_str(d, "message"));
                ct_controls();
                return true;
            }
            ct_message(
                json_object_get_boolean(bc_get(d, "ok"))
                    ? "OpenOCD terminou com código zero. Confira o log e o estado físico do alvo."
                    : "OpenOCD retornou erro. Confira o log; o alvo pode estar parcialmente "
                      "alterado.");
        } else {
            ct_message(ok ? "Operação concluída. Resultado em memória; validação física depende da "
                            "bancada."
                          : message.c_str());
            if (d && op != "communication_terminal")
                ct_append(std::string(json_object_to_json_string_ext(d, JSON_C_TO_STRING_PRETTY)) +
                          "\n");
        }
        ct_controls();
        return true;
    }

    void ct_capture_save() {
        if (busy || !ct_capture.size())
            return;
        auto path = choose_file("Exportar captura UART (opcional)", "Captura JSON\t*.json", true,
                                "BPCB-UART.json");
        if (path.empty())
            return;
        auto *events = ct_capture.snapshot();
        if (!events) {
            ct_message("Memória insuficiente para exportar a captura.");
            return;
        }
        auto *o = json_object_new_object();
        json_object_object_add(o, "format", json_object_new_string("bpcb.uart-capture"));
        json_object_object_add(o, "schema", json_object_new_int(1));
        json_object_object_add(o, "events", events);
        json_object_object_add(o, "dropped_events",
                               json_object_new_int64(static_cast<int64_t>(ct_capture.dropped())));
        bc_error e = {{0}};
        int rc = bc_export_json(o, path.c_str(), &e);
        json_object_put(o);
        ct_message(rc < 0 ? e.text
                          : "Captura exportada por solicitação. Pode conter informações enviadas "
                            "pelo módulo.");
    }

    void ct_map_changed() {
        ct_register->clear();
        int i = ct_map->value();
        auto *m = i < 0 ? nullptr : json_object_array_get_idx(ct_maps, static_cast<size_t>(i));
        auto *a = bc_get(m, "registers");
        for (size_t j = 0; j < json_object_array_length(a); j++) {
            auto *r = json_object_array_get_idx(a, j);
            ct_add_literal(
                ct_register,
                ct_menu((std::string(bc_str(r, "name")) + " · " + bc_str(r, "access")).c_str())
                    .c_str());
        }
        ct_register->value(json_object_array_length(a) ? 0 : -1);
        ct_value->value("");
        if (bc_get(m, "default_address")) {
            char address[16];
            std::snprintf(address, sizeof address, "0x%02X",
                          static_cast<unsigned>(bc_num(m, "default_address")));
            ct_address->value(address);
        }
    }

    void ct_import_map() {
        if (busy)
            return;
        auto path = choose_file("Importar mapa I²C", "Mapa JSON\t*.json", false, nullptr);
        if (path.empty())
            return;
        bc_error e = {{0}};
        auto *m = bc_read_json(path.c_str(), &e);
        if (!m || bc_comm_register_map_validate(m, &e) < 0) {
            json_object_put(m);
            ct_message(e.text);
            return;
        }
        if (json_object_array_length(ct_maps) >= 64) {
            json_object_put(m);
            ct_message("Limite de 64 mapas nesta abertura.");
            return;
        }
        ct_add_literal(ct_map, ct_menu(bc_str(m, "name")).c_str());
        json_object_array_add(ct_maps, m);
        ct_map->value(static_cast<int>(json_object_array_length(ct_maps) - 1));
        ct_map_changed();
        ct_message("Mapa importado em memória. Confira o datasheet e os efeitos de cada acesso.");
    }

    void ct_register_action(bool write) {
        if (busy || session_active)
            return;
        long address;
        if (!ct_number(ct_address, 8, 119, address))
            return;
        int i = ct_map->value(), r = ct_register->value();
        if (i < 0 || r < 0)
            return;
        bc_error e = {{0}};
        auto *o = bc_comm_register_operation(
            json_object_array_get_idx(ct_maps, static_cast<size_t>(i)), static_cast<size_t>(r),
            static_cast<unsigned>(address), write ? ct_value->value() : nullptr, &e);
        if (!o) {
            ct_message(e.text);
            return;
        }
        if (write && !*ct_value->value()) {
            json_object_put(o);
            ct_message("Informe os bytes de dados da escrita.");
            return;
        }
        std::string preview = std::string(write ? "ESCREVER" : "LER") + "\n" +
                              json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY) +
                              "\nConfira o componente e o endereço. Leituras podem limpar flags; "
                              "escritas podem alterar saídas e configuração.";
        if (fl_choice("%s", "Cancelar", "Executar uma vez", nullptr, preview.c_str()) != 1) {
            json_object_put(o);
            return;
        }
        ct_launch("communication_exchange", o, "i2c");
    }

    void ct_scan_start() {
        long first, last;
        if (!ct_number(ct_first, 8, 119, first) || !ct_number(ct_last, 8, 119, last) ||
            first > last) {
            ct_message("Confira o intervalo de endereços.");
            return;
        }
        if (fl_choice(
                "Ler UM BYTE de cada endereço entre 0x%02lX e 0x%02lX? Leituras podem consumir "
                "dados ou limpar flags. Não é detecção de modelo nem garantia de segurança. Após "
                "NACK, adaptadores HID podem precisar de uma nova sessão no mesmo dispositivo, "
                "sem repetir o endereço.",
                "Cancelar", "Iniciar varredura", nullptr, first, last) != 1)
            return;
        auto *o = json_object_new_object();
        json_object_object_add(o, "first", json_object_new_int64(first));
        json_object_object_add(o, "last", json_object_new_int64(last));
        ct_launch("communication_scan", o, "i2c");
    }

    json_object *ct_ocd_request() {
        long address, speed;
        if (!ct_number(ct_offset, 0, 0x7fffffff, address) || !ct_number(ct_speed, 1, 30000, speed))
            return nullptr;
        auto *o = json_object_new_object();
        json_object_object_add(o, "executable", json_object_new_string(ct_exe->value()));
        json_object_object_add(o, "scripts", json_object_new_string(ct_scripts->value()));
        const char *actions[] = {"check", "identify", "verify", "program"};
        json_object_object_add(o, "action", json_object_new_string(actions[ct_action->value()]));
        int index = ct_target->value() - 1;
        if (index >= 0)
            json_object_object_add(
                o, "target",
                bc_clone(json_object_array_get_idx(ct_targets, static_cast<size_t>(index))));
        json_object_object_add(o, "firmware", json_object_new_string(ct_firmware->value()));
        json_object_object_add(o, "address", json_object_new_int64(address));
        json_object_object_add(o, "speed_khz", json_object_new_int64(speed));
        json_object_object_add(o, "timeout_seconds", json_object_new_int(120));
        json_object_object_add(o, "serial", json_object_new_string(ct_serial->value()));
        json_object_object_add(o, "trusted_tools", json_object_new_boolean(ct_trust->value()));
        return o;
    }

    void ct_ocd_preview() {
        if (busy)
            return;
        auto *o = ct_ocd_request();
        if (!o)
            return;
        bc_error e = {{0}};
        auto *plan = bc_openocd_plan(o, &e);
        if (!plan) {
            ct_message(e.text);
            ct_reviewed.clear();
        } else {
            ct_reviewed = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
            ct_sha = bc_str(plan, "sha256");
            ct_preview_buffer.text(json_object_to_json_string_ext(plan, JSON_C_TO_STRING_PRETTY));
            ct_message(
                "Prévia preparada. Confira alvo, arquivo, endereço e SHA-256 antes de executar.");
        }
        json_object_put(o);
        json_object_put(plan);
    }

    void ct_ocd_execute() {
        if (busy || session_active)
            return;
        auto *o = ct_ocd_request();
        if (!o)
            return;
        if (ct_reviewed != json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN)) {
            json_object_put(o);
            ct_message("Prepare uma prévia atualizada antes de executar.");
            return;
        }
        if (!ct_trust->value()) {
            json_object_put(o);
            ct_message("Confirme a origem confiável do executável e dos scripts.");
            return;
        }
        if (std::strcmp(bc_str(o, "action"), "check") &&
            (!communication_checked->value() || std::strcmp(communication_bus_id(), "jtag"))) {
            json_object_put(o);
            ct_message("Selecione JTAG e confira as conexões antes de executar OpenOCD.");
            return;
        }
        std::string summary =
            std::string(bc_str(o, "action")) + " · " + bc_str(bc_get(o, "target"), "id") +
            "\nArquivo: " + bc_str(o, "firmware") + "\nSHA-256: " + ct_sha +
            "\nPode reiniciar/parar ou gravar o alvo. Cancelamento não desfaz alterações parciais.";
        if (fl_choice("%s", "Cancelar", "Executar", nullptr, summary.c_str()) != 1) {
            json_object_put(o);
            return;
        }
        json_object_object_add(o, "expected_sha256", json_object_new_string(ct_sha.c_str()));
        json_object_object_add(
            o, "program_confirmed",
            json_object_new_boolean(!std::strcmp(bc_str(o, "action"), "program")));
        auto *v = json_object_new_object();
        json_object_object_add(v, "operation", o);
        launch("communication_openocd", "", v);
        json_object_put(v);
        ct_controls();
    }

    void ct_import_target() {
        if (busy)
            return;
        auto path =
            choose_file("Importar perfil de alvo OpenOCD", "Perfil JSON\t*.json", false, nullptr);
        if (path.empty())
            return;
        bc_error e = {{0}};
        auto *t = bc_read_json(path.c_str(), &e);
        if (!json_object_is_type(t, json_type_object) || !*bc_str(t, "id") ||
            strlen(bc_str(t, "id")) > 80 || json_object_array_length(ct_targets) >= 64) {
            json_object_put(t);
            ct_message("Perfil inválido ou limite de 64 alvos atingido.");
            return;
        }
        ct_add_literal(ct_target, ct_menu(bc_str(t, "id")).c_str());
        json_object_array_add(ct_targets, t);
        ct_target->value(static_cast<int>(json_object_array_length(ct_targets)));
        ct_reviewed.clear();
        ct_message("Perfil importado em memória; família e script serão validados na prévia.");
    }

    void show_communication_tools(int tab) {
        if (busy || session_active)
            return;
        ct_update_profiles();
        Fl_Group *groups[] = {ct_uart, ct_profiles, ct_i2c, ct_jtag};
        ct_tabs->value(groups[tab]);
        ct_dialog->on_layout();
        skin(ct_dialog);
        ct_dialog->show();
        ct_controls();
    }

    void make_communication_tools() {
        ct_profile_rows = json_object_new_array();
        ct_maps = bc_comm_register_maps();
        ct_targets = bc_openocd_targets();
        ct_capture.clear();
        ct_dialog = new Dialog(1020, 740, "BPCB · Ferramentas de comunicação");
        ct_dialog->minw = 1020;
        ct_dialog->minh = 700;
        ct_dialog->size_range(1020, 700);
        auto label_at = [&](const char *s, int x, int y, int w, int h = 24) {
            auto *b = label(s, 13);
            rect(b, x, y, w, h);
            return b;
        };
        auto field = [&](const char *s, int x, int y, int w) {
            auto *v = new StableInput(0, 0, 1, 1);
            v->box(input_box);
            v->textsize(14);
            v->maximum_size(4095);
            v->value(s);
            rect(v, x, y, w, 32);
            ct_editable.push_back(v);
            return v;
        };
        auto choice = [&](const char *s, int x, int y, int w) {
            auto *v = new Choice();
            v->box(input_box);
            v->textsize(14);
            if (s)
                v->add(s);
            v->value(0);
            rect(v, x, y, w, 32);
            ct_editable.push_back(v);
            return v;
        };
        auto button = [&](const char *s, int x, int y, int w, Fl_Callback *callback) {
            auto *v = new Button(s);
            rect(v, x, y, w, 34);
            v->callback(callback, this);
            ct_editable.push_back(v);
            return v;
        };
        label_at("Terminal, perfis e exploradores", 24, 18, 950, 32);
        ct_note =
            label_at("Usa o adaptador e as conexões da janela Comunicação da PCB. Nada é enviado "
                     "ao abrir estas ferramentas.",
                     24, 52, 968, 44);
        ct_tabs = new Fl_Tabs(24, 110, 972, 362);
        ct_tabs->labelsize(14);
        ct_uart = new Fl_Group(24, 140, 972, 332, "Terminal UART");
        label_at("Formato de envio/visualização", 40, 153, 300);
        ct_format = choice("ASCII|Hexadecimal", 40, 181, 200);
        label_at("Delimitador TX e RX", 260, 153, 200);
        ct_delimiter = choice("Nenhum|LF|CR|CRLF", 260, 181, 200);
        ct_delimiter->value(3);
        label_at("Duração máxima (s)", 480, 153, 200);
        ct_seconds = field("300", 480, 181, 140);
        label_at("Texto ou bytes a enviar uma vez", 40, 226, 880);
        ct_text = field("AT", 40, 254, 924);
        ct_text->maximum_size(BC_COMM_MAX * 3);
        ct_start = button("Abrir terminal", 40, 308, 185, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_terminal_start();
        });
        ct_send = button("Enviar uma vez", 245, 308, 185, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_terminal_send();
        });
        ct_capture_export = button("Exportar captura…", 450, 308, 210, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_capture_save();
        });
        button("Limpar captura…", 680, 308, 284, [](Fl_Widget *, void *v) {
            auto *a = static_cast<App *>(v);
            if (!a->busy && fl_choice("Descartar os eventos UART em memória e iniciar uma captura "
                                      "vazia? Arquivos já exportados permanecem disponíveis.",
                                      "Cancelar", "Limpar captura", nullptr) == 1) {
                a->ct_capture.clear();
                a->ct_log.text("Captura limpa explicitamente.\n");
                a->ct_controls();
            }
        });
        label_at(
            "Bluetooth em modo AT e ESP-AT podem usar AT + CRLF; confirme baud, modo e comandos "
            "no manual do módulo. ASCII mostra controles escapados. Fragmentos e dados permanecem "
            "em memória (até 4096 eventos).",
            40, 365, 924, 70);
        ct_uart->end();
        ct_profiles = new Fl_Group(24, 140, 972, 332, "Perfis nomeados");
        label_at("Perfis salvos neste cadastro BPCB", 40, 153, 900);
        ct_profile = choice(nullptr, 40, 181, 730);
        label_at("Nome para salvar a seleção atual", 40, 229, 880);
        ct_name = field("Minha bancada", 40, 257, 730);
        ct_profile_save = button("Salvar seleção", 40, 313, 200, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_profile_action(0);
        });
        ct_profile_load = button("Carregar para edição", 260, 313, 240, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_profile_action(1);
        });
        ct_profile_delete = button("Excluir perfil", 520, 313, 200, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_profile_action(2);
        });
        label_at(
            "Carregar não conecta nem reaplica parâmetros de testes. A confirmação das conexões é "
            "sempre renovada. Perfis incluem caminhos e série do adaptador; o backup de cadastros "
            "também os preserva.",
            40, 370, 924, 60);
        ct_profiles->end();
        ct_i2c = new Fl_Group(24, 140, 972, 332, "Explorador I²C");
        label_at("Varredura manual: intervalo de endereços 7 bits", 40, 150, 700);
        ct_first = field("0x08", 40, 178, 100);
        ct_last = field("0x77", 160, 178, 100);
        ct_scan = button("Ler 1 byte por endereço…", 280, 178, 270, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_scan_start();
        });
        ct_map = choice(nullptr, 40, 234, 690);
        for (size_t i = 0; i < json_object_array_length(ct_maps); i++)
            ct_add_literal(ct_map,
                           ct_menu(bc_str(json_object_array_get_idx(ct_maps, i), "name")).c_str());
        ct_map->value(0);
        ct_map->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->ct_map_changed();
                a->ct_controls();
            },
            this);
        ct_map_import = button("Importar mapa…", 750, 234, 214, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_import_map();
        });
        ct_register = choice(nullptr, 40, 286, 690);
        ct_register->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->ct_controls();
            },
            this);
        label_at("Endereço", 750, 271, 210);
        ct_address = field("0x48", 750, 300, 214);
        label_at("Dados para escrita (hex; leitura ignora este campo)", 40, 332, 660);
        ct_value = field("", 40, 360, 690);
        ct_map_changed();
        ct_read = button("Ler entrada", 750, 350, 214, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_register_action(false);
        });
        ct_write = button("Escrever entrada…", 750, 394, 214, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_register_action(true);
        });
        label_at("Leituras não inicializam sensores nem compensam dados brutos. OLED SSD1306 usa "
                 "comandos e não permite leitura I²C. Endereços reservados são excluídos.",
                 40, 402, 690, 60);
        ct_i2c->end();
        ct_jtag = new Fl_Group(24, 140, 972, 332, "JTAG / OpenOCD");
        ct_identify = button("IDCODE/BYPASS (CH347)", 40, 151, 275, [](Fl_Widget *, void *v) {
            auto *a = static_cast<App *>(v);
            if (fl_choice(
                    "Reiniciar TAP e ler a cadeia JTAG selecionada? Não descobre o comprimento IR.",
                    "Cancelar", "Ler cadeia", nullptr) == 1)
                a->ct_launch("communication_identify", json_object_new_object(), "jtag");
        });
        ct_target = choice("Selecione o alvo exato…", 335, 151, 410);
        for (size_t i = 0; i < json_object_array_length(ct_targets); i++)
            ct_add_literal(ct_target,
                           ct_menu(bc_str(json_object_array_get_idx(ct_targets, i), "id")).c_str());
        ct_target->value(0);
        ct_target_import = button("Importar alvo…", 765, 151, 199, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_import_target();
        });
        label_at("Executável OpenOCD (caminho absoluto)", 40, 195, 440);
        ct_exe = field("", 40, 222, 380);
        button("…", 430, 222, 50, [](Fl_Widget *, void *v) {
            auto *a = static_cast<App *>(v);
            auto path =
                a->choose_file("Selecionar OpenOCD confiável", "Executável\t*", false, nullptr);
            if (!path.empty()) {
                a->ct_exe->value(path.c_str());
                a->ct_reviewed.clear();
            }
        });
        label_at("Pasta scripts confiável (caminho absoluto)", 500, 195, 464);
        ct_scripts = field("", 500, 222, 404);
        button("…", 914, 222, 50, [](Fl_Widget *, void *v) {
            auto *a = static_cast<App *>(v);
            Fl_Native_File_Chooser chooser;
            chooser.title("Pasta scripts do OpenOCD confiável");
            chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
            if (chooser.show() == 0 && chooser.filename()) {
                a->ct_scripts->value(chooser.filename());
                a->ct_reviewed.clear();
            }
        });
        label_at("Firmware ELF/HEX/BIN, ou vetores SVF/XSVF de FPGA", 40, 262, 924);
        ct_firmware = field("", 40, 289, 864);
        button("…", 914, 289, 50, [](Fl_Widget *, void *v) {
            auto *a = static_cast<App *>(v);
            auto path = a->choose_file("Selecionar firmware ou vetores",
                                       "Firmware\t*.{elf,hex,bin,svf,xsvf}", false, nullptr);
            if (!path.empty()) {
                a->ct_firmware->value(path.c_str());
                a->ct_reviewed.clear();
            }
        });
        label_at("Ação", 40, 329, 185);
        ct_action = choice("Conferir driver (sem USB)|Identificar alvo|Verificar flash|Gravar + "
                           "verificar / executar SVF",
                           40, 356, 335);
        label_at("Endereço BIN", 395, 329, 140);
        ct_offset = field("0", 395, 356, 140);
        label_at("Clock (kHz)", 555, 329, 130);
        ct_speed = field("1875", 555, 356, 130);
        label_at("Série USB exata", 705, 329, 259);
        ct_serial = field("", 705, 356, 259);
        ct_trust = new Fl_Check_Button(40, 407, 550, 28,
                                       "Conferi a origem do executável e dos scripts OpenOCD");
        ct_trust->labelsize(13);
        ct_editable.push_back(ct_trust);
        ct_preview = button("Preparar prévia", 610, 403, 170, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_ocd_preview();
        });
        ct_execute = button("Executar…", 800, 403, 164, [](Fl_Widget *, void *v) {
            static_cast<App *>(v)->ct_ocd_execute();
        });
        ct_jtag->end();
        ct_tabs->end();
        ct_preview_view = display(ct_preview_buffer);
        rect(ct_preview_view, 24, 488, 972, 78);
        ct_preview_buffer.text(
            "Prévia OpenOCD aparece aqui. Para BIN, obtenha o endereço correto do projeto de "
            "firmware. "
            "ESP32 requer uma instalação compatível da Espressif; SVF pode programar FPGA e não "
            "oferece verificação genérica separada.");
        ct_log_view = display(ct_log);
        rect(ct_log_view, 24, 578, 972, 106);
        ct_log.text(
            "Resultados e captura em memória. Configure o adaptador na janela Comunicação da "
            "PCB antes de operar.\n");
        ct_stop = button("Parar operação", 24, 700, 190, [](Fl_Widget *, void *v) {
            auto *a = static_cast<App *>(v);
            if (a->job && a->operation.rfind("communication_", 0) == 0)
                bc_job_cancel(a->job);
        });
        auto *close = button("Fechar", 846, 700, 150, [](Fl_Widget *, void *v) {
            auto *a = static_cast<App *>(v);
            if (a->job && a->operation == "communication_terminal")
                bc_job_cancel(a->job);
            a->ct_dialog->hide();
        });
        ct_editable.erase(std::remove(ct_editable.begin(), ct_editable.end(), close),
                          ct_editable.end());
        ct_dialog->on_layout = [this, close] {
            int width = ct_dialog->w(), height = ct_dialog->h();
            rect(ct_preview_view, 24, 488, width - 48, 70);
            rect(ct_log_view, 24, 570, width - 48, std::max(74, height - 626));
            rect(ct_stop, 24, height - 40, 190, 34);
            rect(close, width - 174, height - 40, 150, 34);
        };
        ct_dialog->on_close = [this] {
            if (job && operation == "communication_terminal")
                bc_job_cancel(job);
            ct_dialog->hide();
        };
        ct_dialog->end();
        ct_controls();
    }

    void destroy_communication_tools() {
        json_object_put(ct_profile_rows);
        json_object_put(ct_maps);
        json_object_put(ct_targets);
        ct_capture.clear();
        ct_log_view->buffer(nullptr);
        ct_preview_view->buffer(nullptr);
        delete ct_dialog;
    }
