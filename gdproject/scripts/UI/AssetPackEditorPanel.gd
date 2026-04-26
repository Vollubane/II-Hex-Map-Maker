class_name AssetPackEditorPanel
extends Panel

## Émis à la fermeture (bouton ×), pour rafraîchir la liste par exemple.
signal editor_closed

const ASSET_PACKS_DIR := "user://Asset Packs"
const MANIFEST_FILE := "manifeste.json"

const KEY_NOM := "nom"
const KEY_VERSION := "version"
const KEY_POID := "poid"
const KEY_DATE := "date"
const KEY_ASSET := "asset"

const _FORBIDDEN_FILENAME_CHARS: Array[String] = ["/", "\\", "\"", ":", "*", "?", "<", ">", "|"]

@onready var _close_button: Button = $Button
@onready var _name_label: Label = $VBoxContainer/DetailPanel/HBoxContainerDetail/NamePanel/HBoxContainer/Name
@onready var _name_row: HBoxContainer = $VBoxContainer/DetailPanel/HBoxContainerDetail/NamePanel/HBoxContainer
@onready var _edit_name_button: Button = $VBoxContainer/DetailPanel/HBoxContainerDetail/NamePanel/HBoxContainer/EditNameButton
@onready var _version_label: Label = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/VersionPanel/VBoxContainer/VersionValuePanel/HBoxContainer/Version
)
@onready var _version_row: HBoxContainer = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/VersionPanel/VBoxContainer/VersionValuePanel/HBoxContainer
)
@onready var _edit_version_button: Button = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/VersionPanel/VBoxContainer/VersionValuePanel/HBoxContainer/EditVersionButton
)

var _pack_dir: String = ""
var _name_line_edit: LineEdit
var _version_line_edit: LineEdit
var _saved_name_for_edit: String = ""
var _saved_version_for_edit: String = ""
var _suppress_name_focus: bool = false
var _suppress_version_focus: bool = false


func _ready() -> void:
	visible = false
	_close_button.pressed.connect(_on_close_pressed)
	_edit_name_button.pressed.connect(_begin_name_edit)
	_edit_version_button.pressed.connect(_begin_version_edit)

	_name_line_edit = LineEdit.new()
	_name_line_edit.visible = false
	_name_line_edit.custom_minimum_size = Vector2(280, 40)
	_name_line_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_name_row.add_child(_name_line_edit)
	_name_row.move_child(_name_line_edit, _name_label.get_index() + 1)
	_name_line_edit.text_submitted.connect(_on_name_line_submitted)
	_name_line_edit.focus_exited.connect(_on_name_focus_exited)

	_version_line_edit = LineEdit.new()
	_version_line_edit.visible = false
	_version_line_edit.custom_minimum_size = Vector2(75, 0)
	_version_row.add_child(_version_line_edit)
	_version_row.move_child(_version_line_edit, _version_label.get_index() + 1)
	_version_line_edit.text_submitted.connect(_on_version_line_submitted)
	_version_line_edit.focus_exited.connect(_on_version_focus_exited)


func _input(event: InputEvent) -> void:
	if _name_line_edit.visible:
		if _click_outside_control(event, _name_line_edit):
			_cancel_name_edit()
			get_viewport().set_input_as_handled()
			return
	if _version_line_edit.visible:
		if _click_outside_control(event, _version_line_edit):
			_cancel_version_edit()
			get_viewport().set_input_as_handled()


func _click_outside_control(event: InputEvent, le: LineEdit) -> bool:
	if not (event is InputEventMouseButton):
		return false
	var mb := event as InputEventMouseButton
	if not mb.pressed or mb.button_index != MOUSE_BUTTON_LEFT:
		return false
	return not le.get_global_rect().has_point(mb.global_position)


func open_for_pack(pack_dir: String) -> void:
	_pack_dir = _norm_dir(pack_dir)
	_reload_from_disk()
	var list := get_parent().get_node_or_null("ListOfAssetPackPanel") as Control
	if list:
		list.visible = false
	visible = true


func _manifest_path() -> String:
	return _pack_dir.path_join(MANIFEST_FILE)


func _reload_from_disk() -> void:
	var d := _read_pack_dict(_manifest_path())
	var folder := _pack_dir.get_file()
	var nom := str(d.get(KEY_NOM, "")).strip_edges()
	if nom.is_empty():
		nom = folder
	_name_label.text = nom
	_version_label.text = str(d.get(KEY_VERSION, ""))


