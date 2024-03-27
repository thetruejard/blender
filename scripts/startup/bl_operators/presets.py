# SPDX-FileCopyrightText: 2009-2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

import bpy
from bpy.types import (
    Menu,
    Operator,
    OperatorFileListElement,
    WindowManager,
)
from bpy.props import (
    BoolProperty,
    CollectionProperty,
    StringProperty,
)
from bpy.app.translations import (
    pgettext_rpt as rpt_,
    pgettext_data as data_,
)


# For preset popover menu
WindowManager.preset_name = StringProperty(
    name="Preset Name",
    description="Name for new preset",
    default=data_("New Preset"),
)


class AddPresetBase:
    """Base preset class, only for subclassing
    subclasses must define
     - preset_values
     - preset_subdir """
    # bl_idname = "script.preset_base_add"
    # bl_label = "Add a Python Preset"

    # only because invoke_props_popup requires. Also do not add to search menu.
    bl_options = {'REGISTER', 'INTERNAL'}

    name: StringProperty(
        name="Name",
        description="Name of the preset, used to make the path name",
        maxlen=64,
        options={'SKIP_SAVE'},
    )
    remove_name: BoolProperty(
        default=False,
        options={'HIDDEN', 'SKIP_SAVE'},
    )
    remove_active: BoolProperty(
        default=False,
        options={'HIDDEN', 'SKIP_SAVE'},
    )

    @staticmethod
    def as_filename(name):  # could reuse for other presets

        # lazy init maketrans
        def maketrans_init():
            cls = AddPresetBase
            attr = "_as_filename_trans"

            trans = getattr(cls, attr, None)
            if trans is None:
                trans = str.maketrans({char: "_" for char in " !@#$%^&*(){}:\";'[]<>,.\\/?"})
                setattr(cls, attr, trans)
            return trans

        name = name.strip()
        name = bpy.path.display_name_to_filepath(name)
        trans = maketrans_init()
        # Strip surrounding "_" as they are displayed as spaces.
        return name.translate(trans).strip("_")

    def execute(self, context):
        import os
        from bpy.utils import is_path_builtin

        if hasattr(self, "pre_cb"):
            self.pre_cb(context)

        preset_menu_class = getattr(bpy.types, self.preset_menu)

        is_xml = getattr(preset_menu_class, "preset_type", None) == 'XML'
        is_preset_add = not (self.remove_name or self.remove_active)

        if is_xml:
            ext = ".xml"
        else:
            ext = ".py"

        name = self.name.strip() if is_preset_add else self.name

        if is_preset_add:
            if not name:
                return {'FINISHED'}

            # Reset preset name
            wm = bpy.data.window_managers[0]
            if name == wm.preset_name:
                wm.preset_name = data_("New Preset")

            filename = self.as_filename(name)

            target_path = os.path.join("presets", self.preset_subdir)
            target_path = bpy.utils.user_resource('SCRIPTS', path=target_path, create=True)

            if not target_path:
                self.report({'WARNING'}, "Failed to create presets path")
                return {'CANCELLED'}

            filepath = os.path.join(target_path, filename) + ext

            if hasattr(self, "add"):
                self.add(context, filepath)
            else:
                print("Writing Preset: %r" % filepath)

                if is_xml:
                    import rna_xml
                    rna_xml.xml_file_write(context, filepath, preset_menu_class.preset_xml_map)
                else:

                    def rna_recursive_attr_expand(value, rna_path_step, level):
                        if isinstance(value, bpy.types.PropertyGroup):
                            for sub_value_attr in value.bl_rna.properties.keys():
                                if sub_value_attr == "rna_type":
                                    continue
                                sub_value = getattr(value, sub_value_attr)
                                rna_recursive_attr_expand(sub_value, "%s.%s" % (rna_path_step, sub_value_attr), level)
                        elif type(value).__name__ == "bpy_prop_collection_idprop":  # could use nicer method
                            file_preset.write("%s.clear()\n" % rna_path_step)
                            for sub_value in value:
                                file_preset.write("item_sub_%d = %s.add()\n" % (level, rna_path_step))
                                rna_recursive_attr_expand(sub_value, "item_sub_%d" % level, level + 1)
                        else:
                            # convert thin wrapped sequences
                            # to simple lists to repr()
                            try:
                                value = value[:]
                            except BaseException:
                                pass

                            file_preset.write("%s = %r\n" % (rna_path_step, value))

                    file_preset = open(filepath, 'w', encoding="utf-8")
                    file_preset.write("import bpy\n")

                    if hasattr(self, "preset_defines"):
                        for rna_path in self.preset_defines:
                            exec(rna_path)
                            file_preset.write("%s\n" % rna_path)
                        file_preset.write("\n")

                    for rna_path in self.preset_values:
                        value = eval(rna_path)
                        rna_recursive_attr_expand(value, rna_path, 1)

                    file_preset.close()

            preset_menu_class.bl_label = bpy.path.display_name(filename)

        else:
            if self.remove_active:
                name = preset_menu_class.bl_label

            # fairly sloppy but convenient.
            filepath = bpy.utils.preset_find(name, self.preset_subdir, ext=ext)

            if not filepath:
                filepath = bpy.utils.preset_find(name, self.preset_subdir, display_name=True, ext=ext)

            if not filepath:
                return {'CANCELLED'}

            # Do not remove bundled presets
            if is_path_builtin(filepath):
                self.report({'WARNING'}, "Unable to remove default presets")
                return {'CANCELLED'}

            try:
                if hasattr(self, "remove"):
                    self.remove(context, filepath)
                else:
                    os.remove(filepath)
            except BaseException as ex:
                self.report({'ERROR'}, rpt_("Unable to remove preset: %r") % ex)
                import traceback
                traceback.print_exc()
                return {'CANCELLED'}

            # XXX, stupid!
            preset_menu_class.bl_label = "Presets"

        if hasattr(self, "post_cb"):
            self.post_cb(context)

        return {'FINISHED'}

    def check(self, _context):
        self.name = self.as_filename(self.name.strip())

    def invoke(self, context, _event):
        if not (self.remove_active or self.remove_name):
            wm = context.window_manager
            return wm.invoke_props_dialog(self)
        else:
            return self.execute(context)


