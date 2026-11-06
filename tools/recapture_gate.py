#!/usr/bin/env python3
"""Recapture a gate manifest with one frozen oracle and three capped workers.

The manifest names existing captures; their scenario text, ranges and original
save/config inputs are reused. Old evidence is never overwritten.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

CORE = ('Sim2-combat-collision', 'scene1-water', 'movement',
        'bios-modifiers-movement', 'scene11-training-coast-save',
        'route-corridor-heal', 'route-guard143', 'route-guard143-heal',
        'route-healthy-radio', 'route-radio-finish',
        'route-radio-lastguard', 'route-fast-north')


def manifest_routes(manifest, baseline):
    names = [recipe['route'] for recipe in manifest['routes']]
    if any(not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', name) for name in names):
        raise ValueError('Unsafe route name')
    if len(names) != len(set(names)):
        raise ValueError('Duplicate route name')
    expected = {recipe['route'] for recipe in baseline['routes']}
    expected.update(manifest.get('additional_routes', []))
    if set(names) != expected or tuple(names[:12]) != CORE:
        raise ValueError('Manifest must include the entire baseline and explicit additions, with twelve core scenarios first')
    return names


def fresh_directory(path):
    path.mkdir(parents=True, exist_ok=False)


def selected_indices(names, only):
    if only and (len(only) != len(set(only)) or not set(only).issubset(names)):
        raise ValueError('Selected routes must be unique manifest members')
    return [i for i, name in enumerate(names) if not only or name in only]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_oracle(source, reference=None):
    spec = importlib.util.spec_from_file_location('gate_original_oracle', source / 'tools/oracle.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if reference:
        module.REFERENCE = reference
    return module


def frame_rows(path):
    with path.open() as stream:
        ranges = 0
        for line in stream:
            if line.startswith('range,'):
                ranges += 1
            if line.startswith('frame,'):
                fields = line.strip().split(',')
                if fields[-1] == 'ranges...':
                    fields[-1:] = ['range' + str(i) for i in range(ranges)]
                rows = list(csv.DictReader(stream, fieldnames=fields))
                if not rows:
                    raise ValueError('No gameplay records: ' + str(path))
                return rows
    raise ValueError('Missing frame header: ' + str(path))


def trace_ranges(path):
    result = []
    with path.open() as stream:
        for line in stream:
            if line.startswith('frame,'):
                break
            if line.startswith('range,'):
                _, kind, address, length = line.strip().split(',')
                result.append((kind, int(address, 16), int(length)))
    return result


def paired(old, new):
    before = frame_rows(old / 'frames.csv')
    after = frame_rows(new / 'frames.csv')
    old_ranges = trace_ranges(old / 'frames.csv')
    new_ranges = trace_ranges(new / 'frames.csv')
    changes = {}
    if len(before) != len(after):
        changes['record_count'] = [len(before), len(after)]
    for left, right in zip(before, after):
        for field in left:
            observed = right.get(field)
            if field.startswith('range') and field[5:].isdigit():
                index = int(field[5:])
                if (index < len(new_ranges) and
                        old_ranges[index][:2] == new_ranges[index][:2] and
                        new_ranges[index][2] >= old_ranges[index][2]):
                    observed = observed[:old_ranges[index][2] * 2]
            if left[field] != observed:
                item = changes.setdefault(field, {'count': 0, 'first': [left['frame'], left[field], observed]})
                item['count'] += 1
    files = {}
    for name in ('actors.csv', 'actor-clocks.csv', 'seed.csv', 'events-input.csv'):
        if (old / name).is_file():
            files[name] = {'old': sha(old / name), 'new': sha(new / name)}
    return {'common_frame_fields_identical': not changes, 'frame_changes': changes,
        'old_ranges': old_ranges, 'new_ranges': new_ranges,
        'paired_files': files, 'paired_files_identical': all(v['old'] == v['new'] for v in files.values())}


def worker(args):
    plan_path = args.cohort / 'recapture-plan.json'
    if sha(plan_path) != args.plan_sha256:
        raise ValueError('Frozen recapture plan changed')
    plan = json.loads(plan_path.read_text())
    recipe = plan['routes'][args.worker]
    for path, digest in {**plan['frozen_files'], **recipe['frozen_files']}.items():
        if sha(Path(path)) != digest:
            raise ValueError('Frozen producer or recipe changed: ' + path)
    old = Path(recipe['old_capture'])
    metadata = json.loads((old / 'run.json').read_text())
    binary = args.cohort / 'oracle'
    if sha(binary) != plan['oracle_sha256']:
        raise ValueError('Frozen oracle binary changed')
    oracle = load_oracle(args.source)
    # Source verification/build happened once before freezing this binary. No
    # game hooks, clocks, guest registers or instructions are replaced here.
    oracle.build = lambda: binary
    scenario = args.cohort / 'scenarios' / (recipe['route'] + '.txt')
    command = ['oracle.py', str(scenario), '--frame-trace', '--actor-trace',
        '--rng-trace', '--item-trace', '--selector-trace', '--snapshot',
        '--export-saves', str(args.cohort / 'original' / recipe['route'] / 'saves')]
    if metadata.get('event_trace') or (old / 'events-input.csv').is_file():
        command.append('--event-trace')
    for span in recipe.get('trace_ranges', metadata.get('trace_ranges', [])):
        command += ['--trace-range', span]
    if metadata.get('import_saves_directory'):
        imported = Path(metadata['import_saves_directory'])
        expected = list(metadata['imported_saves'].values())[-1]
        for name, digest in expected.items():
            if sha(imported / name) != digest:
                raise ValueError('Original imported save changed: ' + str(imported / name))
        command += ['--import-saves', str(imported)]
    imported_config = metadata.get('import_config_directory')
    if imported_config:
        expected = list(metadata['imported_config'].values())[-1]
        if sha(Path(imported_config) / 'CMDATA.CNF') != expected:
            raise ValueError('Original imported configuration changed')
        command += ['--import-config', imported_config]
    sys.argv = command
    oracle.main()
    new = args.cohort / 'original' / recipe['route']
    observed = json.loads((new / 'run.json').read_text())
    if (observed['binary_sha256'] != plan['oracle_sha256'] or
            observed['patch_sha256'] != plan['patch_sha256'] or
            (observed.get('imported_saves') or {}) != (metadata.get('imported_saves') or {}) or
            (observed.get('imported_config') or {}) != (metadata.get('imported_config') or {})):
        raise ValueError('Completed capture differs from frozen producer or imported inputs')
    result = {'route': recipe['route'], 'old_capture': str(old), 'new_capture': str(new),
        'command': command, 'status': 'CAPTURED', 'comparison': paired(old, new)}
    (new / 'pairing.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--cohort', type=Path, required=True)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--oracle', type=Path)
    parser.add_argument('--oracle-receipt', type=Path)
    parser.add_argument('--worker', type=int)
    parser.add_argument('--plan-sha256')
    parser.add_argument('--only', action='append', default=[])
    args = parser.parse_args()
    for name in ('root', 'source', 'cohort'):
        setattr(args, name, getattr(args, name).resolve())
    if args.worker is not None:
        worker(args)
        return
    manifest = json.loads(args.manifest.read_text())
    baseline = json.loads(Path(manifest['baseline_receipt']).read_text())
    names = manifest_routes(manifest, baseline)
    selected = selected_indices(names, args.only)
    fresh_directory(args.cohort)
    scenario_dir = args.cohort / 'scenarios'
    scenario_dir.mkdir()
    original = args.cohort / 'original'
    original.mkdir()
    (args.source / 'captures').symlink_to(original, target_is_directory=True)
    oracle = load_oracle(args.source, args.reference)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=oracle.REFERENCE, text=True).strip()
    if revision != oracle.REVISION:
        raise ValueError('Original reference HEAD differs from the pinned revision')
    oracle.verified_sources(oracle.REFERENCE, args.source / 'tools/oracle-original.patch')
    if args.oracle:
        evidence = json.loads(args.oracle_receipt.read_text())
        if (sha(args.oracle) != evidence['binary_sha256'] or
                sha(args.source / 'tools/oracle-original.patch') != evidence['patch_sha256'] or
                oracle.REVISION != evidence['reference']):
            raise ValueError('Cached oracle does not match its original producer receipt')
        built = args.oracle
    else:
        built = oracle.build()
    binary = args.cohort / 'oracle'
    shutil.copy2(built, binary)
    plan = {'oracle_sha256': sha(binary), 'patch_sha256': sha(args.source / 'tools/oracle-original.patch'),
        'source': str(args.source), 'routes': [],
        'selected_routes': [names[i] for i in selected],
        'frozen_files': {str(path): sha(path) for path in (
            args.source / 'tools/oracle.py', args.source / 'tools/oracle-original.patch',
            args.source / 'tools/run_capped.sh', Path(__file__).resolve(), args.manifest.resolve(),
            Path(manifest['baseline_receipt']).resolve())}}
    for recipe in manifest['routes']:
        name = recipe['route']
        old = Path(recipe.get('capture', str(args.root / 'captures' / name)))
        metadata = json.loads((old / 'run.json').read_text())
        text = metadata['scenario']
        if not any(line.strip().startswith('ips ') for line in text.splitlines()):
            text = 'ips ' + str(metadata['ips']) + '\n' + text
        scenario = scenario_dir / (name + '.txt')
        scenario.write_text(text)
        inputs = [scenario, old / 'run.json', old / 'frames.csv']
        inputs += [old / filename for filename in
                   ('actors.csv', 'actor-clocks.csv', 'seed.csv', 'events-input.csv')
                   if (old / filename).is_file()]
        plan['routes'].append({'route': name, 'old_capture': str(old),
            'trace_ranges': recipe.get('trace_ranges', metadata['trace_ranges']),
            'frozen_files': {str(path): sha(path) for path in inputs}})
    planpath = args.cohort / 'recapture-plan.json'
    planpath.write_text(json.dumps(plan, indent=2) + '\n')
    plan_sha256 = sha(planpath)
    logs = args.cohort / 'recapture-logs'
    logs.mkdir(exist_ok=True)
    results = []

    def capture(index):
        name = plan['routes'][index]['route']
        command = [str(args.source / 'tools/run_capped.sh'), sys.executable, str(Path(__file__).resolve()),
            '--root', str(args.root), '--source', str(args.source), '--cohort', str(args.cohort),
            '--worker', str(index), '--plan-sha256', plan_sha256]
        with (logs / (name + '.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        pairing = original / name / 'pairing.json'
        return {'route': name, 'exit': result.returncode, 'log': str(logs / (name + '.log')),
            'pairing': json.loads(pairing.read_text()) if pairing.exists() else None}

    # Manifest order puts the twelve standing core scenarios first. A batch
    # barrier ensures route captures cannot occupy slots ahead of the core.
    batches = [[i for i in selected if i < 12], [i for i in selected if i >= 12]]
    with ThreadPoolExecutor(max_workers=3) as pool:
        for batch in batches:
            futures = [pool.submit(capture, index) for index in batch]
            for future in as_completed(futures):
                result = future.result()
                results.append(result)
                (args.cohort / 'recapture-results.json').write_text(json.dumps(results, indent=2) + '\n')
                print(result['route'], 'exit=' + str(result['exit']), flush=True)
    print('Selected captures:', len(selected), 'of', len(names), 'manifest scenarios', flush=True)
    raise SystemExit(1 if any(r['exit'] for r in results) else 0)


if __name__ == '__main__':
    main()
