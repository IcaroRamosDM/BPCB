    Dialog *confirmation = nullptr;
    Fl_Box *confirmation_text, *confirmation_title, *confirmation_label, *confirmation_error;
    Fl_Input *confirmation_value;
    Choice *confirmation_unit;
    std::string confirmation_base_unit, confirmation_instrument_request;
    unsigned confirmation_generation = 0;
    Button *confirmation_accept, *confirmation_cancel;
    std::string confirmation_id;
    bool confirmation_number = false, confirmation_measurement = false;
    double confirmation_min = 0, confirmation_max = 0;

    void make_confirmation() {
        confirmation = new Dialog(660, 540, "BPCB · Conferência do teste");
        confirmation->minw = 660;
        confirmation->minh = 540;
        confirmation->size_range(660, 540);
        confirmation->set_modal();
        confirmation_title = label("Conferência do teste", 23, true);
        confirmation_text = label("", 15);
        confirmation_text->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        confirmation_label = label("Tensão medida no multímetro (V)", 13);
        confirmation_value = new StableInput(0, 0, 100, 38);
        confirmation_value->box(input_box);
        confirmation_value->textsize(17);
        confirmation_value->maximum_size(30);
        confirmation_error = label("", 13);
        confirmation_unit = new Choice();
        confirmation_unit->box(input_box);
        confirmation_unit->textsize(14);
        rigol_read = new Button("Ler Rigol");
        rigol_read->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                a->start_rigol(a->confirmation_instrument_request);
            },
            this);
        rigol_details = new Button("Detalhes");
        rigol_details->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->show_rigol_details();
            },
            this);
        confirmation_cancel = new Button("Parar teste", 2);
        confirmation_accept = new Button("Confirmar", 1);
        confirmation_cancel->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->answer_confirmation(false);
            },
            this);
        confirmation_accept->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->answer_confirmation(true);
            },
            this);
        confirmation->on_close = [this] {
            answer_confirmation(false);
        };
        confirmation->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->answer_confirmation(false);
            },
            this);
        confirmation->on_layout = [this] {
            int w = confirmation->w(), h = confirmation->h();
            rect(confirmation_title, 24, 18, w - 48, 42);
            rect(confirmation_text, 24, 82, w - 48, h - 262);
            rect(confirmation_label, 24, h - 169, w - 48, 24);
            rect(confirmation_value, 24, h - 139, w - 444, 38);
            rect(confirmation_unit, w - 408, h - 139, 96, 38);
            rect(rigol_read, w - 300, h - 139, 132, 38);
            rect(rigol_details, w - 156, h - 139, 132, 38);
            rect(confirmation_error, 24, h - 95, w - 48, 28);
            rect(confirmation_cancel, 24, h - 55, 142, 38);
            rect(confirmation_accept, w - 194, h - 55, 170, 38);
        };
        confirmation->end();
    }

    void show_confirmation(json_object *event) {
        if (rigol_acquisition) {
            json_object_put(rigol_acquisition);
            rigol_acquisition = nullptr;
        }
        confirmation_generation++;
        confirmation_instrument_request = bc_str(event, "instrument_request");
        if (confirmation_instrument_request.empty())
            confirmation_instrument_request = bc_str(event, "id");
        confirmation_measurement = json_object_get_boolean(bc_get(event, "measurement"));
        confirmation_id = bc_str(event, "id");
        confirmation_number = json_object_get_boolean(bc_get(event, "number"));
        if (confirmation_instrument_request.rfind("read:", 0) == 0 ||
            confirmation_id == "cap_inicial" || confirmation_id == "cap_final" ||
            confirmation_id == "cap_tempo") {
            rigol_read->show();
            rigol_details->show();
        } else {
            rigol_read->hide();
            rigol_details->hide();
        }
        rigol_controls();
        confirmation_min = bc_num(event, "min");
        confirmation_max = bc_num(event, "max");
        confirmation_text->copy_label(bc_str(event, "message"));
        confirmation_error->copy_label("");
        confirmation_value->value("");
        confirmation_base_unit = bc_str(event, "unit");
        confirmation_unit->clear();
        if (confirmation_base_unit == "V") {
            confirmation_unit->add("V");
            confirmation_unit->add("mV");
            confirmation_unit->value(confirmation_id == "cap_inicial" ? 1 : 0);
        } else if (confirmation_base_unit == "ms" || confirmation_base_unit == "s" ||
                   confirmation_base_unit == "us") {
            confirmation_unit->add("ms");
            confirmation_unit->add("us");
            confirmation_unit->add("s");
            confirmation_unit->value(confirmation_base_unit == "s"    ? 2
                                     : confirmation_base_unit == "us" ? 1
                                                                      : 0);
        } else {
            confirmation_unit->add(confirmation_base_unit.c_str());
            confirmation_unit->value(0);
        }
        for (int i = 0; i < confirmation_unit->size() - 1; i++)
            if (confirmation_unit->text(i) &&
                !std::strcmp(confirmation_unit->text(i), bc_str(event, "display_unit")))
                confirmation_unit->value(i);
        confirmation_label->copy_label(
            "Leitura medida · escolha a unidade ou escreva-a junto do valor");
        if (confirmation_id == "cap_inicial")
            confirmation_error->copy_label("Faixa esperada: 0 a 100 mV. Exemplo: 10 mV = 0,010 V.");
        if (confirmation_number)
            confirmation_unit->show();
        else
            confirmation_unit->hide();
        if (confirmation_number) {
            confirmation_value->show();
            confirmation_label->show();
            confirmation_accept->copy_label(confirmation_id == "medir" ? "Concluir medição"
                                                                       : "Confirmar leitura");
        } else {
            confirmation_value->hide();
            confirmation_label->hide();
            confirmation_accept->copy_label("Preparado: iniciar");
        }
        confirmation->position(win.x() + std::max(0, (win.w() - confirmation->w()) / 2),
                               win.y() + 40);
        skin(confirmation);
        confirmation->on_layout();
        confirmation->show();
        if (confirmation_number)
            confirmation_value->take_focus();
    }

    void answer_confirmation(bool accepted) {
        if (!job) {
            confirmation->hide();
            return;
        }
        if (!accepted) {
            if (rigol_job)
                bc_job_cancel(rigol_job);
            bc_job_reply(job, confirmation_id.c_str(), 0);
            bc_job_cancel(job);
            confirmation->hide();
            set_status("Parada solicitada. Confira a tensão física.");
            return;
        }
        if (confirmation_number) {
            double value = 0;
            bc_error error = {{0}};
            if (bc_parse_reading(confirmation_value->value(), confirmation_base_unit.c_str(),
                                 confirmation_unit->text() ? confirmation_unit->text()
                                                           : confirmation_base_unit.c_str(),
                                 &value, &error)) {
                confirmation_error->copy_label(error.text);
                return;
            }
            if (!confirmation_measurement &&
                (value < confirmation_min || value > confirmation_max)) {
                confirmation_error->copy_label(
                    "Informe uma leitura dentro da faixa, ou clique em Parar teste.");
                return;
            }
            json_object *acquired = rigol_acquisition &&
                                            confirmation_id == bc_str(rigol_acquisition, "field") &&
                                            std::fabs(bc_num(rigol_acquisition, "value") - value) <=
                                                1e-10 * std::max(1.0, std::fabs(value))
                                        ? rigol_acquisition
                                        : nullptr;
            bc_job_reply_acquired(job, confirmation_id.c_str(), 1, value, acquired);
        } else
            bc_job_reply(job, confirmation_id.c_str(), 1);
        confirmation->hide();
    }