class ExecutePreset(Operator):
    """Load a preset"""
    bl_idname = "script.execute_preset"
    bl_label = "Execute a Python Preset"

    filepath: StringProperty(
        subtype='FILE_PATH',
        options={'SKIP_SAVE'},
    )
    menu_idname: StringProperty(
        name="Menu ID Name",
        description="ID name of the menu this was called from",
        options={'SKIP_SAVE'},
    )

    def execute(self, context):
        from os.path import basename, splitext
        filepath = self.filepath

        # change the menu title to the most recently chosen option
        preset_class = getattr(bpy.types, self.menu_idname)
        preset_class.bl_label = bpy.path.display_name(basename(filepath), title_case=False)

        ext = splitext(filepath)[1].lower()

        if ext not in {".py", ".xml"}:
            self.report({'ERROR'}, rpt_("Unknown file type: %r") % ext)
            return {'CANCELLED'}

        if hasattr(preset_class, "reset_cb"):
            preset_class.reset_cb(context)

        if ext == ".py":
            try:
                bpy.utils.execfile(filepath)
            except BaseException as ex:
                self.report({'ERROR'}, "Failed to execute the preset: " + repr(ex))

        elif ext == ".xml":
            import rna_xml
            rna_xml.xml_file_run(context, filepath, preset_class.preset_xml_map)

        if hasattr(preset_class, "post_cb"):
            preset_class.post_cb(context)

        return {'FINISHED'}


class AddPresetRender(AddPresetBase, Operator):
    """Add or remove a Render Preset"""
    bl_idname = "render.preset_add"
    bl_label = "Add Render Preset"
    preset_menu = "RENDER_PT_format_presets"

    preset_defines = [
        "scene = bpy.context.scene"
    ]

    preset_values = [
        "scene.render.fps",
        "scene.render.fps_base",
        "scene.render.pixel_aspect_x",
        "scene.render.pixel_aspect_y",
        "scene.render.resolution_percentage",
        "scene.render.resolution_x",
        "scene.render.resolution_y",
    ]

    preset_subdir = "render"


