#!/usr/bin/env python3
import json
import sys,os
import itertools
import functools
import operator
import difflib
import math

import config.instantiation_file as instantiation_file
import config.modules as modules
import config.makefile as makefile
import config.util as util

# Read the config file
def parse_file(fname):
    with open(fname) as rfp:
        return json.load(rfp)

def chain(*dicts):
    def merge_dicts(x,y):
        merges = {k:merge_dicts(v, y[k]) for k,v in x.items() if isinstance(v, dict) and isinstance(y.get(k), dict)}
        return { **y, **x, **merges }

    return functools.reduce(merge_dicts, dicts)

constants_header_name = 'inc/champsim_constants.h'
instantiation_file_name = 'src/core_inst.cc'
core_modules_file_name = 'inc/ooo_cpu_modules.inc'
cache_modules_file_name = 'inc/cache_modules.inc'

generated_warning = '/***\n * THIS FILE IS AUTOMATICALLY GENERATED\n * Do not edit this file. It will be overwritten when the configure script is run.\n ***/\n\n'

def write_if_different(fname, new_file_string):
    ratio = 0
    if os.path.exists(fname):
        with open(fname, 'rt') as rfp:
            f = list(l.strip() for l in rfp)
        new_file_lines = list(l.strip() for l in new_file_string.splitlines())
        ratio = difflib.SequenceMatcher(a=f, b=new_file_lines).ratio()

    if ratio < 1:
        with open(fname, 'wt') as wfp:
            wfp.write(new_file_string)

###
# Begin default core model definition
###

default_root = { 'block_size': 64, 'page_size': 4096, 'heartbeat_frequency': 10000000, 'num_cores': 1 }

# Read the config file
if len(sys.argv) == 1:
    print("No configuration specified. Building default ChampSim with no prefetching.")
config_file = chain(*map(parse_file, reversed(sys.argv[1:])), default_root)

