#include "PythonScriptLanguage.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/reg_ex.hpp>
#include <godot_cpp/classes/reg_ex_match.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "PythonScript.hpp"

#include "pocketpy.h"

namespace pkpy {

String PythonScriptLanguage::_get_name() const {
	return "Python";
}

void PythonScriptLanguage::_init() {
	py_initialize();

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	// add_project_setting(project_settings, LUA_PATH_SETTING, "res://?.lua;res://?/init.lua");
}

String PythonScriptLanguage::_get_type() const {
	return "PythonScript";
}

String PythonScriptLanguage::_get_extension() const {
	return "py";
}

void PythonScriptLanguage::_finish() {
	py_finalize();
}

PackedStringArray PythonScriptLanguage::_get_reserved_words() const {
	// import keyword
	// print(keyword.kwlist)
	return godot::helpers::append_all(PackedStringArray(),
			"False", "None", "True", "and", "as", "assert",
			"async", "await", "break", "class", "continue",
			"def", "del", "elif", "else", "except", "finally",
			"for", "from", "global", "if", "import", "in", "is",
			"lambda", "nonlocal", "not", "or", "pass", "raise",
			"return", "try", "while", "with", "yield");
}

bool PythonScriptLanguage::_is_control_flow_keyword(const String &keyword) const {
	return false;
}

PackedStringArray PythonScriptLanguage::_get_comment_delimiters() const {
	return godot::helpers::append_all(PackedStringArray(),
			"#");
}

PackedStringArray PythonScriptLanguage::_get_doc_comment_delimiters() const {
	return godot::helpers::append_all(PackedStringArray(),
			"##");
}

PackedStringArray PythonScriptLanguage::_get_string_delimiters() const {
	return godot::helpers::append_all(PackedStringArray(),
			"\" \"", "f\" \"", "r\" \"",
			"' '", "f' '", "r' '",
			"\"\"\" \"\"\"", "f\"\"\" \"\"\"", "r\"\"\" \"\"\"",
			"''' '''", "f''' '''", "r''' '''");
}

Ref<Script> PythonScriptLanguage::_make_template(const String &_template, const String &class_name, const String &base_class_name) const {
	Ref<PythonScript> script = memnew(PythonScript);
	String source_code = _template.replace("_BASE_CLASS_", base_class_name)
								 .replace("_CLASS_", class_name);
	script->set_source_code(source_code);
	return script;
}

TypedArray<Dictionary> PythonScriptLanguage::_get_built_in_templates(const StringName &p_object) const {
	return {};
}

bool PythonScriptLanguage::_is_using_templates() {
	return true;
}

Dictionary PythonScriptLanguage::_validate(const String &script, const String &path, bool validate_functions, bool validate_errors, bool validate_warnings, bool validate_safe_lines) const {
	Dictionary result;
	result["valid"] = true;
	return result;
}

String PythonScriptLanguage::_validate_path(const String &path) const {
	String class_name = path.get_file().get_basename();
	return "";
}

Object *PythonScriptLanguage::_create_script() const {
	return memnew(PythonScript);
}

bool PythonScriptLanguage::_has_named_classes() const {
	return false;
}

bool PythonScriptLanguage::_supports_builtin_mode() const {
	return false;
}

bool PythonScriptLanguage::_supports_documentation() const {
	return false;
}

bool PythonScriptLanguage::_can_inherit_from_file() const {
	return false;
}

int32_t PythonScriptLanguage::_find_function(const String &p_function, const String &p_code) const {
	return -1;
}

String PythonScriptLanguage::_make_function(const String &p_class_name, const String &p_function_name, const PackedStringArray &p_function_args) const {
	return {};
}

bool PythonScriptLanguage::_can_make_function() const {
	return false;
}

Error PythonScriptLanguage::_open_in_external_editor(const Ref<Script> &p_script, int32_t p_line, int32_t p_column) {
	return OK;
}

bool PythonScriptLanguage::_overrides_external_editor() {
	return false;
}

ScriptLanguage::ScriptNameCasing PythonScriptLanguage::_preferred_file_name_casing() const {
	return SCRIPT_NAME_CASING_AUTO;
}

Dictionary PythonScriptLanguage::_complete_code(const String &p_code, const String &p_path, Object *p_owner) const {
	return {};
}

Dictionary PythonScriptLanguage::_lookup_code(const String &p_code, const String &p_symbol, const String &p_path, Object *p_owner) const {
	Dictionary result;
	result["result"] = ERR_UNAVAILABLE;
	result["type"] = Variant::Type::NIL;
	return result;
}

String PythonScriptLanguage::_auto_indent_code(const String &p_code, int32_t p_from_line, int32_t p_to_line) const {
	return p_code;
}

void PythonScriptLanguage::_add_global_constant(const StringName &p_name, const Variant &p_value) {
	// lua_state->get_globals()->set(p_name, p_value);
}

void PythonScriptLanguage::_add_named_global_constant(const StringName &p_name, const Variant &p_value) {
	// lua_state->get_globals()->set(p_name, p_value);
}

void PythonScriptLanguage::_remove_named_global_constant(const StringName &p_name) {
	// lua_state->get_globals()->set(p_name, nullptr);
}

void PythonScriptLanguage::_thread_enter() {
}

void PythonScriptLanguage::_thread_exit() {
}

String PythonScriptLanguage::_debug_get_error() const {
	return {};
}

int32_t PythonScriptLanguage::_debug_get_stack_level_count() const {
	return {};
}

int32_t PythonScriptLanguage::_debug_get_stack_level_line(int32_t p_level) const {
	return {};
}

String PythonScriptLanguage::_debug_get_stack_level_function(int32_t p_level) const {
	return {};
}

String PythonScriptLanguage::_debug_get_stack_level_source(int32_t p_level) const {
	return {};
}

Dictionary PythonScriptLanguage::_debug_get_stack_level_locals(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) {
	return {};
}

Dictionary PythonScriptLanguage::_debug_get_stack_level_members(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) {
	return {};
}

void *PythonScriptLanguage::_debug_get_stack_level_instance(int32_t p_level) {
	return {};
}

Dictionary PythonScriptLanguage::_debug_get_globals(int32_t p_max_subitems, int32_t p_max_depth) {
	return {};
}

String PythonScriptLanguage::_debug_parse_stack_level_expression(int32_t p_level, const String &p_expression, int32_t p_max_subitems, int32_t p_max_depth) {
	return {};
}

TypedArray<Dictionary> PythonScriptLanguage::_debug_get_current_stack_info() {
	return {};
}

void PythonScriptLanguage::_reload_all_scripts() {
}

void PythonScriptLanguage::_reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
}

