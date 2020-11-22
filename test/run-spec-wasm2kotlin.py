#!/usr/bin/env python
#
# Copyright 2017 WebAssembly Community Group participants
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import argparse
import io
import json
import os
import re
import struct
import sys

import find_exe
import utils
from utils import Error

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
WASM2C_DIR = os.path.join(find_exe.REPO_ROOT_DIR, 'wasm2kotlin')


def ReinterpretI32(i32_bits):
    return struct.unpack("<i", struct.pack("<I", i32_bits))[0]


def I32ToKotlin(i32_bits):
    if i32_bits == 0x80000000:
        return "(-0x7fffffff - 1)"
    return "%s" % ReinterpretI32(i32_bits)


def ReinterpretI64(i64_bits):
    return struct.unpack("<q", struct.pack("<Q", i64_bits))[0]


def I64ToKotlin(i64_bits):
    if i64_bits == 0x8000000000000000:
        return "(-0x7fffffffffffffffL - 1L)"
    return "%sL" % ReinterpretI64(i64_bits)


def ReinterpretF32(f32_bits):
    return struct.unpack('<f', struct.pack('<I', f32_bits))[0]


def F32ToKotlin(f32_bits):
    F32_SIGN_BIT = 0x80000000
    F32_INF = 0x7f800000
    F32_SIG_MASK = 0x7fffff

    if (f32_bits & F32_INF) == F32_INF:
        sign = '-' if (f32_bits & F32_SIGN_BIT) == F32_SIGN_BIT else ''
        # NaN or infinity
        if f32_bits & F32_SIG_MASK:
            # NaN
            return '%smake_nan_f32(0x%06x)' % (sign, f32_bits & F32_SIG_MASK)
        else:
            return '%sFloat.POSITIVE_INFINITY' % sign
    elif f32_bits == F32_SIGN_BIT:
        return '-0.0f'
    else:
        s = '%.9g' % ReinterpretF32(f32_bits)
        if ('.' not in s) and ('e' not in s):
            s += '.0'
        return s + 'f'


def ReinterpretF64(f64_bits):
    return struct.unpack('<d', struct.pack('<Q', f64_bits))[0]


def F64ToKotlin(f64_bits):
    F64_SIGN_BIT = 0x8000000000000000
    F64_INF = 0x7ff0000000000000
    F64_SIG_MASK = 0xfffffffffffff

    if (f64_bits & F64_INF) == F64_INF:
        sign = '-' if (f64_bits & F64_SIGN_BIT) == F64_SIGN_BIT else ''
        # NaN or infinity
        if f64_bits & F64_SIG_MASK:
            # NaN
            return '%smake_nan_f64(0x%06x)' % (sign, f64_bits & F64_SIG_MASK)
        else:
            return '%sDouble.POSITIVE_INFINITY' % sign
    elif f64_bits == F64_SIGN_BIT:
        return '-0.0'
    else:
        s = '%.17g' % ReinterpretF64(f64_bits)
        if ('.' not in s) and ('e' not in s):
            s += '.0'
        return s


def MangleType(t):
    return {'i32': 'i', 'i64': 'j', 'f32': 'f', 'f64': 'd'}[t]


def MangleTypes(types):
    if not types:
        return 'v'
    return ''.join(MangleType(t) for t in types)


def MangleName(s):
    def Mangle(match):
        s = match.group(0)
        return b'Z%02X' % s[0]

    # NOTE(binji): Z is not allowed.
    pattern = b'([^_a-zA-Y0-9])'
    result = 'Z_' + re.sub(pattern, Mangle, s.encode('utf-8')).decode('utf-8')
    return result


def LegalizeName(s):
    pattern = '([^_a-zA-Y0-9])'
    result = 'w2k_' + re.sub(pattern, '_', s)
    return result



def IsModuleCommand(command):
    return (command['type'] == 'module' or
            command['type'] == 'assert_uninstantiable')


