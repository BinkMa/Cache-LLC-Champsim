#!/usr/bin/env python3
#
#    Copyright 2023 The ChampSim Contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import json
import sys,os
import itertools
import argparse

import config.filewrite as filewrite
import config.parse as parse
import config.util as util

# Read the config file
def parse_file(fname):
    with open(fname) as rfp:
        return json.load(rfp)

if __name__ == '__main__':
    champsim_root = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description='Configure ChampSim')

    parser.add_argument('--prefix', default='.',
            help='The prefix for the ChampSim sources')
    parser.add_argument('--bindir',
            help='The directory to store the resulting executables')
    parser.add_argument('--module-dir', action='append', default=[],
            help='A directory to search for all modules. The structure is assumed to follow the same as the ChampSim repository: branch direction predictors are under `branch/`, replacement policies under `replacement/`, etc.')
    parser.add_argument('--branch-dir', action='append', default=[],
            help='A directory to search for branch direction predictors')
    parser.add_argument('--btb-dir', action='append', default=[],
            help='A directory to search for branch target predictors')
    parser.add_argument('--prefetcher-dir', action='append', default=[],
            help='A directory to search for prefetchers')
    parser.add_argument('--replacement-dir', action='append', default=[],
            help='A directory to search for replacement policies')
    parser.add_argument('files', nargs='*',
            help='A sequence of JSON files describing the configuration. The last file specified has the highest priority.')

    args = parser.parse_args()

    bindir_name = os.path.expanduser(args.bindir or os.path.join(args.prefix, 'bin'))
    objdir_name = os.path.expanduser(os.path.join(args.prefix, '.csconfig'))

    if not args.files:
        print("No configuration specified. Building default ChampSim with no prefetching.")
    config_files = itertools.product(*(util.wrap_list(parse_file(f)) for f in reversed(args.files)), ({},))

    test_search_dirs = [os.path.join(champsim_root, 'test', 'cpp', 'modules'), champsim_root]
    parsed_test = parse.parse_config({'executable_name': '000-test-main'},
                branch_dir=[os.path.join(d,'branch') for d in test_search_dirs],
                btb_dir=[os.path.join(d,'btb') for d in test_search_dirs],
                pref_dir=[os.path.join(d,'prefetcher') for d in test_search_dirs],
                repl_dir=[os.path.join(d,'replacement') for d in test_search_dirs]
            )

    search_dirs = [*args.module_dir, champsim_root]
    parsed_configs = (
            parse.parse_config(*c,
                branch_dir=[*(os.path.join(d,'branch') for d in search_dirs), *args.branch_dir],
                btb_dir=[*(os.path.join(d,'btb') for d in search_dirs), *args.btb_dir],
                pref_dir=[*(os.path.join(d,'prefetcher') for d in search_dirs), *args.prefetcher_dir],
                repl_dir=[*(os.path.join(d,'replacement') for d in search_dirs), *args.replacement_dir]
            )
        for c in config_files)

    filewrite.write_files(itertools.chain(
        ((*c, bindir_name, (os.path.join(champsim_root, 'src'),), objdir_name) for c in parsed_configs),
        ((*parsed_test, 'test/bin', (os.path.join(champsim_root, 'src'), os.path.join(champsim_root, 'test/cpp/src')), '.csconfig/test'),)
    ))

# vim: set filetype=python:
