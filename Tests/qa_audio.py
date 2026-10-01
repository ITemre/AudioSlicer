import unreal, wave, math, struct, json, os, uuid

ROOT = os.path.join(unreal.Paths.project_saved_dir(), 'AudioSlicerQA')
os.makedirs(ROOT, exist_ok=True)
ASSET_ROOT = '/Game/AudioSlicerQA_' + uuid.uuid4().hex[:8]
results = []
def check(name, condition, detail=''):
    results.append({'test': name, 'passed': bool(condition), 'detail': str(detail)})
    if not condition:
        raise AssertionError(name + ': ' + str(detail))

def make_wave(channels):
    filename = ROOT + '/Demo_%s.wav' % ('Mono' if channels == 1 else 'Stereo')
    rate = 48000
    segments = [(0.5, 1.3), (2.0, 2.8), (3.5, 4.3)]
    with wave.open(filename, 'wb') as f:
        f.setnchannels(channels); f.setsampwidth(2); f.setframerate(rate)
        samples = bytearray()
        for i in range(rate * 5):
            t = i / rate
            region = next(((a,b) for a,b in segments if a <= t < b), None)
            for channel in range(channels):
                value = 0
                if region:
                    a,b = region
                    envelope = min(1., (t-a)/0.025, (b-t)/0.025)
                    value = int(12000 * envelope * math.sin(2 * math.pi * (220 + 110*channel) * t))
                samples += struct.pack('<h', value)
        f.writeframes(samples)
    return filename

try:
    for channels in (1,2):
        task = unreal.AssetImportTask()
        task.filename = make_wave(channels)
        task.destination_path = ASSET_ROOT
        task.automated = True; task.save = True; task.replace_existing = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        source = unreal.load_asset(task.imported_object_paths[0])
        source.set_editor_property('volume', 0.7)
        source.set_editor_property('pitch', 1.1)
        settings = unreal.AudioSilenceDetectionSettings()
        slices = unreal.AudioSlicerLibrary.detect_slices(source, settings)
        check('detect_%d_channels' % channels, len(slices) == 3, len(slices))
        options = unreal.AudioSliceExportOptions()
        options.destination_path = ASSET_ROOT + '/Slices%d' % channels
        options.save_assets = True
        exported = unreal.AudioSlicerLibrary.export_slices(source, slices, options)
        check('export_%d_channels' % channels, len(exported) == 3)
        for asset in exported:
            check('duration_' + asset.get_name(), 0.8 < asset.duration < 0.95, asset.duration)
            check('copy_volume_' + asset.get_name(), abs(asset.get_editor_property('volume') - 0.7) < 0.001)
            check('copy_pitch_' + asset.get_name(), abs(asset.get_editor_property('pitch') - 1.1) < 0.001)
            check('saved_' + asset.get_name(), unreal.EditorAssetLibrary.does_asset_exist(asset.get_path_name()))
        original_paths = [a.get_path_name() for a in exported]
        unique = unreal.AudioSlicerLibrary.export_slices(source, slices, options)
        check('unique_names_%d_channels' % channels, not set(original_paths).intersection(a.get_path_name() for a in unique))
        options.overwrite_existing = True
        updated = unreal.AudioSlicerLibrary.export_slices(source, slices, options)
        check('overwrite_identity_%d_channels' % channels, all(a == b for a,b in zip(exported, updated)))
        zero = unreal.AudioSliceRange(); zero.start_time = 0.; zero.end_time = 0.
        check('empty_slice_%d_channels' % channels, not unreal.AudioSlicerLibrary.export_slices(source, [zero], options))
        def export_range(name, start, end):
            region = unreal.AudioSliceRange()
            region.name = name; region.start_time = start; region.end_time = end
            return unreal.AudioSlicerLibrary.export_slices(source, [region], options)

        options.destination_path = ASSET_ROOT
        duration_before = source.duration
        check('source_overwrite_rejected_%d' % channels,
              not export_range(source.get_name(), 0.5, 1.0))
        check('source_unchanged_%d' % channels,
              source.duration == duration_before and len(unreal.AudioSlicerLibrary.detect_slices(source, settings)) == 3)
        options.overwrite_existing = False
        protected = export_range(source.get_name(), 0.5, 1.0)
        check('source_collision_unique_%d' % channels, len(protected) == 1 and protected[0] != source)
        options.overwrite_existing = True
        options.destination_path = ASSET_ROOT + '/Regression%d' % channels
        for value in (float('nan'), float('inf'), -float('inf')):
            probe = unreal.AudioSliceRange(); probe.start_time = value
            if math.isnan(value) and not math.isnan(probe.start_time):
                results.append({'test': 'nan_input_%d' % channels, 'skipped': True, 'detail': 'Engine Python binding discards NaN before the plugin receives it.'})
                continue
            check('nonfinite_start_%d_%s' % (channels, value), not export_range('Invalid', value, 1.0))
            check('nonfinite_end_%d_%s' % (channels, value), not export_range('Invalid', 0., value))
        huge = export_range('HugeRange', -1.e30, 1.e30)
        check('huge_range_clamped_%d' % channels, len(huge) == 1 and abs(huge[0].duration - 5.) < 0.001)
        first = unreal.AudioSliceRange(); first.name = 'Duplicate'; first.start_time = 0.5; first.end_time = 1.0
        second = unreal.AudioSliceRange(); second.name = 'duplicate'; second.start_time = 2.; second.end_time = 2.8
        duplicate = unreal.AudioSlicerLibrary.export_slices(source, [first, second], options)
        check('duplicate_overwrite_rejected_%d' % channels, len(duplicate) == 1 and duplicate[0].duration < 0.6)
        options.overwrite_existing = False
        unique_batch = unreal.AudioSlicerLibrary.export_slices(source, [first, second], options)
        check('duplicate_unique_%d' % channels, len(unique_batch) == 2 and unique_batch[0] != unique_batch[1])
        options.fade_in_ms = float('inf')
        check('invalid_fade_%d' % channels, not export_range('InvalidFade', 0., 1.))
        options.fade_in_ms = 1.e30; options.fade_out_ms = 1.e30
        check('huge_fades_%d' % channels, len(export_range('HugeFades', 0.5, 1.0)) == 1)
        settings.padding_ms = 1.e30
        padded = unreal.AudioSlicerLibrary.detect_slices(source, settings)
        check('huge_padding_%d' % channels, len(padded) == 1 and padded[0].start_time == 0. and abs(padded[0].end_time - 5.) < 0.001)
        settings.padding_ms = float('inf')
        check('invalid_detection_%d' % channels, not unreal.AudioSlicerLibrary.detect_slices(source, settings))
    unreal.log('AUDIOSLICER_QA_PASS')
except Exception as e:
    results.append({'error': str(e)})
    unreal.log_error('AUDIOSLICER_QA_FAIL: ' + str(e))
finally:
    with open(ROOT + '/qa_results.json','w') as f:
        json.dump(results, f, indent=2)
