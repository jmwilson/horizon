#include "imp_padstack.hpp"
#include "canvas/canvas_gl.hpp"
#include "header_button.hpp"
#include "parameter_window.hpp"
#include "pool/part.hpp"
#include "widgets/parameter_set_editor.hpp"
#include "board/board_layers.hpp"
#include "core/tool_id.hpp"
#include "widgets/action_button.hpp"
#include "actions.hpp"
#include "util/util.hpp"
#include "util/gtk_util.hpp"

namespace horizon {
ImpPadstack::ImpPadstack(const std::string &padstack_filename, const std::string &pool_path, TempMode tmp_mode)
    : ImpLayer(pool_path), core_padstack(padstack_filename, *pool), padstack(core_padstack.get_padstack())
{
    core = &core_padstack;
    core_padstack.signal_tool_changed().connect(sigc::mem_fun(*this, &ImpBase::handle_tool_change));
    view_angle = 0;
    if (tmp_mode == TempMode::YES) {
        core_padstack.set_temp_mode();
        temp_mode = true;
    }
}

void ImpPadstack::canvas_update()
{
    canvas->update(core_padstack.get_canvas_data());
}

void ImpPadstack::construct()
{
    ImpLayer::construct_layer_box();

    main_window->set_title("Padstack - Interactive Manipulator");
    state_store = std::make_unique<WindowStateStore>(main_window, "imp-padstack");

    header_button = Gtk::manage(new HeaderButton);
    main_window->header->set_custom_title(*header_button);
    header_button->show();
    header_button->signal_closed().connect(sigc::mem_fun(*this, &ImpPadstack::update_header));

    name_entry = header_button->add_entry("Name");
    name_entry->set_text(padstack.name);
    name_entry->set_width_chars(padstack.name.size());
    name_entry->signal_changed().connect([this] { core_padstack.set_needs_save(); });
    name_entry->signal_activate().connect(sigc::mem_fun(*this, &ImpPadstack::update_header));

    auto well_known_name_entry = header_button->add_entry("Well-known name");
    well_known_name_entry->set_text(padstack.well_known_name);
    well_known_name_entry->signal_changed().connect([this] { core_padstack.set_needs_save(); });

    auto type_combo = Gtk::manage(new Gtk::ComboBoxText());
    type_combo->append("top", "Top");
    type_combo->append("bottom", "Bottom");
    type_combo->append("through", "Through");
    type_combo->append("via", "Via");
    type_combo->append("hole", "Hole");
    type_combo->append("mechanical", "Mechanical");
    type_combo->show();
    header_button->add_widget("Type", type_combo);
    type_combo->set_active_id(Padstack::type_lut.lookup_reverse(padstack.type));
    type_combo->signal_changed().connect([this] { core_padstack.set_needs_save(); });

    core_padstack.signal_save().connect([this, well_known_name_entry, type_combo] {
        padstack.name = name_entry->get_text();
        padstack.well_known_name = well_known_name_entry->get_text();
        padstack.type = Padstack::type_lut.lookup(static_cast<std::string>(type_combo->get_active_id()));
    });

    auto editor = new ParameterSetEditor(&core_padstack.parameter_set, false); //, &core_padstack.parameters_required);
    editor->signal_create_extra_widget().connect([this, editor](ParameterID id) {
        auto w = Gtk::manage(new Gtk::CheckButton("Required"));
        w->set_tooltip_text("Parameter has to be set in pad");
        w->set_active(core_padstack.parameters_required.count(id));
        w->signal_toggled().connect([this, id, w, editor] {
            if (w->get_active()) {
                core_padstack.parameters_required.insert(id);
            }
            else {
                core_padstack.parameters_required.erase(id);
            }
            editor->signal_changed().emit();
        });
        return w;
    });

    editor->signal_remove_extra_widget().connect(
            [this](ParameterID id) { core_padstack.parameters_required.erase(id); });

    parameter_window = new ParameterWindow(main_window, &core_padstack.parameter_program_code,
                                           &core_padstack.parameter_set, editor);
    parameter_window->signal_changed().connect([this] { core_padstack.set_needs_save(); });
    parameter_window_add_polygon_expand(parameter_window);
    {
        auto button = Gtk::manage(new Gtk::Button("Parameters…"));
        main_window->header->pack_start(*button);
        button->show();
        button->signal_clicked().connect([this] { parameter_window->present(); });
    }


    parameter_window->signal_apply().connect([this] {
        if (core->tool_is_active())
            return;
        auto &ps = padstack;

        if (auto r = ps.parameter_program.set_code(core_padstack.parameter_program_code)) {
            parameter_window->set_error_message("<b>Compile error:</b>" + r.value());
            return;
        }
        else {
            parameter_window->set_error_message("");
        }
        ps.parameter_set = core_padstack.parameter_set;

        if (auto r = ps.parameter_program.run(ps.parameter_set)) {
            parameter_window->set_error_message("<b>Run error:</b>" + r.value());
            return;
        }
        else {
            parameter_window->set_error_message("");
        }
        core_padstack.rebuild("apply parameter set");
        canvas_update();
    });
    core->signal_tool_changed().connect([this](ToolID t) { parameter_window->set_can_apply(t == ToolID::NONE); });

    add_action_button(ToolID::PLACE_SHAPE);
    add_action_button(ToolID::PLACE_SHAPE_RECTANGLE);
    add_action_button(ToolID::PLACE_SHAPE_OBROUND);
    add_action_button(ToolID::PLACE_HOLE).set_margin_top(5);
    add_action_button(ToolID::PLACE_HOLE_SLOT);
    add_action_button_polygon().set_margin_top(5);
    update_header();
}

ActionToolID ImpPadstack::get_doubleclick_action(ObjectType type, const UUID &uu)
{
    const auto a = ImpBase::get_doubleclick_action(type, uu);
    if (a.is_valid())
        return a;

    switch (type) {
    case ObjectType::SHAPE:
        return ToolID::EDIT_SHAPE;

    default:
        return {};
    }
}

std::map<ObjectType, ImpBase::SelectionFilterInfo> ImpPadstack::get_selection_filter_info() const
{
    const std::vector<int> my_layers = {BoardLayers::TOP_PASTE,   BoardLayers::TOP_MASK,      BoardLayers::TOP_COPPER,
                                        BoardLayers::IN1_COPPER,  BoardLayers::BOTTOM_COPPER, BoardLayers::BOTTOM_MASK,
                                        BoardLayers::BOTTOM_PASTE};
    using Flag = ImpBase::SelectionFilterInfo::Flag;
    std::map<ObjectType, ImpBase::SelectionFilterInfo> r = {
            {ObjectType::SHAPE, {my_layers, Flag::DEFAULT}},
            {ObjectType::HOLE, {}},
            {ObjectType::POLYGON, {my_layers, Flag::DEFAULT}},
    };
    return r;
}

void ImpPadstack::update_header()
{
    const auto &name = name_entry->get_text();
    header_button->set_label(name);
    set_window_title(name);
    parameter_window->set_subtitle(name);
}

bool ImpPadstack::set_filename()
{
    GtkFileChooserNative *native = gtk_file_chooser_native_new("Save Padstack", GTK_WINDOW(main_window->gobj()),
                                                               GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
    auto chooser = Glib::wrap(GTK_FILE_CHOOSER(native));
    chooser->set_do_overwrite_confirmation(true);
    if (suggested_filename.empty()) {
        chooser->set_current_folder(Glib::build_filename(pool->get_base_path(), "padstacks"));
    }
    else {
        auto fi = Gio::File::create_for_path(suggested_filename);
        if (!fi->query_exists())
            fi->make_directory_with_parents();
        chooser->set_current_folder(suggested_filename);
    }
    chooser->set_current_name(name_entry->get_text() + ".json");

    std::string filename;
    auto success = run_native_filechooser_with_retry(chooser, "Error saving padstack", [this, chooser, &filename] {
        filename = append_dot_json(chooser->get_filename());
        if (suggested_filename.size()) {
            if (!Gio::File::create_for_path(filename)->has_prefix(Gio::File::create_for_path(suggested_filename)))
                throw std::runtime_error("package-local padstack must be in " + suggested_filename);
        }
        pool->check_filename_throw(ObjectType::PADSTACK, filename);
        core_padstack.set_filename(filename);
    });
    return success;
}

}; // namespace horizon
