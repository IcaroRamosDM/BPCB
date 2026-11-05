    Dialog *history_dialog = nullptr, *record_dialog = nullptr;
    Button *history_open, *history_close, *history_export, *history_delete, *record_setup;
    Button *history_pdf, *history_mark, *history_compare;
    Fl_Check_Button *record_enabled;
    Fl_Input *record_board, *record_device, *record_operator, *history_filter;
    Fl_Hold_Browser *history_entries;
    Fl_Text_Buffer history_text;
    Fl_Text_Display *history_detail;
    Fl_Box *history_note;
    json_object *history_items = nullptr;
    std::vector<std::string> history_ids;
    std::string history_selected;

    const char *run_status(const char *s) {
        return !std::strcmp(s, "ok")          ? "Concluído"
               : !std::strcmp(s, "error")     ? "Falhou"
               : !std::strcmp(s, "cancelled") ? "Interrompido"
                                              : "Sem conclusão registrada";
    }

    void make_history() {
        record_dialog = new Dialog(650, 430, "BPCB · Identificação do ensaio");
        record_dialog->minw = 650;
        record_dialog->minh = 430;
        record_dialog->size_range(650, 430);
        record_dialog->set_modal();
        auto *title = label("Identificação do ensaio", 23, true);
        rect(title, 24, 20, 602, 36);
        auto *note =
            label("Identificação opcional para o resultado e o relatório PDF. Gravar no histórico "
                  "depende de “Guardar resultado”. Não altera a calibração selecionada.",
                  14);
        rect(note, 24, 66, 602, 60);
        const char *names[] = {"Placa física", "Dispositivo / injetor", "Operador"};
        Fl_Input **inputs[] = {&record_board, &record_device, &record_operator};
        for (int i = 0; i < 3; i++) {
            auto *l = label(names[i], 13);
            rect(l, 24, 141 + i * 67, 195, 22);
            *inputs[i] = new StableInput(220, 135 + i * 67, 404, 38);
            (*inputs[i])->box(input_box);
            (*inputs[i])->maximum_size(256);
        }
        auto *done = new Button("Concluir", 1);
        rect(done, 470, 370, 154, 38);
        done->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->record_dialog->hide();
            },
            this);
        record_dialog->on_close = [this] {
            record_dialog->hide();
        };
        record_dialog->on_layout = [] {};
        record_dialog->end();
        history_dialog = new Dialog(1040, 720, "BPCB · Histórico de resultados");
        history_dialog->minw = 800;
        history_dialog->minh = 560;
        history_dialog->size_range(800, 560);
        history_note = label("", 13);
        history_filter = new StableInput(0, 0, 100, 34);
        history_filter->box(input_box);
        history_filter->when(FL_WHEN_CHANGED);
        history_filter->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->filter_history();
            },
            this);
        history_entries = new Fl_Hold_Browser(0, 0, 100, 100);
        history_entries->format_char(0);
        history_entries->textsize(13);
        history_entries->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                int i = a->history_entries->value() - 1;
                if (!a->busy && i >= 0 && i < static_cast<int>(a->history_ids.size())) {
                    a->history_selected = a->history_ids[static_cast<size_t>(i)];
                    a->launch("history_read", a->history_selected);
                }
            },
            this);
        history_detail = display(history_text);
        history_export = new Button("Exportar CSV");
        history_delete = new Button("Excluir registro", 2);
        history_close = new Button("Fechar");
        history_export->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->export_history();
            },
            this);
        history_pdf = new Button("Exportar PDF");
        history_mark = new Button("Definir como A");
        history_compare = new Button("Comparar com A");
        history_pdf->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->export_report(true);
            },
            this);
        history_mark->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (!a->history_record)
                    return;
                if (a->comparison_reference)
                    json_object_put(a->comparison_reference);
                a->comparison_reference = bc_clone(a->history_record);
                a->history_note->copy_label("Referência A definida. Selecione o segundo ensaio e "
                                            "clique em Comparar com A.");
                a->history_controls();
            },
            this);
        history_compare->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_comparison();
            },
            this);
        history_delete->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (!a->busy && !a->history_selected.empty() &&
                    fl_choice("Excluir permanentemente este registro?", "Cancelar", "Excluir",
                              nullptr) == 1)
                    a->launch("history_delete", a->history_selected);
            },
            this);
        history_close->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->history_dialog->hide();
            },
            this);
        history_dialog->on_close = [this] {
            history_dialog->hide();
        };
        history_dialog->on_layout = [this] {
            int w = history_dialog->w(), h = history_dialog->h();
            rect(history_filter, 20, 20, w - 40, 36);
            rect(history_note, 20, 65, w - 40, 40);
            rect(history_entries, 20, 114, 320, h - 230);
            rect(history_detail, 356, 114, w - 376, h - 230);
            rect(history_export, 20, h - 98, 146, 36);
            rect(history_pdf, 178, h - 98, 140, 36);
            rect(history_mark, 330, h - 98, 156, 36);
            rect(history_compare, 498, h - 98, 174, 36);
            rect(history_delete, 20, h - 54, 158, 36);
            rect(history_close, w - 130, h - 54, 110, 36);
        };
        history_dialog->end();
    }

    void filter_history() {
        history_entries->clear();
        history_ids.clear();
        history_selected.clear();
        history_text.text("");
        if (history_record) {
            json_object_put(history_record);
            history_record = nullptr;
        }
        auto fold = [](std::string s) {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        std::string needle = fold(history_filter->value());
        for (size_t i = 0; i < json_object_array_length(history_items); i++) {
            auto *o = json_object_array_get_idx(history_items, i);
            auto *who = bc_get(o, "identity");
            std::string line = std::string(bc_str(o, "started_at")) + "  " + bc_str(o, "board_id") +
                               " / " + bc_str(o, "test_id") + "  " +
                               run_status(bc_str(o, "status")) + "  " +
                               bc_str(who, "physical_board") + " " + bc_str(who, "device") + " " +
                               bc_str(who, "operator");
            if (fold(line).find(needle) == std::string::npos)
                continue;
            history_ids.emplace_back(bc_str(o, "id"));
            char date[128];
            bc_local_time(bc_str(o, "started_at"), date, sizeof date);
            std::string brief = std::string(date).substr(0, 16) + "  " + bc_str(o, "test_id") +
                                " · " + run_status(bc_str(o, "status"));
            history_entries->add(brief.c_str());
        }
        history_controls();
    }

    void show_history() {
        if (busy)
            return;
        history_dialog->on_layout();
        skin(history_dialog);
        history_dialog->show();
        launch("history_list", "");
    }

    void history_controls() {
        if (!history_dialog)
            return;
        set_enabled(history_export, !busy && !history_selected.empty());
        set_enabled(history_delete, !busy && !history_selected.empty());
        set_enabled(history_entries, !busy);
        set_enabled(history_filter, !busy);
        set_enabled(history_open, !busy);
        set_enabled(record_enabled, !busy);
        set_enabled(record_setup, !busy);
        set_enabled(history_pdf, !busy && history_record);
        set_enabled(history_mark, !busy && history_record);
        set_enabled(history_compare, !busy && history_record && comparison_reference);
    }

    void export_history() {
        if (busy || history_selected.empty())
            return;
        Fl_Native_File_Chooser chooser;
        chooser.title("Exportar resultados para CSV");
        chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
        chooser.filter("CSV\t*.csv");
        chooser.preset_file((history_selected + ".csv").c_str());
        if (chooser.show() != 0 || !chooser.filename())
            return;
        std::string saved_board = board_id;
        board_id = history_selected;
        launch("history_export", chooser.filename());
        board_id = saved_board;
    }

    void history_finished(const std::string &op, json_object *end, bool ok,
                          const std::string &message) {
        if (!ok) {
            history_note->copy_label(message.c_str());
            return;
        }
        if (op == "history_list") {
            if (history_items)
                json_object_put(history_items);
            history_items = bc_clone(bc_get(bc_get(end, "data"), "items"));
            filter_history();
            std::string note = "Busca por placa, teste, dispositivo, operador ou data ISO. "
                               "Horários exibidos no fuso local.";
            if (bc_num(bc_get(end, "data"), "skipped") > 0)
                note += " Há arquivos inválidos que não puderam ser lidos.";
            if (json_object_get_boolean(bc_get(bc_get(end, "data"), "truncated")))
                note += " Lista limitada a 20.000 registros.";
            history_note->copy_label(note.c_str());
        } else if (op == "history_read") {
            auto *record = bc_get(end, "data");
            auto *who = bc_get(record, "identity");
            std::ostringstream s;
            if (history_record)
                json_object_put(history_record);
            history_record = bc_clone(record);
            char local[128];
            bc_local_time(bc_str(record, "started_at"), local, sizeof local);
            s << local << " · " << run_status(bc_str(record, "status")) << "\n"
              << bc_str(record, "board_id") << " / " << bc_str(record, "test_id")
              << "\nPlaca física: " << bc_str(who, "physical_board")
              << "\nDispositivo: " << bc_str(who, "device")
              << "\nOperador: " << bc_str(who, "operator") << "\n\n"
              << bc_str(record, "message") << "\n\n";
            auto *events = bc_get(record, "events");
            for (size_t i = 0; i < json_object_array_length(events); i++) {
                auto *ev = json_object_array_get_idx(events, i);
                if (!std::strcmp(bc_str(ev, "type"), "measurement")) {
                    std::string verdict = bc_str(ev, "verdict");
                    s << bc_str(ev, "label") << ": " << bc_num(ev, "value") << " "
                      << bc_str(ev, "unit") << " · "
                      << (verdict == "pass"   ? "Dentro da faixa"
                          : verdict == "fail" ? "Fora da faixa"
                                              : "Sem comparação")
                      << "\n";
                    if (*bc_str(ev, "source"))
                        s << "Origem: " << bc_str(ev, "source") << "\n";
                }
            }
            auto *snapshot = bc_get(record, "snapshot");
            s << "\nPacote: " << bc_str(snapshot, "version") << " · BPCB "
              << bc_str(record, "bpcb_version") << "\n";
            if (!std::strcmp(bc_str(record, "status"), "incomplete"))
                s << "A execução pode estar em andamento ou ter sido encerrada sem gravar sua "
                     "conclusão.\n";
            s << "\nParâmetros utilizados\n";
            auto *params = bc_get(snapshot, "parameters"),
                 *definitions = bc_get(bc_get(snapshot, "test_definition"), "parameters");
            if (params) {
                json_object_object_foreach(params, key, value) {
                    const char *name = key, *unit = "";
                    for (size_t k = 0; k < json_object_array_length(definitions); k++) {
                        auto *d = json_object_array_get_idx(definitions, k);
                        if (!std::strcmp(bc_str(d, "id"), key)) {
                            name = bc_str(d, "label");
                            unit = bc_str(d, "unit");
                            break;
                        }
                    }
                    s << name << ": " << json_object_get_double(value) << " " << unit << "\n";
                }
            }
            auto *cal = bc_get(snapshot, "calibration");
            s << "\nCalibração: "
              << (*bc_str(cal, "physical_id") ? bc_str(cal, "physical_id") : "Sem calibração")
              << "\n";
            s << "\nComponentes no momento do ensaio\n";
            auto *components_used = bc_get(snapshot, "components");
            if (components_used) {
                json_object_object_foreach(components_used, reference, value) {
                    s << reference << ": " << json_object_get_string(value) << "\n";
                }
            }
            s << "\nConferências do operador\n";
            for (size_t i = 0; i < json_object_array_length(events); i++) {
                auto *ev = json_object_array_get_idx(events, i);
                std::string type = bc_str(ev, "type");
                if (type == "confirm")
                    s << bc_str(ev, "message") << "\n";
                if (type == "operator_reply") {
                    s << (json_object_get_boolean(bc_get(ev, "accepted")) ? "Confirmado"
                                                                          : "Recusado");
                    if (bc_get(ev, "value"))
                        s << ": " << bc_num(ev, "value");
                    auto *acquired = bc_get(ev, "acquisition");
                    if (acquired)
                        s << " · Leitura direta de " << bc_str(acquired, "instrument");
                    s << "\n\n";
                }
            }
            if (json_object_get_boolean(bc_get(record, "events_truncated")))
                s << "\nO limite de eventos foi atingido; este registro tem detalhes parciais.\n";
            history_text.text(s.str().c_str());
        } else if (op == "history_delete") {
            history_selected.clear();
            launch("history_list", "");
        } else if (op == "history_export")
            history_note->copy_label("CSV exportado.");
    }
