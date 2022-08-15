import itertools, operator
import os

from . import util

def generate_dirs(path):
    head, tail = os.path.split(path)
    if head != '':
        yield from generate_dirs(head)
    yield os.path.join(head, tail)

def module_opts(source_dir, build_id, name, opts, exe):
    dest_dir = os.path.join(build_id, name)

    retval = '###\n'
    retval += '# Build ID: ' + build_id + '\n'
    retval += '# Module: ' + name + '\n'
    retval += '###\n\n'

    retval += 'required_dirs += ' + ' '.join(generate_dirs(os.path.join('$(objdir)',dest_dir))) + '\n'
    varname = build_id + '_' + name + '_objs'
    for base,dirs,files in os.walk(source_dir):
        for f,ext in map(os.path.splitext, files):
            if ext in ('.cc',):
                retval += varname + ' += $(objdir)/' + os.path.join(dest_dir,f) + '.o\n'

    retval += '$(' + varname + '): | $(objdir)/' + dest_dir + '\n'
    retval += '$(objdir)/{}/%.o: CPPFLAGS += -I{}\n'.format(dest_dir, source_dir)

    for opt in opts:
        retval += '$(objdir)/{}/%.o: CXXFLAGS += {}\n'.format(dest_dir, opt)

    retval += '$(objdir)/{}/%.o: {}/%.cc\n\t$(COMPILE.cc) $(OUTPUT_OPTION) $<\n'.format(dest_dir, source_dir)

    retval += exe + ': $(' + varname + ')\n'

    return retval

def get_makefile_string(build_id, module_info, **config_file):

    retval = '######\n'
    retval += '# Build ID: ' + build_id + '\n'
    retval += '######\n\n'

    name = config_file.get('name')
    executable = '$(bindir)/' + config_file.get('executable_name', 'champsim' + ('' if name is None else '_'+name))

    retval += 'executable_name += ' + executable + '\n'

    # Override the compiler
    for k in ('CC', 'CXX'):
        if k in config_file:
            retval += '{}: {} = {}\n'.format(executable, k, config_file[k])

    # Add compiler flags
    for k in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'LDLIBS'):
        if k in config_file:
            retval += '{}: {} += {}\n'.format(executable, k, config_file[k])

    retval += executable + ': CPPFLAGS += -I$(objdir)/' + build_id + '/inc\n'
    retval += 'required_dirs += ' + ' '.join(generate_dirs(os.path.split(executable)[0])) + '\n'
    retval += '\n'

    retval += '\n'.join(module_opts(v['fname'], build_id, k, v['opts'], executable) for k,v in module_info.items())

    return retval