class AddPresetCamera(AddPresetBase, Operator):
    """Add or remove a Camera Preset"""
    bl_idname = "camera.preset_add"
    bl_label = "Add Camera Preset"
    preset_menu = "CAMERA_PT_presets"

    preset_defines = [
        "cam = bpy.context.camera"
    ]

    preset_subdir = "camera"

    use_focal_length: BoolProperty(
        name="Include Focal Length",
        description="Include focal length into the preset",
        options={'SKIP_SAVE'},
    )

    @property
    def preset_values(self):
        preset_values = [
            "cam.sensor_width",
            "cam.sensor_height",
            "cam.sensor_fit",
        ]
        if self.use_focal_length:
            preset_values.append("cam.lens")
            preset_values.append("cam.lens_unit")
        return preset_values


class AddPresetCameraSafeAreas(AddPresetBase, Operator):
    """Add or remove a Safe Areas Preset"""
    bl_idname = "camera.safe_areas_preset_add"
    bl_label = "Add Safe Area Preset"
    preset_menu = "CAMERA_PT_safe_areas_presets"

    preset_defines = [
        "safe_areas = bpy.context.scene.safe_areas"
    ]

    preset_values = [
        "safe_areas.title",
        "safe_areas.action",
        "safe_areas.title_center",
        "safe_areas.action_center",
    ]

    preset_subdir = "safe_areas"


class AddPresetCloth(AddPresetBase, Operator):
    """Add or remove a Cloth Preset"""
    bl_idname = "cloth.preset_add"
    bl_label = "Add Cloth Preset"
    preset_menu = "CLOTH_PT_presets"

    preset_defines = [
        "cloth = bpy.context.cloth"
    ]

    preset_values = [
        "cloth.settings.quality",
        "cloth.settings.mass",
        "cloth.settings.air_damping",
        "cloth.settings.bending_model",
        "cloth.settings.tension_stiffness",
        "cloth.settings.compression_stiffness",
        "cloth.settings.shear_stiffness",
        "cloth.settings.bending_stiffness",
        "cloth.settings.tension_damping",
        "cloth.settings.compression_damping",
        "cloth.settings.shear_damping",
        "cloth.settings.bending_damping",
    ]

    preset_subdir = "cloth"


class AddPresetFluid(AddPresetBase, Operator):
    """Add or remove a Fluid Preset"""
    bl_idname = "fluid.preset_add"
    bl_label = "Add Fluid Preset"
    preset_menu = "FLUID_PT_presets"

    preset_defines = [
        "fluid = bpy.context.fluid"
    ]

    preset_values = [
        "fluid.domain_settings.viscosity_base",
        "fluid.domain_settings.viscosity_exponent",
    ]

    preset_subdir = "fluid"


class AddPresetHairDynamics(AddPresetBase, Operator):
    """Add or remove a Hair Dynamics Preset"""
    bl_idname = "particle.hair_dynamics_preset_add"
    bl_label = "Add Hair Dynamics Preset"
    preset_menu = "PARTICLE_PT_hair_dynamics_presets"

    preset_defines = [
        "psys = bpy.context.particle_system",
        "cloth = bpy.context.particle_system.cloth",
        "settings = bpy.context.particle_system.cloth.settings",
        "collision = bpy.context.particle_system.cloth.collision_settings",
    ]

    preset_subdir = "hair_dynamics"

    preset_values = [
        "settings.quality",
        "settings.mass",
        "settings.bending_stiffness",
        "psys.settings.bending_random",
        "settings.bending_damping",
        "settings.air_damping",
        "settings.internal_friction",
        "settings.density_target",
        "settings.density_strength",
        "settings.voxel_cell_size",
        "settings.pin_stiffness",
    ]


class AddPresetTextEditor(AddPresetBase, Operator):
    """Add or remove a Text Editor Preset"""
    bl_idname = "text_editor.preset_add"
    bl_label = "Add Text Editor Preset"
    preset_menu = "USERPREF_PT_text_editor_presets"

    preset_defines = [
        "filepaths = bpy.context.preferences.filepaths"
    ]

    preset_values = [
        "filepaths.text_editor",
        "filepaths.text_editor_args"
    ]

    preset_subdir = "text_editor"


class AddPresetTrackingCamera(AddPresetBase, Operator):
    """Add or remove a Tracking Camera Intrinsics Preset"""
    bl_idname = "clip.camera_preset_add"
    bl_label = "Add Camera Preset"
    preset_menu = "CLIP_PT_camera_presets"

    preset_defines = [
        "camera = bpy.context.edit_movieclip.tracking.camera"
    ]

    preset_subdir = "tracking_camera"

    use_focal_length: BoolProperty(
        name="Include Focal Length",
        description="Include focal length into the preset",
        options={'SKIP_SAVE'},
        default=True,
    )

    @property
    def preset_values(self):
        preset_values = [
            "camera.sensor_width",
            "camera.pixel_aspect",
            "camera.k1",
            "camera.k2",
            "camera.k3",
        ]
        if self.use_focal_length:
            preset_values.append("camera.units")
            preset_values.append("camera.focal_length")
        return preset_values


class AddPresetTrackingTrackColor(AddPresetBase, Operator):
    """Add or remove a Clip Track Color Preset"""
    bl_idname = "clip.track_color_preset_add"
    bl_label = "Add Track Color Preset"
    preset_menu = "CLIP_PT_track_color_presets"

    preset_defines = [
        "track = bpy.context.edit_movieclip.tracking.tracks.active"
    ]

    preset_values = [
        "track.color",
        "track.use_custom_color",
    ]

    preset_subdir = "tracking_track_color"


class AddPresetTrackingSettings(AddPresetBase, Operator):
    """Add or remove a motion tracking settings preset"""
    bl_idname = "clip.tracking_settings_preset_add"
    bl_label = "Add Tracking Settings Preset"
    preset_menu = "CLIP_PT_tracking_settings_presets"

    preset_defines = [
        "settings = bpy.context.edit_movieclip.tracking.settings"
    ]

    preset_values = [
        "settings.default_correlation_min",
        "settings.default_pattern_size",
        "settings.default_search_size",
        "settings.default_frames_limit",
        "settings.default_pattern_match",
        "settings.default_margin",
        "settings.default_motion_model",
        "settings.use_default_brute",
        "settings.use_default_normalization",
        "settings.use_default_mask",
        "settings.use_default_red_channel",
        "settings.use_default_green_channel",
        "settings.use_default_blue_channel",
        "settings.default_weight",
    ]

    preset_subdir = "tracking_settings"


class AddPresetEEVEERaytracing(AddPresetBase, Operator):
    """Add or remove an EEVEE ray-tracing preset"""
    bl_idname = "render.eevee_raytracing_preset_add"
    bl_label = "Add Raytracing Preset"
    preset_menu = "RENDER_PT_eevee_next_raytracing_presets"

    preset_defines = [
        "eevee = bpy.context.scene.eevee",
        "options = eevee.ray_tracing_options"
    ]

    preset_values = [
        "eevee.ray_tracing_method",
        "options.resolution_scale",
        "options.sample_clamp",
        "options.screen_trace_max_roughness",
        "options.screen_trace_quality",
        "options.screen_trace_thickness",
        "options.use_denoise",
        "options.denoise_spatial",
        "options.denoise_temporal",
        "options.denoise_bilateral",
    ]

    preset_subdir = "eevee/raytracing"


class AddPresetNodeColor(AddPresetBase, Operator):
    """Add or remove a Node Color Preset"""
    bl_idname = "node.node_color_preset_add"
    bl_label = "Add Node Color Preset"
    preset_menu = "NODE_PT_node_color_presets"

    preset_defines = [
        "node = bpy.context.active_node"
    ]

    preset_values = [
        "node.color",
        "node.use_custom_color",
    ]

    preset_subdir = "node_color"


class AddPresetInterfaceTheme(AddPresetBase, Operator):
    """Add a custom theme to the preset list"""
    bl_idname = "wm.interface_theme_preset_add"
    bl_label = "Add Theme"
    preset_menu = "USERPREF_MT_interface_theme_presets"
    preset_subdir = "interface_theme"


