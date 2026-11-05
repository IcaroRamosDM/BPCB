    Dialog *communication_dialog = nullptr;
    bool communication_edited = false;
    std::string communication_previous_bus = "i2c";
    Fl_Tabs *communication_tabs = nullptr;
    Fl_Group *communication_parameters = nullptr, *communication_manual = nullptr;
    Choice *communication_adapter, *communication_bus, *communication_device,
        *communication_spi_mode, *communication_cs, *communication_bits, *communication_stops,
        *communication_parity, *communication_flow, *communication_ch347_mode,
        *communication_jtag_register;
    StableInput *communication_path, *communication_serial, *communication_hz,
        *communication_timeout, *communication_baud, *communication_address, *communication_write,
        *communication_read, *communication_expect, *communication_mask, *communication_jtag_idle,
        *communication_jtag_ir_bits, *communication_jtag_ir_write;
    Fl_Check_Button *communication_checked, *communication_detach, *communication_lsb,
        *communication_dtr, *communication_rts;
    Button *tool_communication, *communication_refresh, *communication_apply, *communication_probe,
        *communication_export, *communication_import, *communication_send, *communication_cancel;
    Fl_Text_Buffer communication_help, communication_output;
    Fl_Text_Display *communication_help_view, *communication_output_view;
    Fl_Box *communication_note;
    Fl_Widget *communication_mode_label, *communication_address_label;
    json_object *communication_catalog = nullptr, *communication_devices = nullptr,
                *communication_applied = nullptr;
    std::vector<json_object *> communication_visible;
    std::vector<std::string> communication_buses;
    std::vector<Fl_Widget *> communication_spi_widgets, communication_uart_widgets,
        communication_jtag_widgets;
    std::vector<std::pair<Fl_Widget *, std::array<int, 4>>> communication_positions;

    const char *communication_adapter_id() {
        int i = communication_adapter->value();
        return i >= 0 && static_cast<size_t>(i) < json_object_array_length(communication_catalog)
                   ? bc_str(
                         json_object_array_get_idx(communication_catalog, static_cast<size_t>(i)),
                         "id")
                   : "ch341t";
    }

    int communication_cs_base() {
        return !std::strcmp(communication_adapter_id(), "ch347") ? 0 : 3;
    }

    const char *communication_bus_id() {
        int i = communication_bus->value();
        return i >= 0 && static_cast<size_t>(i) < communication_buses.size()
                   ? communication_buses[static_cast<size_t>(i)].c_str()
                   : "i2c";
    }

    void communication_filter_devices() {
        communication_device->clear();
        communication_device->add("Selecione ou informe o caminho abaixo");
        communication_visible.clear();
        for (size_t i = 0; i < json_object_array_length(communication_devices); i++) {
            auto *item = json_object_array_get_idx(communication_devices, i);
            std::string path = bc_str(item, "device");
            bool uart = !std::strcmp(communication_bus_id(), "uart");
            bool serial = path.rfind("/dev/tty", 0) == 0 || path.rfind("COM", 0) == 0;
            bool ch347 = !std::strcmp(communication_adapter_id(), "ch347");
            bool hid_uart = ch347 && bc_get(item, "mode") && bc_num(item, "mode") == 2 &&
                            bc_num(item, "interface") == 0;
            if ((serial || hid_uart) != uart)
                continue;
            if (ch347 && bc_get(item, "mode")) {
                int mode = static_cast<int>(bc_num(item, "mode"));
                if (!bc_comm_ch347_supports(mode, communication_bus_id()) ||
                    (communication_ch347_mode->value() > 0 &&
                     communication_ch347_mode->value() - 1 != mode) ||
                    (mode == 2 && bc_num(item, "interface") != (uart ? 0 : 1)))
                    continue;
            }
            if (std::strcmp(communication_adapter_id(), "serial") &&
                std::strcmp(communication_adapter_id(), bc_str(item, "adapter")))
                continue;
            std::string text = std::string(bc_str(item, "label")) + " · " + bc_str(item, "access");
            ct_add_literal(communication_device, text.c_str());
            communication_visible.push_back(item);
        }
        communication_device->value(0);
    }

    void communication_invalidate() {
        communication_edited = true;
        communication_checked->value(0);
        if (communication_applied)
            json_object_object_add(communication_applied, "connections_checked",
                                   json_object_new_boolean(0));
        communication_note->copy_label("Seleção alterada. Confira novamente as conexões e clique "
                                       "Usar seleção antes de executar.");
        communication_controls();
    }

    void communication_mode_changed(bool adapter_change) {
        if (busy || session_active)
            return;
        if (adapter_change) {
            communication_bus->clear();
            communication_buses.clear();
            auto *item = json_object_array_get_idx(
                communication_catalog, static_cast<size_t>(communication_adapter->value()));
            auto *buses = bc_get(item, "buses");
            for (size_t i = 0; i < json_object_array_length(buses); i++) {
                communication_buses.emplace_back(
                    json_object_get_string(json_object_array_get_idx(buses, i)));
                communication_bus->add(communication_buses.back().c_str());
            }
            communication_bus->value(0);
            communication_path->value("");
            communication_serial->value("");
            communication_ch347_mode->value(0);
        }
        communication_invalidate();
        const char *bus = communication_bus_id();
        if ((communication_previous_bus == "uart") != (!std::strcmp(bus, "uart"))) {
            communication_path->value("");
            communication_serial->value("");
        }
        communication_previous_bus = bus;
        communication_hz->value(!std::strcmp(bus, "jtag")                            ? "1875000"
                                : !std::strcmp(bus, "spi")                           ? "1000000"
                                : !std::strcmp(communication_adapter_id(), "ch341t") ? "20000"
                                                                                     : "100000");
        communication_checked->value(0);
        communication_detach->value(0);
        communication_cs->clear();
        if (!std::strcmp(communication_adapter_id(), "ch347")) {
            communication_cs->add("CS0|CS1");
        } else {
            communication_cs->add("AD3|AD4|AD5|AD6|AD7");
        }
        communication_cs->value(0);
        for (auto *w : communication_spi_widgets)
            std::strcmp(bus, "spi") ? w->hide() : w->show();
        for (auto *w : communication_uart_widgets)
            std::strcmp(bus, "uart") ? w->hide() : w->show();
        for (auto *w : communication_jtag_widgets)
            std::strcmp(bus, "jtag") ? w->hide() : w->show();
        bool ch347 = !std::strcmp(communication_adapter_id(), "ch347");
        ch347 ? communication_ch347_mode->show() : communication_ch347_mode->hide();
        ch347 ? communication_mode_label->show() : communication_mode_label->hide();
        bool jtag = !std::strcmp(bus, "jtag");
        communication_address_label->copy_label(jtag ? "Bits JTAG" : "Endereço I²C (7 bits)");
        communication_address->value(jtag ? "32" : "0x48");
        if (jtag)
            communication_read->value("4");
#ifndef _WIN32
        bool detachable = (!std::strcmp(communication_adapter_id(), "ch341t") ||
                           !std::strcmp(communication_adapter_id(), "ft232h") ||
                           !std::strcmp(communication_adapter_id(), "ch347")) &&
                          std::strcmp(bus, "uart");
        detachable ? communication_detach->show() : communication_detach->hide();
#else
        communication_detach->hide();
#endif
        (!std::strcmp(bus, "i2c") || !std::strcmp(bus, "jtag"))
            ? communication_address->activate()
            : communication_address->deactivate();
        communication_hz->activate();
        if (!std::strcmp(bus, "uart"))
            communication_hz->deactivate();
        auto *item = json_object_array_get_idx(communication_catalog,
                                               static_cast<size_t>(communication_adapter->value()));
        communication_help.text(bc_str(item, "wiring"));
        communication_filter_devices();
        communication_dialog->redraw();
    }

    json_object *communication_draft() {
        auto *input = json_object_new_object();
        bc_error e = {{0}};
        json_object_object_add(input, "adapter",
                               json_object_new_string(communication_adapter_id()));
        json_object_object_add(input, "bus", json_object_new_string(communication_bus_id()));
        json_object_object_add(input, "ch347_mode",
                               json_object_new_int(!std::strcmp(communication_adapter_id(), "ch347")
                                                       ? communication_ch347_mode->value() - 1
                                                       : -1));
        json_object_object_add(input, "device",
                               json_object_new_string(communication_path->value()));
        json_object_object_add(input, "serial",
                               json_object_new_string(communication_serial->value()));
        const std::pair<const char *, StableInput *> numbers[] = {
            {"frequency_hz", communication_hz},
            {"timeout_ms", communication_timeout},
            {"baud", communication_baud}};
        for (auto &field : numbers) {
            char *end = nullptr;
            errno = 0;
            long value = std::strtol(field.second->value(), &end, 10);
            if (errno || !end || end == field.second->value() || *end) {
                communication_note->copy_label(
                    "Confira frequência, timeout e baud: use números inteiros.");
                json_object_put(input);
                return nullptr;
            }
            json_object_object_add(input, field.first, json_object_new_int64(value));
        }
        json_object_object_add(input, "spi_mode",
                               json_object_new_int(communication_spi_mode->value()));
        json_object_object_add(
            input, "chip_select",
            json_object_new_int(communication_cs->value() + communication_cs_base()));
        json_object_object_add(input, "data_bits",
                               json_object_new_int(communication_bits->value() + 7));
        json_object_object_add(input, "stop_bits",
                               json_object_new_int(communication_stops->value() + 1));
        const char *parities[] = {"none", "even", "odd"};
        json_object_object_add(
            input, "parity",
            json_object_new_string(parities[std::clamp(communication_parity->value(), 0, 2)]));
        json_object_object_add(
            input, "flow_control",
            json_object_new_string(communication_flow->value() == 1 ? "rtscts" : "none"));
        const std::pair<const char *, Fl_Check_Button *> booleans[] = {
            {"connections_checked", communication_checked},
            {"allow_kernel_detach", communication_detach},
            {"lsb_first", communication_lsb},
            {"dtr", communication_dtr},
            {"rts", communication_rts}};
        for (auto &field : booleans)
            json_object_object_add(input, field.first,
                                   json_object_new_boolean(field.second->value()));
        auto *config = bc_comm_config(input, &e);
        json_object_put(input);
        if (!config)
            communication_note->copy_label(e.text);
        return config;
    }

    void communication_use() {
        if (busy || session_active)
            return;
        auto *config = communication_draft();
        if (!config)
            return;
        if (!json_object_get_boolean(bc_get(config, "connections_checked"))) {
            communication_note->copy_label(
                "Confira as conexões e marque a confirmação antes de usar esta seleção.");
            json_object_put(config);
            return;
        }
        if (communication_applied)
            json_object_put(communication_applied);
        communication_applied = config;
        std::string note =
            "Em uso nesta abertura: " + std::string(bc_str(config, "adapter")) + " · " +
            bc_str(config, "bus") + " · " +
            (*bc_str(config, "device") ? bc_str(config, "device")
                                       : "somente se houver um dispositivo compatível");
        communication_note->copy_label(note.c_str());
    }

    void show_communication() {
        if (busy || session_active)
            return;
        tools_dialog->hide();
        communication_dialog->on_layout();
        skin(communication_dialog);
        communication_dialog->show();
        communication_controls();
    }

    void communication_start(const char *operation) {
        if (busy || session_active)
            return;
        auto *v = json_object_new_object();
        if (std::strcmp(operation, "communication_list")) {
            auto *c = communication_draft();
            if (!c) {
                json_object_put(v);
                return;
            }
            json_object_object_add(v, "communication", c);
        }
        communication_note->copy_label("Conferindo dispositivos…");
        launch(operation, "", v);
        json_object_put(v);
    }

    void communication_save() {
        if (busy)
            return;
        auto *c = communication_draft();
        if (!c)
            return;
        auto path = choose_file("Exportar configuração de comunicação", "Configuração JSON\t*.json",
                                true, "BPCB-comunicacao.json");
        if (!path.empty()) {
            bc_error e = {{0}};
            if (bc_export_json(c, path.c_str(), &e) < 0)
                communication_note->copy_label(e.text);
            else
                communication_note->copy_label("Configuração exportada. Contém caminho USB/serial; "
                                               "conexões precisam ser conferidas ao importar.");
        }
        json_object_put(c);
    }

    void communication_assign(json_object *c) {
        for (size_t i = 0; i < json_object_array_length(communication_catalog); i++)
            if (!std::strcmp(bc_str(json_object_array_get_idx(communication_catalog, i), "id"),
                             bc_str(c, "adapter")))
                communication_adapter->value(static_cast<int>(i));
        communication_mode_changed(true);
        for (size_t i = 0; i < communication_buses.size(); i++)
            if (communication_buses[i] == bc_str(c, "bus"))
                communication_bus->value(static_cast<int>(i));
        communication_mode_changed(false);
        communication_path->value(bc_str(c, "device"));
        communication_serial->value(bc_str(c, "serial"));
        const std::pair<const char *, StableInput *> numbers[] = {
            {"frequency_hz", communication_hz},
            {"timeout_ms", communication_timeout},
            {"baud", communication_baud}};
        for (auto &field : numbers) {
            char text[32];
            std::snprintf(text, sizeof text, "%.0f", bc_num(c, field.first));
            field.second->value(text);
        }
        communication_spi_mode->value(static_cast<int>(bc_num(c, "spi_mode")));
        communication_cs->value(static_cast<int>(bc_num(c, "chip_select")) -
                                communication_cs_base());
        communication_ch347_mode->value(
            bc_get(c, "ch347_mode") ? static_cast<int>(bc_num(c, "ch347_mode")) + 1 : 0);
        communication_bits->value(static_cast<int>(bc_num(c, "data_bits")) - 7);
        communication_stops->value(static_cast<int>(bc_num(c, "stop_bits")) - 1);
        communication_parity->value(!std::strcmp(bc_str(c, "parity"), "odd")    ? 2
                                    : !std::strcmp(bc_str(c, "parity"), "even") ? 1
                                                                                : 0);
        communication_flow->value(!std::strcmp(bc_str(c, "flow_control"), "rtscts") ? 1 : 0);
        communication_dtr->value(json_object_get_boolean(bc_get(c, "dtr")));
        communication_rts->value(json_object_get_boolean(bc_get(c, "rts")));
        communication_lsb->value(json_object_get_boolean(bc_get(c, "lsb_first")));
        communication_checked->value(0);
        communication_detach->value(0);
        communication_filter_devices();
    }

    void communication_load() {
        if (busy || session_active)
            return;
        auto path = choose_file("Importar configuração de comunicação", "Configuração JSON\t*.json",
                                false, nullptr);
        if (path.empty())
            return;
        bc_error e = {{0}};
        auto *raw = bc_read_json(path.c_str(), &e), *c = raw ? bc_comm_config(raw, &e) : nullptr;
        if (raw)
            json_object_put(raw);
        if (!c) {
            communication_note->copy_label(e.text);
            return;
        }
        communication_assign(c);
        json_object_put(c);
        communication_note->copy_label("Importada para edição. Confira as conexões, autorize a "
                                       "liberação do driver se necessária e clique Usar seleção.");
    }

    void communication_transmit() {
        if (busy || session_active || running)
            return;
        auto *config = communication_draft();
        if (!config)
            return;
        if (!json_object_get_boolean(bc_get(config, "connections_checked"))) {
            communication_note->copy_label(
                "Confira as conexões e marque a confirmação antes de transmitir.");
            json_object_put(config);
            return;
        }
        auto *o = json_object_new_object();
        bool jtag = !std::strcmp(communication_bus_id(), "jtag");
        bool scan = !jtag || communication_jtag_register->value() < 2;
        json_object_object_add(o, "write",
                               json_object_new_string(scan ? communication_write->value() : ""));
        char *end = nullptr;
        long n = std::strtol(communication_read->value(), &end, 10);
        if (!end || end == communication_read->value() || *end || n < 0 || n > BC_COMM_MAX) {
            communication_note->copy_label("Quantidade de leitura inválida (0…4096).");
            json_object_put(config);
            json_object_put(o);
            return;
        }
        if (jtag) {
            const char *registers[] = {"dr", "ir", "reset", "idle"};
            int reg = std::clamp(communication_jtag_register->value(), 0, 3);
            char *bits_end = nullptr, *idle_end = nullptr;
            long bits = std::strtol(communication_address->value(), &bits_end, 10);
            long idle = std::strtol(communication_jtag_idle->value(), &idle_end, 10);
            if (!bits_end || bits_end == communication_address->value() || *bits_end || bits < 1 ||
                bits > BC_COMM_MAX * 8 || !idle_end ||
                idle_end == communication_jtag_idle->value() || *idle_end || idle < 0 ||
                idle > 4096) {
                communication_note->copy_label(
                    "JTAG: confira bits (1…32768) e clocks Idle (0…4096).");
                json_object_put(config);
                json_object_put(o);
                return;
            }
            n = reg < 2 ? (bits + 7) / 8 : 0;
            json_object_object_add(o, "register", json_object_new_string(registers[reg]));
            json_object_object_add(o, "bits", json_object_new_int64(reg < 2 ? bits : 0));
            json_object_object_add(o, "idle_cycles", json_object_new_int64(idle));
            if (reg == 0) {
                char *ir_end = nullptr;
                long ir_bits = std::strtol(communication_jtag_ir_bits->value(), &ir_end, 10);
                if (!ir_end || ir_end == communication_jtag_ir_bits->value() || *ir_end ||
                    ir_bits < 0 || ir_bits > BC_COMM_MAX * 8 ||
                    (!ir_bits && *communication_jtag_ir_write->value())) {
                    communication_note->copy_label(
                        "Confira o tamanho e os bytes da instrução IR opcional.");
                    json_object_put(config);
                    json_object_put(o);
                    return;
                }
                if (ir_bits) {
                    json_object_object_add(o, "ir_bits", json_object_new_int64(ir_bits));
                    json_object_object_add(
                        o, "ir_write",
                        json_object_new_string(communication_jtag_ir_write->value()));
                }
            }
        }
        json_object_object_add(o, "read", json_object_new_int64(n));
        if (!std::strcmp(bc_str(config, "bus"), "i2c")) {
            long address = std::strtol(communication_address->value(), &end, 0);
            if (!end || end == communication_address->value() || *end) {
                communication_note->copy_label("Endereço inválido; use decimal ou 0x48.");
                json_object_put(config);
                json_object_put(o);
                return;
            }
            json_object_object_add(o, "address", json_object_new_int64(address));
        }
        if (scan && *communication_expect->value())
            json_object_object_add(o, "expect",
                                   json_object_new_string(communication_expect->value()));
        if (scan && *communication_mask->value())
            json_object_object_add(o, "mask", json_object_new_string(communication_mask->value()));
        bc_error e = {{0}};
        if (bc_comm_operation_check(config, nullptr, o, &e) < 0) {
            communication_note->copy_label(e.text);
            json_object_put(config);
            json_object_put(o);
            return;
        }
        std::string preview =
            std::string(bc_str(config, "adapter")) + " · " + bc_str(config, "bus") +
            "\nDispositivo: " + bc_str(config, "device") +
            "\nEscrita: " + std::string(bc_str(o, "write")).substr(0, 300) +
            "\nLeitura: " + std::to_string(n) +
            " bytes\n\nEsta operação pode alterar a PCB. UART pode alterar DTR/RTS "
            "ao abrir. Ela não executa as proteções elétricas de cada procedimento.";
        if (jtag)
            preview += "\nJTAG: " + std::string(bc_str(o, "register")) + " · " +
                       std::to_string(static_cast<int>(bc_num(o, "bits"))) +
                       " bits; a abertura reinicia TAP. Varreduras IR podem alterar o "
                       "funcionamento do alvo.";
        if (bc_get(o, "ir_bits"))
            preview +=
                "\nIR antes de DR: " + std::to_string(static_cast<int>(bc_num(o, "ir_bits"))) +
                " bits · " + std::string(bc_str(o, "ir_write")).substr(0, 300);
        if (fl_choice("%s", "Cancelar", "Transmitir", nullptr, preview.c_str()) != 1) {
            json_object_put(config);
            json_object_put(o);
            return;
        }
        auto *v = json_object_new_object();
        json_object_object_add(v, "communication", config);
        json_object_object_add(v, "operation", o);
        communication_output.text("Transação em andamento…");
        launch("communication_exchange", "", v);
        json_object_put(v);
    }

    bool communication_finished(const std::string &op, json_object *end, bool ok,
                                const std::string &message) {
        if (ct_finished(op, end, ok, message))
            return true;
        if (op.rfind("communication_", 0) != 0)
            return false;
        if (!ok) {
            communication_note->copy_label(message.c_str());
            communication_output.text(message.c_str());
            return true;
        }
        auto *data = bc_get(end, "data");
        if (op == "communication_list") {
            if (communication_devices)
                json_object_put(communication_devices);
            communication_devices = bc_clone(data);
            communication_filter_devices();
            communication_note->copy_label(
                "Lista atualizada. Selecione o dispositivo; enumerar não envia comandos à PCB.");
        } else if (op == "communication_probe")
            communication_note->copy_label(bc_str(data, "scope"));
        else {
            std::ostringstream text;
            text << "Recebido (hex): " << bc_str(data, "received")
                 << "\nBytes escritos: " << bc_num(data, "bytes_written")
                 << " · lidos: " << bc_num(data, "bytes_read");
            if (bc_num(data, "actual_hz"))
                text << "\nClock efetivo configurado: " << bc_num(data, "actual_hz") << " Hz";
            if (bc_get(data, "matched"))
                text << "\nComparação: "
                     << (json_object_get_boolean(bc_get(data, "matched")) ? "corresponde"
                                                                          : "DIFERE do esperado");
            text << "\nResultado em memória. A comparação se limita aos bytes informados.";
            communication_output.text(text.str().c_str());
            communication_note->copy_label("Transação concluída. Confira os dados recebidos.");
        }
        return true;
    }

    void communication_controls() {
        ct_controls();
        if (!communication_dialog)
            return;
        bool enabled = !busy && !running && !session_active;
        for (auto &field : communication_positions) {
            auto *w = field.first;
            if (w != communication_note && w != communication_help_view &&
                w != communication_output_view)
                set_enabled(w, enabled);
        }
        set_enabled(communication_cancel, busy && operation.rfind("communication_", 0) == 0);
        if (!std::strcmp(communication_bus_id(), "uart"))
            communication_hz->deactivate();
        if (std::strcmp(communication_bus_id(), "i2c") &&
            std::strcmp(communication_bus_id(), "jtag"))
            communication_address->deactivate();
        if (!std::strcmp(communication_bus_id(), "jtag")) {
            communication_read->deactivate();
            char *end = nullptr;
            long bits = std::strtol(communication_address->value(), &end, 10);
            if (end && end != communication_address->value() && !*end && bits > 0 &&
                bits <= BC_COMM_MAX * 8) {
                std::string n =
                    std::to_string(communication_jtag_register->value() < 2 ? (bits + 7) / 8 : 0);
                communication_read->value(n.c_str());
            }
            if (communication_jtag_register->value() != 0) {
                communication_jtag_ir_bits->deactivate();
                communication_jtag_ir_write->deactivate();
            }
            if (communication_jtag_register->value() >= 2) {
                communication_write->deactivate();
                communication_expect->deactivate();
                communication_mask->deactivate();
            }
        }
        bool hid_uart = !std::strcmp(communication_adapter_id(), "ch347") &&
                        !std::strcmp(communication_bus_id(), "uart") &&
                        (communication_ch347_mode->value() == 3 ||
                         std::string(communication_path->value()).rfind("/dev/hidraw", 0) == 0);
        if (hid_uart) {
            communication_flow->value(0);
            communication_dtr->value(0);
            communication_rts->value(0);
            communication_flow->deactivate();
            communication_dtr->deactivate();
            communication_rts->deactivate();
        }
        set_enabled(tool_communication, enabled);
        for (auto *b : {communication_apply, communication_probe, communication_export,
                        communication_import, communication_refresh, communication_send})
            set_enabled(b, enabled);
    }

    void make_communication() {
        communication_catalog = bc_comm_catalog();
        communication_devices = json_object_new_array();
        communication_dialog = new Dialog(900, 740, "BPCB · Comunicação da PCB");
        communication_dialog->minw = 840;
        communication_dialog->minh = 700;
        communication_dialog->size_range(840, 700);
        auto place = [this](Fl_Widget *w, int x, int y, int width, int height) {
            rect(w, x, y, width, height);
            communication_positions.push_back({w, {x, y, width, height}});
            return w;
        };
        auto text = [&](const char *name, int x, int y, int width) {
            return place(label(name, 13), x, y, width, 24);
        };
        auto input = [&](int x, int y, int width, const char *value) {
            auto *w = new StableInput(0, 0, 1, 1);
            w->box(input_box);
            w->textsize(14);
            w->value(value);
            w->maximum_size(1023);
            place(w, x, y, width, 32);
            return w;
        };
        auto choice = [&](int x, int y, int width, const char *items) {
            auto *w = new Choice();
            w->box(input_box);
            w->textsize(14);
            if (items)
                w->add(items);
            w->value(0);
            place(w, x, y, width, 32);
            return w;
        };
        auto checkbox = [&](const char *name, int x, int y, int width) {
            auto *w = new Fl_Check_Button(0, 0, 1, 1, name);
            w->labelsize(13);
            place(w, x, y, width, 28);
            return w;
        };
        place(label("Comunicação da PCB", 23, true), 24, 16, 820, 36);
        communication_note =
            label("Seleção em memória; o fluxo legado usa CH341T/I²C a 20 kHz. Nenhum "
                  "dispositivo foi consultado.",
                  13);
        place(communication_note, 24, 54, 820, 44);
        text("Adaptador", 24, 98, 300);
        text("Barramento", 314, 98, 100);
        communication_mode_label = text("Modo CH347", 434, 98, 140);
        communication_adapter = choice(24, 124, 270, nullptr);
        for (size_t i = 0; i < json_object_array_length(communication_catalog); i++)
            communication_adapter->add(
                bc_str(json_object_array_get_idx(communication_catalog, i), "label"));
        communication_adapter->value(0);
        communication_bus = choice(314, 124, 100, nullptr);
        communication_ch347_mode =
            choice(434, 124, 140, "Automático|M0 UARTs|M1 I²C + SPI|M2 HID|M3 JTAG");
        communication_refresh = new Button("Atualizar dispositivos");
        place(communication_refresh, 594, 124, 240, 32);
        text("Dispositivo detectado", 24, 163, 740);
        communication_device = choice(24, 187, 746, nullptr);
        text("Caminho USB/HID ou porta serial (vazio: somente um dispositivo compatível)", 24, 226,
             570);
        text("Série (opcional)", 610, 226, 160);
        communication_path = input(24, 251, 565, "");
        communication_serial = input(610, 251, 160, "");
        communication_checked =
            checkbox("Conferi conexões, GND e níveis lógicos do adaptador e da PCB", 24, 291, 746);
        communication_tabs = new Fl_Tabs(24, 330, 852, 306);
        communication_tabs->labelsize(14);
        communication_parameters = new Fl_Group(24, 360, 852, 276, "Parâmetros e ligações");
        text("Clock (Hz)", 40, 371, 180);
        communication_hz = input(40, 397, 160, "20000");
        text("Timeout total (ms)", 220, 371, 150);
        communication_timeout = input(220, 397, 150, "1000");
        auto *mode_label = text("Modo (CPOL, CPHA)", 390, 371, 150);
        communication_spi_mode = choice(390, 397, 140, "0 (0, 0)|1 (0, 1)|2 (1, 0)|3 (1, 1)");
        auto *cs_label = text("CS ativo em 0", 550, 371, 130);
        communication_cs = choice(550, 397, 130, "AD3|AD4|AD5|AD6|AD7");
        communication_lsb = checkbox("LSB primeiro", 700, 397, 145);
        communication_spi_widgets = {mode_label, communication_spi_mode, cs_label, communication_cs,
                                     communication_lsb};
        auto *reg_label = text("Operação JTAG", 390, 371, 150);
        communication_jtag_register = choice(390, 397, 140, "DR|IR|Reset TAP|Idle TCK");
        auto *idle_label = text("Clocks Idle", 550, 371, 130);
        communication_jtag_idle = input(550, 397, 130, "0");
        auto *ir_bits_label = text("Bits IR (opcional)", 40, 438, 150);
        communication_jtag_ir_bits = input(40, 464, 150, "0");
        auto *ir_write_label = text("Instrução IR hexadecimal (opcional)", 210, 438, 470);
        communication_jtag_ir_write = input(210, 464, 470, "");
        communication_jtag_widgets = {reg_label,      communication_jtag_register,
                                      idle_label,     communication_jtag_idle,
                                      ir_bits_label,  communication_jtag_ir_bits,
                                      ir_write_label, communication_jtag_ir_write};
        auto *baud_label = text("Baud UART", 40, 438, 150);
        communication_baud = input(40, 464, 150, "115200");
        auto *bits_label = text("Dados", 210, 438, 85);
        communication_bits = choice(210, 464, 85, "7|8");
        communication_bits->value(1);
        auto *stops_label = text("Parada", 315, 438, 85);
        communication_stops = choice(315, 464, 85, "1|2");
        auto *parity_label = text("Paridade", 420, 438, 105);
        communication_parity = choice(420, 464, 105, "Nenhuma|Par|Ímpar");
        auto *flow_label = text("Fluxo", 545, 438, 135);
        communication_flow = choice(545, 464, 135, "Nenhum|RTS + CTS");
        communication_dtr = checkbox("DTR", 700, 438, 70);
        communication_rts = checkbox("RTS", 700, 472, 70);
        communication_uart_widgets = {baud_label,         communication_baud,   bits_label,
                                      communication_bits, stops_label,          communication_stops,
                                      parity_label,       communication_parity, flow_label,
                                      communication_flow, communication_dtr,    communication_rts};
        communication_detach = checkbox(
            "Linux: autorizar liberação temporária do driver da interface USB", 40, 505, 790);
        communication_help_view = display(communication_help);
        place(communication_help_view, 40, 546, 812, 75);
        communication_parameters->end();
        communication_manual = new Fl_Group(24, 360, 852, 276, "Transferência manual");
        communication_address_label = text("Endereço I²C (7 bits)", 40, 371, 170);
        communication_address = input(40, 397, 150, "0x48");
        text("Ler bytes", 210, 371, 120);
        communication_read = input(210, 397, 120, "1");
        text("Escrever bytes hex (ex.: 01 A0 FF)", 350, 371, 490);
        communication_write = input(350, 397, 490, "");
        communication_write->maximum_size(BC_COMM_MAX * 3);
        text("Resposta esperada hex (opcional)", 40, 438, 390);
        communication_expect = input(40, 464, 385, "");
        communication_expect->maximum_size(BC_COMM_MAX * 3);
        text("Máscara hex (opcional; FF compara todos os bits)", 445, 438, 395);
        communication_mask = input(445, 464, 395, "");
        communication_mask->maximum_size(BC_COMM_MAX * 3);
        communication_send = new Button("Transmitir uma vez", 1);
        place(communication_send, 40, 508, 210, 34);
        communication_output_view = display(communication_output);
        place(communication_output_view, 270, 508, 570, 113);
        communication_output.text(
            "Transmissão explícita. Não há varredura I²C automática nem repetição de escritas. "
            "Configure o SPI para ler o mesmo número de bytes enviados.");
        communication_manual->end();
        auto *tools = new Fl_Group(24, 360, 852, 276, "Terminal e exploradores");
        const char *names[] = {"Terminal UART", "Perfis nomeados", "Explorador I²C",
                               "JTAG / OpenOCD"};
        for (int i = 0; i < 4; i++) {
            auto *b = new Button(names[i]);
            place(b, 60 + (i % 2) * 400, 395 + (i / 2) * 85, 350, 52);
            b->argument(i);
            b->callback(
                [](Fl_Widget *w, void *v) {
                    static_cast<App *>(v)->show_communication_tools(
                        static_cast<int>(w->argument()));
                },
                this);
        }
        auto *lab_button =
            new Button("Laboratório IoT, protocolos, canais, firmware e qualificação");
        place(lab_button, 60, 565, 750, 38);
        lab_button->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_lab();
            },
            this);
        tools->end();
        communication_tabs->end();
        communication_tabs->value(communication_parameters);
        communication_apply = new Button("Usar seleção", 1);
        communication_probe = new Button("Conferir acesso");
        communication_export = new Button("Exportar config.");
        communication_import = new Button("Importar config.");
        communication_cancel = new Button("Parar");
        auto *close = new Button("Fechar");
        communication_apply->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_use();
            },
            this);
        communication_probe->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_start("communication_probe");
            },
            this);
        communication_export->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_save();
            },
            this);
        communication_import->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_load();
            },
            this);
        communication_cancel->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (a->job && a->operation.rfind("communication_", 0) == 0)
                    bc_job_cancel(a->job);
            },
            this);
        close->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_dialog->hide();
            },
            this);
        communication_refresh->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_start("communication_list");
            },
            this);
        communication_adapter->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_mode_changed(true);
            },
            this);
        communication_bus->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_mode_changed(false);
            },
            this);
        communication_device->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                int n = a->communication_device->value() - 1;
                if (n >= 0 && static_cast<size_t>(n) < a->communication_visible.size()) {
                    auto *item = a->communication_visible[static_cast<size_t>(n)];
                    a->communication_path->value(bc_str(item, "device"));
                    a->communication_serial->value(bc_str(item, "serial"));
                    if (!std::strcmp(a->communication_adapter_id(), "ch347") &&
                        bc_get(item, "mode"))
                        a->communication_ch347_mode->value(static_cast<int>(bc_num(item, "mode")) +
                                                           1);
                    a->communication_invalidate();
                }
            },
            this);
        communication_path->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->communication_invalidate();
            },
            this);
        for (auto *w : std::vector<Fl_Widget *>{
                 communication_path, communication_serial, communication_hz, communication_timeout,
                 communication_baud, communication_spi_mode, communication_cs, communication_bits,
                 communication_stops, communication_parity, communication_flow, communication_dtr,
                 communication_rts, communication_lsb, communication_detach,
                 communication_jtag_register, communication_jtag_idle, communication_jtag_ir_bits,
                 communication_jtag_ir_write}) {
            w->when(FL_WHEN_CHANGED);
            w->callback(
                [](Fl_Widget *, void *v) {
                    static_cast<App *>(v)->communication_invalidate();
                },
                this);
        }
        communication_ch347_mode->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (a->busy || a->session_active)
                    return;
                a->communication_path->value("");
                a->communication_serial->value("");
                a->communication_invalidate();
                a->communication_filter_devices();
            },
            this);
        communication_address->when(FL_WHEN_CHANGED);
        communication_address->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_controls();
            },
            this);
        communication_send->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->communication_transmit();
            },
            this);
        communication_dialog->on_layout = [this, close] {
            int h = communication_dialog->h();
            double scale = static_cast<double>(communication_dialog->w()) / 900.0;
            communication_tabs->resize(24, 330, communication_dialog->w() - 48, h - 434);
            communication_parameters->resize(24, 360, communication_dialog->w() - 48, h - 464);
            communication_manual->resize(24, 360, communication_dialog->w() - 48, h - 464);
            for (auto &field : communication_positions) {
                auto &r = field.second;
                rect(field.first, static_cast<int>(r[0] * scale), r[1],
                     static_cast<int>(r[2] * scale), r[3]);
            }
            rect(communication_help_view, static_cast<int>(40 * scale), 546,
                 static_cast<int>(812 * scale), std::max(36, h - 662));
            rect(communication_output_view, static_cast<int>(270 * scale), 508,
                 static_cast<int>(570 * scale), std::max(76, h - 624));
            Button *buttons[] = {communication_apply,  communication_probe,  communication_export,
                                 communication_import, communication_cancel, close};
            int xs[] = {24, 179, 354, 504, 654, 754}, ws[] = {145, 165, 140, 140, 90, 122};
            for (int i = 0; i < 6; i++)
                rect(buttons[i], static_cast<int>(xs[i] * scale), h - 70,
                     static_cast<int>(ws[i] * scale), 38);
        };
        communication_dialog->on_close = [this] {
            communication_dialog->hide();
        };
        communication_dialog->end();
        communication_mode_changed(true);
        communication_edited = false;
    }

    void destroy_communication() {
        if (communication_applied)
            json_object_put(communication_applied);
        if (communication_catalog)
            json_object_put(communication_catalog);
        if (communication_devices)
            json_object_put(communication_devices);
        communication_help_view->buffer(nullptr);
        communication_output_view->buffer(nullptr);
        delete communication_dialog;
    }
