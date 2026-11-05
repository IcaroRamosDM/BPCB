    Dialog *cal_dialog = nullptr;
    Button *cal_btn, *cal_save, *cal_use, *cal_none, *cal_close;
    Choice *cal_profiles;
    CommitInput *cal_unit, *cal_vref;
    Fl_Box *cal_title, *cal_intro, *cal_unit_label, *cal_vref_label, *cal_saved_label, *cal_note;
    Fl_Scroll *cal_scroll;
    Fl_Group *cal_canvas;
    std::vector<Fl_Box *> cal_headers, cal_labels;
    std::vector<Fl_Check_Button *> cal_checks;
    std::vector<std::vector<CommitInput *>> cal_points;
    json_object *cal_library = nullptr;
    std::string cal_active_id;

    void calibration_controls() {
        if (!cal_dialog)
            return;
        set_enabled(cal_save, !busy);
        set_enabled(cal_use, !busy && cal_profiles->value() >= 0);
        set_enabled(cal_none, !busy);
    }

    void make_calibration() {
        cal_dialog = new Dialog(1120, 720, "BPCB · Calibração da placa física");
        cal_dialog->size_range(760, 560);
        cal_title = label("Calibração por placa física", 24, true);
        cal_intro =
            label("Use dois pontos estáveis por canal. Leitura base é sem correção; referência é a "
                  "medição externa no ponto indicado. Marque somente os canais que vai calibrar.",
                  14);
        cal_saved_label = label("Perfis salvos nesta bancada", 13);
        cal_profiles = new Choice();
        cal_profiles->textsize(14);
        cal_use = new Button("Usar perfil");
        cal_none = new Button("Sem calibração");
        cal_unit_label = label("Identificação da placa física", 13);
        cal_unit = new CommitInput(0, 0, 100, 36);
        cal_unit->box(input_box);
        cal_unit->textsize(14);
        cal_unit->maximum_size(64);
        cal_vref_label = label("AVDD medida (mV)", 13);
        cal_vref = new CommitInput(0, 0, 100, 36);
        cal_vref->box(input_box);
        cal_vref->textsize(14);
        cal_vref->value("5000");
        cal_scroll = new Fl_Scroll(18, 263, 1084, 260);
        cal_scroll->type(Fl_Scroll::BOTH);
        cal_canvas = new Fl_Group(18, 263, 1084, 260);
        cal_canvas->resizable(nullptr);
        for (const char *header : {"CANAL / PONTO · V", "BASE 1 (V)", "REFERÊNCIA 1 (V)",
                                   "BASE 2 (V)", "REFERÊNCIA 2 (V)"})
            cal_headers.push_back(label(header, 12, true));
        for (std::size_t i = 0; i < 4; i++) {
            auto *check = new Fl_Check_Button(0, 0, 24, 24);
            cal_checks.push_back(check);
            cal_labels.push_back(label("", 13));
            std::vector<CommitInput *> row;
            for (int j = 0; j < 4; j++) {
                auto *input = new CommitInput(0, 0, 100, 36);
                input->box(input_box);
                input->textsize(14);
                input->maximum_size(24);
                row.push_back(input);
            }
            cal_points.push_back(row);
        }
        cal_canvas->end();
        cal_scroll->end();
        cal_note = label(
            "Nenhum perfil selecionado. A calibração só é gravada ao clicar em Salvar e usar.", 13);
        cal_save = new Button("Salvar e usar", 1);
        cal_close = new Button("Fechar");
        cal_profiles->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->calibration_fill();
            },
            this);
        cal_use->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (a->busy)
                    return;
                int n = a->cal_profiles->value();
                if (n >= 0 && a->cal_library) {
                    auto *cal =
                        json_object_array_get_idx(a->cal_library, static_cast<std::size_t>(n));
                    a->cal_active_id = bc_str(cal, "physical_id");
                    a->update_preview();
                    set_label(a->cal_note,
                              ("Perfil em uso nesta sessão: " + a->cal_active_id).c_str());
                }
            },
            this);
        cal_none->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (a->busy)
                    return;
                a->cal_active_id.clear();
                a->update_preview();
                set_label(a->cal_note,
                          "Sem correção selecionada. Perfis salvos foram preservados.");
            },
            this);
        cal_save->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->save_calibration();
            },
            this);
        cal_close->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->cal_dialog->hide();
            },
            this);
        cal_dialog->on_close = [this] {
            cal_dialog->hide();
        };
        cal_dialog->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->cal_dialog->hide();
            },
            this);
        cal_dialog->on_layout = [this] {
            int w = cal_dialog->w(), h = cal_dialog->h();
            rect(cal_title, 22, 14, w - 44, 36);
            rect(cal_intro, 22, 58, w - 44, 54);
            rect(cal_saved_label, 22, 120, 340, 24);
            rect(cal_profiles, 22, 148, std::max(210, w - 402), 36);
            rect(cal_use, w - 366, 148, 132, 36);
            rect(cal_none, w - 222, 148, 200, 36);
            rect(cal_unit_label, 22, 194, w - 262, 22);
            rect(cal_unit, 22, 222, w - 262, 36);
            rect(cal_vref_label, w - 222, 194, 200, 22);
            rect(cal_vref, w - 222, 222, 200, 36);
            int sx = cal_scroll->xposition(), sy = cal_scroll->yposition();
            cal_scroll->scroll_to(0, 0);
            rect(cal_scroll, 18, 280, w - 36, h - 440);
            int cw = std::max(1040, w - 58);
            rect(cal_canvas, 18, 280, cw, 244);
            int label_width = 240, col = (cw - label_width - 20) / 4;
            rect(cal_headers[0], 48, 284, label_width - 30, 28);
            for (std::size_t k = 0; k < 4; k++)
                rect(cal_headers[k + 1], 18 + label_width + static_cast<int>(k) * col, 284,
                     col - 10, 28);
            for (std::size_t i = 0; i < 4; i++) {
                int y = 322 + static_cast<int>(i) * 50;
                rect(cal_checks[i], 22, y + 5, 24, 28);
                rect(cal_labels[i], 52, y, label_width - 34, 38);
                for (std::size_t k = 0; k < 4; k++)
                    rect(cal_points[i][k], 18 + label_width + static_cast<int>(k) * col, y,
                         col - 12, 36);
            }
            cal_scroll->scroll_to(sx, sy);
            rect(cal_note, 22, h - 100, w - 44, 40);
            rect(cal_close, w - 322, h - 48, 120, 36);
            rect(cal_save, w - 190, h - 48, 168, 36);
        };
        cal_dialog->end();
    }

    void calibration_fill() {
        int n = cal_profiles->value();
        if (n < 0 || !cal_library)
            return;
        auto *cal = json_object_array_get_idx(cal_library, static_cast<std::size_t>(n));
        cal_unit->value(bc_str(cal, "physical_id"));
        cal_vref->value(std::to_string(bc_num(cal, "vref_mv")).c_str());
        for (std::size_t i = 0; i < 4; i++) {
            auto *ch = json_object_array_get_idx(bc_get(cal, "channels"), i);
            cal_checks[i]->value(json_object_get_boolean(bc_get(ch, "calibrated")));
            auto *points = bc_get(ch, "points");
            for (std::size_t k = 0; k < 4; k++)
                cal_points[i][k]->value(
                    points ? std::to_string(
                                 json_object_get_double(json_object_array_get_idx(points, k)))
                                 .c_str()
                           : "");
        }
        calibration_controls();
    }

    void show_calibration() {
        if (!profile || !bc_get(profile, "calibration") || busy)
            return;
        auto *channels = bc_get(bc_get(profile, "calibration"), "channels");
        for (std::size_t i = 0; i < 4; i++)
            set_label(cal_labels[i], bc_str(json_object_array_get_idx(channels, i), "label"));
        cal_dialog->position(win.x() + 30, win.y() + 30);
        cal_dialog->on_layout();
        skin(cal_dialog);
        cal_dialog->show();
        calibration_controls();
        if (!busy)
            launch("calibration_list", "");
    }

    void calibration_list_received(json_object *list) {
        if (cal_library)
            json_object_put(cal_library);
        cal_library = bc_clone(list);
        cal_profiles->clear();
        for (std::size_t i = 0; i < json_object_array_length(list); i++)
            cal_profiles->add(bc_str(json_object_array_get_idx(list, i), "physical_id"));
        // Nao selecionar uma placa fisica automaticamente.
        cal_profiles->value(-1);
        if (!cal_active_id.empty())
            for (std::size_t i = 0; i < json_object_array_length(list); i++)
                if (cal_active_id == bc_str(json_object_array_get_idx(list, i), "physical_id"))
                    cal_profiles->value(static_cast<int>(i));
        calibration_controls();
    }

    static bool calibration_number(CommitInput *input, double &value) {
        std::string text = input->value();
        std::replace(text.begin(), text.end(), ',', '.');
        char *end = nullptr;
        errno = 0;
        value = std::strtod(text.c_str(), &end);
        return !text.empty() && end != text.c_str() && !*end && !errno && std::isfinite(value);
    }

    void save_calibration() {
        if (busy) {
            set_label(cal_note, "Encerre a sessão elétrica antes de salvar a calibração.");
            return;
        }
        double vref = 0;
        if (!calibration_number(cal_vref, vref)) {
            set_label(cal_note, "Informe a AVDD em mV.");
            return;
        }
        auto *values = json_object_new_object(), *points = json_object_new_array();
        json_object_object_add(values, "physical_id", json_object_new_string(cal_unit->value()));
        json_object_object_add(values, "vref_mv", json_object_new_double(vref));
        json_object_object_add(values, "components",
                               json_object_get(bc_get(profile, "components")));
        json_object_object_add(values, "points", points);
        for (std::size_t i = 0; i < 4; i++) {
            if (!cal_checks[i]->value()) {
                json_object_array_add(points, nullptr);
                continue;
            }
            auto *row = json_object_new_array();
            json_object_array_add(points, row);
            for (auto *input : cal_points[i]) {
                double value = 0;
                if (!calibration_number(input, value)) {
                    set_label(cal_note, "Complete os quatro valores de cada canal marcado.");
                    json_object_put(values);
                    return;
                }
                json_object_array_add(row, json_object_new_double(value));
            }
        }
        launch("calibration_save", "", values);
        json_object_put(values);
    }
