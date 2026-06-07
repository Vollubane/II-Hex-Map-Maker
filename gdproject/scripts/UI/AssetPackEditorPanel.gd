class_name AssetPackEditorPanel
extends Panel

## Emitted when the editor closes (×); parent lists may refresh.
signal editor_closed

const NATIVE_IMPORTER_CLASS_NAME := "Importer"

const AssetDisplayerScene: PackedScene = preload("res://Scene/UI Node/Elements/AssetDisplayer.tscn")
const EditLinePanelScene: PackedScene = preload("res://Scene/UI Node/Elements/EditLinePanel.tscn")
const GroupDisplayerScene: PackedScene = preload("res://Scene/UI Node/Elements/GroupDisplayer.tscn")
const DeleteGroupPanelScene: PackedScene = preload("res://Scene/UI Node/Elements/DeleteGroupPanel.tscn")

const KEY_NOM        := "nom"
const KEY_VERSION    := "version"
const KEY_ASSETS_DATA := "assets_data"
const KEY_GROUPS     := "groups"
const KEY_GROUP      := "group"

const _FORBIDDEN_FILENAME_CHARS: Array[String] = ["/", "\\", "\"", ":", "*", "?", "<", ">", "|"]
const _FORBIDDEN_GROUP_CHARS: Array[String]    = ["\\", "\"", ":", "*", "?", "<", ">", "|"]

const _WINDOWS_RESERVED_NAMES: Array[String] = [
	"CON", "PRN", "AUX", "NUL",
	"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
	"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
]

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
@onready var _group_count_label: Label = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/CountDataPanel/VBoxContainer/PanelGroupDetail/HBoxContainerGroupDetail/GroupNumber
)
@onready var _assets_count_label: Label = (
	$VBoxContainer/DetailPanel/HBoxContainerDetail/CountDataPanel/VBoxContainer/PanelAssetsDetail/HBoxContainerAssetsDetail/AssetsNumber
)

@onready var _import_button: Button = $VBoxContainer/AssetUtilitaryButtons/HBoxContainer/ImportButton
@onready var _delete_selected_button: Button = $VBoxContainer/AssetUtilitaryButtons/HBoxContainer/SupAssetsButton
@onready var _group_assets_button: Button = $VBoxContainer/AssetUtilitaryButtons/HBoxContainer/GroupAssetsButton
@onready var _show_group_list_button: Button = $VBoxContainer/AssetUtilitaryButtons/HBoxContainer/ShowGroupListButton
@onready var _repair_pack_button: Button = $VBoxContainer/AssetUtilitaryButtons/HBoxContainer/RepairPackButton

@onready var _assets_grid: GridContainer = $VBoxContainer/AssetContainer/ScrollContainer/AssetsGrid
@onready var _groups_panel: Panel = $AssetPackEditorGroupsPanel
@onready var _groups_vbox: VBoxContainer = $AssetPackEditorGroupsPanel/ScrollContainer/VBoxContainer

var _pack_dir: String = ""
var _group_colors: Dictionary = {}   ## group_name → Color, assigned once per session
var _group_color_counter: int = 0    ## monotonic index into the golden-ratio hue sequence

var _name_line_edit: LineEdit
var _version_line_edit: LineEdit
var _saved_name_for_edit: String = ""
var _saved_version_for_edit: String = ""
var _suppress_name_focus: bool = false
var _suppress_version_focus: bool = false

var _file_dialog: FileDialog
var _importer_busy: bool = false
var _pending_group_keys: Array[String] = []


func _ready() -> void:
	visible = false
	_import_button.tooltip_text = "Choisir un dossier : tous les fichiers .gltf qu'il contient (y compris sous-dossiers) sont ajoutés au pack."
	_close_button.pressed.connect(_on_close_pressed)
	_edit_name_button.pressed.connect(_begin_name_edit)
	_edit_version_button.pressed.connect(_begin_version_edit)
	_import_button.pressed.connect(_on_import_assets_pressed)
	_delete_selected_button.pressed.connect(_on_delete_selected_assets_pressed)
	_group_assets_button.pressed.connect(_on_group_assets_pressed)
	_show_group_list_button.pressed.connect(_on_show_group_list_pressed)
	_repair_pack_button.pressed.connect(_on_repair_pack_pressed)

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


