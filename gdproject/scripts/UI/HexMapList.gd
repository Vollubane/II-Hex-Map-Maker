extends Panel

const MAPS_DIR := "user://Map"

## Heure locale PC (dict Time.get_datetime_dict_from_system).
const KEY_LAST_EDITED_LOCAL := "last_edited_local"
const KEY_VERSION := "iihexmm_version"
const KEY_UNIQUE_TILES := "unique_tile_count"
const KEY_TILE_VARIANTS := "tile_variant_count"

const DataDisplayerScene := preload("res://Scene/UI Node/Elements/DataDisplayer.tscn")
const DeletePanelScene := preload("res://Scene/UI Node/Elements/DeletePanel.tscn")
const RootMapScene := preload("res://Scene/RootMap.tscn")

@onready var _vbox: VBoxContainer = $ScrollContainer/VBoxContainer
@onready var _new_map_button: Button = $"HexMap Button"
@onready var _close_button: Button = $Button


func _ready() -> void:
	_ensure_maps_dir()
	_new_map_button.pressed.connect(_on_new_map_pressed)
	_close_button.pressed.connect(_on_close_pressed)
	_refresh_map_list()


func _ensure_maps_dir() -> void:
	var da := DirAccess.open("user://")
	if da == null:
		push_error("HexMapList: cannot open user://")
		return
	if da.dir_exists("Map"):
		return
	var err := da.make_dir("Map")
	if err != OK:
		push_error("HexMapList: cannot create user://Map (%s)" % err)


func _app_version() -> String:
	return str(ProjectSettings.get_setting("application/config/version", "0.0.1"))


func _read_map_dict(path: String) -> Dictionary:
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


func _minimal_map_dict() -> Dictionary:
	return {
		KEY_LAST_EDITED_LOCAL: Time.get_datetime_dict_from_system(),
		KEY_VERSION: _app_version(),
		KEY_UNIQUE_TILES: 0,
		KEY_TILE_VARIANTS: 0,
	}


func _stem_from_path(path: String) -> String:
	var base := path.get_file()
	if base.to_lower().ends_with(".json"):
		return base.left(base.length() - 5)
	return base


func _format_datetime_dict_pc(dt: Dictionary) -> String:
	return "%02d/%02d/%04d %02d:%02d" % [
		int(dt.get("day", 1)),
		int(dt.get("month", 1)),
		int(dt.get("year", 1970)),
		int(dt.get("hour", 0)),
		int(dt.get("minute", 0)),
	]


func _format_info_line(data: Dictionary) -> String:
	var date_s := "—"
	var local_any: Variant = data.get(KEY_LAST_EDITED_LOCAL, null)
	if local_any is Dictionary:
		date_s = _format_datetime_dict_pc(local_any as Dictionary)
	var ver: String = str(data.get(KEY_VERSION, "?"))
	var u: int = int(data.get(KEY_UNIQUE_TILES, 0))
	var v: int = int(data.get(KEY_TILE_VARIANTS, 0))
	return "date : %s | IIHMM : V%s | Tiles : %d | Tiles Variante : %d" % [date_s, ver, u, v]


func _list_map_json_paths() -> Array[String]:
	var da := DirAccess.open(MAPS_DIR)
	if da == null:
		return []
	var names: Array[String] = []
	da.list_dir_begin()
	var entry := da.get_next()
	while entry != "":
		if not da.current_is_dir() and entry.ends_with(".json"):
			names.append(MAPS_DIR.path_join(entry))
		entry = da.get_next()
	da.list_dir_end()
	names.sort()
	return names


func _refresh_map_list() -> void:
	for c in _vbox.get_children():
		c.queue_free()
	await get_tree().process_frame
	for path in _list_map_json_paths():
		_add_row(path)


func _add_row(path: String) -> void:
	var data := _read_map_dict(path)
	var stem := _stem_from_path(path)
	var info := _format_info_line(data)
	var d: DataDisplayer = DataDisplayerScene.instantiate()
	_vbox.add_child(d)
	d.set_meta("map_json_path", path)
	d.set_title_and_info(stem, info)
	d.delete_pressed.connect(_on_row_delete_pressed.bind(d))
	d.launch_pressed.connect(_on_row_play_pressed.bind(d))
	d.empty_pressed.connect(_on_row_copy_pressed.bind(d))
	d.title_submitted.connect(_on_row_title_submitted.bind(d))