default_core = { 'frequency' : 4000, 'ifetch_buffer_size': 64, 'decode_buffer_size': 32, 'dispatch_buffer_size': 32, 'rob_size': 352, 'lq_size': 128, 'sq_size': 72, 'fetch_width' : 6, 'decode_width' : 6, 'dispatch_width' : 6, 'execute_width' : 4, 'lq_width' : 2, 'sq_width' : 2, 'retire_width' : 5, 'mispredict_penalty' : 1, 'scheduler_size' : 128, 'decode_latency' : 1, 'dispatch_latency' : 1, 'schedule_latency' : 0, 'execute_latency' : 0, 'branch_predictor': 'bimodal', 'btb': 'basic_btb' }
default_dib  = { 'window_size': 16,'sets': 32, 'ways': 8 }
default_l1i  = { 'sets': 64, 'ways': 8, 'rq_size': 64, 'wq_size': 64, 'pq_size': 32, 'mshr_size': 8, 'latency': 4, 'fill_latency': 1, 'max_read': 2, 'max_write': 2, 'prefetch_as_load': False, 'virtual_prefetch': True, 'wq_check_full_addr': True, 'prefetch_activate': 'LOAD,PREFETCH', 'prefetcher': 'no_instr', 'replacement': 'lru'}
default_l1d  = { 'sets': 64, 'ways': 12, 'rq_size': 64, 'wq_size': 64, 'pq_size': 8, 'mshr_size': 16, 'latency': 5, 'fill_latency': 1, 'max_read': 2, 'max_write': 2, 'prefetch_as_load': False, 'virtual_prefetch': False, 'wq_check_full_addr': True, 'prefetch_activate': 'LOAD,PREFETCH', 'prefetcher': 'no', 'replacement': 'lru'}
default_l2c  = { 'sets': 1024, 'ways': 8, 'rq_size': 32, 'wq_size': 32, 'pq_size': 16, 'mshr_size': 32, 'latency': 10, 'fill_latency': 1, 'max_read': 1, 'max_write': 1, 'prefetch_as_load': False, 'virtual_prefetch': False, 'wq_check_full_addr': False, 'prefetch_activate': 'LOAD,PREFETCH', 'prefetcher': 'no', 'replacement': 'lru'}
default_itlb = { 'sets': 16, 'ways': 4, 'rq_size': 16, 'wq_size': 16, 'pq_size': 0, 'mshr_size': 8, 'latency': 1, 'fill_latency': 1, 'max_read': 2, 'max_write': 2, 'prefetch_as_load': False, 'virtual_prefetch': True, 'wq_check_full_addr': True, 'prefetch_activate': 'LOAD,PREFETCH', 'prefetcher': 'no', 'replacement': 'lru'}
default_dtlb = { 'sets': 16, 'ways': 4, 'rq_size': 16, 'wq_size': 16, 'pq_size': 0, 'mshr_size': 8, 'latency': 1, 'fill_latency': 1, 'max_read': 2, 'max_write': 2, 'prefetch_as_load': False, 'virtual_prefetch': False, 'wq_check_full_addr': True, 'prefetch_activate': 'LOAD,PREFETCH', 'prefetcher': 'no', 'replacement': 'lru'}
default_stlb = { 'sets': 128, 'ways': 12, 'rq_size': 32, 'wq_size': 32, 'pq_size': 0, 'mshr_size': 16, 'latency': 8, 'fill_latency': 1, 'max_read': 1, 'max_write': 1, 'prefetch_as_load': False, 'virtual_prefetch': False, 'wq_check_full_addr': False, 'prefetch_activate': 'LOAD,PREFETCH', 'prefetcher': 'no', 'replacement': 'lru'}
default_llc  = { 'latency': 20, 'fill_latency': 1, 'prefetch_as_load': False, 'virtual_prefetch': False, 'wq_check_full_addr': False, 'prefetch_activate': 'LOAD,PREFETCH', 'prefetcher': 'no', 'replacement': 'lru', 'lower_level': 'DRAM' }
default_pmem = { 'name': 'DRAM', 'frequency': 3200, 'channels': 1, 'ranks': 1, 'banks': 8, 'rows': 65536, 'columns': 128, 'lines_per_column': 8, 'channel_width': 8, 'wq_size': 64, 'rq_size': 64, 'tRP': 12.5, 'tRCD': 12.5, 'tCAS': 12.5, 'turn_around_time': 7.5 }
default_vmem = { 'size': 8589934592, 'num_levels': 5, 'minor_fault_penalty': 200 }
default_ptw = { 'pscl5_set' : 1, 'pscl5_way' : 2, 'pscl4_set' : 1, 'pscl4_way': 4, 'pscl3_set' : 2, 'pscl3_way' : 4, 'pscl2_set' : 4, 'pscl2_way': 8, 'ptw_rq_size': 16, 'ptw_mshr_size': 5, 'ptw_max_read': 2, 'ptw_max_write': 2}

###
# Establish default optional values
###

pmem = chain(config_file.get('physical_memory', {}), default_pmem)
vmem = chain(config_file.get('virtual_memory', {}), default_vmem)

cores = config_file.get('ooo_cpu', [{}])

# Copy or trim cores as necessary to fill out the specified number of cores
cpu_repeat_factor = math.ceil(config_file['num_cores'] / len(cores));
cores = list(itertools.islice(itertools.chain.from_iterable(itertools.repeat(c, cpu_repeat_factor) for c in cores), config_file['num_cores']))

# Default core elements
cores = [chain(cpu, {'name': 'cpu'+str(i), 'index': i, 'DIB': config_file.get('DIB',{})}, {'DIB': default_dib}, default_core) for i,cpu in enumerate(cores)]

# Assign defaults that are unique per core
def combine_named(*iterables):
    iterable = sorted(itertools.chain(*iterables), key=operator.itemgetter('name'))
    iterable = itertools.groupby(iterable, key=operator.itemgetter('name'))
    return {kv[0]: chain(*kv[1]) for kv in iterable}