func _ensure_import_file_dialog() -> void:
	if _file_dialog != null:
		return
	_file_dialog = FileDialog.new()
	_file_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_file_dialog.use_native_dialog = true
	## Dossier source : l'importeur enregistre tous les `.gltf` trouvés sous ce chemin (récursif).
	_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	_file_dialog.title = "Choisir le dossier d'assets (fichiers .gltf inclus)"
	add_child(_file_dialog)
	_file_dialog.dir_selected.connect(_on_import_folder_selected)


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
	_pack_dir = ManifestUtils.normalize_dir(pack_dir)
	_group_colors.clear()
	_group_color_counter = 0
	await _refresh_all_from_manifest()
	var list := get_parent().get_node_or_null("ListOfAssetPackPanel") as Control
	if list:
		list.visible = false
	visible = true


func _refresh_pack_header(dict: Dictionary) -> void:
	var folder := _pack_dir.get_file()
	var nom := str(dict.get(KEY_NOM, "")).strip_edges()
	if nom.is_empty():
		nom = folder
	_name_label.text = nom
	_version_label.text = str(dict.get(KEY_VERSION, ""))


func _update_stats(dict: Dictionary) -> void:
	var groups: Variant = dict.get(KEY_GROUPS, [])
	var group_count := 0
	if groups is Array:
		group_count = groups.size()
	_group_count_label.text = str(group_count)

	var assets: Variant = dict.get(KEY_ASSETS_DATA, {})
	var asset_count := 0
	if assets is Dictionary:
		for asset_key in assets.keys():
			if str(asset_key).to_lower().ends_with(".gltf"):
				asset_count += 1
	_assets_count_label.text = str(asset_count)


## Libellé UI : clé interne / fichier = `…/foo.gltf`, affichage = `foo` (sans extension).
func _asset_display_title(gltf_manifest_key: String) -> String:
	return gltf_manifest_key.get_file().get_basename()


func _manifest_gltf_keys_sorted(data: Dictionary) -> PackedStringArray:
	var assets: Variant = data.get(KEY_ASSETS_DATA, {})
	if typeof(assets) != TYPE_DICTIONARY:
		return PackedStringArray()
	var bucket: Array[String] = []
	for asset_key in assets.keys():
		var asset_key_str := str(asset_key)
		if asset_key_str.to_lower().ends_with(".gltf"):
			bucket.append(asset_key_str)
	bucket.sort()
	var result := PackedStringArray()
	for s in bucket:
		result.append(s)
	return result


func _refresh_all_from_manifest() -> void:
	if _pack_dir.is_empty():
		return
	var manifest_data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	_refresh_pack_header(manifest_data)
	_update_stats(manifest_data)
	await _rebuild_assets_grid(manifest_data)


func _clear_assets_grid() -> void:
	for c in _assets_grid.get_children():
		c.queue_free()


func _rebuild_assets_grid(data: Dictionary) -> void:
	_clear_assets_grid()
	var groups_variant: Variant = data.get(KEY_GROUPS, [])
	if groups_variant is Array:
		_init_group_colors(groups_variant as Array)
	await get_tree().process_frame
	var assets_data_variant: Variant = data.get(KEY_ASSETS_DATA, {})
	var assets_dict: Dictionary = assets_data_variant if typeof(assets_data_variant) == TYPE_DICTIONARY else {}
	for gltf_key in _manifest_gltf_keys_sorted(data):
		var disp: AssetDisplayer = AssetDisplayerScene.instantiate() as AssetDisplayer
		_assets_grid.add_child(disp)
		disp.set_meta("gltf_key", gltf_key)
		disp.set_preview_and_title(_load_capture_thumbnail(gltf_key), _asset_display_title(gltf_key))
		disp.title_submitted.connect(_on_asset_title_submitted.bind(disp))
		var row: Variant = assets_dict.get(gltf_key, {})
		if row is Dictionary:
			var group_name: String = (row as Dictionary).get(KEY_GROUP, "")
			if not group_name.is_empty():
				disp.set_group_color(_color_for_group(group_name))