func _sanitize_filename_stem(s: String) -> String:
	var t := s.strip_edges()
	if t.to_lower().ends_with(".json"):
		t = t.substr(0, t.length() - 5)
	return t.strip_edges()


func _on_row_title_submitted(new_title: String, displayer: DataDisplayer) -> void:
	var old_path: String = displayer.get_meta("map_json_path", "")
	var stem := _sanitize_filename_stem(new_title)
	if stem.is_empty():
		displayer.set_title(_stem_from_path(old_path))
		return
	var new_path: String = MAPS_DIR.path_join(stem + ".json")
	if new_path == old_path:
		return
	if FileAccess.file_exists(new_path):
		push_warning("HexMapList: name already used")
		displayer.set_title(_stem_from_path(old_path))
		return
	var da := DirAccess.open(MAPS_DIR)
	if da == null:
		displayer.set_title(_stem_from_path(old_path))
		return
	var err := da.rename(old_path.get_file(), new_path.get_file())
	if err != OK:
		push_error("HexMapList: rename failed %s" % err)
		displayer.set_title(_stem_from_path(old_path))
		return
	displayer.set_meta("map_json_path", new_path)
	displayer.set_title(stem)
	_refresh_map_list()


func _allocate_copy_stem(source_stem: String) -> String:
	for n in range(2, 100000):
		var candidate := "%s%d" % [source_stem, n]
		var p := MAPS_DIR.path_join(candidate + ".json")
		if not FileAccess.file_exists(p):
			return candidate
	return "%s_%d" % [source_stem, Time.get_ticks_msec()]


func _copy_json_file(src: String, dest: String) -> Error:
	if not FileAccess.file_exists(src):
		return FAILED
	var f := FileAccess.open(src, FileAccess.READ)
	if f == null:
		return FAILED
	var txt := f.get_as_text()
	var out := FileAccess.open(dest, FileAccess.WRITE)
	if out == null:
		return FAILED
	out.store_string(txt)
	return OK


func _on_row_copy_pressed(displayer: DataDisplayer) -> void:
	var src: String = displayer.get_meta("map_json_path", "")
	var stem := _stem_from_path(src)
	var dest_stem := _allocate_copy_stem(stem)
	var dest := MAPS_DIR.path_join(dest_stem + ".json")
	if _copy_json_file(src, dest) != OK:
		push_error("HexMapList: copy failed")
		return
	_refresh_map_list()


func _on_row_delete_pressed(displayer: DataDisplayer) -> void:
	var path: String = displayer.get_meta("map_json_path", "")
	if path.is_empty():
		return
	var dp: DeletePanel = DeletePanelScene.instantiate()
	get_parent().add_child(dp)
	dp.layout_mode = 1
	dp.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dp.move_to_front()
	dp.setup(DeletePanel.FileKind.Map, path, displayer)
	dp.delete_finished.connect(_on_delete_panel_finished)


func _on_delete_panel_finished(_path: String, was_deleted: bool) -> void:
	if was_deleted:
		_refresh_map_list()


func _on_row_play_pressed(displayer: DataDisplayer) -> void:
	var path: String = displayer.get_meta("map_json_path", "")
	if path.is_empty():
		return
	RuntimeData.active_map_json_path = path
	var rm := RootMapScene.instantiate()
	var main_menu := get_parent()
	if main_menu == null:
		return
	var holder := main_menu.get_parent()
	if holder == null:
		return
	holder.add_child(rm)
	main_menu.queue_free()


func _allocate_new_map_stem() -> String:
	if not FileAccess.file_exists(MAPS_DIR.path_join("NewMap.json")):
		return "NewMap"
	for n in range(2, 100000):
		var s := "NewMap%d" % n
		if not FileAccess.file_exists(MAPS_DIR.path_join(s + ".json")):
			return s
	return "NewMap_%d" % Time.get_ticks_msec()


func _on_new_map_pressed() -> void:
	var stem := _allocate_new_map_stem()
	var path := MAPS_DIR.path_join(stem + ".json")
	var err := _write_json(path, _minimal_map_dict())
	if err != OK:
		push_error("HexMapList: cannot create new map json")
		return
	_refresh_map_list()


func _on_close_pressed() -> void:
	visible = false
	var main_panel := get_parent().get_node_or_null("MainPanel")
	if main_panel:
		main_panel.visible = true