def read_element_name(cpu, elem):
    return cpu.get(elem) if isinstance(cpu.get(elem), str) else cpu.get(elem,{}).get('name', cpu['name']+'_'+elem)

# Defaults for first-level caches
def named_l1i_defaults(cpu):
    return {'name': read_element_name(cpu, 'L1I'), 'frequency': cpu['frequency'], 'lower_level': read_element_name(cpu, 'L2C'), 'lower_translate': read_element_name(cpu, 'ITLB'), '_needs_translate': True, '_is_instruction_cache': True, **default_l1i}

def named_l1d_defaults(cpu):
    return {'name': read_element_name(cpu, 'L1D'), 'frequency': cpu['frequency'], 'lower_level': read_element_name(cpu, 'L2C'), 'lower_translate': read_element_name(cpu, 'DTLB'), '_needs_translate': True, **default_l1d}

def named_itlb_defaults(cpu):
    return {'name': read_element_name(cpu, 'ITLB'), 'frequency': cpu['frequency'], 'lower_level': read_element_name(cpu, 'STLB'), **default_itlb}

def named_dtlb_defaults(cpu):
    return {'name': read_element_name(cpu, 'DTLB'), 'frequency': cpu['frequency'], 'lower_level': read_element_name(cpu, 'STLB'), **default_dtlb}

# Defaults for second-level caches
def named_l2c_defaults(cpu):
    return {'name': read_element_name(cpu, 'L2C'), 'frequency': cpu['frequency'], 'lower_level': 'LLC', 'lower_translate': read_element_name(cpu, 'STLB'), **default_l2c}

def sequence_l2c_defaults(name, uls):
    uls = list(uls)
    intern_default_l2c  = { 'latency': 10, 'fill_latency': 1, 'prefetch_as_load': False, 'virtual_prefetch': False, 'wq_check_full_addr': False, 'prefetch_activate': 'LOAD,PREFETCH', 'prefetcher': 'no', 'replacement': 'lru'}
    return {'name': name, 'frequency': max(x['frequency'] for x in uls), 'sets': 512*len(uls), 'ways': 8, 'rq_size': 16*len(uls), 'wq_size': 16*len(uls), 'pq_size': 16*len(uls), 'mshr_size': 32*len(uls), 'max_read': math.ceil(0.5*len(uls)), 'max_write': math.ceil(0.5*len(uls)), **intern_default_l2c}

def named_stlb_defaults(cpu):
    return {'name': read_element_name(cpu, 'STLB'), 'frequency': cpu['frequency'], 'lower_level': read_element_name(cpu, 'PTW'), **default_stlb}

def sequence_stlb_defaults(name, uls):
    uls = list(uls)
    intern_default_stlb  = { 'latency': 8, 'fill_latency': 1, 'prefetch_as_load': False, 'virtual_prefetch': False, 'wq_check_full_addr': False, 'prefetch_activate': 'LOAD,PREFETCH', 'prefetcher': 'no', 'replacement': 'lru'}
    return {'name': name, 'frequency': max(x['frequency'] for x in uls), 'sets': 64*len(uls), 'ways': 12, 'rq_size': 16*len(uls), 'wq_size': 16*len(uls), 'pq_size': 16*len(uls), 'mshr_size': 8*len(uls), 'max_read': math.ceil(0.5*len(uls)), 'max_write': math.ceil(0.5*len(uls)), **intern_default_stlb}

# Defaults for third-level caches
def named_ptw_defaults(cpu):
    return {'name': read_element_name(cpu, 'PTW'), 'cpu': cpu['index'], 'frequency': cpu['frequency'], 'lower_level': read_element_name(cpu, 'L1D'), **default_ptw}

def named_llc_defaults(name, uls):
    uls = list(uls)
    return {'name': name, 'frequency': max(x['frequency'] for x in uls), 'sets': 2048*len(uls), 'ways': 16, 'rq_size': 32*len(uls), 'wq_size': 32*len(uls), 'pq_size': 32*len(uls), 'mshr_size': 64*len(uls), 'max_read': len(uls), 'max_write': len(uls), **default_llc}