class CWriter(object):

    def __init__(self, spec_json, prefix, out_file, out_dir):
        self.source_filename = os.path.basename(spec_json['source_filename'])
        self.commands = spec_json['commands']
        self.out_file = out_file
        self.out_dir = out_dir
        self.prefix = prefix
        self.module_idx = 0
        self.module_name_to_idx = {}
        self.module_prefix_map = {}

    def Write(self):
        self.out_file.write("@file:JvmName(\"SpecTestMain\")\n")
        self.out_file.write("package wabt.spec_test\n")
        self._MaybeWriteDummyModule()
        self._CacheModulePrefixes()
        self.out_file.write(self.prefix)
        self.out_file.write("\nfun run_spec_tests(spectest: Z_spectest) {\n\n")
        for command in self.commands:
            self._WriteCommand(command)
        self.out_file.write("\n}\n")

    def GetModuleFilenames(self):
        return [c['filename'] for c in self.commands if IsModuleCommand(c)]

    def GetModulePrefix(self, idx_or_name=None):
        if idx_or_name is not None:
            return self.module_prefix_map[idx_or_name]
        return self.module_prefix_map[self.module_idx - 1]

    def _CacheModulePrefixes(self):
        idx = 0
        for command in self.commands:
            if IsModuleCommand(command):
                name = os.path.basename(command['filename'])
                name = os.path.splitext(name)[0]
                #name = re.sub(r'[^a-zA-Z0-9_]', '_', name)
                name = MangleName(name)

                self.module_prefix_map[idx] = name

                if 'name' in command:
                    self.module_name_to_idx[command['name']] = idx
                    self.module_prefix_map[command['name']] = name

                idx += 1
            elif command['type'] == 'register':
                name = MangleName(command['as'])
                if 'name' in command:
                    self.module_prefix_map[command['name']] = name
                    name_idx = self.module_name_to_idx[command['name']]
                else:
                    name_idx = idx - 1

                self.module_prefix_map[name_idx] = name

    def _MaybeWriteDummyModule(self):
        if len(self.GetModuleFilenames()) == 0:
            # This test doesn't have any valid modules, so just use a dummy instead.
            filename = utils.ChangeExt(self.source_filename, '-dummy.wasm')
            with open(os.path.join(self.out_dir, filename), 'wb') as wasm_file:
                wasm_file.write(b'\x00\x61\x73\x6d\x01\x00\x00\x00')

            dummy_command = {'type': 'module', 'line': 0, 'filename': filename}
            self.commands.insert(0, dummy_command)

    def _WriteFileAndLine(self, command):
        self.out_file.write('// %s:%d\n' % (self.source_filename, command['line']))

    def _WriteCommand(self, command):
        command_funcs = {
            'module': self._WriteModuleCommand,
            'assert_uninstantiable': self._WriteAssertUninstantiableCommand,
            'action': self._WriteActionCommand,
            'assert_return': self._WriteAssertReturnCommand,
            'assert_trap': self._WriteAssertActionCommand,
            'assert_exhaustion': self._WriteAssertActionCommand,
        }

        func = command_funcs.get(command['type'])
        if func is not None:
            self._WriteFileAndLine(command)
            func(command)
            self.out_file.write('\n')

    def _WriteModuleCommand(self, command):
        self.module_idx += 1
        self.out_file.write('var %s = ' % self.GetModulePrefix())
        self.out_file.write('run_test(::%s, spectest);\n' % self.GetModulePrefix())

    def _WriteAssertUninstantiableCommand(self, command):
        self.module_idx += 1
        self.out_file.write('ASSERT_TRAP { run_test(::%s, spectest) };\n' % self.GetModulePrefix())

    def _WriteActionCommand(self, command):
        self.out_file.write('%s;\n' % self._Action(command))

    def _WriteAssertReturnCommand(self, command):
        expected = command['expected']
        if len(expected) == 1:
            type_ = expected[0]['type']
            value = expected[0]['value']
            if value == 'nan:canonical':
                assert_map = {
                    'f32': 'ASSERT_RETURN_CANONICAL_NAN_F32',
                    'f64': 'ASSERT_RETURN_CANONICAL_NAN_F64',
                }
                assert_macro = assert_map[(type_)]
                self.out_file.write('%s({ %s });\n' % (assert_macro, self._Action(command)))
            elif value == 'nan:arithmetic':
                assert_map = {
                    'f32': 'ASSERT_RETURN_ARITHMETIC_NAN_F32',
                    'f64': 'ASSERT_RETURN_ARITHMETIC_NAN_F64',
                }
                assert_macro = assert_map[(type_)]
                self.out_file.write('%s({ %s });\n' % (assert_macro, self._Action(command)))
            else:
                assert_map = {
                    'i32': 'ASSERT_RETURN_I32',
                    'f32': 'ASSERT_RETURN_F32',
                    'i64': 'ASSERT_RETURN_I64',
                    'f64': 'ASSERT_RETURN_F64',
                }

                assert_macro = assert_map[type_]
                self.out_file.write('%s({ %s }, %s);\n' %
                                    (assert_macro,
                                     self._Action(command),
                                     self._ConstantList(expected)))
        elif len(expected) == 0:
            self._WriteAssertActionCommand(command)
        else:
            raise Error('Unexpected result with multiple values: %s' % expected)

    def _WriteAssertActionCommand(self, command):
        assert_map = {
            'assert_exhaustion': 'ASSERT_EXHAUSTION',
            'assert_return': 'ASSERT_RETURN',
            'assert_trap': 'ASSERT_TRAP',
        }

        assert_macro = assert_map[command['type']]
        self.out_file.write('%s { %s };\n' % (assert_macro, self._Action(command)))

    def _Constant(self, const):
        type_ = const['type']
        value = const['value']
        if type_ in ('f32', 'f64') and value in ('nan:canonical', 'nan:arithmetic'):
            assert False
        if type_ == 'i32':
            return I32ToKotlin(int(value))
        elif type_ == 'i64':
            return I64ToKotlin(int(value))
        elif type_ == 'f32':
            return F32ToKotlin(int(value))
        elif type_ == 'f64':
            return F64ToKotlin(int(value))
        else:
            assert False

    def _ConstantList(self, consts):
        return ', '.join(self._Constant(const) for const in consts)

    def _ActionSig(self, action, expected):
        type_ = action['type']
        result_types = [result['type'] for result in expected]
        arg_types = [arg['type'] for arg in action.get('args', [])]
        if type_ == 'invoke':
            return MangleTypes(result_types) + MangleTypes(arg_types)
        elif type_ == 'get':
            return MangleType(result_types[0])
        else:
            raise Error('Unexpected action type: %s' % type_)

    def _Action(self, command):
        action = command['action']
        expected = command['expected']
        type_ = action['type']
        mangled_module_name = self.GetModulePrefix(action.get('module'))
        field = (mangled_module_name + '.' + MangleName(action['field']) +
                 MangleName(self._ActionSig(action, expected)))
        if type_ == 'invoke':
            return '%s(%s)' % (field, self._ConstantList(action.get('args', [])))
        elif type_ == 'get':
            return '%s' % field
        else:
            raise Error('Unexpected action type: %s' % type_)


