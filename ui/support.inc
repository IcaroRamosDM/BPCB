    Dialog *diagnostic_dialog = nullptr, *recovery_dialog = nullptr;
    Fl_Text_Buffer diagnostic_text, recovery_text;
    Fl_Text_Display *diagnostic_view, *recovery_view;
    Fl_Check_Button *diagnostic_instrument;
    Button *diagnostic_preview, *diagnostic_export, *recovery_refresh, *recovery_inspect,
        *recovery_undo, *recovery_keep;
    Choice *recovery_choice;
    json_object *diagnostic_snapshot = nullptr, *support_messages = json_object_new_array(),
                *recovery_items = nullptr;
    std::string recovery_token, recovery_id;
    bool recovery_blocked = false;

    const char *recovery_state_label(const char *state) {
        if (!std::strcmp(state, "prepared"))
            return "Preparada, revisão pendente";
        if (!std::strcmp(state, "applying"))
            return "Restauração interrompida";
        if (!std::strcmp(state, "recovering"))
            return "Recuperação interrompida";
        if (!std::strcmp(state, "completed"))
            return "Restauração concluída";
        if (!std::strcmp(state, "recovered"))
            return "Dados anteriores recuperados";
        if (!std::strcmp(state, "kept"))
            return "Dados atuais mantidos";
        if (!std::strcmp(state, "rolled_back"))
            return "Falha revertida";
        if (!std::strcmp(state, "legacy"))
            return "Cópia antiga, revisão manual";
        return "Diário inválido, revisão manual";
    }

    void support_log(const std::string &op, json_object *end) {
        if (!end || op.rfind("diagnostic_", 0) == 0)
            return;
        auto *row = json_object_new_object();
        json_object_object_add(row, "operation", json_object_new_string(op.c_str()));
        json_object_object_add(row, "status", json_object_new_string(bc_str(end, "status")));
        std::string message = bc_str(end, "message");
        if (message.size() > 2000)
            message.resize(2000);
        json_object_object_add(row, "message", json_object_new_string(message.c_str()));
        json_object_array_add(support_messages, row);
        if (json_object_array_length(support_messages) > 100)
            json_object_array_del_idx(support_messages, 0, 1);
    }

    void invalidate_diagnostic() {
        if (diagnostic_snapshot) {
            json_object_put(diagnostic_snapshot);
            diagnostic_snapshot = nullptr;
        }
        diagnostic_text.text(
            "Clique em Gerar prévia para conferir exatamente o conteúdo que será exportado.");
        support_controls();
    }

    void show_diagnostic() {
        if (busy)
            return;
        tools_dialog->hide();
        diagnostic_dialog->on_layout();
        skin(diagnostic_dialog);
        diagnostic_dialog->show();
        support_controls();
    }

    void preview_diagnostic() {
        if (busy)
            return;
        auto *context = json_object_new_object();
        json_object_object_add(context, "messages", bc_clone(support_messages));
        json_object_object_add(context, "history_enabled",
                               json_object_new_boolean(record_enabled->value()));
        json_object_object_add(context, "include_instrument",
                               json_object_new_boolean(diagnostic_instrument->value()));
        if (rigol_snapshot)
            json_object_object_add(context, "instrument", bc_clone(rigol_snapshot));
        invalidate_diagnostic();
        launch("diagnostic_preview", "", context);
        json_object_put(context);
    }

    void export_diagnostic() {
        if (busy || !diagnostic_snapshot)
            return;
        auto path = choose_file("Exportar a prévia do diagnóstico", "JSON\t*.json", true,
                                "BPCB-diagnostico.json");
        if (path.empty())
            return;
        launch("diagnostic_export", path, diagnostic_snapshot);
    }

    void recovery_guard() {
        bc_error e = {{0}};
        int pending = bc_recovery_pending(root.c_str(), &e);
        recovery_blocked = pending != 0;
    }

    void show_recovery() {
        if (busy || session_active)
            return;
        tools_dialog->hide();
        recovery_dialog->on_layout();
        skin(recovery_dialog);
        recovery_dialog->show();
        recovery_token.clear();
        launch("recovery_list", "");
    }

    void inspect_recovery() {
        int index = recovery_choice->value();
        if (busy || index < 0 || !recovery_items)
            return;
        auto *item = json_object_array_get_idx(recovery_items, static_cast<size_t>(index));
        if (!item)
            return;
        recovery_id = bc_str(item, "id");
        recovery_token.clear();
        launch("recovery_inspect", recovery_id);
    }

    void apply_recovery(bool keep) {
        if (busy || session_active || recovery_token.empty())
            return;
        if (mount_changed) {
            recovery_text.text(
                "Há edições não salvas de componentes. Preserve-as antes de recuperar os dados.");
            return;
        }
        const char *message =
            keep ? "Manter os dados atuais, mesmo que a restauração tenha sido parcial, e liberar "
                   "os ensaios? Confira cada cadastro antes de testar."
                 : "Recuperar os valores anteriores indicados na prévia? A restauração será "
                   "desfeita somente onde os dados não mudaram depois dela.";
        if (fl_choice("%s", "Cancelar", keep ? "Manter atuais" : "Recuperar anteriores", nullptr,
                      message) != 1)
            return;
        auto *values = json_object_new_object();
        json_object_object_add(values, "keep_current", json_object_new_boolean(keep));
        launch("recovery_apply", recovery_id, values, recovery_token);
        json_object_put(values);
    }

    void support_controls() {
        if (!diagnostic_dialog)
            return;
        set_enabled(diagnostic_preview, !busy);
        set_enabled(diagnostic_instrument, !busy);
        set_enabled(diagnostic_export, !busy && diagnostic_snapshot);
        set_enabled(recovery_refresh, !busy && !session_active);
        set_enabled(recovery_choice, !busy);
        set_enabled(recovery_inspect, !busy && recovery_choice->value() >= 0);
        set_enabled(recovery_undo, !busy && !session_active && !recovery_token.empty());
        set_enabled(recovery_keep, !busy && !session_active && !recovery_token.empty());
    }

    bool support_finished(const std::string &op, json_object *end, bool ok,
                          const std::string &message) {
        if (op == "diagnostic_preview") {
            if (diagnostic_snapshot)
                json_object_put(diagnostic_snapshot);
            diagnostic_snapshot = ok ? bc_clone(bc_get(end, "data")) : nullptr;
            diagnostic_text.text(
                ok ? json_object_to_json_string_ext(diagnostic_snapshot, JSON_C_TO_STRING_PRETTY)
                   : message.c_str());
            return true;
        }
        if (op == "diagnostic_export") {
            set_status(ok ? "Prévia exportada. Nenhum dado foi enviado pela rede." : message);
            return true;
        }
        if (op.rfind("recovery_", 0) != 0)
            return false;
        recovery_guard();
        if (!ok) {
            recovery_token.clear();
            recovery_text.text(message.c_str());
            return true;
        }
        if (op == "recovery_list") {
            if (recovery_items)
                json_object_put(recovery_items);
            recovery_items = bc_clone(bc_get(end, "data"));
            recovery_choice->clear();
            recovery_token.clear();
            int first = -1;
            for (size_t i = 0; i < json_object_array_length(recovery_items); i++) {
                auto *item = json_object_array_get_idx(recovery_items, i);
                std::string name = bc_str(item, "id");
                name += " · ";
                name += recovery_state_label(bc_str(item, "state"));
                recovery_choice->add(name.c_str());
                if (first < 0 && json_object_get_boolean(bc_get(item, "pending")))
                    first = static_cast<int>(i);
            }
            recovery_choice->value(first >= 0                                 ? first
                                   : json_object_array_length(recovery_items) ? 0
                                                                              : -1);
            recovery_text.text(
                recovery_blocked
                    ? "Há restauração pendente ou diário inválido. Novos ensaios estão bloqueados. "
                      "Selecione a cópia e confira a prévia antes de recuperar os dados anteriores "
                      "ou manter os atuais. Nenhuma ação é automática."
                    : "Nenhuma restauração interrompida. As cópias concluídas também podem ser "
                      "conferidas para desfazer uma restauração. Cópias antigas (legacy) exigem "
                      "conferência manual.");
        } else if (op == "recovery_inspect") {
            auto *data = bc_get(end, "data");
            recovery_token = bc_str(data, "token");
            std::ostringstream text;
            text << recovery_id << "\nEstado: " << recovery_state_label(bc_str(data, "state"))
                 << "\nAlterações posteriores: " << bc_num(data, "conflicts") << "\n\n";
            auto *files = bc_get(data, "files");
            for (size_t i = 0; i < json_object_array_length(files); i++) {
                auto *item = json_object_array_get_idx(files, i);
                text << bc_str(item, "path") << "\n  " << bc_str(item, "status") << "\n";
            }
            text << "\nRecuperar anteriores recusa alterações posteriores. Manter atuais apenas "
                    "encerra a pendência. As cópias permanecem na pasta de dados.";
            recovery_text.text(text.str().c_str());
        } else {
            recovery_token.clear();
            cal_active_id.clear();
            if (profile)
                load_board(board_id);
            show_recovery();
            set_status("Recuperação revisada. Confira os cadastros e a calibração antes do próximo "
                       "ensaio.");
        }
        return true;
    }

    void make_support() {
        diagnostic_dialog = new Dialog(900, 680, "BPCB · Diagnóstico técnico");
        diagnostic_dialog->minw = 760;
        diagnostic_dialog->minh = 580;
        diagnostic_dialog->size_range(760, 580);
        auto *intro = label("Confira a prévia antes de exportar. Contém versão, sistema, pacote e "
                            "até 100 mensagens desta abertura. Mensagens podem citar caminhos "
                            "locais. Nenhum envio é automático.",
                            14);
        diagnostic_instrument = new Fl_Check_Button(
            0, 0, 100, 28, "Incluir última consulta ao Rigol (pode conter IP e número de série)");
        diagnostic_instrument->labelsize(14);
        diagnostic_instrument->value(0);
        diagnostic_instrument->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->invalidate_diagnostic();
            },
            this);
        diagnostic_view = display(diagnostic_text);
        diagnostic_text.text("Clique em Gerar prévia para conferir o conteúdo.");
        diagnostic_preview = new Button("Gerar prévia");
        diagnostic_export = new Button("Exportar prévia", 1);
        auto *close = new Button("Fechar");
        diagnostic_preview->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->preview_diagnostic();
            },
            this);
        diagnostic_export->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->export_diagnostic();
            },
            this);
        close->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->diagnostic_dialog->hide();
            },
            this);
        diagnostic_dialog->on_layout = [this, intro, close] {
            int w = diagnostic_dialog->w(), h = diagnostic_dialog->h();
            rect(intro, 24, 20, w - 48, 66);
            rect(diagnostic_instrument, 24, 100, w - 48, 30);
            rect(diagnostic_view, 24, 146, w - 48, h - 224);
            rect(diagnostic_preview, 24, h - 58, 170, 38);
            rect(diagnostic_export, 206, h - 58, 180, 38);
            rect(close, w - 154, h - 58, 130, 38);
        };
        diagnostic_dialog->on_close = [this] {
            diagnostic_dialog->hide();
        };
        diagnostic_dialog->end();
        recovery_dialog = new Dialog(930, 660, "BPCB · Recuperação de restauração");
        recovery_dialog->minw = 800;
        recovery_dialog->minh = 560;
        recovery_dialog->size_range(800, 560);
        recovery_choice = new Choice();
        recovery_choice->textsize(14);
        recovery_choice->box(input_box);
        recovery_choice->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->recovery_token.clear();
                a->recovery_text.text("Seleção alterada. Confira novamente a prévia.");
                a->support_controls();
            },
            this);
        recovery_refresh = new Button("Atualizar lista");
        recovery_inspect = new Button("Conferir prévia", 1);
        recovery_undo = new Button("Recuperar anteriores");
        recovery_keep = new Button("Manter atuais");
        auto *close_recovery = new Button("Fechar");
        recovery_view = display(recovery_text);
        recovery_refresh->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_recovery();
            },
            this);
        recovery_inspect->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->inspect_recovery();
            },
            this);
        recovery_undo->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->apply_recovery(false);
            },
            this);
        recovery_keep->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->apply_recovery(true);
            },
            this);
        close_recovery->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->recovery_dialog->hide();
            },
            this);
        recovery_dialog->on_layout = [this, close_recovery] {
            int w = recovery_dialog->w(), h = recovery_dialog->h();
            rect(recovery_choice, 24, 24, w - 48, 36);
            rect(recovery_refresh, 24, 76, 170, 38);
            rect(recovery_inspect, 206, 76, 180, 38);
            rect(recovery_view, 24, 132, w - 48, h - 210);
            rect(recovery_undo, 24, h - 58, 230, 38);
            rect(recovery_keep, 266, h - 58, 180, 38);
            rect(close_recovery, w - 154, h - 58, 130, 38);
        };
        recovery_dialog->on_close = [this] {
            recovery_dialog->hide();
        };
        recovery_dialog->end();
    }

    void destroy_support() {
        if (diagnostic_snapshot)
            json_object_put(diagnostic_snapshot);
        if (recovery_items)
            json_object_put(recovery_items);
        json_object_put(support_messages);
        diagnostic_view->buffer(nullptr);
        recovery_view->buffer(nullptr);
        delete diagnostic_dialog;
        delete recovery_dialog;
    }
