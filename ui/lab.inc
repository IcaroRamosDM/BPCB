    Dialog *lx_dialog = nullptr;
    Fl_Tabs *lx_tabs = nullptr;
    Fl_Scroll *lx_pages[6]{};
    Fl_Box *lx_note = nullptr, *lx_capture_note = nullptr, *lx_binding_note = nullptr;
    Fl_Text_Buffer lx_preview_buffer, lx_log_buffer;
    Fl_Text_Display *lx_preview_view = nullptr, *lx_log_view = nullptr;
    Choice *lx_device, *lx_metric, *lx_action, *lx_pattern, *lx_protocol, *lx_encoding, *lx_ending,
        *lx_source, *lx_fault, *lx_channel, *lx_fw_family, *lx_fw_backend, *lx_fw_target,
        *lx_fw_action, *lx_fw_images, *lx_qual_procedure;
    StableInput *lx_address, *lx_shunt, *lx_lsb, *lx_accel, *lx_gyro, *lx_height, *lx_write,
        *lx_success, *lx_max_bytes, *lx_timeout, *lx_length, *lx_slave, *lx_function, *lx_start,
        *lx_quantity, *lx_values, *lx_fault_at, *lx_fragment, *lx_delay, *lx_fault_offset,
        *lx_fault_xor, *lx_fw_name, *lx_fw_version, *lx_fw_role, *lx_fw_offset, *lx_fw_exe,
        *lx_fw_scripts, *lx_fw_serial, *lx_fw_speed, *lx_qual_os, *lx_qual_expected_v,
        *lx_qual_measured_v;
    Fl_Check_Button *lx_fw_trust;
    Choice *lx_qual_flags[7]{};
    Button *lx_stop, *lx_export;
    json_object *lx_frame = nullptr;
    json_object *lx_devices = nullptr, *lx_capture = nullptr, *lx_last_result = nullptr,
                *lx_bound = nullptr, *lx_fw_manifest = nullptr, *lx_targets = nullptr,
                *lx_qual_plan = nullptr, *lx_qual_evidence = nullptr;
    std::string lx_bound_for, lx_fw_review, lx_fw_hash;
    bool lx_qual_waiting = false;
    std::vector<Fl_Widget *> lx_editable;
    std::vector<std::string> lx_metric_ids, lx_action_ids;
    std::map<Fl_Widget *, std::function<void()>> lx_callbacks;

    void lx_message(const char *s) {
        if (lx_note) {
            lx_note->copy_label(s);
            lx_note->redraw();
        }
    }

    Button *lx_button(const char *s, int x, int y, int width, std::function<void()> fn) {
        auto *b = new Button(s);
        rect(b, x, y, width, 34);
        lx_editable.push_back(b);
        lx_callbacks[b] = std::move(fn);
        b->callback(
            [](Fl_Widget *w, void *v) {
                auto *a = static_cast<App *>(v);
                auto it = a->lx_callbacks.find(w);
                if (it != a->lx_callbacks.end())
                    it->second();
            },
            this);
        return b;
    }

    void lx_label(const char *s, int x, int y, int width, int height = 24) {
        auto *b = label(s, 13);
        rect(b, x, y, width, height);
    }

    StableInput *lx_input(const char *s, int x, int y, int width) {
        auto *i = new StableInput(0, 0, 1, 1);
        i->box(input_box);
        i->textsize(14);
        i->maximum_size(4095);
        i->value(s);
        rect(i, x, y, width, 32);
        lx_editable.push_back(i);
        return i;
    }

    Choice *lx_choice(const char *s, int x, int y, int width) {
        auto *c = new Choice();
        c->box(input_box);
        c->textsize(14);
        if (s)
            c->add(s);
        c->value(0);
        rect(c, x, y, width, 32);
        lx_editable.push_back(c);
        return c;
    }

    void lx_text(json_object *o, const char *k, const char *v) {
        json_object_object_add(o, k, json_object_new_string(v));
    }

    bool lx_int(StableInput *field, uint64_t min, uint64_t max, uint64_t &value) {
        errno = 0;
        char *end = nullptr;
        const char *s = field->value();
        unsigned long long n = std::strtoull(s, &end, 0);
        if (errno || !s[0] || s[0] == '-' || end == s || !end || *end || n < min || n > max) {
            lx_message("Campo inteiro inválido. Use decimal ou 0x hexadecimal na faixa indicada.");
            return false;
        }
        value = n;
        return true;
    }

    bool lx_number(StableInput *field, double min, double max, double &value) {
        std::string s = field->value();
        std::replace(s.begin(), s.end(), ',', '.');
        char *end = nullptr;
        errno = 0;
        value = std::strtod(s.c_str(), &end);
        if (errno || end == s.c_str() || *end || !std::isfinite(value) || value < min ||
            value > max) {
            lx_message("Valor numérico inválido ou fora da faixa.");
            return false;
        }
        return true;
    }

    void lx_result(json_object *r, const char *error = nullptr) {
        json_object_put(lx_last_result);
        lx_last_result = r ? bc_clone(r) : nullptr;
        std::string s;
        if (error && *error)
            s = std::string(error) + "\n";
        if (r) {
            if (bc_get(r, "value"))
                s +=
                    "Valor: " + std::to_string(bc_num(r, "value")) + " " + bc_str(r, "unit") + "\n";
            s += json_object_to_json_string_ext(r, JSON_C_TO_STRING_PRETTY);
        }
        if (s.size() > 131072)
            s.resize(131072);
        lx_log_buffer.text(s.c_str());
        lx_controls();
    }

    void lx_controls() {
        if (!lx_dialog)
            return;
        bool active = !busy && !running && !session_active;
        for (auto *w : lx_editable)
            set_enabled(w, active);
        set_enabled(lx_stop, busy && operation.rfind("communication_", 0) == 0);
        set_enabled(lx_export, active && lx_last_result);
        if (active && lx_protocol) {
            int mode = lx_protocol->value();
            set_enabled(lx_success, mode == 0);
            set_enabled(lx_length, mode == 2);
            for (auto *w : {lx_slave, lx_function, lx_start, lx_quantity, lx_values})
                set_enabled(w, mode == 3);
            for (Fl_Widget *w :
                 std::initializer_list<Fl_Widget *>{lx_write, lx_encoding, lx_ending})
                set_enabled(w, mode != 3);
        }
        if (active && lx_device && lx_shunt) {
            int i = lx_device->value();
            set_enabled(lx_shunt, i == 2);
            set_enabled(lx_lsb, i == 2);
            set_enabled(lx_accel, i == 3);
            set_enabled(lx_gyro, i == 3);
            set_enabled(lx_height, i == 4);
            set_enabled(lx_pattern, i == 4);
        }
    }

    void lx_save_result() {
        if (!lx_last_result || busy)
            return;
        auto path = choose_file("Exportar resultado do laboratório (opcional)", "JSON\t*.json",
                                true, "BPCB-Laboratorio.json");
        if (path.empty())
            return;
        bc_error e = {{0}};
        int rc = bc_export_json(lx_last_result, path.c_str(), &e);
        lx_message(
            rc ? e.text
               : "Resultado exportado explicitamente; histórico permanece conforme sua opção.");
    }

    void lx_device_changed() {
        int index = lx_device->value();
        if (index < 0 || static_cast<size_t>(index) >= json_object_array_length(lx_devices))
            return;
        auto *d = json_object_array_get_idx(lx_devices, static_cast<size_t>(index));
        lx_metric->clear();
        lx_action->clear();
        std::string metrics = bc_str(d, "metrics"), actions = bc_str(d, "actions");
        std::istringstream ms(metrics), as(actions);
        std::string item;
        lx_metric_ids.clear();
        lx_action_ids.clear();
        while (std::getline(ms, item, ',')) {
            lx_metric_ids.push_back(item);
            ct_add_literal(lx_metric, bc_lab_metric_label(item.c_str()));
        }
        while (std::getline(as, item, ',')) {
            lx_action_ids.push_back(item);
            ct_add_literal(lx_action, item == "read"      ? "Ler medição existente"
                                      : item == "measure" ? "Inicializar e medir"
                                      : item == "pattern" ? "Exibir padrão OLED"
                                                          : "Desligar OLED");
        }
        if (lx_fw_exe)
            lx_controls();
        lx_metric->value(0);
        lx_action->value(0);
        char address[20];
        std::snprintf(address, sizeof address, "0x%02X",
                      static_cast<unsigned>(bc_num(d, "address")));
        lx_address->value(address);
    }

    json_object *lx_iot_request() {
        int i = lx_device->value();
        if (i < 0 || static_cast<size_t>(i) >= json_object_array_length(lx_devices) ||
            !lx_metric->text() || !lx_action->text())
            return nullptr;
        uint64_t address, range = 2, gyro = 250, height = 64;
        double shunt = 0.1, lsb = 0.0001;
        if (!lx_int(lx_address, 8, 119, address))
            return nullptr;
        if (i == 2 &&
            (!lx_number(lx_shunt, 0.000001, 100, shunt) || !lx_number(lx_lsb, 1e-9, 1, lsb)))
            return nullptr;
        if (i == 3 && (!lx_int(lx_accel, 2, 16, range) || !lx_int(lx_gyro, 250, 2000, gyro)))
            return nullptr;
        if (i == 4 && !lx_int(lx_height, 32, 64, height))
            return nullptr;
        auto *o = json_object_new_object();
        lx_text(o, "kind", "peripheral");
        lx_text(o, "device",
                bc_str(json_object_array_get_idx(lx_devices, static_cast<size_t>(i)), "id"));
        lx_text(o, "action", lx_action_ids.at(static_cast<size_t>(lx_action->value())).c_str());
        lx_text(o, "metric", lx_metric_ids.at(static_cast<size_t>(lx_metric->value())).c_str());
        json_object_object_add(o, "address", json_object_new_int64(static_cast<int64_t>(address)));
        json_object_object_add(o, "range", json_object_new_int64(static_cast<int64_t>(range)));
        json_object_object_add(o, "gyro_range", json_object_new_int64(static_cast<int64_t>(gyro)));
        json_object_object_add(o, "height", json_object_new_int64(static_cast<int64_t>(height)));
        json_object_object_add(o, "shunt_ohms", json_object_new_double(shunt));
        json_object_object_add(o, "current_lsb", json_object_new_double(lsb));
        const char *patterns[] = {"checkerboard", "bars", "white", "black"};
        lx_text(o, "pattern", patterns[lx_pattern->value()]);
        bc_error e = {{0}};
        if (bc_peripheral_validate("i2c", o, &e) < 0) {
            lx_message(e.text);
            json_object_put(o);
            return nullptr;
        }
        return o;
    }

    json_object *lx_protocol_request() {
        const char *protocols[] = {"at", "delimited", "length", "modbus_rtu", "frame"},
                   *endings[] = {"none", "lf", "cr", "crlf"};
        int index = lx_protocol->value();
        if (index < 0 || index > 4)
            return nullptr;
        uint64_t maximum = 0, timeout = 0, length = 1, slave = 1, function = 3, start = 0,
                 quantity = 1;
        if (!lx_int(lx_max_bytes, 1, 4096, maximum) || !lx_int(lx_timeout, 50, 10000, timeout))
            return nullptr;
        if (index == 2 && !lx_int(lx_length, 1, 4096, length))
            return nullptr;
        if (index == 3 &&
            (!lx_int(lx_slave, 1, 247, slave) || !lx_int(lx_function, 1, 16, function) ||
             !lx_int(lx_start, 0, 65535, start) || !lx_int(lx_quantity, 1, 2000, quantity)))
            return nullptr;
        auto *o = json_object_new_object();
        lx_text(o, "kind", "uart_protocol");
        lx_text(o, "protocol", protocols[index]);
        json_object_object_add(o, "max_bytes",
                               json_object_new_int64(static_cast<int64_t>(maximum)));
        json_object_object_add(o, "timeout_ms",
                               json_object_new_int64(static_cast<int64_t>(timeout)));
        bc_error e = {{0}};
        if (index == 3) {
            json_object_object_add(o, "slave", json_object_new_int64(static_cast<int64_t>(slave)));
            json_object_object_add(o, "function",
                                   json_object_new_int64(static_cast<int64_t>(function)));
            json_object_object_add(o, "start", json_object_new_int64(static_cast<int64_t>(start)));
            json_object_object_add(o, "quantity",
                                   json_object_new_int64(static_cast<int64_t>(quantity)));
            lx_text(o, "values", lx_values->value());
        } else {
            auto *raw =
                bc_comm_text_operation(lx_write->value(), lx_encoding->value() == 1, "none", 1, &e);
            if (!raw) {
                json_object_put(o);
                lx_message(e.text);
                return nullptr;
            }
            lx_text(o, "write", bc_str(raw, "write"));
            json_object_put(raw);
            lx_text(o, "delimiter", endings[lx_ending->value()]);
            lx_text(o, "success", lx_success->value());
            json_object_object_add(o, "length",
                                   json_object_new_int64(static_cast<int64_t>(length)));
        }
        if (index == 4) {
            json_object_object_del(o, "success");
            json_object_object_del(o, "length");
            json_object_object_add(o, "frame", lx_frame ? bc_clone(lx_frame) : bc_frame_example());
        }
        if (bc_uart_protocol_validate(o, &e) < 0) {
            lx_message(e.text);
            json_object_put(o);
            return nullptr;
        }
        return o;
    }

    void lx_preview(json_object *o) {
        if (!o)
            return;
        lx_preview_buffer.text(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY));
        json_object_put(o);
    }

    void lx_run(json_object *o, const char *bus, bool confirm = true) {
        if (!o)
            return;
        if (busy || running || session_active) {
            json_object_put(o);
            return;
        }
        bc_error e = {{0}};
        auto *c = communication_applied ? bc_comm_config(communication_applied, &e) : nullptr;
        if (!c || strcmp(bc_str(c, "bus"), bus) ||
            !json_object_get_boolean(bc_get(c, "connections_checked"))) {
            json_object_put(c);
            json_object_put(o);
            lx_message("Escolha o adaptador/barramento e use a seleção após conferir conexões.");
            return;
        }
        if (confirm &&
            fl_choice("Executar uma vez no adaptador selecionado? A ação pode configurar "
                      "registradores, saídas ou o módulo. Confira a prévia e as conexões.",
                      "Cancelar", "Executar", nullptr) != 1) {
            json_object_put(c);
            json_object_put(o);
            return;
        }
        auto *d = json_object_new_object();
        json_object_object_add(d, "api", json_object_new_int(1));
        lx_text(d, "bus", bus);
        json_object_object_add(d, "max_transfer", json_object_new_int(4096));
        json_object_object_add(d, "max_timeout_ms", json_object_new_int(10000));
        auto *v = json_object_new_object();
        json_object_object_add(v, "communication", c);
        json_object_object_add(v, "definition", d);
        json_object_object_add(v, "operation", o);
        launch("communication_lab", "", v);
        json_object_put(v);
        lx_message("Operação em andamento; resultado em memória.");
        lx_controls();
    }

    void lx_capture_load() {
        auto path = choose_file("Importar captura ou reprodução", "JSON\t*.json", false, "");
        if (path.empty())
            return;
        bc_error e = {{0}};
        auto *input = bc_read_json(path.c_str(), &e);
        auto *capture = input ? (bc_get(input, "exchanges") ? bc_clone(input)
                                                            : bc_replay_from_capture(input, &e))
                              : nullptr;
        json_object_put(input);
        if (!capture || bc_replay_validate(capture, &e) < 0) {
            json_object_put(capture);
            lx_message(e.text);
            return;
        }
        json_object_put(lx_capture);
        lx_capture = capture;
        std::string s = std::to_string(json_object_array_length(bc_get(capture, "exchanges"))) +
                        " transações · " + bc_str(capture, "bus") +
                        " · reprodução sem acesso ao hardware";
        lx_capture_note->copy_label(s.c_str());
        lx_message("Captura carregada somente em memória. Escolha qual operação reproduzir.");
    }

    json_object *lx_manual_request() {
        auto *config = communication_draft();
        if (!config)
            return nullptr;
        uint64_t read, address, bits, idle;
        bool jtag = !strcmp(bc_str(config, "bus"), "jtag");
        auto *op = json_object_new_object();
        lx_text(op, "write", communication_write->value());
        if (!lx_int(communication_read, 0, 4096, read)) {
            json_object_put(config);
            json_object_put(op);
            return nullptr;
        }
        json_object_object_add(op, "read", json_object_new_int64(static_cast<int64_t>(read)));
        if (!strcmp(bc_str(config, "bus"), "i2c")) {
            if (!lx_int(communication_address, 8, 119, address)) {
                json_object_put(config);
                json_object_put(op);
                return nullptr;
            }
            json_object_object_add(op, "address",
                                   json_object_new_int64(static_cast<int64_t>(address)));
        }
        if (jtag) {
            const char *registers[] = {"dr", "ir", "reset", "idle"};
            int index = communication_jtag_register->value();
            if (index < 0 || index > 4 || !lx_int(communication_address, 0, 32768, bits) ||
                !lx_int(communication_jtag_idle, 0, 4096, idle)) {
                json_object_put(config);
                json_object_put(op);
                return nullptr;
            }
            lx_text(op, "register", registers[index]);
            json_object_object_add(op, "bits", json_object_new_int64(static_cast<int64_t>(bits)));
            json_object_object_add(op, "idle_cycles",
                                   json_object_new_int64(static_cast<int64_t>(idle)));
            json_object_object_add(
                op, "read",
                json_object_new_int64(index < 2 ? static_cast<int64_t>((bits + 7) / 8) : 0));
        }
        if (*communication_expect->value())
            lx_text(op, "expect", communication_expect->value());
        if (*communication_mask->value())
            lx_text(op, "mask", communication_mask->value());
        bc_error e = {{0}};
        int rc = bc_comm_operation_validate(bc_str(config, "bus"), op, &e);
        json_object_put(config);
        if (rc < 0) {
            json_object_put(op);
            lx_message(e.text);
            return nullptr;
        }
        return op;
    }

    void lx_replay_run() {
        if (!lx_capture || busy)
            return;
        auto *op = lx_source->value() == 0   ? lx_iot_request()
                   : lx_source->value() == 1 ? lx_protocol_request()
                                             : lx_manual_request();
        if (!op)
            return;
        uint64_t at, fragment, delay, offset, x;
        if (!lx_int(lx_fault_at, 0, 511, at) || !lx_int(lx_fragment, 1, 4096, fragment) ||
            !lx_int(lx_delay, 0, 60000, delay) || !lx_int(lx_fault_offset, 0, 4095, offset) ||
            !lx_int(lx_fault_xor, 1, 255, x)) {
            json_object_put(op);
            return;
        }
        auto *request = json_object_new_object(), *fault = json_object_new_object();
        json_object_object_add(request, "capture", bc_clone(lx_capture));
        json_object_object_add(request, "operation", op);
        json_object_object_add(request, "fault", fault);
        const char *faults[] = {"none",     "fragment", "delay",      "timeout",
                                "truncate", "corrupt",  "disconnect", "nack"};
        lx_text(fault, "type", faults[lx_fault->value()]);
        json_object_object_add(fault, "offset",
                               json_object_new_int64(static_cast<int64_t>(offset)));
        json_object_object_add(fault, "xor", json_object_new_int64(static_cast<int64_t>(x)));
        json_object_object_add(fault, "exchange", json_object_new_int64(static_cast<int64_t>(at)));
        json_object_object_add(fault, "fragment_size",
                               json_object_new_int64(static_cast<int64_t>(fragment)));
        json_object_object_add(fault, "delay_ms",
                               json_object_new_int64(static_cast<int64_t>(delay)));
        auto *values = json_object_new_object();
        json_object_object_add(values, "operation", request);
        launch("communication_replay", "", values);
        json_object_put(values);
        lx_controls();
    }

    std::string lx_binding_key() {
        return board_id + "/" + (test ? bc_str(test, "id") : "");
    }

    void lx_bindings_refresh() {
        auto *d = test ? bc_get(test, "communication") : nullptr;
        lx_channel->clear();
        if (lx_bound_for != lx_binding_key()) {
            json_object_put(lx_bound);
            lx_bound = json_object_new_object();
            lx_bound_for = lx_binding_key();
        }
        if (bc_num(d, "api") != 2) {
            lx_binding_note->copy_label(
                "Abra um teste multicanal instalado pelo Studio para associar seus adaptadores.");
            return;
        }
        auto *channels = bc_get(d, "channels");
        std::ostringstream summary;
        summary << bc_str(test, "title") << "\n";
        json_object_object_foreach(channels, id, decl) {
            ct_add_literal(lx_channel, id);
            summary << id << " · " << bc_str(decl, "bus") << " · ";
            auto *bound = bc_get(lx_bound, id);
            summary << (bound ? bc_str(bound, "device") : "não selecionado") << "\n";
        }
        lx_channel->value(0);
        lx_binding_note->copy_label(summary.str().c_str());
    }

    void lx_bind_current() {
        if (busy || !lx_channel->text() || !communication_applied)
            return;
        auto *d = bc_get(bc_get(test, "communication"), "channels"),
             *definition = bc_get(d, lx_channel->text());
        bc_error e = {{0}};
        auto *t = json_object_new_object();
        json_object_object_add(t, "communication", bc_clone(definition));
        auto *c = bc_comm_resolve(nullptr, t, communication_applied, &e);
        json_object_put(t);
        if (!c) {
            lx_message(e.text);
            return;
        }
        if (!*bc_str(c, "device") && !*bc_str(c, "serial")) {
            json_object_put(c);
            lx_message("Multicanal exige caminho/porta ou série exata.");
            return;
        }
        json_object_object_add(lx_bound, lx_channel->text(), c);
        lx_bindings_refresh();
        lx_message("Canal associado em memória. Configure e associe os demais antes de executar.");
    }

    json_object *lx_binding_values() {
        auto *d = test ? bc_get(test, "communication") : nullptr;
        if (bc_num(d, "api") != 2)
            return nullptr;
        auto *o = json_object_new_object(), *channels = json_object_new_object();
        json_object_object_add(o, "api", json_object_new_int(2));
        json_object_object_add(o, "channels", channels);
        if (lx_bound_for == lx_binding_key()) {
            json_object_object_foreach(bc_get(d, "channels"), id, decl) {
                (void)decl;
                if (bc_get(lx_bound, id))
                    json_object_object_add(channels, id, bc_clone(bc_get(lx_bound, id)));
            }
        }
        return o;
    }

    void lx_fw_targets_refresh() {
        lx_fw_target->clear();
        const char *families[] = {"stm32", "esp32", "fpga"};
        const char *family = families[lx_fw_family->value()];
        for (size_t i = 0; i < json_object_array_length(lx_targets); i++) {
            auto *row = json_object_array_get_idx(lx_targets, i);
            if (!strcmp(family, bc_str(row, "family")))
                ct_add_literal(lx_fw_target, bc_str(row, "id"));
        }
        lx_fw_target->value(0);
    }

    void lx_fw_images_refresh() {
        lx_fw_images->clear();
        auto *images = bc_get(lx_fw_manifest, "images");
        for (size_t i = 0; i < json_object_array_length(images); i++) {
            auto *image = json_object_array_get_idx(images, i);
            std::ostringstream s;
            s << bc_str(image, "role") << " · " << bc_str(image, "file");
            if (bc_get(image, "address"))
                s << " · 0x" << std::hex << static_cast<uint64_t>(bc_num(image, "address"));
            ct_add_literal(lx_fw_images, s.str().c_str());
        }
        lx_fw_images->value(0);
        lx_fw_review.clear();
        lx_fw_hash.clear();
    }

    void lx_fw_add() {
        auto path = choose_file("Adicionar imagem ou vetor de firmware",
                                "Firmware\t*.{bin,elf,hex,svf,xsvf}", false, "");
        if (path.empty())
            return;
        auto *images = bc_get(lx_fw_manifest, "images");
        if (json_object_array_length(images) >= 16) {
            lx_message("Use até 16 imagens.");
            return;
        }
        std::string extension = fs::path(path).extension().string();
        if (extension.empty())
            return;
        extension.erase(0, 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        uint64_t address = 0;
        if (extension == "bin" && !lx_int(lx_fw_offset, 0, UINT32_MAX, address))
            return;
        if (!bc_lab_id(lx_fw_role->value())) {
            lx_message("Papel da imagem: use letras, números e sublinhado, até 40 caracteres.");
            return;
        }
        auto *image = json_object_new_object();
        lx_text(image, "file", path.c_str());
        lx_text(image, "format", extension.c_str());
        lx_text(image, "role", lx_fw_role->value());
        if (extension == "bin")
            json_object_object_add(image, "address",
                                   json_object_new_int64(static_cast<int64_t>(address)));
        json_object_array_add(images, image);
        lx_fw_images_refresh();
    }

    json_object *lx_fw_request() {
        if (!lx_fw_target->text())
            return nullptr;
        uint64_t speed;
        if (!lx_int(lx_fw_speed, 1, 30000, speed))
            return nullptr;
        auto *m = bc_clone(lx_fw_manifest);
        const char *families[] = {"stm32", "esp32", "fpga"},
                   *actions[] = {"check", "program", "verify"};
        lx_text(m, "name", lx_fw_name->value());
        lx_text(m, "version", lx_fw_version->value());
        lx_text(m, "family", families[lx_fw_family->value()]);
        lx_text(m, "backend", lx_fw_backend->value() == 1 ? "esptool" : "openocd");
        if (!(json_object_is_type(bc_get(m, "target"), json_type_object) &&
              !strcmp(bc_str(bc_get(m, "target"), "id"), lx_fw_target->text())))
            lx_text(m, "target", lx_fw_target->text());
        auto *q = json_object_new_object();
        json_object_object_add(q, "manifest", m);
        lx_text(q, "executable", lx_fw_exe->value());
        lx_text(q, "scripts", lx_fw_scripts->value());
        lx_text(q, "serial", lx_fw_serial->value());
        lx_text(q, "action", actions[lx_fw_action->value()]);
        json_object_object_add(q, "speed_khz", json_object_new_int64(static_cast<int64_t>(speed)));
        json_object_object_add(q, "trusted_tools", json_object_new_boolean(lx_fw_trust->value()));
        json_object_object_add(q, "program_confirmed", json_object_new_boolean(0));
        if (communication_applied)
            json_object_object_add(q, "communication", bc_clone(communication_applied));
        return q;
    }

    void lx_fw_preview() {
        auto *q = lx_fw_request();
        if (!q)
            return;
        bc_error e = {{0}};
        auto *plan = bc_firmware_plan(q, &e);
        if (!plan) {
            lx_message(e.text);
            json_object_put(q);
            return;
        }
        lx_fw_review = json_object_to_json_string_ext(q, JSON_C_TO_STRING_PLAIN);
        lx_fw_hash = bc_str(plan, "sha256");
        lx_preview_buffer.text(json_object_to_json_string_ext(plan, JSON_C_TO_STRING_PRETTY));
        lx_message(
            "Prévia atual preparada: confira alvo, imagens, endereços, apagamento e SHA-256.");
        json_object_put(plan);
        json_object_put(q);
    }

    void lx_fw_execute() {
        auto *q = lx_fw_request();
        if (!q)
            return;
        if (lx_fw_review != json_object_to_json_string_ext(q, JSON_C_TO_STRING_PLAIN) ||
            lx_fw_hash.empty()) {
            json_object_put(q);
            lx_message("Prepare uma prévia atual antes de executar.");
            return;
        }
        if (!lx_fw_trust->value()) {
            json_object_put(q);
            lx_message("Confirme a origem confiável da ferramenta e dos scripts.");
            return;
        }
        bool program = !strcmp(bc_str(q, "action"), "program");
        if (fl_choice(program
                          ? "Gravar TODAS as imagens da prévia? Setores podem ser apagados; "
                            "cancelamento não restaura o firmware anterior."
                          : "Executar a consulta/verificação revisada? A ferramenta pode abrir a "
                            "porta ou reiniciar/parar o alvo.",
                      "Cancelar", program ? "Gravar pacote" : "Executar", nullptr) != 1) {
            json_object_put(q);
            return;
        }
        json_object_object_add(q, "program_confirmed", json_object_new_boolean(program));
        lx_text(q, "expected_sha256", lx_fw_hash.c_str());
        auto *v = json_object_new_object();
        json_object_object_add(v, "operation", q);
        launch("communication_firmware", "", v);
        json_object_put(v);
        lx_controls();
    }

    void lx_fw_import() {
        auto path = choose_file("Importar manifesto de firmware", "JSON\t*.json", false, "");
        if (path.empty())
            return;
        bc_error e = {{0}};
        auto *input = bc_read_json(path.c_str(), &e);
        auto *m =
            input ? bc_firmware_manifest(input, fs::path(path).parent_path().string().c_str(), &e)
                  : nullptr;
        json_object_put(input);
        if (!m) {
            lx_message(e.text);
            return;
        }
        lx_fw_adopt(m);
    }

    void lx_fw_adopt(json_object *m) {
        json_object_put(lx_fw_manifest);
        lx_fw_manifest = m;
        lx_fw_name->value(bc_str(m, "name"));
        lx_fw_version->value(bc_str(m, "version"));
        lx_fw_family->value(!strcmp(bc_str(m, "family"), "esp32")  ? 1
                            : !strcmp(bc_str(m, "family"), "fpga") ? 2
                                                                   : 0);
        lx_fw_backend->value(!strcmp(bc_str(m, "backend"), "esptool"));
        lx_fw_targets_refresh();
        const char *id = json_object_is_type(bc_get(m, "target"), json_type_object)
                             ? bc_str(bc_get(m, "target"), "id")
                             : bc_str(m, "target");
        int found = -1;
        for (int i = 0; i < lx_fw_target->size() - 1; i++)
            if (lx_fw_target->text(i) && !strcmp(lx_fw_target->text(i), id))
                found = i;
        if (found < 0)
            found = ct_add_literal(lx_fw_target, id);
        lx_fw_target->value(found);
        lx_fw_images_refresh();
        lx_message("Manifesto importado; revise a ferramenta, alvo e imagens antes de executar.");
    }

    void lx_qual_prepare() {
        auto *config =
            communication_applied ? bc_clone(communication_applied) : communication_draft();
        if (!config)
            return;
        const char *procedures[] = {"uart_loopback", "spi_loopback", "i2c_component", "jtag_chain"};
        bc_error e = {{0}};
        auto *plan = bc_qualification_plan(config, procedures[lx_qual_procedure->value()], &e);
        json_object_put(config);
        if (!plan) {
            lx_message(e.text);
            return;
        }
        json_object_put(lx_qual_plan);
        lx_qual_plan = plan;
        json_object_put(lx_qual_evidence);
        lx_qual_evidence = nullptr;
        for (auto *flag : lx_qual_flags)
            flag->value(0);
        lx_preview_buffer.text(json_object_to_json_string_ext(plan, JSON_C_TO_STRING_PRETTY));
        lx_message("Roteiro preparado sem acessar hardware. Confira cada etapa no modo/dispositivo "
                   "selecionado.");
    }

    void lx_qual_run() {
        if (!lx_qual_plan || busy)
            return;
        const char *procedure = bc_str(lx_qual_plan, "procedure");

        json_object_put(lx_qual_evidence);
        lx_qual_evidence = nullptr;
        lx_qual_waiting = true;
        if (!strcmp(procedure, "jtag_chain")) {
            auto *o = json_object_new_object();
            json_object_object_add(o, "maximum", json_object_new_int(32));
            ct_launch("communication_identify", o, "jtag");
            if (!busy)
                lx_qual_waiting = false;
            return;
        }
        if (!strcmp(procedure, "i2c_component")) {
            lx_run(lx_iot_request(), "i2c");
            if (!busy)
                lx_qual_waiting = false;
            return;
        }
        auto *o =
            json_tokener_parse("{\"write\":\"A5 5A 00 FF\",\"read\":4,\"expect\":\"A5 5A 00 FF\"}");
        lx_preview_buffer.text(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY));
        if (fl_choice(
                "Loopback exige a PCB desconectada e TX–RX (UART) ou MOSI–MISO (SPI) unidos no "
                "adaptador. As ligações estão preparadas?",
                "Cancelar", "Executar loopback", nullptr) != 1) {
            json_object_put(o);
            lx_qual_waiting = false;
            return;
        }
        lx_run(o, !strcmp(procedure, "uart_loopback") ? "uart" : "spi", false);
        if (!busy)
            lx_qual_waiting = false;
    }

    void lx_qual_assess() {
        if (!lx_qual_plan)
            return;
        auto *a = json_object_new_object();
        lx_text(a, "fingerprint", bc_str(lx_qual_plan, "fingerprint"));
        const char *keys[] = {"model_revision",  "connections", "levels",        "timing",
                              "known_component", "known_chain", "display_visual"};
        for (unsigned i = 0; i < 7; i++)
            if (lx_qual_flags[i]->value())
                json_object_object_add(a, keys[i],
                                       json_object_new_boolean(lx_qual_flags[i]->value() == 1));
        lx_text(a, "native_os", lx_qual_os->value());
        if (lx_qual_evidence)
            json_object_object_add(a, "functional", bc_clone(lx_qual_evidence));

        if (*lx_qual_expected_v->value() || *lx_qual_measured_v->value()) {
            double expected, measured;
            if (!lx_number(lx_qual_expected_v, 1, 5.5, expected) ||
                !lx_number(lx_qual_measured_v, 0, 5.5, measured)) {
                json_object_put(a);
                return;
            }
            json_object_object_add(a, "expected_voltage", json_object_new_double(expected));
            json_object_object_add(a, "measured_voltage", json_object_new_double(measured));
        }
        bc_error e = {{0}};
        auto *r = bc_qualification_assess(lx_qual_plan, a, &e);
        json_object_put(a);
        lx_result(r, r ? nullptr : e.text);
        json_object_put(r);
        lx_message("Avaliação em memória: pendente, aprovada ou reprovada segundo os registros do "
                   "operador. Exportação é opcional.");
    }

    bool lx_finished(const std::string &op, json_object *end, bool ok, const std::string &message) {
        if (op.rfind("firmware_bundle_", 0) == 0) {
            lx_bundle_finished(op, bc_get(end, "data"), ok, message);
            lx_controls();
            return true;
        }
        bool handled = op == "communication_lab" || op == "communication_replay" ||
                       op == "communication_firmware";
        auto *data = bc_get(end, "data");
        if (lx_qual_waiting && (op == "communication_lab" || op == "communication_identify")) {
            lx_qual_waiting = false;
            json_object_put(lx_qual_evidence);
            lx_qual_evidence = data ? bc_clone(data) : nullptr;
            if (lx_qual_evidence && !bc_get(lx_qual_evidence, "transport") &&
                communication_applied) {
                auto *transport = json_object_new_object();
                json_object_object_add(transport, "configuration", bc_clone(communication_applied));
                json_object_object_add(lx_qual_evidence, "transport", transport);
            }
        }
        if (handled) {
            lx_result(data, ok ? nullptr : message.c_str());
            lx_message(ok ? "Operação concluída. Confira o resultado; dados permanecem em memória."
                          : message.c_str());
            lx_controls();
            return true;
        }
        return false;
    }

    void show_lab(int tab = 0) {
        if (!lx_dialog)
            return;
        lx_bindings_refresh();
        lx_dialog->on_layout();
        skin(lx_dialog);
        if (tab >= 0 && tab < 6)
            lx_tabs->value(lx_pages[tab]);
        lx_dialog->show();
        lx_controls();
    }

    void make_lab() {
        lx_devices = bc_peripheral_catalog();
        lx_targets = bc_openocd_targets();
        lx_bound = json_object_new_object();
        lx_fw_manifest =
            json_tokener_parse("{\"schema\":1,\"name\":\"Firmware da "
                               "placa\",\"version\":\"1.0\",\"family\":\"stm32\",\"target\":"
                               "\"stm32f1x\",\"backend\":\"openocd\",\"images\":[]}");
        lx_dialog = new Dialog(1020, 740, "BPCB · Laboratório e qualificação");
        lx_dialog->minw = 1020;
        lx_dialog->minh = 700;
        lx_dialog->size_range(1020, 700);
        lx_label("Componentes, protocolos, canais, capturas e firmware", 24, 18, 972, 32);
        lx_note = label(
            "Seleção e resultados ficam em memória. Abrir estas ferramentas não transmite dados.",
            13);
        rect(lx_note, 24, 53, 972, 44);
        lx_tabs = new Fl_Tabs(24, 108, 972, 370);
        const char *names[] = {"Componentes IoT", "Protocolos UART", "Canais do teste",
                               "Reprodução",      "Firmware",        "Qualificação"};
        for (int i = 0; i < 6; i++) {
            lx_pages[i] = new Fl_Scroll(24, 140, 972, 338, names[i]);
            lx_pages[i]->type(Fl_Scroll::VERTICAL);
            lx_pages[i]->end();
        }
        lx_tabs->end();
        lx_pages[0]->begin();
        lx_label("Componente", 40, 151, 280);
        lx_label("Grandeza", 350, 151, 280);
        lx_label("Ação explícita", 660, 151, 300);
        lx_device = lx_choice(nullptr, 40, 178, 280);
        for (size_t i = 0; i < json_object_array_length(lx_devices); i++)
            ct_add_literal(lx_device, bc_str(json_object_array_get_idx(lx_devices, i), "label"));
        lx_device->value(0);
        lx_metric = lx_choice(nullptr, 350, 178, 280);
        lx_action = lx_choice(nullptr, 660, 178, 300);
        lx_device->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->lx_device_changed();
            },
            this);
        lx_label("Endereço I²C", 40, 225, 170);
        lx_address = lx_input("0x76", 40, 252, 170);
        lx_label("Shunt INA219 (ohm)", 230, 225, 230);
        lx_shunt = lx_input("0.1", 230, 252, 230);
        lx_label("LSB corrente INA219 (A)", 480, 225, 230);
        lx_lsb = lx_input("0.0001", 480, 252, 230);
        lx_label("Aceleração (2/4/8/16 g)", 40, 302, 220);
        lx_accel = lx_input("2", 40, 329, 220);
        lx_label("Giroscópio (250/500/1000/2000)", 280, 302, 260);
        lx_gyro = lx_input("250", 280, 329, 260);
        lx_label("Altura OLED (32/64)", 560, 302, 180);
        lx_height = lx_input("64", 560, 329, 180);
        lx_label("Padrão OLED", 760, 302, 200);
        lx_pattern = lx_choice("Quadriculado|Barras|Branco|Preto", 760, 329, 200);
        lx_label("BME/BMP280 aplicam compensação de fábrica. Ler usa configuração existente; medir "
                 "configura o sensor. OLED confirma ACK: confira a imagem visualmente. Apenas os "
                 "campos do componente escolhido são utilizados.",
                 40, 383, 920, 58);
        lx_button("Preparar prévia", 40, 452, 200, [this] {
            lx_preview(lx_iot_request());
        });
        lx_button("Executar uma vez…", 260, 452, 220, [this] {
            lx_run(lx_iot_request(), "i2c");
        });
        lx_pages[0]->end();
        lx_pages[1]->begin();
        lx_label("Protocolo", 40, 151, 250);
        lx_protocol =
            lx_choice("AT|Delimitado|Comprimento fixo|Modbus RTU|Quadro binário", 40, 178, 250);
        lx_protocol->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->lx_controls();
            },
            this);
        lx_label("Formato da escrita", 310, 151, 220);
        lx_encoding = lx_choice("ASCII|Hexadecimal", 310, 178, 220);
        lx_label("Terminador enviado/recebido", 550, 151, 260);
        lx_ending = lx_choice("Nenhum|LF|CR|CRLF", 550, 178, 260);
        lx_ending->value(3);
        lx_label("Comando (AT: um comando sem CR/LF embutido)", 40, 225, 920);
        lx_write = lx_input("AT", 40, 252, 920);
        lx_write->maximum_size(BC_COMM_MAX * 3);
        lx_label("Sucesso AT", 40, 299, 190);
        lx_success = lx_input("OK", 40, 326, 190);
        lx_label("Máximo bytes", 250, 299, 190);
        lx_max_bytes = lx_input("1024", 250, 326, 190);
        lx_label("Timeout total (ms)", 460, 299, 210);
        lx_timeout = lx_input("1000", 460, 326, 210);
        lx_label("Comprimento fixo", 690, 299, 270);
        lx_length = lx_input("1", 690, 326, 270);
        lx_label("Modbus: escravo", 40, 373, 180);
        lx_slave = lx_input("1", 40, 400, 180);
        lx_label("Função 1/2/3/4/5/6/15/16", 240, 373, 250);
        lx_function = lx_input("3", 240, 400, 250);
        lx_label("Primeiro endereço (base zero)", 510, 373, 250);
        lx_start = lx_input("0", 510, 400, 250);
        lx_label("Quantidade", 780, 373, 180);
        lx_quantity = lx_input("1", 780, 400, 180);
        lx_label("Dados Modbus para escrita (hex); leituras deixam vazio", 40, 447, 920);
        lx_values = lx_input("", 40, 474, 920);
        lx_label("AT trata eco, OK/ERROR e eventos ESP-AT. Modbus verifica endereço, função e CRC; "
                 "RS-485 exige transceptor de direção automática e qualificação física de "
                 "temporização. Nenhum comando é repetido pelo BPCB.",
                 40, 523, 920, 56);
        lx_button("Preparar prévia", 40, 590, 200, [this] {
            lx_preview(lx_protocol_request());
        });
        lx_button("Enviar uma vez…", 260, 590, 220, [this] {
            lx_run(lx_protocol_request(), "uart");
        });
        lx_button("Editar formato binário…", 660, 590, 300, [this] {
            auto *r = WorkbenchFrameEditor::edit(lx_frame);
            if (r) {
                json_object_put(lx_frame);
                lx_frame = r;
                lx_message(
                    "Formato aplicado em memória; use Prévia ou a reprodução antes do hardware.");
            }
        });
        lx_pages[1]->end();
        lx_pages[2]->begin();
        lx_binding_note = label("Abra um teste multicanal instalado pelo Studio.", 13);
        rect(lx_binding_note, 40, 151, 920, 145);
        lx_label("Canal declarado no teste", 40, 309, 920);
        lx_channel = lx_choice(nullptr, 40, 336, 920);
        lx_button("Configurar adaptador…", 40, 392, 250, [this] {
            skin(communication_dialog);
            communication_dialog->show();
        });
        lx_button("Associar seleção atual", 310, 392, 250, [this] {
            lx_bind_current();
        });
        lx_button("Remover associação", 580, 392, 230, [this] {
            if (lx_channel->text())
                json_object_object_del(lx_bound, lx_channel->text());
            lx_bindings_refresh();
        });
        lx_button("Atualizar canais", 40, 448, 230, [this] {
            lx_bindings_refresh();
        });
        lx_label(
            "Configure, confira conexões e use a seleção na janela Comunicação; depois associe-a "
            "ao canal. Repita para os demais. Associações valem somente nesta abertura e neste "
            "teste. Interfaces compartilhadas são usadas sequencialmente.",
            40, 510, 920, 70);
        lx_pages[2]->end();
        lx_pages[3]->begin();
        lx_button("Importar captura JSON…", 40, 151, 280, [this] {
            lx_capture_load();
        });
        lx_capture_note = label("Sem captura importada.", 13);
        rect(lx_capture_note, 40, 200, 920, 56);
        lx_label("Operação a reproduzir", 40, 275, 410);
        lx_source = lx_choice(
            "Componente da aba IoT|Protocolo da aba UART|Transferência manual configurada", 40, 302,
            600);
        lx_label("Falha simulada", 40, 349, 280);
        lx_fault = lx_choice("Nenhuma|Fragmentação UART|Atraso|Timeout|Truncar resposta|Corromper "
                             "byte|Desconexão|NACK I²C",
                             40, 376, 280);
        lx_label("Transação (índice base zero)", 340, 349, 300);
        lx_fault_at = lx_input("0", 340, 376, 300);
        lx_label("Fragmentos (bytes)", 40, 423, 280);
        lx_fragment = lx_input("1", 40, 450, 280);
        lx_label("Atraso virtual (ms)", 340, 423, 300);
        lx_delay = lx_input("1000", 340, 450, 300);
        lx_label("Byte a corromper (índice)", 660, 349, 280);
        lx_fault_offset = lx_input("0", 660, 376, 280);
        lx_label("Máscara XOR (1…255)", 660, 423, 280);
        lx_fault_xor = lx_input("1", 660, 450, 280);
        lx_button("Reproduzir sem hardware", 40, 506, 300, [this] {
            lx_replay_run();
        });
        lx_label(
            "Importação, reprodução e falhas são locais. Capturas com eventos perdidos ou várias "
            "sessões são recusadas. O resultado diferencia aprovação, divergência e erro simulado.",
            40, 563, 920, 62);
        lx_pages[3]->end();
        lx_pages[4]->begin();
        lx_label("Família", 40, 151, 200);
        lx_fw_family = lx_choice("STM32|ESP32|FPGA", 40, 178, 200);
        lx_fw_family->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->lx_fw_targets_refresh();
            },
            this);
        lx_label("Ferramenta", 260, 151, 200);
        lx_fw_backend = lx_choice("OpenOCD / JTAG|esptool v5 / UART", 260, 178, 200);
        lx_label("Alvo exato", 480, 151, 480);
        lx_fw_target = lx_choice(nullptr, 480, 178, 480);
        lx_label("Nome do pacote", 40, 225, 440);
        lx_fw_name = lx_input("Firmware da placa", 40, 252, 440);
        lx_label("Versão", 500, 225, 460);
        lx_fw_version = lx_input("1.0", 500, 252, 460);
        lx_label("Imagens adicionadas", 40, 299, 920);
        lx_fw_images = lx_choice(nullptr, 40, 326, 920);
        lx_label("Papel da próxima imagem", 40, 373, 300);
        lx_fw_role = lx_input("application", 40, 400, 300);
        lx_label("Endereço BIN da próxima imagem", 360, 373, 300);
        lx_fw_offset = lx_input("0x08000000", 360, 400, 300);
        lx_button("Adicionar imagem…", 680, 400, 280, [this] {
            lx_fw_add();
        });
        lx_button("Remover imagem", 40, 452, 200, [this] {
            int i = lx_fw_images->value();
            auto *images = bc_get(lx_fw_manifest, "images");
            if (i >= 0 && static_cast<size_t>(i) < json_object_array_length(images))
                json_object_array_del_idx(images, static_cast<size_t>(i), 1);
            lx_fw_images_refresh();
        });
        lx_button("Importar manifesto…", 260, 452, 220, [this] {
            lx_fw_import();
        });
        lx_button("Exportar manifesto…", 500, 452, 230, [this] {
            auto *q = lx_fw_request();
            if (!q)
                return;
            bc_error e = {{0}};
            auto *m = bc_firmware_manifest(bc_get(q, "manifest"), "", &e);
            json_object_put(q);
            if (!m) {
                lx_message(e.text);
                return;
            }
            auto path =
                choose_file("Salvar manifesto de firmware", "JSON\t*.json", true, "firmware.json");
            if (!path.empty() && bc_export_json(m, path.c_str(), &e) < 0)
                lx_message(e.text);
            json_object_put(m);
        });
        lx_label("Executável confiável (OpenOCD ou esptool v5)", 40, 509, 920);
        lx_fw_exe = lx_input("", 40, 536, 720);
        lx_button("Selecionar…", 780, 536, 180, [this] {
            auto path = choose_file("Selecionar ferramenta confiável", nullptr, false, "");
            if (!path.empty())
                lx_fw_exe->value(path.c_str());
        });
        lx_label("Pasta scripts do OpenOCD (esptool ignora)", 40, 583, 920);
        lx_fw_scripts = lx_input("", 40, 610, 720);
        lx_button("Pasta…", 780, 610, 180, [this] {
            Fl_Native_File_Chooser chooser;
            chooser.title("Scripts OpenOCD");
            chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
            if (chooser.show() == 0 && chooser.filename())
                lx_fw_scripts->value(chooser.filename());
        });
        lx_label("Série CH347T M3 (JTAG)", 40, 657, 440);
        lx_fw_serial = lx_input("", 40, 684, 440);
        lx_label("Clock JTAG (kHz)", 500, 657, 220);
        lx_fw_speed = lx_input("1875", 500, 684, 220);
        lx_label("Ação", 40, 731, 440);
        lx_fw_action =
            lx_choice("Conferir ferramenta (sem USB)|Gravar e verificar|Verificar", 40, 758, 440);
        lx_fw_trust =
            new Fl_Check_Button(500, 758, 460, 32, "Conferi a origem da ferramenta e dos scripts");
        lx_fw_trust->labelsize(13);
        lx_editable.push_back(lx_fw_trust);
        lx_button("Preparar prévia", 40, 815, 220, [this] {
            lx_fw_preview();
        });
        lx_button("Executar prévia…", 280, 815, 230, [this] {
            lx_fw_execute();
        });
        lx_label(
            "ESP32: escolha UART VCP exata e coloque a placa no bootloader manualmente. Várias "
            "imagens STM32 BIN são unidas em HEX endereçado; FPGA usa SVF/XSVF. Endereços e "
            "geometria vêm do projeto de firmware, sem suposições.",
            40, 875, 920, 72);
        lx_button("Importar pacote completo…", 40, 900, 300, [this] {
            lx_bundle_import();
        });
        lx_button("Exportar pacote completo…", 360, 900, 300, [this] {
            lx_bundle_export();
        });
        lx_pages[4]->end();
        lx_pages[5]->begin();
        lx_label("Procedimento de qualificação", 40, 151, 920);
        lx_qual_procedure = lx_choice("UART: loopback|SPI: loopback e temporização|I2C: componente "
                                      "conhecido|JTAG: cadeia conhecida",
                                      40, 178, 920);
        lx_button("Preparar roteiro", 40, 232, 240, [this] {
            lx_qual_prepare();
        });
        lx_button("Ensaio funcional…", 300, 232, 250, [this] {
            lx_qual_run();
        });
        const char *flags[] = {"Modelo/revisão e pinagem",
                               "GND, ligações e pull-ups",
                               "Níveis elétricos compatíveis",
                               "SPI: clock, CPOL/CPHA e CS medidos",
                               "I2C: componente/endereço conhecidos",
                               "JTAG: cadeia e IDs conferidos",
                               "OLED: imagem conferida visualmente"};
        for (unsigned i = 0; i < 7; i++) {
            int x = 40 + static_cast<int>(i % 2) * 470, y = 292 + static_cast<int>(i / 2) * 77;
            lx_label(flags[i], x, y, 450);
            lx_qual_flags[i] = lx_choice("Pendente|Conferido|Reprovado", x, y + 27, 450);
        }
        lx_label("Sistema/driver efetivamente usados", 40, 610, 920);
        lx_qual_os = lx_input(
#ifdef _WIN32
            "Windows nativo — informe driver/versão",
#else
            "Linux nativo — informe driver/versão",
#endif
            40, 637, 920);
        lx_label("Tensão lógica esperada (V; opcional)", 40, 686, 440);
        lx_qual_expected_v = lx_input("", 40, 713, 440);
        lx_label("Tensão lógica medida (V; opcional)", 500, 686, 460);
        lx_qual_measured_v = lx_input("", 500, 713, 460);
        lx_button("Avaliar qualificação", 40, 843, 280, [this] {
            lx_qual_assess();
        });
        lx_button("Registro de compatibilidade…", 340, 843, 350, [this] {
            show_compatibility();
        });
        lx_label("Qualificação exige hardware e conferência do operador. Simulações não qualificam "
                 "dispositivos.",
                 40, 900, 920, 72);
        lx_pages[5]->end();
        lx_dialog->begin();
        lx_preview_view = new Fl_Text_Display(24, 490, 972, 74);
        lx_preview_view->buffer(&lx_preview_buffer);
        lx_preview_view->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
        lx_preview_view->textsize(13);
        lx_preview_buffer.text(
            "Prévia das operações aparece aqui. Nenhuma operação começa ao selecionar uma aba.");
        lx_log_view = new Fl_Text_Display(24, 576, 972, 106);
        lx_log_view->buffer(&lx_log_buffer);
        lx_log_view->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
        lx_log_view->textsize(13);
        lx_stop = lx_button("Parar operação", 24, 700, 190, [this] {
            if (job && operation.rfind("communication_", 0) == 0)
                bc_job_cancel(job);
        });
        lx_export = lx_button("Exportar resultado…", 234, 700, 220, [this] {
            lx_save_result();
        });
        auto *close = lx_button("Fechar", 846, 700, 150, [this] {
            lx_dialog->hide();
        });
        lx_editable.erase(std::remove(lx_editable.begin(), lx_editable.end(), close),
                          lx_editable.end());
        lx_dialog->on_close = [this] {
            lx_dialog->hide();
        };
        lx_dialog->on_layout = [this, close] {
            int w = lx_dialog->w(), h = lx_dialog->h();
            rect(lx_preview_view, 24, 490, w - 48, 70);
            rect(lx_log_view, 24, 572, w - 48, std::max(74, h - 626));
            rect(lx_stop, 24, h - 40, 190, 34);
            rect(lx_export, 234, h - 40, 220, 34);
            rect(close, w - 174, h - 40, 150, 34);
        };
        lx_dialog->end();
        lx_device_changed();
        lx_fw_targets_refresh();
        lx_controls();
    }

    void destroy_lab() {
        json_object_put(lx_frame);
        json_object_put(lx_devices);
        json_object_put(lx_capture);
        json_object_put(lx_last_result);
        json_object_put(lx_bound);
        json_object_put(lx_fw_manifest);
        json_object_put(lx_targets);
        json_object_put(lx_qual_plan);
        json_object_put(lx_qual_evidence);
        lx_preview_view->buffer(nullptr);
        lx_log_view->buffer(nullptr);
        delete lx_dialog;
        lx_callbacks.clear();
    }