class RemovePresetInterfaceTheme(AddPresetBase, Operator):
    """Remove a custom theme from the preset list"""
    bl_idname = "wm.interface_theme_preset_remove"
    bl_label = "Remove Theme"
    preset_menu = "USERPREF_MT_interface_theme_presets"
    preset_subdir = "interface_theme"

    remove_active: BoolProperty(
        default=True,
        options={'HIDDEN', 'SKIP_SAVE'},
    )

    @classmethod
    def poll(cls, context):
        from bpy.utils import is_path_builtin
        preset_menu_class = getattr(bpy.types, cls.preset_menu)
        name = preset_menu_class.bl_label
        filepath = bpy.utils.preset_find(name, cls.preset_subdir, ext=".xml")
        if not bool(filepath) or is_path_builtin(filepath):
            cls.poll_message_set("Built-in themes cannot be removed")
            return False
        return True

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(self, event, title="Remove Custom Theme", confirm_text="Delete")


class SavePresetInterfaceTheme(AddPresetBase, Operator):
    """Save a custom theme in the preset list"""
    bl_idname = "wm.interface_theme_preset_save"
    bl_label = "Save Theme"
    preset_menu = "USERPREF_MT_interface_theme_presets"
    preset_subdir = "interface_theme"

    remove_active: BoolProperty(
        default=True,
        options={'HIDDEN', 'SKIP_SAVE'},
    )

    @classmethod
    def poll(cls, context):
        from bpy.utils import is_path_builtin

        preset_menu_class = getattr(bpy.types, cls.preset_menu)
        name = preset_menu_class.bl_label
        filepath = bpy.utils.preset_find(name, cls.preset_subdir, ext=".xml")
        if (not filepath) or is_path_builtin(filepath):
            cls.poll_message_set("Built-in themes cannot be overwritten")
            return False
        return True

    def execute(self, context):
        from bpy.utils import is_path_builtin
        import rna_xml
        preset_menu_class = getattr(bpy.types, self.preset_menu)
        name = preset_menu_class.bl_label
        filepath = bpy.utils.preset_find(name, self.preset_subdir, ext=".xml")
        if not bool(filepath) or is_path_builtin(filepath):
            self.report({'ERROR'}, "Built-in themes cannot be overwritten")
            return {'CANCELLED'}

        try:
            rna_xml.xml_file_write(context, filepath, preset_menu_class.preset_xml_map)
        except BaseException as ex:
            self.report({'ERROR'}, "Unable to overwrite preset: %s" % str(ex))
            import traceback
            traceback.print_exc()
            return {'CANCELLED'}

        return {'FINISHED'}

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(self, event, title="Overwrite Custom Theme?", confirm_text="Save")


class AddPresetKeyconfig(AddPresetBase, Operator):
    """Add a custom keymap configuration to the preset list"""
    bl_idname = "wm.keyconfig_preset_add"
    bl_label = "Add Custom Keymap Configuration"
    preset_menu = "USERPREF_MT_keyconfigs"
    preset_subdir = "keyconfig"

    def add(self, _context, filepath):
        bpy.ops.preferences.keyconfig_export(filepath=filepath)
        bpy.utils.keyconfig_set(filepath)


class RemovePresetKeyconfig(AddPresetBase, Operator):
    """Remove a custom keymap configuration from the preset list"""
    bl_idname = "wm.keyconfig_preset_remove"
    bl_label = "Remove Keymap Configuration"
    preset_menu = "USERPREF_MT_keyconfigs"
    preset_subdir = "keyconfig"

    remove_active: BoolProperty(
        default=True,
        options={'HIDDEN', 'SKIP_SAVE'},
    )

    @classmethod
    def poll(cls, context):
        from bpy.utils import is_path_builtin
        keyconfigs = bpy.context.window_manager.keyconfigs
        preset_menu_class = getattr(bpy.types, cls.preset_menu)
        name = keyconfigs.active.name
        filepath = bpy.utils.preset_find(name, cls.preset_subdir, ext=".py")
        if not bool(filepath) or is_path_builtin(filepath):
            cls.poll_message_set("Built-in keymap configurations cannot be removed")
            return False
        return True

    def pre_cb(self, context):
        keyconfigs = bpy.context.window_manager.keyconfigs
        preset_menu_class = getattr(bpy.types, self.preset_menu)
        preset_menu_class.bl_label = keyconfigs.active.name

    def post_cb(self, context):
        keyconfigs = bpy.context.window_manager.keyconfigs
        keyconfigs.remove(keyconfigs.active)

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(
            self, event, title="Remove Keymap Configuration", confirm_text="Delete")