PackedStringArray PythonScriptLanguage::_get_recognized_extensions() const {
	return godot::helpers::append_all(PackedStringArray(),
			"py", "pyi");
}

TypedArray<Dictionary> PythonScriptLanguage::_get_public_functions() const {
	return {};
}

Dictionary PythonScriptLanguage::_get_public_constants() const {
	return {};
}

TypedArray<Dictionary> PythonScriptLanguage::_get_public_annotations() const {
	return {};
}

void PythonScriptLanguage::_profiling_start() {
	// TODO
}

void PythonScriptLanguage::_profiling_stop() {
	// TODO
}

void PythonScriptLanguage::_profiling_set_save_native_calls(bool p_enable) {
	// TODO
}

int32_t PythonScriptLanguage::_profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) {
	// TODO
	return 0;
}

int32_t PythonScriptLanguage::_profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) {
	// TODO
	return 0;
}

void PythonScriptLanguage::_frame() {
}

bool PythonScriptLanguage::_handles_global_class_type(const String &type) const {
	return type == _get_type();
}

Dictionary PythonScriptLanguage::_get_global_class_name(const String &path) const {
	Ref<PythonScript> script = ResourceLoader::get_singleton()->load(path);

	Dictionary result;
	if (script.is_valid() && script->_is_valid()) {
		result["name"] = script->_get_global_name();
		result["base_type"] = script->_get_instance_base_type();
		result["icon_path"] = script->_get_class_icon_path();
		result["is_abstract"] = script->_is_abstract();
		result["is_tool"] = script->_is_tool();
	}
	return result;
}

PythonScriptLanguage *PythonScriptLanguage::get_singleton() {
	return instance;
}

PythonScriptLanguage *PythonScriptLanguage::get_or_create_singleton() {
	if (!instance) {
		instance = memnew(PythonScriptLanguage);
		Engine::get_singleton()->register_script_language(instance);
	}
	return instance;
}

void PythonScriptLanguage::delete_singleton() {
	if (instance) {
		Engine::get_singleton()->unregister_script_language(instance);
		memdelete(instance);
		instance = nullptr;
	}
}

void PythonScriptLanguage::_bind_methods() {
}

PythonScriptLanguage *PythonScriptLanguage::instance = nullptr;

} //namespace pkpy