func _read_pack_dict(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return {}
	var txt := f.get_as_text()
	var parsed = JSON.parse_string(txt)
	return parsed if parsed is Dictionary else {}


func _write_json(path: String, data: Dictionary) -> Error:
	var json_text := JSON.stringify(data, "\t")
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return FAILED
	f.store_string(json_text)
	return OK


func _sanitize_filename_stem(s: String) -> String:
	var t := s.strip_edges()
	if t.to_lower().ends_with(".json"):
		t = t.substr(0, t.length() - 5)
	return t.strip_edges()


func _norm_dir(p: String) -> String:
	return p.rstrip("/").replace("\\", "/")


func _pack_dirs_sorted() -> Array[String]:
	var da := DirAccess.open(ASSET_PACKS_DIR)
	if da == null:
		return []
	var out: Array[String] = []
	da.list_dir_begin()
	var entry := da.get_next()
	while entry != "":
		if da.current_is_dir() and not entry.begins_with("."):
			var sub := ASSET_PACKS_DIR.path_join(entry)
			if FileAccess.file_exists(sub.path_join(MANIFEST_FILE)):
				out.append(sub)
		entry = da.get_next()
	da.list_dir_end()
	out.sort()
	return out


func _is_nom_taken_by_other_pack(exclude_pack_dir: String, nom: String) -> bool:
	var want := nom.strip_edges()
	if want.is_empty():
		return true
	var ex := _norm_dir(exclude_pack_dir)
	for d in _pack_dirs_sorted():
		var p := _norm_dir(d)
		if p == ex:
			continue
		var m := _read_pack_dict(d.path_join(MANIFEST_FILE))
		var other := str(m.get(KEY_NOM, "")).strip_edges()
		if other == want:
			return true
	return false


func _title_contains_forbidden(s: String) -> bool:
	for ch in _FORBIDDEN_FILENAME_CHARS:
		if s.contains(ch):
			return true
	return false


func _begin_name_edit() -> void:
	_saved_name_for_edit = _name_label.text
	_name_line_edit.text = _saved_name_for_edit
	_name_label.visible = false
	_name_line_edit.visible = true
	set_process_input(true)
	_name_line_edit.grab_focus()
	_name_line_edit.call_deferred("select_all")


func _cancel_name_edit() -> void:
	if not _name_line_edit.visible:
		return
	_name_line_edit.text = _saved_name_for_edit
	_name_label.text = _saved_name_for_edit
	_name_line_edit.visible = false
	_name_label.visible = true
	set_process_input(false)


func _on_name_line_submitted(new_text: String) -> void:
	if _title_contains_forbidden(new_text):
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return
	var nom := new_text.strip_edges()
	var stem := _sanitize_filename_stem(new_text)
	var folder_base := _pack_dir.get_file()
	var mp := _manifest_path()
	var data := _read_pack_dict(mp)

	if stem.is_empty() or nom.is_empty():
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	if _is_nom_taken_by_other_pack(_pack_dir, nom):
		push_warning("AssetPackEditorPanel: nom déjà utilisé")
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	var da := DirAccess.open(ASSET_PACKS_DIR)
	if da == null:
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	if stem != folder_base and da.dir_exists(stem):
		push_warning("AssetPackEditorPanel: dossier déjà existant")
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	if stem != folder_base:
		var err_rn := da.rename(folder_base, stem)
		if err_rn != OK:
			push_error("AssetPackEditorPanel: renommage dossier %s" % err_rn)
			_suppress_name_focus = true
			_cancel_name_edit()
			call_deferred("_clear_name_suppress")
			return
		_pack_dir = ASSET_PACKS_DIR.path_join(stem)
		mp = _manifest_path()

	data = _read_pack_dict(mp)
	data[KEY_NOM] = nom
	if _write_json(mp, data) != OK:
		push_error("AssetPackEditorPanel: écriture manifeste")
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	_suppress_name_focus = true
	_name_label.text = nom
	_name_line_edit.text = nom
	_name_line_edit.visible = false
	_name_label.visible = true
	set_process_input(false)
	call_deferred("_clear_name_suppress")


func _clear_name_suppress() -> void:
	_suppress_name_focus = false


func _on_name_focus_exited() -> void:
	if _suppress_name_focus:
		return
	if not _name_line_edit.visible:
		return
	_cancel_name_edit()


func _begin_version_edit() -> void:
	_saved_version_for_edit = _version_label.text
	_version_line_edit.text = _saved_version_for_edit
	_version_label.visible = false
	_version_line_edit.visible = true
	set_process_input(true)
	_version_line_edit.grab_focus()
	_version_line_edit.call_deferred("select_all")


func _cancel_version_edit() -> void:
	if not _version_line_edit.visible:
		return
	_version_line_edit.text = _saved_version_for_edit
	_version_label.text = _saved_version_for_edit
	_version_line_edit.visible = false
	_version_label.visible = true
	if not _name_line_edit.visible:
		set_process_input(false)


func _on_version_line_submitted(new_text: String) -> void:
	var mp := _manifest_path()
	var data := _read_pack_dict(mp)
	data[KEY_VERSION] = new_text.strip_edges()
	if _write_json(mp, data) != OK:
		push_error("AssetPackEditorPanel: écriture manifeste (version)")
		_suppress_version_focus = true
		_cancel_version_edit()
		call_deferred("_clear_version_suppress")
		return
	_suppress_version_focus = true
	_version_label.text = data[KEY_VERSION]
	_version_line_edit.visible = false
	_version_label.visible = true
	if not _name_line_edit.visible:
		set_process_input(false)
	call_deferred("_clear_version_suppress")


func _clear_version_suppress() -> void:
	_suppress_version_focus = false


func _on_version_focus_exited() -> void:
	if _suppress_version_focus:
		return
	if not _version_line_edit.visible:
		return
	_cancel_version_edit()


func _on_close_pressed() -> void:
	if _name_line_edit.visible:
		_cancel_name_edit()
	if _version_line_edit.visible:
		_cancel_version_edit()
	visible = false
	var list := get_parent().get_node_or_null("ListOfAssetPackPanel") as Control
	if list:
		list.visible = true
	editor_closed.emit()