func _apply_group_colors_to_grid() -> void:
	if _pack_dir.is_empty():
		return
	var data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	var assets_variant: Variant = data.get(KEY_ASSETS_DATA, {})
	if typeof(assets_variant) != TYPE_DICTIONARY:
		return
	var assets_dict: Dictionary = assets_variant
	for c in _assets_grid.get_children():
		var disp := c as AssetDisplayer
		if disp == null:
			continue
		var gltf_key: String = str(disp.get_meta("gltf_key", ""))
		var row: Variant = assets_dict.get(gltf_key, {})
		var group_name: String = ""
		if row is Dictionary:
			group_name = (row as Dictionary).get(KEY_GROUP, "")
		if not group_name.is_empty():
			disp.set_group_color(_color_for_group(group_name))
		else:
			disp.clear_group_color()


func _refresh_single_displayer(displayer: AssetDisplayer, gltf_key: String) -> void:
	displayer.set_meta("gltf_key", gltf_key)
	displayer.set_preview_and_title(_load_capture_thumbnail(gltf_key), _asset_display_title(gltf_key))


func _load_capture_thumbnail(gltf_filename: String) -> Texture2D:
	var png_rel := _pack_dir.path_join("capture").path_join(gltf_filename.get_basename() + ".png")
	if not FileAccess.file_exists(png_rel):
		return null
	var abs_path := ProjectSettings.globalize_path(png_rel)
	var img := Image.load_from_file(abs_path)
	if img == null:
		return null
	return ImageTexture.create_from_image(img)


func _set_action_buttons_busy(busy: bool) -> void:
	_import_button.disabled = busy
	_delete_selected_button.disabled = busy
	_group_assets_button.disabled = busy
	_show_group_list_button.disabled = busy
	_repair_pack_button.disabled = busy


func _on_import_assets_pressed() -> void:
	if _importer_busy or _pack_dir.is_empty():
		return
	_ensure_import_file_dialog()
	_file_dialog.popup()


func _on_import_folder_selected(absolute_folder: String) -> void:
	var source_path := absolute_folder.strip_edges()
	if source_path.is_empty():
		return
	_run_importer_call(func(importer: Node) -> void:
		var ok := importer.call("setupImportNewAssets", _pack_dir, source_path) as bool
		if not ok:
			push_warning("AssetPackEditorPanel: import depuis dossier impossible (voir console).")
			SystemEventBus.warning_event.emit("Import failed.", 4.0)
	)


func _on_delete_selected_assets_pressed() -> void:
	if _importer_busy or _pack_dir.is_empty():
		return
	var selected_keys := _selected_gltf_keys()
	if selected_keys.is_empty():
		push_warning("AssetPackEditorPanel: no assets selected to remove.")
		SystemEventBus.warning_event.emit("Select assets to remove first.", 3.5)
		return
	var keys_array: Array = []
	for key in selected_keys:
		keys_array.append(str(key))
	_run_importer_call(func(importer: Node) -> void:
		importer.call("setupRemoveAssetsFromPack", _pack_dir, keys_array)
	)


func _on_group_assets_pressed() -> void:
	if _importer_busy or _pack_dir.is_empty():
		return
	_pending_group_keys = _selected_gltf_keys()
	if _pending_group_keys.is_empty():
		push_warning("AssetPackEditorPanel: no assets selected to group.")
		SystemEventBus.warning_event.emit("Select assets to group first.", 3.5)
		return
	var panel := EditLinePanelScene.instantiate() as Control
	add_child(panel)
	panel.setup("   Assign group to selected assets   ", "group/name...")
	panel.text_submitted.connect(_on_group_name_submitted, CONNECT_ONE_SHOT)


func _on_group_name_submitted(p_group_name: String) -> void:
	var selected_keys := _pending_group_keys
	_pending_group_keys = []
	var group_name := p_group_name.strip_edges()
	if group_name.is_empty():
		push_warning("AssetPackEditorPanel: group name cannot be empty.")
		SystemEventBus.warning_event.emit("Group name cannot be empty.", 3.5)
		return
	for forbidden_char in _FORBIDDEN_GROUP_CHARS:
		if group_name.contains(forbidden_char):
			push_warning("AssetPackEditorPanel: illegal characters in group name.")
			SystemEventBus.warning_event.emit("Illegal characters in group name.", 3.5)
			return
	var data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	var groups_variant: Variant = data.get(KEY_GROUPS, [])
	var groups: Array = groups_variant if groups_variant is Array else []
	if groups.has(group_name):
		push_warning("AssetPackEditorPanel: group name already exists.")
		SystemEventBus.warning_event.emit("Group already exists.", 3.5)
		return
	var assets_variant: Variant = data.get(KEY_ASSETS_DATA, {})
	if typeof(assets_variant) != TYPE_DICTIONARY:
		push_warning("AssetPackEditorPanel: invalid manifest (assets_data).")
		return
	var assets_dict: Dictionary = assets_variant
	for asset_key in selected_keys:
		if not assets_dict.has(asset_key):
			continue
		var row: Variant = assets_dict[asset_key]
		if row is Dictionary:
			(row as Dictionary)[KEY_GROUP] = group_name
	groups.append(group_name)
	data[KEY_GROUPS] = groups
	data[KEY_ASSETS_DATA] = assets_dict
	if ManifestUtils.write_manifest(_pack_dir, data) != OK:
		push_error("AssetPackEditorPanel: manifest write failed (group assign).")
		SystemEventBus.warning_event.emit("Failed to save group.", 4.0)
		return
	await _refresh_all_from_manifest()
	if _groups_panel.visible:
		_rebuild_groups_list()