class AddPresetOperator(AddPresetBase, Operator):
    """Add or remove an Operator Preset"""
    bl_idname = "wm.operator_preset_add"
    bl_label = "Operator Preset"
    preset_menu = "WM_MT_operator_presets"

    operator: StringProperty(
        name="Operator",
        maxlen=64,
        options={'HIDDEN', 'SKIP_SAVE'},
    )

    preset_defines = [
        "op = bpy.context.active_operator",
    ]

    @property
    def preset_subdir(self):
        return AddPresetOperator.operator_path(self.operator)

    @property
    def preset_values(self):
        properties_blacklist = Operator.bl_rna.properties.keys()

        prefix, suffix = self.operator.split("_OT_", 1)
        op = getattr(getattr(bpy.ops, prefix.lower()), suffix)
        operator_rna = op.get_rna_type()
        del op

        ret = []
        for prop_id, prop in operator_rna.properties.items():
            if not prop.is_skip_preset:
                if prop_id not in properties_blacklist:
                    ret.append("op.%s" % prop_id)

        return ret

    @staticmethod
    def operator_path(operator):
        import os
        prefix, suffix = operator.split("_OT_", 1)
        return os.path.join("operator", "%s.%s" % (prefix.lower(), suffix))


class WM_MT_operator_presets(Menu):
    bl_label = "Operator Presets"

    def draw(self, context):
        self.operator = context.active_operator.bl_idname

        # dummy 'default' menu item
        layout = self.layout
        layout.operator("wm.operator_defaults")
        layout.separator()

        Menu.draw_preset(self, context)

    @property
    def preset_subdir(self):
        return AddPresetOperator.operator_path(self.operator)

    preset_operator = "script.execute_preset"


class WM_OT_operator_presets_cleanup(Operator):
    """Remove outdated operator properties from presets that may cause problems"""

    bl_idname = "wm.operator_presets_cleanup"
    bl_label = "Clean Up Operator Presets"

    operator: StringProperty(name="operator")
    properties: CollectionProperty(name="properties", type=OperatorFileListElement)

    def _cleanup_preset(self, filepath, properties_exclude):
        import os
        import re
        if not (os.path.isfile(filepath) and os.path.splitext(filepath)[1].lower() == ".py"):
            return
        with open(filepath, "r", encoding="utf-8") as fh:
            lines = fh.read().splitlines(True)
        if not lines:
            return
        regex_exclude = re.compile("(" + "|".join([re.escape("op." + prop) for prop in properties_exclude]) + ")\\b")
        lines = [line for line in lines if not regex_exclude.match(line)]
        with open(filepath, "w", encoding="utf-8") as fh:
            fh.write("".join(lines))

    def _cleanup_operators_presets(self, operators, properties_exclude):
        import os
        base_preset_directory = bpy.utils.user_resource('SCRIPTS', path="presets", create=False)
        if not base_preset_directory:
            return
        for operator in operators:
            operator_path = AddPresetOperator.operator_path(operator)
            directory = os.path.join(base_preset_directory, operator_path)

            if not os.path.isdir(directory):
                continue
            for filename in os.listdir(directory):
                self._cleanup_preset(os.path.join(directory, filename), properties_exclude)

    def execute(self, context):
        properties_exclude = []
        operators = []
        if self.operator:
            operators.append(self.operator)
            for prop in self.properties:
                properties_exclude.append(prop.name)
        else:
            # Cleanup by default I/O Operators Presets
            operators = [
                "WM_OT_alembic_export",
                "WM_OT_alembic_import",
                "WM_OT_collada_export",
                "WM_OT_collada_import",
                "WM_OT_gpencil_export_svg",
                "WM_OT_gpencil_export_pdf",
                "WM_OT_gpencil_export_svg",
                "WM_OT_gpencil_import_svg",
                "WM_OT_obj_export",
                "WM_OT_obj_import",
                "WM_OT_ply_export",
                "WM_OT_ply_import",
                "WM_OT_stl_export",
                "WM_OT_stl_import",
                "WM_OT_usd_export",
                "WM_OT_usd_import",
            ]
            properties_exclude = [
                "filepath",
                "directory",
                "files",
                "filename"
            ]

        self._cleanup_operators_presets(operators, properties_exclude)
        return {'FINISHED'}


