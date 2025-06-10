#include <godot_cpp/classes/script.hpp>

#include "PythonScriptInstance.hpp"

#include "PythonScript.hpp"
#include "PythonScriptLanguage.hpp"
#include "PythonScriptProperty.hpp"

namespace pkpy {

PythonScriptInstance::PythonScriptInstance(Object *owner, Ref<PythonScript> script)
	: owner(owner)
	, script(script)
{
	known_instances.insert(owner, this);

	const PythonScriptMetadata& metadata = script->get_metadata();
	for (auto [name, signal] : metadata.signals) {
		data->set(name, Signal(owner, name));
	}
}

PythonScriptInstance::~PythonScriptInstance() {
	known_instances.erase(owner);
}

GDExtensionBool set_func(PythonScriptInstance *p_instance, const StringName *p_name, const Variant *p_value) {
	// 1) try calling `_set`
	if (const PythonScriptMethod *_set = p_instance->script->get_metadata().methods.getptr("_set")) {
		Variant value_was_set = PythonCoroutine::invoke_lua(_set->method, Array::make(p_instance->owner, *p_name, *p_value), false);
		if (value_was_set) {
			return true;
		}
	}

	// b) try setter function from script property
	const PythonScriptProperty *property = p_instance->script->get_metadata().properties.getptr(*p_name);
	if (property && property->set_value(p_instance, *p_value)) {
		return true;
	}

	// c) try setting owner Object property
	if (ClassDB::class_set_property(p_instance->owner, *p_name, *p_value) == OK) {
		return true;
	}

	// d) set raw data
	p_instance->data->rawset(*p_name, *p_value);
	return true;
}

GDExtensionBool get_func(PythonScriptInstance *p_instance, const StringName *p_name, Variant *p_value) {
	// a) try calling `_get`
	if (const PythonScriptMethod *_get = p_instance->script->get_metadata().methods.getptr("_get")) {
		Variant value = PythonFunction::invoke_lua(_get->method, Array::make(p_instance->owner, *p_name), false);
		if (value != Variant()) {
			*p_value = value;
			return true;
		}
	}

	// b) try getter function from script property
	const PythonScriptProperty *property = p_instance->script->get_metadata().properties.getptr(*p_name);
	if (property && property->get_value(p_instance, *p_value)) {
		return true;
	}

	// c) access raw data
	if (auto data_value = p_instance->data->try_get(*p_name)) {
		*p_value = *data_value;
		return true;
	}

	// d) fallback to default property value, if there is one
	if (property) {
		Variant value = property->instantiate_default_value();
		p_instance->data->rawset(*p_name, value);
		*p_value = value;
		return true;
	}

	// e) for methods, return a bound Callable
	if (p_instance->script->get_metadata().methods.has(*p_name)) {
		*p_value = Callable(p_instance->owner, *p_name);
		return true;
	}

	return false;
}

GDExtensionScriptInstanceGetPropertyList get_property_list_func;
GDExtensionScriptInstanceFreePropertyList2 free_property_list_func;
GDExtensionScriptInstanceGetClassCategory get_class_category_func;

GDExtensionBool property_can_revert_func(PythonScriptInstance *p_instance, const StringName *p_name) {
	if (const PythonScriptMethod *method = p_instance->script->get_metadata().methods.getptr("_property_can_revert")) {
		Variant result = PythonFunction::invoke_lua(method->method, Array::make(p_instance->owner, *p_name), false);
		if (result) {
			return true;
		}
	}

	return false;
}

GDExtensionBool property_get_revert_func(PythonScriptInstance *p_instance, const StringName *p_name, Variant *r_ret) {
	if (const PythonScriptMethod *method = p_instance->script->get_metadata().methods.getptr("_property_get_revert")) {
		Variant result = PythonFunction::invoke_lua(method->method, Array::make(p_instance->owner, *p_name), true);
		if (PythonError *error = Object::cast_to<PythonError>(result)) {
			ERR_PRINT(error->get_message());
		}
		else {
			*r_ret = result;
			return true;
		}
	}

	return false;
}

Object *get_owner_func(PythonScriptInstance *p_instance) {
	return p_instance->owner;
}

void get_property_state_func(PythonScriptInstance *p_instance, GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
	for (Variant key : *p_instance->data.ptr()) {
		StringName name = key;
		Variant value = p_instance->data->get(key);
		p_add_func(&name, &value, p_userdata);
	}
}

GDExtensionScriptInstanceGetMethodList get_method_list_func;
GDExtensionScriptInstanceFreeMethodList2 free_method_list_func;

GDExtensionVariantType get_property_type_func(PythonScriptInstance *p_instance, const StringName *p_name, GDExtensionBool *r_is_valid) {
	if (const PythonScriptProperty *property = p_instance->script->get_metadata().properties.getptr(*p_name)) {
		*r_is_valid = true;
		return (GDExtensionVariantType) property->type;
	}
	else {
		*r_is_valid = false;
		return GDEXTENSION_VARIANT_TYPE_NIL;
	}
}

GDExtensionBool validate_property_func(PythonScriptInstance *p_instance, GDExtensionPropertyInfo *p_property) {
	if (const PythonScriptMethod *_validate_property = p_instance->script->get_metadata().methods.getptr("_validate_property")) {
		PropertyInfo property_info(p_property);
		Dictionary property_info_dict = property_info;
		PythonFunction::invoke_lua(_validate_property->method, Array::make(p_instance->owner, property_info_dict), false);
		return true;
	}
	else {
		return false;
	}
}

GDExtensionBool has_method_func(PythonScriptInstance *p_instance, const StringName *p_name) {
	return p_instance->script->_has_method(*p_name);
}

GDExtensionInt get_method_argument_count_func(PythonScriptInstance *p_instance, const StringName *p_name, GDExtensionBool *r_is_valid) {
	Variant result = p_instance->script->_get_script_method_argument_count(*p_name);
	*r_is_valid = result.get_type() != Variant::Type::NIL;
	return result;
}

void call_func(PythonScriptInstance *p_instance, const StringName *p_method, const Variant **p_args, GDExtensionInt p_argument_count, Variant *r_return, GDExtensionCallError *r_error) {
	if (const PythonScriptMethod *method = p_instance->script->get_metadata().methods.getptr(*p_method)) {
		r_error->error = GDEXTENSION_CALL_OK;
		*r_return = PythonCoroutine::invoke_lua(method->method, VariantArguments(p_instance->owner, p_args, p_argument_count), false);
	}
	else {
		r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
	}
}

void notification_func(PythonScriptInstance *p_instance, int32_t p_what, GDExtensionBool p_reversed) {
	if (const PythonScriptMethod *_notification = p_instance->script->get_metadata().methods.getptr("_notification")) {
		PythonCoroutine::invoke_lua(_notification->method, Array::make(p_instance->owner, p_what, p_reversed), false);
	}
}

void to_string_func(PythonScriptInstance *p_instance, GDExtensionBool *r_is_valid, String *r_out) {
	if (const PythonScriptMethod *_to_string = p_instance->script->get_metadata().methods.getptr("_to_string")) {
		Variant result = PythonFunction::invoke_lua(_to_string->method, Array::make(p_instance->owner), false);
		if (result) {
			*r_out = result;
			*r_is_valid = true;
		}
		else {
			*r_is_valid = false;
		}
	}
	else {
		*r_is_valid = false;
	}
}

void refcount_incremented_func(PythonScriptInstance *) {
}

GDExtensionBool refcount_decremented_func(PythonScriptInstance *) {
	return true;
}

void *get_script_func(PythonScriptInstance *instance) {
	return instance->script.ptr()->_owner;
}

GDExtensionBool is_placeholder_func(PythonScriptInstance *instance) {
	return false;
}

GDExtensionScriptInstanceSet set_fallback_func;
GDExtensionScriptInstanceGet get_fallback_func;

void *get_language_func(PythonScriptInstance *instance) {
	return PythonScriptLanguage::get_singleton()->_owner;
}

void free_func(PythonScriptInstance *instance) {
	memdelete(instance);
}

GDExtensionScriptInstanceInfo3 script_instance_info = {
	(GDExtensionScriptInstanceSet) set_func,
	(GDExtensionScriptInstanceGet) get_func,
	(GDExtensionScriptInstanceGetPropertyList) get_property_list_func,
	(GDExtensionScriptInstanceFreePropertyList2) free_property_list_func,
	(GDExtensionScriptInstanceGetClassCategory) get_class_category_func,
	(GDExtensionScriptInstancePropertyCanRevert) property_can_revert_func,
	(GDExtensionScriptInstancePropertyGetRevert) property_get_revert_func,
	(GDExtensionScriptInstanceGetOwner) get_owner_func,
	(GDExtensionScriptInstanceGetPropertyState) get_property_state_func,
	(GDExtensionScriptInstanceGetMethodList) get_method_list_func,
	(GDExtensionScriptInstanceFreeMethodList2) free_method_list_func,
	(GDExtensionScriptInstanceGetPropertyType) get_property_type_func,
	(GDExtensionScriptInstanceValidateProperty) validate_property_func,
	(GDExtensionScriptInstanceHasMethod) has_method_func,
	(GDExtensionScriptInstanceGetMethodArgumentCount) get_method_argument_count_func,
	(GDExtensionScriptInstanceCall) call_func,
	(GDExtensionScriptInstanceNotification2) notification_func,
	(GDExtensionScriptInstanceToString) to_string_func,
	(GDExtensionScriptInstanceRefCountIncremented) refcount_incremented_func,
	(GDExtensionScriptInstanceRefCountDecremented) refcount_decremented_func,
	(GDExtensionScriptInstanceGetScript) get_script_func,
	(GDExtensionScriptInstanceIsPlaceholder) is_placeholder_func,
	(GDExtensionScriptInstanceSet) set_fallback_func,
	(GDExtensionScriptInstanceGet) get_fallback_func,
	(GDExtensionScriptInstanceGetLanguage) get_language_func,
	(GDExtensionScriptInstanceFree) free_func,
};
GDExtensionScriptInstanceInfo3 *PythonScriptInstance::get_script_instance_info() {
	return &script_instance_info;
}

PythonScriptInstance *PythonScriptInstance::attached_to_object(Object *owner) {
	if (PythonScriptInstance **ptr = known_instances.getptr(owner)) {
		return *ptr;
	}
	else {
		return nullptr;
	}
}

Variant PythonScriptInstance::rawget(const Variant& self, const Variant& index) {
	if (PythonScriptInstance *instance = attached_to_object(self)) {
		return instance->data->rawget(index);
	}
	else {
		return {};
	}
}

void PythonScriptInstance::rawset(const Variant& self, const Variant& index, const Variant& value) {
	if (PythonScriptInstance *instance = attached_to_object(self)) {
		instance->data->rawset(index, value);
	}
}

HashMap<Object *, PythonScriptInstance *> PythonScriptInstance::known_instances;

}
