#include "PythonScript.hpp"

#include "Common.hpp"
#include "PythonScriptInstance.hpp"
#include "PythonScriptLanguage.hpp"

#include "gdextension_interface.h"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/error_macros.hpp"
#include "godot_cpp/godot.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/signal.hpp"

#include <stdlib.h>
#include <thread>

namespace pkpy {

PythonScript::PythonScript() :
		ScriptExtension() {
	placeholders.insert(this, {});
	placeholder_fallback_enabled = true;
}

PythonScript::~PythonScript() {
	placeholders.erase(this);
}

bool PythonScript::_editor_can_reload_from_file() {
	return true;
}

void PythonScript::_placeholder_erased(void *p_placeholder) {
	placeholders.get(this).erase(p_placeholder);
}

bool PythonScript::_can_instantiate() const {
	return _is_valid() && (_is_tool() || !Engine::get_singleton()->is_editor_hint());
}

Ref<Script> PythonScript::_get_base_script() const {
	return {};
}

StringName PythonScript::_get_global_name() const {
	return meta.class_name;
}

bool PythonScript::_inherits_script(const Ref<Script> &script) const {
	// if (const PythonScript *py_script = Object::cast_to<PythonScript>(script.ptr())) {
	// 	py_Type derived = meta.exposed_type;
	// 	py_Type base = py_script->meta.exposed_type;
	// 	if (!derived || !base)
	// 		return false;
	// 	return py_issubclass(derived, base);
	// }
	return false;
}

StringName PythonScript::_get_instance_base_type() const {
	return meta.extends;
}

void *PythonScript::_instance_create(Object *for_object) const {
	PythonScriptInstance *ud = (PythonScriptInstance *)py_newobject(py_retval(), meta.type, -1, sizeof(PythonScriptInstance));
	new (ud) PythonScriptInstance(for_object, Ref<PythonScript>(this));
	py_assign(&ud->py, py_retval());
	// assign owner
	Variant owner = for_object;
	py_newvariant(py_emplacedict(&ud->py, py_name("owner")), &owner);
	// assign default values
	for (const auto &it : meta.default_values) {
		py_Name name = godot_name_to_python(it.key);
		Variant value = it.value.duplicate();
		py_newvariant(py_retval(), &value);
		py_setdict(&ud->py, name, py_retval());
	}
	// assign signals
	for (const auto &it : meta.signals) {
		py_Name name = godot_name_to_python(it.key);
		Variant value = Signal(for_object, it.key);
		py_newvariant(py_retval(), &value);
		py_setdict(&ud->py, name, py_retval());
	}
	// call __init__
	py_push(&ud->py);
	bool ok = py_pushmethod(pyctx()->names.__init__);
	if (!ok) {
		py_pop();
	} else {
		py_StackRef p0 = py_peek(0);
		if (!py_vectorcall(0, 0)) {
			log_python_error_and_clearexc(p0);
			return NULL;
		}
	}
	return godot::internal::gdextension_interface_script_instance_create3(PythonScriptInstance::get_script_instance_info(), ud);
}

void *PythonScript::_placeholder_instance_create(Object *for_object) const {
	void *placeholder = godot::internal::gdextension_interface_placeholder_script_instance_create(PythonScriptLanguage::get_singleton()->_owner, this->_owner, for_object->_owner);
	placeholders.get(this).insert(placeholder);
	_update_placeholder_exports(placeholder);
	return placeholder;
}

bool PythonScript::_instance_has(Object *p_object) const {
	return PythonScriptInstance::attached_to_object(p_object);
}

bool PythonScript::_has_source_code() const {
	return !source_code.is_empty();
}

String PythonScript::_get_source_code() const {
	return source_code;
}

void PythonScript::_set_source_code(const String &code) {
	source_code = code;
}

Error PythonScript::_reload(bool keep_state) {
	(void)keep_state;

	PythonContextLock lock;
	std::thread::id tid = std::this_thread::get_id();
	if (tid != pyctx()->main_thread_id) {
		py_switchvm(0);
	}

	placeholder_fallback_enabled = true;
	printf("=> PythonScript: %p reload from %s\n", this, get_path().utf8().get_data());

	meta.is_valid = false;
	PythonScriptMeta new_meta;
	auto ctx = &pyctx()->reloading_context;
	ctx->reset();

	String basename = get_path().get_file().get_basename();
	if (basename.is_empty() || !has_source_code()) {
		return OK;
	}
	auto path_cstr = get_path().utf8();

	printf("==> reloading python script: %s\n", path_cstr.get_data());
	String module_path = "godot.scripts." + basename;
	auto module_path_cstr = module_path.utf8();

	py_GlobalRef module = py_getmodule(module_path_cstr);
	if (module == NULL) {
		module = py_newmodule(module_path_cstr);
	}

	// NOTE: old variables still exist if not overwritten
	py_StackRef p0 = py_peek(0);
	bool ok = py_exec(source_code.utf8().get_data(), path_cstr, RELOAD_MODE, module);
	if (!ok) {
		log_python_error_and_clearexc(p0);
		return ERR_COMPILATION_FAILED;
	}

	ctx->class_name = StringName(basename);
	py_Type exposed_type = tp_nil;
	py_Name class_name = godot_name_to_python(ctx->class_name);
	py_ItemRef exposed_class = py_getdict(module, class_name);
	if (!exposed_class || !py_istype(exposed_class, tp_type)) {
		ERR_PRINT("Failed to find class '" + ctx->class_name + "' in " + get_path());
		return ERR_COMPILATION_FAILED;
	}

	exposed_type = py_totype(exposed_class);
	Vector<DefineStatement *> defines;

	std::pair<Vector<DefineStatement *> *, PythonScriptMeta *> ctx_pair = { &defines, &new_meta };

	py_applydict(
			exposed_class, [](py_Name name, py_ItemRef value, void *ctx) -> bool {
				auto ctx_pair = (std::pair<Vector<DefineStatement *> *, PythonScriptMeta *> *)ctx;
				Vector<DefineStatement *> *defines = ctx_pair->first;
				PythonScriptMeta *new_meta = ctx_pair->second;
				StringName name_godot = python_name_to_godot(name);

				if (py_istype(value, pyctx()->tp_DefineStatement)) {
					DefineStatement *d = (DefineStatement *)py_touserdata(value);
					d->name = name_godot;
					defines->push_back(d);
				} else if (py_istype(value, tp_function)) {
					new_meta->methods[name_godot] = 0;
				}
				return true;
			},
			&ctx_pair);

	defines.sort();

	PackedStringArray buffer;
	buffer.push_back("# " + get_path());
	buffer.push_back("extends " + ctx->extends);
	buffer.push_back("");
	for (DefineStatement *d : defines) {
		if (d->is_signal()) {
			SignalStatement *s = (SignalStatement *)d;
			buffer.append("signal " + s->name + "(" + String(", ").join(s->arguments) + ")");
			new_meta.signals[s->name] = s->arguments;
		} else {
			ExportStatement *e = (ExportStatement *)d;
			buffer.append(e->template_.replace("?", e->name));
			new_meta.default_values[e->name] = e->default_value;
		}
	}

	Ref<GDScript> gds = memnew(GDScript);
	new_meta.gds = gds;
	new_meta.gds->set_source_code(String("\n").join(buffer));
	Error err = new_meta.gds->reload(false);
	if (err != OK) {
		ERR_PRINT("Failed to compile GDScript: " + itos(err) + "\n" + new_meta.gds->get_source_code());
		return ERR_COMPILATION_FAILED;
	}

	new_meta.type = exposed_type;
	new_meta.class_name = ctx->class_name;
	new_meta.extends = ctx->extends;

	new_meta.is_valid = true;
	meta = std::move(new_meta);

	placeholder_fallback_enabled = false;
	return OK;
}

TypedArray<Dictionary> PythonScript::_get_documentation() const {
	// get doc from exposed class
	return {};
}

String PythonScript::_get_class_icon_path() const {
	return String();
}

bool PythonScript::_has_method(const StringName &p_method) const {
	if (!_is_valid())
		return false;
	return meta.methods.has(p_method);
}

bool PythonScript::_has_static_method(const StringName &p_method) const {
	return false;
}

Variant PythonScript::_get_script_method_argument_count(const StringName &p_method) const {
	return {};
}

Dictionary PythonScript::_get_method_info(const StringName &p_method) const {
	return {};
}

bool PythonScript::_is_tool() const {
	if (!_is_valid())
		return false;
	return meta.gds->is_tool();
}

bool PythonScript::_is_valid() const {
	return meta.is_valid;
}

bool PythonScript::_is_abstract() const {
	return false;
}

ScriptLanguage *PythonScript::_get_language() const {
	return PythonScriptLanguage::get_singleton();
}

bool PythonScript::_has_script_signal(const StringName &p_signal) const {
	if (!_is_valid())
		return false;
	return meta.signals.has(p_signal);
}

TypedArray<Dictionary> PythonScript::_get_script_signal_list() const {
	if (!_is_valid())
		return {};
	return meta.gds->get_script_signal_list();
}

bool PythonScript::_has_property_default_value(const StringName &p_property) const {
	return _get_property_default_value(p_property).get_type() != Variant::NIL;
}

Variant PythonScript::_get_property_default_value(const StringName &p_property) const {
	if (!_is_valid())
		return Variant();
	auto it = meta.default_values.find(p_property);
	if (it != meta.default_values.end()) {
		return it->value;
	}
	return Variant();
}

void PythonScript::_update_exports() {
	for (void *placeholder : placeholders.get(this)) {
		_update_placeholder_exports(placeholder);
	}
}

TypedArray<Dictionary> PythonScript::_get_script_method_list() const {
	TypedArray<Dictionary> methods;
	return methods;
}

TypedArray<Dictionary> PythonScript::_get_script_property_list() const {
	if (!_is_valid())
		return {};
	auto retval = meta.gds->get_script_property_list();
	// category
	if (!retval.is_empty() && retval[0].get("usage") == Variant(PROPERTY_USAGE_CATEGORY)) {
		char buf[32];
		snprintf(buf, sizeof(buf), " (%d)", meta.type);
		String category = String(meta.class_name) + buf;
		retval[0].set("name", category);
	}
	return retval;
}

int32_t PythonScript::_get_member_line(const StringName &p_member) const {
	return {};
}

Dictionary PythonScript::_get_constants() const {
	return {};
}

TypedArray<StringName> PythonScript::_get_members() const {
	TypedArray<StringName> members;
	return members;
}

bool PythonScript::_is_placeholder_fallback_enabled() const {
	return placeholder_fallback_enabled;
}

Variant PythonScript::_get_rpc_config() const {
	return {};
}

Variant PythonScript::_new(const Variant **args, GDExtensionInt arg_count, GDExtensionCallError &error) {
	if (!_can_instantiate()) {
		error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return {};
	}
	Variant object = ClassDB::instantiate(_get_instance_base_type());
	if (Object *obj = object) {
		obj->set_script(this);
	}
	return object;
}

void PythonScript::_bind_methods() {
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "new", &PythonScript::_new);
}