caches = combine_named(
        config_file.get('cache', []),
        # Copy values from the core specification and config root, if these are dicts
        ({'name': read_element_name(*cn), **cn[0][cn[1]]} for cn in itertools.product(cores, ('L1I', 'L1D', 'ITLB', 'DTLB')) if isinstance(cn[0].get(cn[1]), dict)),
        ({'name': read_element_name(*cn), **config_file[cn[1]]} for cn in itertools.product(cores, ('L1I', 'L1D', 'ITLB', 'DTLB')) if isinstance(config_file.get(cn[1]), dict)),
        # Apply defaults named after the cores
        map(named_l1i_defaults, cores),
        map(named_l1d_defaults, cores),
        map(named_itlb_defaults, cores),
        map(named_dtlb_defaults, cores)
        )

cores = [chain({n: read_element_name(cpu, n) for n in ('L1I', 'L1D', 'ITLB', 'DTLB')}, cpu) for cpu in cores]

second_level_names = [caches[c['L1D']]['lower_level'] for c in cores]
l2c_upper_levels = sorted(caches.values(), key=operator.itemgetter('lower_level'))
l2c_upper_levels = itertools.groupby(l2c_upper_levels, key=operator.itemgetter('lower_level'))
l2c_upper_levels = filter(lambda u: u[0] in second_level_names, l2c_upper_levels)

stlb_level_names = [caches[c['DTLB']]['lower_level'] for c in cores]
stlb_upper_levels = sorted(caches.values(), key=operator.itemgetter('lower_level'))
stlb_upper_levels = itertools.groupby(stlb_upper_levels, key=operator.itemgetter('lower_level'))
stlb_upper_levels = filter(lambda u: u[0] in stlb_level_names, stlb_upper_levels)

caches = combine_named(
        caches.values(),
        # Copy values from the core specification and config root, if these are dicts
        ({'name': read_element_name(*cn), **cn[0][cn[1]]} for cn in itertools.product(cores, ('L2C', 'STLB')) if isinstance(cn[0].get(cn[1]), dict)),
        ({'name': read_element_name(*cn), **config_file[cn[1]]} for cn in itertools.product(cores, ('L2C', 'STLB')) if isinstance(config_file.get(cn[1]), dict)),
        # Apply defaults named after the second-level caches
        (sequence_l2c_defaults(*ul) for ul in l2c_upper_levels),
        (sequence_stlb_defaults(*ul) for ul in stlb_upper_levels),
        # Apply defaults named after the cores
        map(named_l2c_defaults, cores),
        map(named_stlb_defaults, cores),
        )

ptws = combine_named(
                 config_file.get('ptws',[]),
                 ({'name': read_element_name(c,'PTW'), **c['PTW']} for c in cores if isinstance(c.get('PTW'), dict)),
                 ({'name': read_element_name(c,'PTW'), **config_file['PTW']} for c in cores if isinstance(config_file.get('PTW'), dict)),
                 map(named_ptw_defaults, cores),
                )

third_level_names = [caches[caches[c['L1D']]['lower_level']]['lower_level'] for c in cores]
upper_levels = sorted(caches.values(), key=operator.itemgetter('lower_level'))
upper_levels = itertools.groupby(upper_levels, key=operator.itemgetter('lower_level'))
upper_levels = filter(lambda u: u[0] in third_level_names, upper_levels)

caches = combine_named(
        caches.values(),
        ({'name': 'LLC', **config_file.get('LLC', {})},),
        (named_llc_defaults(*ul) for ul in upper_levels)
        )

# Remove caches that are inaccessible
accessible_names = tuple(map(lambda x: x['name'], itertools.chain.from_iterable(util.iter_system(caches, cpu[name]) for cpu,name in itertools.product(cores, ('ITLB', 'DTLB', 'L1I', 'L1D')))))
caches = dict(filter(lambda x: x[0] in accessible_names, caches.items()))