func _on_show_group_list_pressed() -> void:
	if _pack_dir.is_empty():
		return
	_groups_panel.visible = not _groups_panel.visible
	if _groups_panel.visible:
		_show_group_list_button.text = "Hide Group\nList"
		_rebuild_groups_list()
	else:
		_show_group_list_button.text = "Show Group\nList"
		_clear_groups_list()


func _clear_groups_list() -> void:
	for c in _groups_vbox.get_children():
		c.queue_free()


func _color_for_group(p_name: String) -> Color:
	if not _group_colors.has(p_name):
		var hue := fmod(_group_color_counter * 0.618033988749895, 1.0)
		_group_color_counter += 1
		_group_colors[p_name] = Color.from_hsv(hue, 0.75, 0.95)
	return _group_colors[p_name]


func _init_group_colors(p_groups: Array) -> void:
	for group_name in p_groups:
		_color_for_group(str(group_name))


func _rebuild_groups_list() -> void:
	_clear_groups_list()
	var data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	var groups_variant: Variant = data.get(KEY_GROUPS, [])
	if not groups_variant is Array:
		return
	for group_name in (groups_variant as Array):
		var disp := GroupDisplayerScene.instantiate() as GroupDisplayer
		_groups_vbox.add_child(disp)
		disp.set_group_name(str(group_name))
		disp.set_group_color(_color_for_group(str(group_name)))
		disp.rename_submitted.connect(_on_group_rename_submitted.bind(disp))
		disp.delete_pressed.connect(_on_group_delete_requested)


func _on_group_rename_submitted(p_old_name: String, p_new_name: String, p_disp: GroupDisplayer) -> void:
	if p_new_name.is_empty() or p_new_name == p_old_name:
		return
	var data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	var groups_variant: Variant = data.get(KEY_GROUPS, [])
	var groups: Array = groups_variant if groups_variant is Array else []
	if groups.has(p_new_name):
		push_warning("AssetPackEditorPanel: group name already exists.")
		SystemEventBus.warning_event.emit("Group already exists.", 3.5)
		p_disp.set_group_name(p_old_name)
		return
	var old_idx := groups.find(p_old_name)
	if old_idx >= 0:
		groups[old_idx] = p_new_name
	var assets_variant: Variant = data.get(KEY_ASSETS_DATA, {})
	if typeof(assets_variant) == TYPE_DICTIONARY:
		var assets_dict: Dictionary = assets_variant
		for asset_key in assets_dict.keys():
			var row: Variant = assets_dict[asset_key]
			if row is Dictionary and (row as Dictionary).get(KEY_GROUP, "") == p_old_name:
				(row as Dictionary)[KEY_GROUP] = p_new_name
		data[KEY_ASSETS_DATA] = assets_dict
	data[KEY_GROUPS] = groups
	if ManifestUtils.write_manifest(_pack_dir, data) != OK:
		push_error("AssetPackEditorPanel: manifest write failed (group rename).")
		SystemEventBus.warning_event.emit("Failed to save group rename.", 4.0)
		p_disp.set_group_name(p_old_name)
		return
	if _group_colors.has(p_old_name):
		_group_colors[p_new_name] = _group_colors[p_old_name]
		_group_colors.erase(p_old_name)
	p_disp.set_group_color(_color_for_group(p_new_name))
	_rebuild_groups_list()
	_apply_group_colors_to_grid()