def Compile(kotlinc, out_dir, main_jar, kotlin_filenames, *args):
    out_dir = os.path.abspath(out_dir)
    main_jar = utils.ChangeDir(main_jar, out_dir)
    kotlinc.RunWithArgs('-d', main_jar, *args, *kotlin_filenames, cwd=out_dir)
    return main_jar


def main(args):
    parser = argparse.ArgumentParser()
    parser.add_argument('-o', '--out-dir', metavar='PATH',
                        help='output directory for files.')
    parser.add_argument('-P', '--prefix', metavar='PATH', help='prefix file.',
                        default=os.path.join(SCRIPT_DIR, 'SpecWasm2KotlinPrefix.kt'))
    parser.add_argument('--bindir', metavar='PATH',
                        default=find_exe.GetDefaultPath(),
                        help='directory to search for all executables.')
    parser.add_argument('--wasmrt-dir', metavar='PATH',
                        help='directory with wasm-rt files', default=WASM2C_DIR)
    parser.add_argument('--kotlinc', metavar='PATH',
                        help='the path to the Kotlin compiler', default='kotlinc')
    parser.add_argument('--kotlin', metavar='PATH',
                        help='the path to the Kotlin runner', default='kotlin')
    parser.add_argument('--cflags', metavar='FLAGS',
                        help='additional flags for Kotlin compiler.',
                        action='append', default=[])
    parser.add_argument('--compile', help='compile the Kotlin code (default)',
                        dest='compile', action='store_true')
    parser.add_argument('--no-compile', help='don\'t compile the Kotlin code',
                        dest='compile', action='store_false')
    parser.set_defaults(compile=True)
    parser.add_argument('--no-run', help='don\'t run the compiled executable',
                        dest='run', action='store_false')
    parser.add_argument('-v', '--verbose', help='print more diagnotic messages.',
                        action='store_true')
    parser.add_argument('--no-error-cmdline',
                        help='don\'t display the subprocess\'s commandline when '
                        'an error occurs', dest='error_cmdline',
                        action='store_false')
    parser.add_argument('-p', '--print-cmd',
                        help='print the commands that are run.',
                        action='store_true')
    parser.add_argument('file', help='wast file.')
    options = parser.parse_args(args)

    with utils.TempDirectory(options.out_dir, 'run-spec-wasm2kotlin-') as out_dir:
        # Parse JSON file and generate main .kt file with calls to test functions.
        wast2json = utils.Executable(
            find_exe.GetWast2JsonExecutable(options.bindir),
            error_cmdline=options.error_cmdline)
        wast2json.AppendOptionalArgs({'-v': options.verbose})

        json_file_path = utils.ChangeDir(
            utils.ChangeExt(options.file, '.json'), out_dir)
        wast2json.RunWithArgs(options.file, '-o', json_file_path)

        wasm2kotlin = utils.Executable(
            find_exe.GetWasm2KotlinExecutable(options.bindir),
            error_cmdline=options.error_cmdline)

        kotlinc = utils.Executable(options.kotlinc, *options.cflags)
        kotlin = utils.Executable(options.kotlin)

        with open(json_file_path) as json_file:
            spec_json = json.load(json_file)

        prefix = ''
        if options.prefix:
            with open(options.prefix) as prefix_file:
                prefix = prefix_file.read() + '\n'

        output = io.StringIO()
        cwriter = CWriter(spec_json, prefix, output, out_dir)
        cwriter.Write()

        main_filename = utils.ChangeExt(json_file_path, '_main.kt')
        with open(main_filename, 'w') as out_main_file:
            out_main_file.write(output.getvalue())

        kotlin_filenames = []
        #includes = '-I%s' % options.wasmrt_dir

        # Compile wasm-rt-impl.
        wasm_rt_impl_kotlin = os.path.join(options.wasmrt_dir, 'wasm_rt_impl.kt')
        wasm_rt_impl_kotlin_out = utils.ChangeDir('wasm_rt_impl.kt', out_dir)
        with open(wasm_rt_impl_kotlin) as rt_file:
            with open(wasm_rt_impl_kotlin_out, "w") as out_file:
                out_file.write(rt_file.read())

        kotlin_filenames.append("wasm_rt_impl.kt")

        for i, wasm_filename in enumerate(cwriter.GetModuleFilenames()):
            kotlin_filename = utils.ChangeExt(wasm_filename, '.kt')
            wasm2kotlin.RunWithArgs(wasm_filename, '-p', 'wabt.spec_test', '-o', kotlin_filename, cwd=out_dir)
            if options.compile:
                kotlin_filenames.append(kotlin_filename)
                #defines = '-DWASM_RT_MODULE_PREFIX=%s' % 
                #o_filenames.append(Compile(kotlinc, c_filename, out_dir, includes, defines))

        if options.compile:
            main_kt = os.path.basename(main_filename)
            main_jar = Compile(kotlinc, out_dir, utils.ChangeExt(main_kt, ".jar"), kotlin_filenames + [main_kt])

        if options.compile and options.run:
            kotlin.RunWithArgs("-J-ea", "-classpath", main_jar, "wabt.spec_test.SpecTestMain", cwd=out_dir)

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