String PythonScript::_to_string() const {
	return String("[%s:%d]") % Array::make(get_class_static(), get_instance_id());
}

static Variant construct_default_variant(Variant::Type type) {
	UninitializedVariant uninitialized_res;
	GDExtensionCallError error;
	internal::gdextension_interface_variant_construct((GDExtensionVariantType)type, uninitialized_res.ptr(), nullptr, 0, &error);
	if (error.error != GDEXTENSION_CALL_OK) {
		ERR_PRINT("construct_default_variant() failed: " + Variant::get_type_name(type));
		return Variant();
	}
	return *uninitialized_res.ptr();
}

void PythonScript::_update_placeholder_exports(void *placeholder) const {
	if (!_is_valid()) {
		return;
	}
	Array properties;
	Dictionary default_values;
	TypedArray<Dictionary> raw_properties = _get_script_property_list();
	for (int i = 0; i < raw_properties.size(); i++) {
		properties.append(raw_properties[i]);
		int type = raw_properties[i].get("type");
		int usage = raw_properties[i].get("usage");
		if (type == 0 || usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) {
			continue;
		}

		StringName name = raw_properties[i].get("name");
		if (name.is_empty()) {
			continue;
		}
		Variant val = _get_property_default_value(name);

		if (val.get_type() == type) {
			default_values[name] = val.duplicate();
			continue;
		}

		default_values[name] = construct_default_variant((Variant::Type)type);
	}

	// String repr = Variant(default_values).stringify();
	// WARN_PRINT("Updating placeholder exports: " + repr);
	godot::internal::gdextension_interface_placeholder_script_instance_update(placeholder, properties._native_ptr(), default_values._native_ptr());
}

HashMap<const PythonScript *, HashSet<void *>> PythonScript::placeholders;

} //namespace pkpy