func _on_group_delete_requested(p_group_name: String) -> void:
	var panel := DeleteGroupPanelScene.instantiate() as DeleteGroupPanel
	add_child(panel)
	panel.setup(p_group_name)
	panel.deletion_confirmed.connect(
		func(delete_assets: bool): _on_group_deletion_confirmed(p_group_name, delete_assets),
		CONNECT_ONE_SHOT
	)


func _on_group_deletion_confirmed(p_group_name: String, p_delete_assets: bool) -> void:
	var data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	var groups_variant: Variant = data.get(KEY_GROUPS, [])
	var groups: Array = groups_variant if groups_variant is Array else []
	groups.erase(p_group_name)
	data[KEY_GROUPS] = groups

	if p_delete_assets:
		var assets_variant: Variant = data.get(KEY_ASSETS_DATA, {})
		var gltf_keys_to_delete: Array[String] = []
		if typeof(assets_variant) == TYPE_DICTIONARY:
			for asset_key in (assets_variant as Dictionary).keys():
				var row: Variant = (assets_variant as Dictionary)[asset_key]
				if row is Dictionary and (row as Dictionary).get(KEY_GROUP, "") == p_group_name:
					gltf_keys_to_delete.append(str(asset_key))
		if ManifestUtils.write_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE), data) != OK:
			push_error("AssetPackEditorPanel: manifest write failed (group delete with assets).")
			SystemEventBus.warning_event.emit("Failed to save group deletion.", 4.0)
			return
		_rebuild_groups_list()
		if gltf_keys_to_delete.is_empty():
			await _refresh_all_from_manifest()
		else:
			_run_importer_call(func(importer: Node) -> void:
				importer.call("setupRemoveAssetsFromPack", _pack_dir, gltf_keys_to_delete)
			)
	else:
		var assets_variant: Variant = data.get(KEY_ASSETS_DATA, {})
		if typeof(assets_variant) == TYPE_DICTIONARY:
			for asset_key in (assets_variant as Dictionary).keys():
				var row: Variant = (assets_variant as Dictionary)[asset_key]
				if row is Dictionary and (row as Dictionary).get(KEY_GROUP, "") == p_group_name:
					(row as Dictionary).erase(KEY_GROUP)
			data[KEY_ASSETS_DATA] = assets_variant
		if ManifestUtils.write_manifest(_pack_dir, data) != OK:
			push_error("AssetPackEditorPanel: manifest write failed (group delete).")
			SystemEventBus.warning_event.emit("Failed to save group deletion.", 4.0)
			return
		_rebuild_groups_list()
		await _refresh_all_from_manifest()


func _on_repair_pack_pressed() -> void:
	if _importer_busy or _pack_dir.is_empty():
		return
	_run_importer_call(func(importer: Node) -> void:
		var ok := importer.call("setupRepareAssetsPack", _pack_dir) as bool
		if not ok:
			push_warning("AssetPackEditorPanel: repair setup failed.")
			SystemEventBus.warning_event.emit("Repair setup failed.", 4.0)
	)


func _run_importer_call(setup_cb: Callable) -> void:
	if _importer_busy:
		return
	if not ClassDB.class_exists(NATIVE_IMPORTER_CLASS_NAME):
		push_error(
			"AssetPackEditorPanel: native `%s` missing. Load the GDExtension (cpp.dll)." % NATIVE_IMPORTER_CLASS_NAME
		)
		SystemEventBus.warning_event.emit("Extension not loaded — import unavailable.", 5.0)
		return
	var importer := ClassDB.instantiate(NATIVE_IMPORTER_CLASS_NAME) as Node
	if importer == null:
		push_error("AssetPackEditorPanel: could not instantiate native Importer.")
		SystemEventBus.warning_event.emit("Import unavailable.", 5.0)
		return
	_importer_busy = true
	_set_action_buttons_busy(true)
	add_child(importer)
	importer.tree_exited.connect(_on_importer_finished, CONNECT_ONE_SHOT)
	setup_cb.call(importer)


func _on_importer_finished() -> void:
	_importer_busy = false
	_set_action_buttons_busy(false)
	SystemEventBus.loading_end_event.emit()
	call_deferred("_refresh_all_from_manifest_deferred")


func _refresh_all_from_manifest_deferred() -> void:
	await _refresh_all_from_manifest()