# Establish latencies in caches
for cache in caches.values():
    cache['hit_latency'] = cache.get('hit_latency') or (cache['latency'] - cache['fill_latency'])

# Scale frequencies
def scale_frequencies(it):
    it_a, it_b = itertools.tee(it, 2)
    max_freq = max(x['frequency'] for x in it_a)
    for x in it_b:
        x['frequency'] = max_freq / x['frequency']

pmem['io_freq'] = pmem['frequency'] # Save value
scale_frequencies(itertools.chain(cores, caches.values(), ptws.values(), (pmem,)))

# TLBs use page offsets, Caches use block offsets
tlb_path = itertools.chain.from_iterable(util.iter_system(caches, cpu[name]) for cpu,name in itertools.product(cores, ('ITLB', 'DTLB')))
l1d_path = itertools.chain.from_iterable(util.iter_system(caches, cpu[name]) for cpu,name in itertools.product(cores, ('L1I', 'L1D')))
caches = combine_named(
        ({'offset_bits': 'lg2(' + str(config_file['page_size']) + ')', '_needs_translate': False, **c} for c in tlb_path),
        ({'offset_bits': 'lg2(' + str(config_file['block_size']) + ')', '_needs_translate': cache.get('_needs_translate', False) or cache.get('virtual_prefetch', False), **c} for c in l1d_path),
        caches.values()
        )

# Try the local module directories, then try to interpret as a path
def default_dir(dirname, f):
    fname = os.path.join(dirname, f)
    if not os.path.exists(fname):
        fname = os.path.relpath(os.path.expandvars(os.path.expanduser(f)))
    if not os.path.exists(fname):
        print('Path "' + fname + '" does not exist. Exiting...')
        sys.exit(1)
    return fname

def wrap_list(attr):
    if not isinstance(attr, list):
        attr = [attr]
    return attr

for cache in caches.values():
    cache['replacement'] = [default_dir('replacement', f) for f in wrap_list(cache.get('replacement', []))]
    cache['prefetcher']  = [default_dir('prefetcher', f) for f in wrap_list(cache.get('prefetcher', []))]

for cpu in cores:
    cpu['branch_predictor'] = [default_dir('branch', f) for f in wrap_list(cpu.get('branch_predictor', []))]
    cpu['btb']              = [default_dir('btb', f) for f in wrap_list(cpu.get('btb', []))]

###
# Check to make sure modules exist and they correspond to any already-built modules.
###

def default_modules(dirname):
    return tuple(os.path.join(dirname, d) for d in os.listdir(dirname) if os.path.isdir(os.path.join(dirname, d)))

repl_module_names = itertools.chain(default_modules('replacement'), *(c['replacement'] for c in caches.values()))
pref_module_names = list(itertools.chain(((m,m.endswith('_instr')) for m in default_modules('prefetcher')), *(zip(c['prefetcher'], itertools.repeat(c.get('_is_instruction_cache',False))) for c in caches.values())))
branch_module_names = itertools.chain(default_modules('branch'), *(c['branch_predictor'] for c in cores))
btb_module_names = itertools.chain(default_modules('btb'), *(c['btb'] for c in cores))

repl_data   = {modules.get_module_name(fname): {'fname':fname, **modules.get_repl_data(modules.get_module_name(fname))} for fname in repl_module_names}
pref_data   = {modules.get_module_name(fname): {'fname':fname, **modules.get_pref_data(modules.get_module_name(fname),is_instr)} for fname,is_instr in pref_module_names}
branch_data = {modules.get_module_name(fname): {'fname':fname, **modules.get_branch_data(modules.get_module_name(fname))} for fname in branch_module_names}
btb_data    = {modules.get_module_name(fname): {'fname':fname, **modules.get_btb_data(modules.get_module_name(fname))} for fname in btb_module_names}

for cpu in cores:
    cpu['branch_predictor'] = [module_name for module_name,data in branch_data.items() if data['fname'] in cpu['branch_predictor']]
    cpu['btb']              = [module_name for module_name,data in btb_data.items() if data['fname'] in cpu['btb']]