class AddPresetGpencilBrush(AddPresetBase, Operator):
    """Add or remove grease pencil brush preset"""
    bl_idname = "scene.gpencil_brush_preset_add"
    bl_label = "Add Grease Pencil Brush Preset"
    preset_menu = "VIEW3D_PT_gpencil_brush_presets"

    preset_defines = [
        "brush = bpy.context.tool_settings.gpencil_paint.brush",
        "settings = brush.gpencil_settings",
    ]

    preset_values = [
        "settings.input_samples",
        "settings.active_smooth_factor",
        "settings.angle",
        "settings.angle_factor",
        "settings.use_settings_stabilizer",
        "brush.smooth_stroke_radius",
        "brush.smooth_stroke_factor",
        "settings.pen_smooth_factor",
        "settings.pen_smooth_steps",
        "settings.pen_subdivision_steps",
        "settings.use_settings_random",
        "settings.random_pressure",
        "settings.random_strength",
        "settings.uv_random",
        "settings.pen_jitter",
        "settings.use_jitter_pressure",
        "settings.use_trim",
    ]

    preset_subdir = "gpencil_brush"


class AddPresetGpencilMaterial(AddPresetBase, Operator):
    """Add or remove grease pencil material preset"""
    bl_idname = "scene.gpencil_material_preset_add"
    bl_label = "Add Grease Pencil Material Preset"
    preset_menu = "MATERIAL_PT_gpencil_material_presets"

    preset_defines = [
        "material = bpy.context.object.active_material",
        "gpcolor = material.grease_pencil",
    ]

    preset_values = [
        "gpcolor.mode",
        "gpcolor.stroke_style",
        "gpcolor.color",
        "gpcolor.stroke_image",
        "gpcolor.pixel_size",
        "gpcolor.mix_stroke_factor",
        "gpcolor.alignment_mode",
        "gpcolor.alignment_rotation",
        "gpcolor.fill_style",
        "gpcolor.fill_color",
        "gpcolor.fill_image",
        "gpcolor.gradient_type",
        "gpcolor.mix_color",
        "gpcolor.mix_factor",
        "gpcolor.flip",
        "gpcolor.texture_offset",
        "gpcolor.texture_scale",
        "gpcolor.texture_angle",
        "gpcolor.texture_clamp",
        "gpcolor.mix_factor",
        "gpcolor.show_stroke",
        "gpcolor.show_fill",
    ]

    preset_subdir = "gpencil_material"


classes = (
    AddPresetCamera,
    AddPresetCloth,
    AddPresetFluid,
    AddPresetHairDynamics,
    AddPresetInterfaceTheme,
    RemovePresetInterfaceTheme,
    SavePresetInterfaceTheme,
    AddPresetKeyconfig,
    RemovePresetKeyconfig,
    AddPresetNodeColor,
    AddPresetOperator,
    AddPresetRender,
    AddPresetCameraSafeAreas,
    AddPresetTextEditor,
    AddPresetTrackingCamera,
    AddPresetTrackingSettings,
    AddPresetTrackingTrackColor,
    AddPresetGpencilBrush,
    AddPresetGpencilMaterial,
    AddPresetEEVEERaytracing,
    ExecutePreset,
    WM_MT_operator_presets,
    WM_OT_operator_presets_cleanup,
)
