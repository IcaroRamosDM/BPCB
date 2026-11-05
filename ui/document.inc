    Dialog *document = nullptr;
    Fl_Scroll *document_scroll = nullptr;
    Fl_Box *document_image = nullptr, *document_info = nullptr;
    Button *document_prev, *document_next, *document_zoom_in, *document_zoom_out,
        *document_fit_button, *document_close;
    std::unique_ptr<Fl_PNG_Image> document_bitmap;
    std::string document_file;
    int document_page = 0, document_pages = 0;
    double document_scale = 1;
    bool document_fit = true;

    void make_document() {
        document = new Dialog(1100, 760, "BPCB · Documentação");
        document_info = label("Carregando documento…", 15, true);
        document_prev = new Button("← Página");
        document_next = new Button("Página →");
        document_zoom_out = new Button("− Zoom");
        document_zoom_in = new Button("+ Zoom");
        document_fit_button = new Button("Enquadrar");
        document_close = new Button("Fechar");
        document_scroll = new Fl_Scroll(16, 70, 1068, 674);
        document_scroll->type(Fl_Scroll::BOTH);
        document_scroll->box(FL_FLAT_BOX);
        document_image = new Fl_Box(16, 70, 1, 1);
        document_image->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        document_scroll->end();
        document_prev->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (!a->busy && a->document_page > 0) {
                    --a->document_page;
                    a->render_document();
                }
            },
            this);
        document_next->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (!a->busy && a->document_page + 1 < a->document_pages) {
                    ++a->document_page;
                    a->render_document();
                }
            },
            this);
        document_zoom_in->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (!a->busy) {
                    a->document_fit = false;
                    a->document_scale = std::min(4.0, a->document_scale + 0.5);
                    a->render_document();
                }
            },
            this);
        document_zoom_out->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (!a->busy) {
                    a->document_fit = false;
                    a->document_scale = std::max(0.5, a->document_scale - 0.5);
                    a->render_document();
                }
            },
            this);
        document_fit_button->callback(
            [](Fl_Widget *, void *v) {
                auto *a = static_cast<App *>(v);
                if (!a->busy) {
                    a->document_fit = true;
                    a->render_document();
                }
            },
            this);
        document_close->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->document->hide();
            },
            this);
        document->on_close = [this] {
            document->hide();
        };
        document->callback(
            [](Fl_Widget *, void *v) {
                static_cast<App *>(v)->document->hide();
            },
            this);
        document->on_layout = [this] {
            int w = document->w(), h = document->h();
            rect(document_info, 18, 14, w - 674, 38);
            rect(document_prev, w - 646, 14, 100, 36);
            rect(document_next, w - 540, 14, 100, 36);
            rect(document_fit_button, w - 434, 14, 100, 36);
            rect(document_zoom_out, w - 328, 14, 94, 36);
            rect(document_zoom_in, w - 228, 14, 94, 36);
            rect(document_close, w - 128, 14, 110, 36);
            rect(document_scroll, 16, 70, w - 32, h - 86);
        };
        document->end();
    }

    void show_document(const char *file) {
        document_file = file;
        document_page = 0;
        document_pages = 0;
        document_scale = 1;
        document_fit = true;
        document_image->image(nullptr);
        document_bitmap.reset();
        document_scroll->scroll_to(0, 0);
        document_image->resize(16, 70, 1, 1);
        document->position(win.x() + 30, win.y() + 30);
        document->on_layout();
        skin(document);
        document->show();
        render_document();
    }

    void render_document() {
        auto *options = json_object_new_object();
        json_object_object_add(options, "page", json_object_new_int(document_page));
        json_object_object_add(options, "scale",
                               json_object_new_double(document_fit ? 0 : document_scale));
        json_object_object_add(options, "width", json_object_new_int(document_scroll->w() - 22));
        json_object_object_add(options, "height", json_object_new_int(document_scroll->h() - 22));
        document_info->copy_label("Renderizando página…");
        for (auto *b : {document_prev, document_next, document_zoom_in, document_zoom_out,
                        document_fit_button})
            b->deactivate();
        launch("render_document", document_file, options);
        json_object_put(options);
    }

    void document_rendered(json_object *result, bool ok, const std::string &error) {
        if (!ok) {
            document_info->copy_label(error.c_str());
            return;
        }
        auto image = std::make_unique<Fl_PNG_Image>(bc_str(result, "path"));
        if (image->fail()) {
            document_info->copy_label("Não foi possível mostrar a página.");
            return;
        }
        document_scale = bc_num(result, "scale");
        document_pages = static_cast<int>(bc_num(result, "pages"));
        document_image->image(nullptr);
        document_bitmap = std::move(image);
        document_scroll->scroll_to(0, 0);
        document_image->resize(16, 70, document_bitmap->w(), document_bitmap->h());
        document_image->image(document_bitmap.get());
        std::string info = std::to_string(document_page + 1) + " / " +
                           std::to_string(document_pages) + " · " +
                           std::to_string(static_cast<int>(document_scale * 100)) + "%";
        document_info->copy_label(info.c_str());
        document_prev->activate();
        document_next->activate();
        document_zoom_in->activate();
        document_zoom_out->activate();
        document_fit_button->activate();
        if (document_page == 0)
            document_prev->deactivate();
        if (document_page + 1 >= document_pages)
            document_next->deactivate();
        document_scroll->redraw();
        set_status("Documento aberto no BPCB.");
    }