for cache in caches.values():
    cache['replacement'] = [module_name for module_name,data in repl_data.items() if data['fname'] in cache['replacement']]
    cache['prefetcher']  = [module_name for module_name,data in pref_data.items() if data['fname'] in cache['prefetcher']]

###
# Begin file writing
###

# Instantiation file
write_if_different(instantiation_file_name, generated_warning + instantiation_file.get_instantiation_string(cores, caches.values(), ptws.values(), pmem, vmem))

# Core modules file
write_if_different(core_modules_file_name, generated_warning + modules.get_branch_string(branch_data) + modules.get_btb_string(btb_data))

# Cache modules file
write_if_different(cache_modules_file_name, generated_warning + modules.get_repl_string(repl_data) + modules.get_pref_string(pref_data))

# Constants header
constants_file = generated_warning
constants_file += '#ifndef CHAMPSIM_CONSTANTS_H\n'
constants_file += '#define CHAMPSIM_CONSTANTS_H\n'
constants_file += '#include <cstdlib>\n'
constants_file += '#include "util.h"\n'
constants_file += 'constexpr unsigned BLOCK_SIZE = {block_size};\n'.format(**config_file)
constants_file += 'constexpr unsigned PAGE_SIZE = {page_size};\n'.format(**config_file)
constants_file += 'constexpr uint64_t STAT_PRINTING_PERIOD = {heartbeat_frequency};\n'.format(**config_file)
constants_file += 'constexpr std::size_t NUM_CPUS = {num_cores};\n'.format(**config_file)
constants_file += 'constexpr std::size_t NUM_CACHES = ' + str(len(caches)) + ';\n'
constants_file += 'constexpr auto LOG2_BLOCK_SIZE = lg2(BLOCK_SIZE);\n'
constants_file += 'constexpr auto LOG2_PAGE_SIZE = lg2(PAGE_SIZE);\n'
constants_file += f'constexpr static std::size_t NUM_BRANCH_MODULES = {len(branch_data)};\n'
constants_file += f'constexpr static std::size_t NUM_BTB_MODULES = {len(btb_data)};\n'
constants_file += f'constexpr static std::size_t NUM_REPLACEMENT_MODULES = {len(repl_data)};\n'
constants_file += f'constexpr static std::size_t NUM_PREFETCH_MODULES = {len(pref_data)};\n'

constants_file += 'constexpr uint64_t DRAM_IO_FREQ = {io_freq};\n'.format(**pmem)
constants_file += 'constexpr std::size_t DRAM_CHANNELS = {channels};\n'.format(**pmem)
constants_file += 'constexpr std::size_t DRAM_RANKS = {ranks};\n'.format(**pmem)
constants_file += 'constexpr std::size_t DRAM_BANKS = {banks};\n'.format(**pmem)
constants_file += 'constexpr std::size_t DRAM_ROWS = {rows};\n'.format(**pmem)
constants_file += 'constexpr std::size_t DRAM_COLUMNS = {columns};\n'.format(**pmem)
constants_file += 'constexpr std::size_t DRAM_CHANNEL_WIDTH = {channel_width};\n'.format(**pmem)
constants_file += 'constexpr std::size_t DRAM_WQ_SIZE = {wq_size};\n'.format(**pmem)
constants_file += 'constexpr std::size_t DRAM_RQ_SIZE = {rq_size};\n'.format(**pmem)
constants_file += '#endif\n'
write_if_different(constants_header_name, constants_file)

# Makefile
module_info = tuple(itertools.chain(repl_data.values(), pref_data.values(), branch_data.values(), btb_data.values()))
generated_files = (constants_header_name, instantiation_file_name, core_modules_file_name, cache_modules_file_name)
write_if_different('_configuration.mk', 'generated_files = ' + ' '.join(generated_files) + '\n\n' + makefile.get_makefile_string(module_info, **config_file))

# vim: set filetype=python:
