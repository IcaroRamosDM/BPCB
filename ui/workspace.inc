    Button *tools_open = nullptr;
    Dialog *tools_dialog = nullptr, *prepare_dialog = nullptr, *backup_dialog = nullptr,
           *chart_dialog = nullptr, *session_dialog = nullptr, *comparison_dialog = nullptr;
    Button *tool_prepare, *tool_backup, *tool_graph, *tool_pdf, *tool_session, *tool_history,
        *tool_rigol, *tool_diagnostic, *tool_recovery, *session_pdf, *chart_png, *chart_csv,
        *prepare_check, *backup_create, *backup_inspect, *backup_restore, *session_begin,
        *session_next, *session_end;
    Fl_Text_Buffer prepare_text, backup_text, session_text, comparison_text;
    Fl_Text_Display *prepare_view, *backup_view, *session_view, *comparison_view;
    Fl_Check_Button *backup_history;
    Choice *backup_policy, *chart_signal;
    TracePlot *chart;
    Fl_Check_Browser *session_choices;
    std::vector<std::string> session_ids, session_queue;
    std::vector<std::string> session_summary;
    size_t session_index = 0;
    bool session_active = false, session_waiting = false, session_success = false;
    std::string session_board, session_choices_board, backup_path, backup_token;
    json_object *session_record = nullptr;
    json_object *current_record = nullptr, *history_record = nullptr,
                *comparison_reference = nullptr;

    void show_tools() {
        workspace_controls();
        skin(tools_dialog);
        tools_dialog->show();
    }

    void show_preparation() {
        if (busy)
            return;
        tools_dialog->hide();
        prepare_dialog->on_layout();
        skin(prepare_dialog);
        prepare_dialog->show();
    }

    void run_preparation() {
        if (busy)
            return;
        auto *v = values();
        json_object_object_add(v, "test_id",
                               json_object_new_string(test ? bc_str(test, "id") : ""));
        json_object_object_add(v, "calibration_id", json_object_new_string(cal_active_id.c_str()));
        json_object_object_add(v, "rigol_endpoint",
                               json_object_new_string(rigol_endpoint->value()));
        launch("prepare", "", v);
        json_object_put(v);
    }

    void show_backup() {
        if (busy || session_active)
            return;
        tools_dialog->hide();
        backup_dialog->on_layout();
        skin(backup_dialog);
        backup_dialog->show();
    }

    std::string choose_file(const char *title, const char *filter, bool save,
                            const char *suggested) {
        Fl_Native_File_Chooser chooser;
        chooser.title(title);
        chooser.filter(filter);
        chooser.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE
                          : Fl_Native_File_Chooser::BROWSE_FILE);
        if (suggested)
            chooser.preset_file(suggested);
        return chooser.show() == 0 && chooser.filename() ? chooser.filename() : "";
    }

    void create_backup() {
        if (busy || session_active)
            return;
        auto path = choose_file("Criar backup dos cadastros salvos", "Backup BPCB\t*.bpcbbackup",
                                true, "BPCB-cadastros.bpcbbackup");
        if (path.empty())
            return;
        auto *v = json_object_new_object();
        json_object_object_add(v, "include_history",
                               json_object_new_boolean(backup_history->value()));
        launch("backup_create", path, v);
        json_object_put(v);
    }

    void inspect_backup() {
        if (busy || session_active)
            return;
        auto path = choose_file("Conferir backup", "Backup BPCB\t*.bpcbbackup", false, nullptr);
        if (path.empty())
            return;
        backup_path = path;
        backup_token.clear();
        backup_policy->value(0);
        backup_text.text("Conferindo conteúdo e compatibilidade…");
        launch("backup_inspect", path);
    }

    void restore_backup() {
        if (busy || session_active || backup_token.empty())
            return;
        if (mount_changed) {
            backup_text.text(
                "Salve ou descarte conscientemente as edições de componentes antes de restaurar.");
            return;
        }
        if (backup_policy->value() == 1 &&
            fl_choice("Substituir os dados indicados como conflito pelo conteúdo deste backup? Uma "
                      "cópia de recuperação será preservada.",
                      "Cancelar", "Substituir", nullptr) != 1)
            return;
        auto *v = json_object_new_object();
        json_object_object_add(v, "replace", json_object_new_boolean(backup_policy->value() == 1));
        launch("backup_restore", backup_path, v, backup_token);
        json_object_put(v);
    }

    void export_pdf_record(json_object *record, const char *suggested) {
        if (busy || !record)
            return;
        int choice = fl_choice("Conteúdo do PDF. Gráficos estão disponíveis apenas para resultados "
                               "retidos nesta abertura.",
                               "Cancelar", "Somente medidas", "Medidas e gráficos");
        if (choice <= 0)
            return;
        auto path = choose_file("Exportar relatório PDF", "PDF\t*.pdf", true, suggested);
        if (path.empty())
            return;
        auto *copy = bc_clone(record);
        json_object_object_add(copy, "include_plots", json_object_new_boolean(choice == 2));
        launch("report_pdf", path, copy);
        json_object_put(copy);
    }

    void export_chart(bool png) {
        if (busy || chart->series.empty())
            return;
        auto path =
            choose_file(png ? "Exportar sinal e zoom atual" : "Exportar todas as amostras retidas",
                        png ? "PNG\t*.png" : "CSV\t*.csv", true,
                        png ? "BPCB-grafico.png" : "BPCB-amostras.csv");
        if (path.empty())
            return;
        auto *plot = chart->snapshot();
        launch(png ? "plot_png" : "plot_csv", path, plot);
        json_object_put(plot);
    }

    void export_report(bool historical = false) {
        if (busy)
            return;
        auto *record = historical ? history_record : current_record;
        if (!record)
            return;
        export_pdf_record(record, "BPCB-relatorio.pdf");
    }

    void show_chart() {
        tools_dialog->hide();
        chart_dialog->on_layout();
        skin(chart_dialog);
        chart_dialog->show();
    }

    void plot_event(json_object *event) {
        const std::string kind = bc_str(event, "type");
        double time =
            bc_get(event, "elapsed") ? bc_num(event, "elapsed") : bc_num(event, "elapsed_seconds");
        size_t count = chart->series.size();
        if (kind == "sample") {
            auto *v = bc_get(event, "values");
            json_object_object_foreach(v, key, value) {
                double target = (!*bc_str(event, "target_signal") ||
                                 !std::strcmp(key, bc_str(event, "target_signal"))) &&
                                        bc_get(event, "target")
                                    ? bc_num(event, "target")
                                    : NAN;
                chart->add(key, bc_str(event, "unit"), time, json_object_get_double(value), target,
                           bc_get(event, "limit") ? bc_num(event, "limit") : NAN);
            }
        } else if (kind == "measurement")
            chart->add(bc_str(event, "label"), bc_str(event, "unit"), time, bc_num(event, "value"));
        if (count != chart->series.size()) {
            chart_signal->clear();
            int selected = 0, index = 0;
            for (const auto &[name, data] : chart->series) {
                (void)data;
                chart_signal->add(name.c_str());
                if (name == chart->selected)
                    selected = index;
                index++;
            }
            chart_signal->value(selected);
        }
    }

    void show_comparison() {
        if (!history_record || !comparison_reference)
            return;
        bc_error e = {{0}};
        auto *result = bc_compare_records(comparison_reference, history_record, &e);
        if (!result) {
            history_note->copy_label(e.text);
            return;
        }
        std::ostringstream text;
        char a[128], b[128];
        bc_local_time(bc_str(comparison_reference, "started_at"), a, sizeof a);
        bc_local_time(bc_str(history_record, "started_at"), b, sizeof b);
        text << "A: " << a << "\nB: " << b
             << "\nDiferença = B - A. Comparação informativa; confira as condições.\n\n";
        auto *warnings = bc_get(result, "warnings");
        for (size_t i = 0; i < json_object_array_length(warnings); i++)
            text << "ATENÇÃO: " << json_object_get_string(json_object_array_get_idx(warnings, i))
                 << "\n";
        auto *rows = bc_get(result, "rows");
        for (size_t i = 0; i < json_object_array_length(rows); i++) {
            auto *row = json_object_array_get_idx(rows, i);
            text << "\n"
                 << bc_str(row, "label") << " (" << bc_str(row, "unit")
                 << ")\nA: " << bc_num(row, "before") << "  |  B: " << bc_num(row, "after")
                 << "  |  diferença: " << bc_num(row, "difference");
            if (bc_get(row, "percent"))
                text << " (" << bc_num(row, "percent") << "%)";
            text << "\n";
        }
        comparison_text.text(text.str().c_str());
        json_object_put(result);
        comparison_dialog->on_layout();
        skin(comparison_dialog);
        comparison_dialog->show();
    }

    void show_session() {
        if (busy && !running)
            return;
        tools_dialog->hide();
        if (!session_active && session_choices_board != board_id) {
            session_choices_board = board_id;
            session_choices->clear();
            session_ids.clear();
            auto *available = bc_get(profile, "tests");
            auto *plan = bc_get(profile, "session_plan"), *ordered = bc_get(plan, "tests");
            for (size_t i = 0; ordered && i < json_object_array_length(ordered); i++) {
                auto *item =
                    bc_test(profile, json_object_get_string(json_object_array_get_idx(ordered, i)));
                if (!item || !json_object_get_boolean(bc_get(item, "enabled")))
                    continue;
                session_ids.emplace_back(bc_str(item, "id"));
                session_choices->add(bc_str(item, "title"), 1);
            }
            for (size_t i = 0; i < json_object_array_length(available); i++) {
                auto *item = json_object_array_get_idx(available, i);
                if (!json_object_get_boolean(bc_get(item, "enabled")) ||
                    std::find(session_ids.begin(), session_ids.end(), bc_str(item, "id")) !=
                        session_ids.end())
                    continue;
                session_ids.emplace_back(bc_str(item, "id"));
                session_choices->add(bc_str(item, "title"));
            }
            if (ordered && json_object_array_length(ordered))
                session_text.text((std::string("Roteiro do pacote: ") + bc_str(plan, "title") +
                                   "\nOs testes sugeridos estão selecionados na ordem definida "
                                   "pelo autor. Confira a seleção antes de iniciar a sessão.")
                                      .c_str());
        }
        session_dialog->on_layout();
        skin(session_dialog);
        session_dialog->show();
        workspace_controls();
    }

    void update_session_summary() {
        std::ostringstream text;
        text << "Bancada: " << session_board << "\n";
        for (size_t i = 0; i < session_queue.size(); i++) {
            auto *step = json_object_array_get_idx(bc_get(session_record, "steps"), i);
            text << i + 1 << ". " << (step ? bc_str(step, "title") : session_queue[i]) << " — "
                 << (i < session_summary.size() ? session_summary[i] : "Pendente") << "\n";
        }
        text << "\n"
             << (session_active
                     ? (session_waiting ? "Etapa preparada. Confira as instruções e parâmetros na "
                                          "tela principal e clique em Iniciar teste."
                                        : "Use Preparar próxima para conferir o próximo ensaio.")
                     : (session_success
                            ? "Sessão concluída."
                            : "Sessão encerrada. Os itens pendentes não foram executados."));
        if (session_record)
            json_object_object_add(
                session_record, "state",
                json_object_new_string(session_active ? "Em andamento"
                                       : session_success
                                           ? "Concluída"
                                           : "Encerrada com etapas interrompidas ou pendentes"));
        session_text.text(text.str().c_str());
        workspace_controls();
    }

    void begin_session() {
        if (busy || session_active || !profile)
            return;
        session_queue.clear();
        session_summary.clear();
        for (size_t i = 0; i < session_ids.size(); i++)
            if (session_choices->checked(static_cast<int>(i + 1)))
                session_queue.push_back(session_ids[i]);
        if (session_queue.empty() || session_queue.size() > 16) {
            session_text.text("Selecione de 1 a 16 ensaios.");
            session_queue.clear();
            return;
        }
        if (session_record)
            json_object_put(session_record);
        session_record = json_object_new_object();
        json_object_object_add(session_record, "format", json_object_new_string("bpcb.session"));
        json_object_object_add(session_record, "board_id",
                               json_object_new_string(board_id.c_str()));
        char stamp[32];
        std::time_t now = std::time(nullptr);
        std::tm utc = *std::gmtime(&now);
        std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &utc);
        json_object_object_add(session_record, "started_at", json_object_new_string(stamp));
        auto *steps = json_object_new_array();
        for (const auto &id : session_queue) {
            auto *step = json_object_new_object(), *definition = bc_test(profile, id.c_str());
            json_object_object_add(step, "test_id", json_object_new_string(id.c_str()));
            json_object_object_add(
                step, "title",
                json_object_new_string(definition ? bc_str(definition, "title") : id.c_str()));
            json_object_object_add(step, "summary", json_object_new_string("Pendente"));
            json_object_array_add(steps, step);
        }
        json_object_object_add(session_record, "steps", steps);
        session_index = 0;
        session_success = false;
        session_board = board_id;
        session_active = true;
        session_waiting = false;
        update_session_summary();
    }

    void next_session() {
        if (busy || !session_active || session_waiting || session_index >= session_queue.size())
            return;
        if (board_id != session_board) {
            session_active = false;
            update_session_summary();
            return;
        }
        auto *step = bc_test(profile, session_queue[session_index].c_str());
        if (!step)
            return;
        select_test(step);
        session_waiting = true;
        update_session_summary();
        session_dialog->hide();
        set_status("Etapa preparada. Confira conexões e parâmetros; Iniciar teste executa somente "
                   "esta etapa.");
    }

    void end_session() {
        if (running && session_active) {
            bc_job_cancel(job);
            set_status("Parada da sessão solicitada. Confira a condição física.");
            return;
        }
        session_active = false;
        session_waiting = false;
        update_session_summary();
    }

    void session_finished(json_object *end) {
        if (!session_active || !session_waiting)
            return;
        std::string summary = run_status(bc_str(end, "status"));
        bool failed = false;
        auto *events = bc_get(current_record, "events");
        for (size_t i = 0; i < json_object_array_length(events); i++)
            if (!std::strcmp(bc_str(json_object_array_get_idx(events, i), "verdict"), "fail"))
                failed = true;
        if (failed)
            summary += " · leitura fora da faixa";
        auto *step = json_object_array_get_idx(bc_get(session_record, "steps"), session_index);
        if (step) {
            json_object_object_add(step, "summary", json_object_new_string(summary.c_str()));
            if (current_record)
                json_object_object_add(step, "record", bc_clone(current_record));
        }
        session_summary.push_back(summary);
        session_index++;
        session_waiting = false;
        session_success = !std::strcmp(bc_str(end, "status"), "ok") && !failed &&
                          session_index == session_queue.size();
        if (std::strcmp(bc_str(end, "status"), "ok") || failed ||
            session_index == session_queue.size())
            session_active = false;
        update_session_summary();
        if (!close_after) {
            skin(session_dialog);
            session_dialog->show();
        }
    }

    void workspace_controls() {
        if (!tools_dialog)
            return;
        set_enabled(tool_prepare, !busy);
        set_enabled(tool_backup, !busy && !session_active);
        set_enabled(tool_pdf, !busy && current_record);
        set_enabled(tool_history, !busy);
        set_enabled(tool_session, profile != nullptr);
        set_enabled(tool_rigol, !busy);
        set_enabled(tool_diagnostic, !busy);
        set_enabled(tool_recovery, !busy && !session_active);
        set_enabled(session_pdf, !busy && session_record);
        set_enabled(chart_png, !busy && !chart->series.empty());
        set_enabled(chart_csv, !busy && !chart->series.empty());
        set_enabled(prepare_check, !busy);
        set_enabled(backup_create, !busy && !session_active);
        set_enabled(backup_inspect, !busy && !session_active);
        set_enabled(backup_restore, !busy && !session_active && !backup_token.empty());
        set_enabled(backup_policy, !busy);
        set_enabled(session_begin, !busy && !session_active && profile);
        set_enabled(session_next, !busy && session_active && !session_waiting);
        set_enabled(session_end, session_active);
        set_enabled(session_choices, !busy && !session_active);
    }

    bool workspace_finished(const std::string &op, json_object *end, bool ok,
                            const std::string &message) {
        if (op == "prepare") {
            std::ostringstream text;
            if (!ok)
                text << message;
            else {
                auto *items = bc_get(bc_get(end, "data"), "checks");
                for (size_t i = 0; i < json_object_array_length(items); i++) {
                    auto *item = json_object_array_get_idx(items, i);
                    text << bc_str(item, "name") << " · " << bc_str(item, "status") << "\n"
                         << bc_str(item, "detail") << "\n\n";
                }
            }
            prepare_text.text(text.str().c_str());
            return true;
        }
        if (op == "report_pdf") {
            set_status(ok ? "Relatório PDF exportado." : message);
            history_note->copy_label(ok ? "Relatório PDF exportado." : message.c_str());
            return true;
        }
        if (op == "plot_png" || op == "plot_csv") {
            set_status(ok ? "Gráfico exportado." : message);
            return true;
        }
        if (op.rfind("backup_", 0) != 0)
            return false;
        if (op == "backup_restore")
            recovery_guard();
        if (!ok) {
            backup_token.clear();
            backup_text.text(message.c_str());
            return true;
        }
        if (op == "backup_inspect") {
            auto *data = bc_get(end, "data");
            backup_token = bc_str(data, "token");
            std::ostringstream text;
            text << backup_path << "\n\nHistórico incluído: "
                 << (json_object_get_boolean(bc_get(data, "history_included")) ? "sim" : "não")
                 << "\nConflitos: " << bc_num(data, "conflicts") << "\n\n";
            auto *items = bc_get(data, "files");
            for (size_t i = 0; i < json_object_array_length(items); i++) {
                auto *item = json_object_array_get_idx(items, i);
                text << bc_str(item, "status") << " · " << bc_str(item, "path") << "\n";
            }
            backup_text.text(text.str().c_str());
        } else if (op == "backup_create")
            backup_text.text(
                "Backup criado. Inclui os cadastros salvos, calibrações e tema; o histórico segue "
                "a opção escolhida. Pacotes executáveis são instalados separadamente.");
        else {
            backup_token.clear();
            backup_text.text("Restauração concluída. A cópia anterior está em restore-recovery-* "
                             "na pasta de dados. Confira os cadastros antes de ensaiar.");
            cal_active_id.clear();
            if (profile)
                load_board(board_id);
            bc_error e = {{0}};
            auto *pref = bc_read_json((root + "/ui.json").c_str(), &e);
            if (pref) {
                bool restored = json_object_get_boolean(bc_get(pref, "dark"));
                if (restored != dark)
                    toggle_theme();
                json_object_put(pref);
            }
        }
        return true;
    }

    void make_workspace() {
        tools_dialog = new Dialog(620, 454, "BPCB · Ferramentas");
        tools_dialog->size_range(620, 454);
        tools_dialog->minw = 620;
        tools_dialog->minh = 454;
        auto *intro = label("Ferramentas da bancada", 23, true);
        rect(intro, 24, 20, 570, 36);
        Button **buttons[] = {&tool_prepare,  &tool_backup,       &tool_graph, &tool_pdf,
                              &tool_session,  &tool_history,      &tool_rigol, &tool_diagnostic,
                              &tool_recovery, &tool_communication};
        const char *names[] = {"Preparação do ensaio", "Backup e restauração",
                               "Gráficos da execução", "Exportar resultado em PDF",
                               "Sessão guiada",        "Histórico e comparação",
                               "Conexão com o Rigol",  "Diagnóstico técnico",
                               "Recuperação",          "Comunicação da PCB"};
        for (int i = 0; i < 10; i++) {
            *buttons[i] = new Button(names[i]);
            rect(*buttons[i], 24 + (i % 2) * 294, 82 + (i / 2) * 66, 278, 46);
        }
        tool_prepare->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_preparation();
            },
            this);
        tool_backup->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_backup();
            },
            this);
        tool_graph->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_chart();
            },
            this);
        tool_pdf->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->export_report();
            },
            this);
        tool_session->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_session();
            },
            this);
        tool_history->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->tools_dialog->hide();
                a->show_history();
            },
            this);
        tool_rigol->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->tools_dialog->hide();
                skin(a->rigol_dialog);
                a->rigol_dialog->show();
            },
            this);
        tool_diagnostic->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_diagnostic();
            },
            this);
        tool_recovery->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_recovery();
            },
            this);
        tool_communication->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_communication();
            },
            this);
        tools_dialog->on_close = [this] {
            tools_dialog->hide();
        };
        tools_dialog->on_layout = [] {};
        tools_dialog->end();
        prepare_dialog = new Dialog(850, 620, "BPCB · Preparação");
        prepare_dialog->minw = 720;
        prepare_dialog->minh = 500;
        prepare_dialog->size_range(720, 500);
        prepare_view = display(prepare_text);
        prepare_check = new Button("Conferir agora", 1);
        prepare_check->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->run_preparation();
            },
            this);
        prepare_text.text("Abra uma bancada e selecione o teste. Configure o Rigol no diálogo "
                          "Rigol e escolha a calibração, se houver. Conferir agora verifica o "
                          "software e o acesso aos dispositivos, sem acionar saídas.");
        auto *close_prepare = new Button("Fechar");
        close_prepare->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->prepare_dialog->hide();
            },
            this);
        prepare_dialog->on_layout = [this, close_prepare] {
            rect(prepare_view, 24, 24, prepare_dialog->w() - 48, prepare_dialog->h() - 104);
            rect(prepare_check, 24, prepare_dialog->h() - 58, 180, 38);
            rect(close_prepare, prepare_dialog->w() - 154, prepare_dialog->h() - 58, 130, 38);
        };
        prepare_dialog->on_close = [this] {
            prepare_dialog->hide();
        };
        prepare_dialog->end();
        backup_dialog = new Dialog(900, 660, "BPCB · Backup e restauração");
        backup_dialog->minw = 850;
        backup_dialog->minh = 600;
        backup_dialog->size_range(850, 600);
        backup_history = new Fl_Check_Button(24, 20, 400, 28, "Incluir histórico no backup");
        backup_history->value(0);
        backup_history->labelsize(14);
        backup_create = new Button("Criar backup");
        backup_inspect = new Button("Abrir e conferir backup");
        backup_policy = new Choice();
        backup_policy->add("Manter dados atuais em conflito");
        backup_policy->add("Substituir conflitos pelo backup");
        backup_policy->value(0);
        backup_policy->textsize(14);
        backup_policy->box(input_box);
        backup_view = display(backup_text);
        backup_text.text("O backup reúne cadastros salvos, calibrações e tema. O histórico é "
                         "opcional. Para restaurar em outro computador, instale primeiro os "
                         "pacotes .bancada compatíveis com o sistema; os executáveis não são "
                         "copiados no backup. Confira os conflitos antes de Restaurar.");
        backup_restore = new Button("Restaurar", 1);
        auto *close_backup = new Button("Fechar");
        backup_create->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->create_backup();
            },
            this);
        backup_inspect->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->inspect_backup();
            },
            this);
        backup_restore->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->restore_backup();
            },
            this);
        close_backup->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->backup_dialog->hide();
            },
            this);
        backup_dialog->on_layout = [this, close_backup] {
            int w = backup_dialog->w(), h = backup_dialog->h();
            rect(backup_create, 24, 62, 180, 38);
            rect(backup_inspect, 216, 62, 230, 38);
            rect(backup_policy, 24, 116, w - 48, 38);
            rect(backup_view, 24, 174, w - 48, h - 250);
            rect(backup_restore, 24, h - 58, 160, 38);
            rect(close_backup, w - 154, h - 58, 130, 38);
        };
        backup_dialog->on_close = [this] {
            backup_dialog->hide();
        };
        backup_dialog->end();
        chart_dialog = new Dialog(950, 650, "BPCB · Gráficos");
        chart_dialog->minw = 700;
        chart_dialog->minh = 480;
        chart_dialog->size_range(700, 480);
        chart_signal = new Choice();
        chart_signal->textsize(14);
        chart_signal->box(input_box);
        chart = new TracePlot();
        chart_signal->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (a->chart_signal->text())
                    a->chart->select(a->chart_signal->text());
            },
            this);
        auto *zoom_in = new Button("Zoom +"), *zoom_out = new Button("Zoom −"),
             *fit = new Button("Visão completa");
        chart_png = new Button("Exportar PNG");
        chart_csv = new Button("Exportar CSV");
        zoom_in->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->chart->zoom(.65);
            },
            this);
        zoom_out->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->chart->zoom(1.5);
            },
            this);
        fit->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->chart->fit();
            },
            this);
        chart_png->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->export_chart(true);
            },
            this);
        chart_csv->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->export_chart(false);
            },
            this);
        auto *chart_note = label("Última execução · até 3.000 pontos por sinal, em memória. "
                                 "Iniciar outro ensaio limpa o gráfico.",
                                 13);
        chart_dialog->on_layout = [this, chart_note, zoom_in, zoom_out, fit] {
            int w = chart_dialog->w(), h = chart_dialog->h();
            rect(chart_signal, 24, 20, w - 48, 36);
            rect(zoom_in, 24, 68, 90, 34);
            rect(zoom_out, 122, 68, 90, 34);
            rect(fit, 220, 68, 140, 34);
            rect(chart_png, 368, 68, 146, 34);
            rect(chart_csv, 522, 68, 146, 34);
            rect(chart, 20, 120, w - 40, h - 168);
            rect(chart_note, 24, h - 38, w - 48, 30);
        };
        chart_dialog->on_close = [this] {
            chart_dialog->hide();
        };
        chart_dialog->end();
        comparison_dialog = new Dialog(880, 620, "BPCB · Comparação de ensaios");
        comparison_dialog->minw = 700;
        comparison_dialog->minh = 480;
        comparison_dialog->size_range(700, 480);
        comparison_view = display(comparison_text);
        comparison_dialog->on_layout = [this] {
            rect(comparison_view, 24, 24, comparison_dialog->w() - 48, comparison_dialog->h() - 48);
        };
        comparison_dialog->on_close = [this] {
            comparison_dialog->hide();
        };
        comparison_dialog->end();
        session_dialog = new Dialog(920, 680, "BPCB · Sessão guiada");
        session_dialog->minw = 850;
        session_dialog->minh = 600;
        session_dialog->size_range(850, 600);
        auto *session_intro =
            label("Selecione os ensaios. Cada etapa exige preparação e início pelo operador. "
                  "Falha, cancelamento ou resultado fora da faixa encerra a sequência.",
                  14);
        session_choices = new Fl_Check_Browser(0, 0, 100, 100);
        session_choices->textsize(14);
        session_view = display(session_text);
        session_begin = new Button("Criar sessão");
        session_next = new Button("Preparar próxima", 1);
        session_end = new Button("Encerrar sessão", 2);
        session_pdf = new Button("Exportar PDF");
        session_pdf->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->export_pdf_record(a->session_record, "BPCB-sessao.pdf");
            },
            this);
        auto *close_session = new Button("Fechar");
        session_begin->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->begin_session();
            },
            this);
        session_next->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->next_session();
            },
            this);
        session_end->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->end_session();
            },
            this);
        close_session->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->session_dialog->hide();
            },
            this);
        session_dialog->on_layout = [this, session_intro, close_session] {
            int w = session_dialog->w(), h = session_dialog->h();
            rect(session_intro, 24, 20, w - 48, 62);
            rect(session_choices, 24, 98, 310, h - 182);
            rect(session_view, 354, 98, w - 378, h - 182);
            rect(session_begin, 24, h - 58, 140, 38);
            rect(session_next, 176, h - 58, 160, 38);
            rect(session_end, 348, h - 58, 150, 38);
            rect(session_pdf, 510, h - 58, 150, 38);
            rect(close_session, w - 134, h - 58, 110, 38);
        };
        session_dialog->on_close = [this] {
            session_dialog->hide();
        };
        session_dialog->end();
    }

    void close_workspace() {
        for (auto *d : {tools_dialog, prepare_dialog, backup_dialog, chart_dialog, session_dialog,
                        comparison_dialog})
            d->hide();
    }

    void destroy_workspace() {
        if (session_record)
            json_object_put(session_record);
        if (current_record)
            json_object_put(current_record);
        if (history_record)
            json_object_put(history_record);
        if (comparison_reference)
            json_object_put(comparison_reference);
        prepare_view->buffer(nullptr);
        backup_view->buffer(nullptr);
        session_view->buffer(nullptr);
        comparison_view->buffer(nullptr);
        for (auto *d : {tools_dialog, prepare_dialog, backup_dialog, chart_dialog, session_dialog,
                        comparison_dialog})
            delete d;
    }