func _selected_gltf_keys() -> Array[String]:
	var result: Array[String] = []
	for c in _assets_grid.get_children():
		if c is AssetDisplayer:
			var disp := c as AssetDisplayer
			if disp.is_button_pressed():
				var key := str(disp.get_meta("gltf_key", "")).strip_edges()
				if not key.is_empty():
					result.append(key)
	return result


func _normalize_gltf_filename(s: String) -> String:
	var trimmed := s.strip_edges().replace("\\", "/")
	var base := trimmed.get_file()
	if base.is_empty():
		return ""
	return base.get_basename() + ".gltf"


func _is_windows_plain_filename_ok(name_part: String) -> bool:
	if name_part.strip_edges() != name_part:
		return false
	if name_part.ends_with(" ") or name_part.ends_with("."):
		return false
	for i in range(name_part.length()):
		if name_part.unicode_at(i) <= 31:
			return false
	for forbidden_char in _FORBIDDEN_FILENAME_CHARS:
		if name_part.contains(forbidden_char):
			return false
	return true


func _is_windows_reserved_stem(gltf_key: String) -> bool:
	var stem := gltf_key.get_basename().strip_edges().to_upper()
	if stem.is_empty():
		return true
	return stem in _WINDOWS_RESERVED_NAMES


func _title_contains_forbidden_chars(s: String) -> bool:
	for forbidden_char in _FORBIDDEN_FILENAME_CHARS:
		if s.contains(forbidden_char):
			return true
	return false


func _gltf_name_conflicts(candidate: String, old_key_in_manifest: String) -> bool:
	var data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	var assets_variant: Variant = data.get(KEY_ASSETS_DATA, {})
	if typeof(assets_variant) != TYPE_DICTIONARY:
		return false
	var assets_dict: Dictionary = assets_variant
	for asset_key in assets_dict.keys():
		var asset_key_str := str(asset_key)
		if not asset_key_str.to_lower().ends_with(".gltf"):
			continue
		if asset_key_str == old_key_in_manifest:
			continue
		if asset_key_str.to_lower() == candidate.to_lower():
			return true
	return false


func _rename_asset_files(old_key: String, new_key: String) -> Error:
	var dir_access := DirAccess.open(_pack_dir)
	if dir_access == null:
		return ERR_CANT_OPEN
	if old_key.to_lower() == new_key.to_lower():
		if not dir_access.file_exists(old_key):
			return OK
		if old_key != new_key and dir_access.file_exists(new_key):
			return ERR_ALREADY_EXISTS
		if old_key != new_key:
			return dir_access.rename(old_key, new_key)
		return OK
	if not dir_access.file_exists(old_key):
		return ERR_DOES_NOT_EXIST
	if dir_access.file_exists(new_key):
		return ERR_ALREADY_EXISTS
	var rename_err := dir_access.rename(old_key, new_key)
	if rename_err != OK:
		return rename_err
	var capture_dir := DirAccess.open(_pack_dir.path_join("capture"))
	if capture_dir:
		var old_png := old_key.get_basename() + ".png"
		var new_png := new_key.get_basename() + ".png"
		if capture_dir.file_exists(old_png) and not capture_dir.file_exists(new_png):
			capture_dir.rename(old_png, new_png)
	return OK


