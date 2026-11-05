    std::string bundle_path;

    void lx_bundle_export() {
        if (busy)
            return;
        auto *q = lx_fw_request();
        if (!q)
            return;
        auto path =
            choose_file("Exportar manifesto e imagens em um pacote",
                        "Pacote de firmware\t*.bpcbfirmware", true, "firmware.bpcbfirmware");
        if (path.empty()) {
            json_object_put(q);
            return;
        }
        auto *v = json_object_new_object();
        json_object_object_add(v, "manifest", bc_clone(bc_get(q, "manifest")));
        json_object_put(q);
        launch("firmware_bundle_export", path, v);
        json_object_put(v);
        lx_controls();
    }

    void lx_bundle_import() {
        if (busy)
            return;
        auto path = choose_file("Inspecionar pacote portátil de firmware",
                                "Pacote de firmware\t*.bpcbfirmware", false, "");
        if (path.empty())
            return;
        bundle_path = path;
        launch("firmware_bundle_inspect", path);
        lx_controls();
    }

    void lx_bundle_finished(const std::string &op, json_object *data, bool ok,
                            const std::string &message) {
        if (!ok) {
            lx_message(message.c_str());
            return;
        }
        if (op == "firmware_bundle_export") {
            lx_message(
                "Pacote completo exportado: manifesto e imagens com SHA-256. A ferramenta de "
                "gravação continua separada.");
            return;
        }
        if (op == "firmware_bundle_import") {
            lx_fw_adopt(bc_clone(data));
            lx_fw_review.clear();
            lx_fw_hash.clear();
            lx_fw_trust->value(0);
            lx_message("Imagens importadas e verificadas. Revise a ferramenta e prepare uma nova "
                       "prévia antes de gravar.");
            return;
        }
        auto *m = bc_get(data, "manifest");
        std::string summary =
            std::string(bc_str(m, "name")) + " · " + bc_str(m, "version") +
            "\nFamília: " + bc_str(m, "family") + "\nAlvo: " +
            json_object_to_json_string_ext(bc_get(m, "target"), JSON_C_TO_STRING_PLAIN) + "\n";
        auto *a = bc_get(m, "images");
        for (size_t i = 0; i < json_object_array_length(a); i++) {
            auto *x = json_object_array_get_idx(a, i);
            summary += std::string(bc_str(x, "role")) + " · " +
                       std::to_string((unsigned long long)bc_num(x, "bytes")) + " bytes\n";
        }
        summary += "SHA-256 do pacote: ";
        summary += bc_str(data, "sha256");
        summary += "\n\nEscolha a pasta onde conservar as imagens. Nenhuma gravação no alvo será "
                   "iniciada.";
        if (fl_choice("%s", "Cancelar", "Importar imagens", nullptr, summary.c_str()) != 1)
            return;
        Fl_Native_File_Chooser chooser;
        chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
        chooser.title("Escolha a pasta que receberá o novo diretório de firmware");
        if (chooser.show() || !chooser.filename())
            return;
        const char *name =
            fl_input("Nome do novo diretório (ainda não deve existir):", "firmware-importado");
        if (!name)
            return;
        std::string leaf = name;
        if (leaf.empty() || leaf == "." || leaf == ".." || leaf.size() > 120 ||
            leaf.find_first_of("/\\:") != std::string::npos) {
            lx_message("Nome de diretório inválido.");
            return;
        }
        auto *v = json_object_new_object();
        json_object_object_add(
            v, "destination",
            json_object_new_string((fs::path(chooser.filename()) / leaf).string().c_str()));
        json_object_object_add(v, "expected_sha256", bc_clone(bc_get(data, "sha256")));
        launch("firmware_bundle_import", bundle_path, v);
        json_object_put(v);
    }

    void show_preparation_guide() {
        if (busy || !test || !valid || session_active)
            return;
        bc_error e = {{0}};
        auto *v = values();
        auto *effective = bc_apply(profile, v, &e);
        auto *rendered =
            effective ? bc_preview(effective, bc_test(effective, bc_str(test, "id")), &e) : nullptr;
        if (!rendered) {
            json_object_put(effective);
            json_object_put(v);
            set_status(e.text);
            return;
        }
        Fl_Double_Window dialog(960, 740, "BPCB · Preparar teste");
        dialog.color(p.bg);
        dialog.begin();
        auto *title =
            label("1 Procedimento  ·  2 Comunicação  ·  3 Conexões  ·  4 Executar", 19, true);
        rect(title, 24, 18, 912, 48);
        Fl_Text_Buffer text;
        auto *details = new Fl_Text_Display(24, 82, 912, 478);
        details->buffer(&text);
        details->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
        details->textsize(14);
        details->color(p.panel);
        details->textcolor(p.text);
        std::string summary =
            std::string(bc_str(profile, "pcb")) + " · " + bc_str(profile, "revision") + "\n" +
            bc_str(test, "title") + "\nModo: " +
            (!strcmp(bc_str(test, "mode"), "simulation") ? "Simulação, sem acesso a hardware"
                                                         : "Ensaio físico") +
            "\n\n" + bc_str(test, "purpose") + "\n\n" + bc_str(rendered, "instructions") +
            "\n\nVALORES ESPERADOS\n" + bc_str(rendered, "expected");
        auto *wiring = bc_get(test, "wiring");
        if (wiring) {
            char *map = bc_wiring_text(wiring, &e);
            if (map && summary.find(map) == std::string::npos) {
                summary += "\n\n";
                summary += map;
            }
            free(map);
        }
        summary += "\n\nPARÂMETROS DESTA EXECUÇÃO\n";
        auto *effective_test = bc_test(effective, bc_str(test, "id"));
        auto *parameters = bc_get(effective_test, "parameters");
        for (size_t i = 0; i < json_object_array_length(parameters); i++) {
            auto *parameter = json_object_array_get_idx(parameters, i);
            auto *value = bc_get(parameter, "default");
            char number[100];
            snprintf(number, sizeof number, "%.9g", json_object_get_double(value));
            summary += std::string(bc_str(parameter, "label")) + ": " +
                       (value ? (json_object_is_type(value, json_type_int) ||
                                         json_object_is_type(value, json_type_double)
                                     ? number
                                     : json_object_get_string(value))
                              : "não informado") +
                       " " + bc_str(parameter, "unit") + "\n";
        }
        auto *definition = bc_get(test, "communication");
        if (!definition)
            definition = bc_get(profile, "communication");
        summary += "\nCOMUNICAÇÃO\n";
        auto describe = [&](const char *id, json_object *c) {
            summary += std::string(id) + ": " + bc_str(c, "adapter") + " · " + bc_str(c, "bus");
            if (*bc_str(c, "device"))
                summary += " · porta " + std::string(bc_str(c, "device"));
            if (*bc_str(c, "serial"))
                summary += " · série " + std::string(bc_str(c, "serial"));
            summary += "\n";
        };
        auto *c = bc_get(v, "communication");
        if (!definition)
            summary += "Transporte definido pelo pacote. Siga as conexões do procedimento.\n";
        else if (c && bc_num(c, "api") == 2) {
            auto *channels = bc_get(c, "channels");
            if (json_object_is_type(channels, json_type_object)) {
                json_object_object_foreach(channels, id, config) {
                    describe(id, config);
                }
            }
        } else if (c && *bc_str(c, "adapter"))
            describe("Canal único", c);
        else
            summary += "Escolha o adaptador ou associe os canais antes de executar.\n";
        summary += "\nHistórico de resultados: ";
        summary += record_enabled->value() ? "ativado por você." : "desativado.";
        json_object_put(rendered);
        json_object_put(effective);
        json_object_put(v);
        text.text(summary.c_str());
        auto *checked = new Fl_Check_Button(
            24, 578, 912, 32,
            "Conferi a revisão da PCB, os pinos, os níveis e as conexões desta execução");
        checked->labelcolor(p.text);
        checked->labelsize(14);
        auto *note = label("Configurar ou associar canais retorna à preparação. A conferência vale "
                           "apenas para esta execução.",
                           13);
        rect(note, 24, 618, 912, 40);
        std::map<Fl_Widget *, std::function<void()>> actions;
        auto add = [&](int x, int width, const char *s, std::function<void()> fn) {
            auto *b = new Button(s);
            rect(b, x, 680, width, 36);
            actions[b] = std::move(fn);
            if (!definition && (x == 24 || x == 232))
                b->deactivate();
            b->callback(
                [](Fl_Widget *w, void *c) {
                    auto *a = static_cast<std::map<Fl_Widget *, std::function<void()>> *>(c);
                    a->at(w)();
                },
                &actions);
        };
        add(24, 196, "Selecionar adaptador…", [&] {
            dialog.hide();
            show_communication();
        });
        add(232, 188, "Associar canais…", [&] {
            dialog.hide();
            show_lab(2);
        });
        add(608, 140, "Cancelar", [&] {
            dialog.hide();
        });
        add(764, 172, "Executar teste", [&] {
            bool physical = !strcmp(bc_str(test, "mode"), "hardware");
            if (physical && !checked->value()) {
                note->copy_label("Confira as conexões desta execução antes de continuar.");
                return;
            }
            auto *current = values();
            auto *c = bc_get(current, "communication");
            if (physical && c) {
                if (bc_num(c, "api") == 2) {
                    auto *a = bc_get(c, "channels");
                    json_object_object_foreach(a, k, channel) {
                        (void)k;
                        json_object_object_add(channel, "connections_checked",
                                               json_object_new_boolean(1));
                    }
                } else
                    json_object_object_add(c, "connections_checked", json_object_new_boolean(1));
            }
            bc_error error = {{0}};
            auto *resolved =
                physical ? bc_comm_resolve(profile, test, c, &error) : json_object_new_object();
            if (!resolved) {
                note->copy_label(error.text);
                json_object_put(current);
                return;
            }
            json_object_put(resolved);
            dialog.hide();
            execute(current);
            json_object_put(current);
        });
        dialog.end();
        dialog.resizable(details);
        dialog.size_range(960, 680);
        dialog.set_modal();
        dialog.show();
        while (dialog.shown())
            Fl::wait();
        details->buffer(nullptr);
    }

    std::string compatibility_description(json_object *record) {
        auto *report = bc_get(record, "report"), *c = bc_get(report, "configuration");
        const char *status = bc_str(record, "status");
        std::string out =
            std::string(!strcmp(status, "validated") ? "Validado pelo operador"
                        : !strcmp(status, "failed")  ? "Reprovado"
                                                     : "Ensaio parcial") +
            "\n\nModelo/revisão: " + bc_str(record, "name") + "\nSistema: " + bc_str(record, "os") +
            "\nDriver: " + bc_str(record, "driver") +
            "\nVersão do BPCB: " + bc_str(record, "app_version") +
            "\n\nAdaptador: " + bc_str(c, "adapter") + "\nBarramento: " + bc_str(c, "bus") +
            "\nDispositivo: " + bc_str(c, "device") + "\nSérie: " + bc_str(c, "serial") +
            "\nRoteiro: " + bc_str(report, "procedure") + "\n\n";
        auto *a = bc_get(report, "assessment");
        if (json_object_is_type(a, json_type_array))
            for (size_t i = 0; i < json_object_array_length(a); i++) {
                auto *r = json_object_array_get_idx(a, i);
                const char *st = bc_str(r, "status");
                out += std::string(!strcmp(st, "pass")   ? "✓ Conferido: "
                                   : !strcmp(st, "fail") ? "× Reprovado: "
                                                         : "○ Pendente: ") +
                       bc_str(r, "label") + "\n";
            }
        out += "\nEste registro documenta o ensaio informado pelo operador. Exportar preserva os "
               "detalhes técnicos e as evidências.";
        return out;
    }

    void show_compatibility() {
        if (busy)
            return;
        bc_error e = {{0}};
        auto *records = bc_compatibility_list(root.c_str(), &e);
        if (!records) {
            lx_message(e.text);
            return;
        }
        Fl_Double_Window dialog(1020, 760, "BPCB · Compatibilidade registrada");
        dialog.color(p.bg);
        dialog.begin();
        auto *title = label("Compatibilidade por configuração, sistema e driver", 21, true);
        rect(title, 24, 18, 972, 40);
        auto *note =
            label("Registros do operador: sem ensaio / parcial / validado / reprovado. Não "
                  "ativam o histórico nem autorizam transmissões.",
                  13);
        rect(note, 24, 64, 972, 46);
        auto *list = new Fl_Hold_Browser(24, 124, 370, 330);
        list->color(p.panel);
        list->textcolor(p.text);
        list->textsize(13);
        Fl_Text_Buffer text;
        auto *details = new Fl_Text_Display(414, 124, 582, 330);
        details->buffer(&text);
        details->textsize(13);
        details->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
        details->color(p.panel);
        details->textcolor(p.text);
        auto *model =
            WorkbenchFrameEditor::input(24, 510, 310, "Modelo e revisão do adaptador", "");
        auto *os = WorkbenchFrameEditor::input(354, 510, 310, "Sistema e versão",
                                               lx_qual_os ? lx_qual_os->value() : "");
        auto *driver = WorkbenchFrameEditor::input(684, 510, 312, "Driver e versão", "");
        auto *feedback =
            label("Selecione um registro para conferir o roteiro e as evidências.", 13);
        rect(feedback, 24, 560, 972, 58);
        auto refresh = [&] {
            list->clear();
            for (size_t i = 0; i < json_object_array_length(records); i++) {
                auto *r = json_object_array_get_idx(records, i);
                const char *s = bc_str(r, "status");
                const char *state = !strcmp(s, "validated") ? "Validado"
                                    : !strcmp(s, "failed")  ? "Reprovado"
                                                            : "Parcial";
                std::string line = std::string(state) + " · " + bc_str(r, "name") + " · " +
                                   bc_str(bc_get(r, "report"), "procedure");
                list->add(line.c_str());
            }
            if (!json_object_array_length(records))
                text.text("Sem ensaio registrado. Faça uma qualificação e salve explicitamente seu "
                          "resultado aqui.");
        };
        std::function<void()> selection = [&] {
            int i = list->value() - 1;
            if (i < 0 || (size_t)i >= json_object_array_length(records))
                return;
            auto *r = json_object_array_get_idx(records, (size_t)i);
            text.text(compatibility_description(r).c_str());
        };
        list->callback(
            [](Fl_Widget *, void *c) {
                (*static_cast<std::function<void()> *>(c))();
            },
            &selection);
        std::map<Fl_Widget *, std::function<void()>> actions;
        auto add = [&](int x, int y, int width, const char *s, std::function<void()> fn) {
            auto *b = new Button(s);
            rect(b, x, y, width, 36);
            actions[b] = std::move(fn);
            b->callback(
                [](Fl_Widget *w, void *c) {
                    static_cast<std::map<Fl_Widget *, std::function<void()>> *>(c)->at(w)();
                },
                &actions);
        };
        auto save = [&](json_object *r) {
            bc_error error = {{0}};
            bool exists = false;
            for (size_t i = 0; i < json_object_array_length(records); i++)
                exists |=
                    !strcmp(bc_str(r, "id"), bc_str(json_object_array_get_idx(records, i), "id"));
            if (exists && fl_choice("Substituir o registro anterior desta configuração e roteiro?",
                                    "Cancelar", "Substituir", nullptr) != 1)
                return;
            if (bc_compatibility_save(root.c_str(), r, exists, &error) < 0) {
                feedback->copy_label(error.text);
                return;
            }
            auto *a = bc_compatibility_list(root.c_str(), &error);
            if (a) {
                json_object_put(records);
                records = a;
                refresh();
            }
            feedback->copy_label("Compatibilidade salva explicitamente. Histórico de resultados "
                                 "permanece na opção escolhida.");
        };
        add(24, 636, 300, "Registrar qualificação atual", [&] {
            bc_error error = {{0}};
            auto *r = lx_last_result ? bc_compatibility_record(lx_last_result, model->value(),
                                                               os->value(), driver->value(), &error)
                                     : nullptr;
            if (!r) {
                feedback->copy_label(
                    *error.text ? error.text : "Avalie primeiro a qualificação no laboratório.");
                return;
            }
            save(r);
            json_object_put(r);
        });
        add(340, 636, 210, "Conferir seleção atual", [&] {
            bc_error error = {{0}};
            auto *c =
                communication_applied ? bc_clone(communication_applied) : communication_draft();
            auto *r = c ? bc_compatibility_match(records, c, os->value(), driver->value(), &error)
                        : nullptr;
            json_object_put(c);
            if (!r)
                text.text(error.text);
            else {
                const char *status = bc_str(r, "status");
                std::string summary =
                    std::string(
                        !strcmp(status, "untested") ? "Sem ensaio nesta configuração/sistema/driver"
                        : !strcmp(status, "validated") ? "Compatibilidade validada pelo operador"
                        : !strcmp(status, "failed")
                            ? "Há ensaio reprovado nesta seleção"
                            : "Compatibilidade parcial ou versão do BPCB alterada") +
                    "\n\n" + bc_str(r, "scope");
                auto *matches = bc_get(r, "records");
                for (size_t i = 0; i < json_object_array_length(matches); i++)
                    summary +=
                        "\n\n" + compatibility_description(json_object_array_get_idx(matches, i));
                text.text(summary.c_str());
            }
            json_object_put(r);
        });
        add(566, 636, 196, "Importar registro…", [&] {
            auto path = choose_file("Importar registro do operador", "JSON\t*.json", false, "");
            if (path.empty())
                return;
            bc_error error = {{0}};
            auto *r = bc_read_json(path.c_str(), &error);
            auto *a = json_object_new_array();
            if (r)
                json_object_array_add(a, bc_clone(r));
            if (!r || bc_compatibility_validate(a, &error) < 0)
                feedback->copy_label(error.text);
            else
                save(r);
            json_object_put(a);
            json_object_put(r);
        });
        add(778, 636, 218, "Exportar selecionado…", [&] {
            int i = list->value() - 1;
            if (i < 0 || (size_t)i >= json_object_array_length(records)) {
                feedback->copy_label("Selecione um registro.");
                return;
            }
            auto path = choose_file("Exportar compatibilidade", "JSON\t*.json", true,
                                    "compatibilidade.json");
            if (path.empty())
                return;
            bc_error error = {{0}};
            feedback->copy_label(bc_export_json(json_object_array_get_idx(records, (size_t)i),
                                                path.c_str(), &error) < 0
                                     ? error.text
                                     : "Registro exportado.");
        });
        add(836, 704, 160, "Fechar", [&] {
            dialog.hide();
        });
        dialog.end();
        dialog.size_range(1020, 760);
        dialog.set_modal();
        refresh();
        dialog.show();
        while (dialog.shown())
            Fl::wait();
        details->buffer(nullptr);
        json_object_put(records);
    }