func _on_asset_title_submitted(new_text: String, displayer: AssetDisplayer) -> void:
	var old_key := str(displayer.get_meta("gltf_key", "")).strip_edges()
	if old_key.is_empty():
		return
	var new_key := _normalize_gltf_filename(new_text)
	if new_key.is_empty():
		push_warning("AssetPackEditorPanel: asset name cannot be empty.")
		SystemEventBus.warning_event.emit("Asset name cannot be empty.", 3.5)
		_refresh_single_displayer(displayer, old_key)
		return
	var base_only := new_key.get_basename()
	if not _is_windows_plain_filename_ok(base_only):
		push_warning("AssetPackEditorPanel: illegal characters or trailing separator in asset name.")
		SystemEventBus.warning_event.emit("Illegal characters in asset name.", 3.5)
		_refresh_single_displayer(displayer, old_key)
		return
	if _title_contains_forbidden_chars(base_only) or _is_windows_reserved_stem(new_key):
		push_warning("AssetPackEditorPanel: illegal file name for Windows.")
		SystemEventBus.warning_event.emit("Illegal file name.", 3.5)
		_refresh_single_displayer(displayer, old_key)
		return
	if new_key == old_key:
		return
	if _gltf_name_conflicts(new_key, old_key):
		push_warning("AssetPackEditorPanel: asset name already in use.")
		SystemEventBus.warning_event.emit("Asset name already in use.", 3.5)
		_refresh_single_displayer(displayer, old_key)
		return

	var data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	var assets_variant: Variant = data.get(KEY_ASSETS_DATA, {})
	if typeof(assets_variant) != TYPE_DICTIONARY:
		push_warning("AssetPackEditorPanel: invalid manifest (assets_data).")
		_refresh_single_displayer(displayer, old_key)
		return
	var assets_dict: Dictionary = assets_variant
	if not assets_dict.has(old_key):
		push_warning("AssetPackEditorPanel: asset key missing from manifest.")
		_refresh_single_displayer(displayer, old_key)
		return

	if old_key.to_lower() != new_key.to_lower():
		var file_rename_err := _rename_asset_files(old_key, new_key)
		if file_rename_err != OK:
			push_error("AssetPackEditorPanel: failed to rename files on disk (%s)." % file_rename_err)
			SystemEventBus.warning_event.emit("Asset rename failed.", 4.0)
			_refresh_single_displayer(displayer, old_key)
			return

	var row_variant: Variant = assets_dict[old_key]
	assets_dict.erase(old_key)
	assets_dict[new_key] = row_variant
	data[KEY_ASSETS_DATA] = assets_dict
	if ManifestUtils.write_manifest(_pack_dir, data) != OK:
		push_error("AssetPackEditorPanel: manifest write failed after rename.")
	displayer.set_meta("gltf_key", new_key)
	await _refresh_all_from_manifest()


func _is_nom_taken_by_other_pack(exclude_dir: String, nom: String) -> bool:
	return ManifestUtils.is_pack_nom_taken(exclude_dir, nom)


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
	if _title_contains_forbidden_chars(new_text):
		push_warning("AssetPackEditorPanel: illegal characters in pack name.")
		SystemEventBus.warning_event.emit("Illegal characters in pack name.", 3.5)
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return
	var nom := new_text.strip_edges()
	var stem := ManifestUtils.sanitize_stem(new_text)
	var folder_base := _pack_dir.get_file()
	var data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))

	if stem.is_empty() or nom.is_empty():
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	if ManifestUtils.is_pack_nom_taken(_pack_dir, nom):
		push_warning("AssetPackEditorPanel: pack display name already in use.")
		SystemEventBus.warning_event.emit("Pack name already in use.", 3.5)
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	var dir_access := DirAccess.open(ManifestUtils.ASSET_PACKS_DIR)
	if dir_access == null:
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	if stem != folder_base and dir_access.dir_exists(stem):
		push_warning("AssetPackEditorPanel: folder name already exists.")
		SystemEventBus.warning_event.emit("A folder with this name already exists.", 3.5)
		_suppress_name_focus = true
		_cancel_name_edit()
		call_deferred("_clear_name_suppress")
		return

	if stem != folder_base:
		var rename_err := dir_access.rename(folder_base, stem)
		if rename_err != OK:
			push_error("AssetPackEditorPanel: folder rename failed (%s)." % rename_err)
			SystemEventBus.warning_event.emit("Pack rename failed.", 4.0)
			_suppress_name_focus = true
			_cancel_name_edit()
			call_deferred("_clear_name_suppress")
			return
		_pack_dir = ManifestUtils.ASSET_PACKS_DIR.path_join(stem)

	data = ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	data[KEY_NOM] = nom
	if ManifestUtils.write_manifest(_pack_dir, data) != OK:
		push_error("AssetPackEditorPanel: manifest write failed.")
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
	var data := ManifestUtils.read_dict(_pack_dir.path_join(ManifestUtils.MANIFEST_FILE))
	data[KEY_VERSION] = new_text.strip_edges()
	if ManifestUtils.write_manifest(_pack_dir, data) != OK:
		push_error("AssetPackEditorPanel: manifest write failed (version).")
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
	_groups_panel.visible = false
	_show_group_list_button.text = "Show Group\nList"
	_clear_groups_list()
	visible = false
	var list := get_parent().get_node_or_null("ListOfAssetPackPanel") as Control
	if list:
		list.visible = true
	editor_closed.emit()
