#!/usr/bin/env python
#
# Copyright 2020 Soni L.
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
# Based on wasm2c, under the following license notice:
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
import sys

import find_exe
import utils

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
WASM2C_DIR = os.path.join(find_exe.REPO_ROOT_DIR, 'wasm2kotlin')
IS_WINDOWS = sys.platform == 'win32'


def bencode(data):
    # modified? bencode
    if isinstance(data, int):
        return "i{}e".format(data).encode("utf-8")
    if isinstance(data, str):
        data = data.encode("utf-8")
    if isinstance(data, bytes):
        return "{}:".format(len(data)).encode("utf-8") + data
    if isinstance(data, list):
        return b"l" + b"".join(bencode(x) for x in data) + b"e"
    if isinstance(data, dict):
        def gen(x):
            for k in sorted(x):
                yield k
                yield x[k]
        return b"d" + b"".join(bencode(x) for x in gen(data)) + b"e"
    raise TypeError()


def MangleType(t):
    return {'i32': 'i', 'i64': 'j', 'f32': 'f', 'f64': 'd',
            'externref': 'e', 'funcref': 'c'}[t]


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


replacement = {}
replacement['"'] = '\\"'
replacement['$'] = '\\$'
replacement['\n'] = '\\n'
replacement['\r'] = '\\r'
replacement['\\'] = '\\\\'
for i in range(0x20):
    if chr(i) not in replacement:
        replacement[chr(i)] = "\\u{:04x}".format(i)


def LegalizeString(s):
    pattern = '([\x00-\x19"$\\\\])'
    result = re.sub(pattern, lambda x: replacement[x.group(1)], s)
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
        self.written_commands = []

    def Write(self):
        self.out_file.write("@file:JvmName(\"SpecTestMain\")\n")
        self.out_file.write("package wabt.spec_test\n")
        self._MaybeWriteDummyModule()
        self._CacheModulePrefixes()
        self.out_file.write(self.prefix)
        self.out_file.write("\nfun run_spec_tests(moduleRegistry: wasm_rt_impl.ModuleRegistry) {\n\n")
        self.out_file.write("runString(moduleRegistry, \"")
        for command in self.commands:
            self._WriteCommand(command)
        self.out_file.write(LegalizeString(bencode(self.written_commands).decode("utf-8")))
        self.out_file.write("\")")
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

    def _WriteFileAndLine(self, command, cmd_out):
        cmd_out['file'] = self.source_filename
        cmd_out['line'] = command['line']

    def _WriteCommand(self, command):
        command_funcs = {
            'module': self._WriteModuleCommand,
            'assert_uninstantiable': self._WriteAssertUninstantiableCommand,
            'action': self._WriteActionCommand,
            'assert_return': self._WriteAssertReturnCommand,
            'assert_trap': self._WriteAssertActionCommand,
            'assert_exception': self._WriteAssertActionCommand,
            'assert_exhaustion': self._WriteAssertActionCommand,
        }

        func = command_funcs.get(command['type'])
        if func is not None:
            cmd_out = {}
            cmd_out['type'] = command['type']
            self._WriteFileAndLine(command, cmd_out)
            func(command, cmd_out)
            self.written_commands.append(cmd_out)

    def _WriteModuleCommand(self, command, cmd_out):
        self.module_idx += 1
        prefix = self.GetModulePrefix()
        cmd_out['prefix'] = prefix

    def _WriteAssertUninstantiableCommand(self, command, cmd_out):
        self.module_idx += 1
        prefix = self.GetModulePrefix()
        cmd_out['prefix'] = prefix

    def _WriteActionCommand(self, command, cmd_out):
        self._Action(command, cmd_out)

    def _WriteAssertReturnCommand(self, command, cmd_out):
        self._Action(command, cmd_out)

    def _WriteAssertActionCommand(self, command, cmd_out):
        self._Action(command, cmd_out)

    def _Action(self, command, cmd_out):
        action = command['action']
        mangled_module_name = self.GetModulePrefix(action.get('module'))
        field = MangleName(action['field'])

        cmd_out['action'] = command['action']
        cmd_out['expected'] = command['expected']
        cmd_out['mangled_module_name'] = mangled_module_name
        cmd_out['field'] = field


def Compile(kotlinc, main_jar, kotlin_filenames, *args):
    argfile = utils.ChangeExt(main_jar, '.args')
    with open(argfile, 'w', encoding='utf-8') as out_arg_file:
        out_arg_file.write(' '.join(['-d', main_jar, *args, *kotlin_filenames]))
    kotlinc.RunWithArgs("-Werror", "@{}".format(argfile))
    return main_jar


def main(args):
    default_compiler = 'kotlinc'
    default_runner = 'kotlin'
    if IS_WINDOWS:
        default_compiler = 'kotlinc.bat'
        default_runner = 'kotlin.bat'
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
                        help='the path to the Kotlin compiler', default=default_compiler)
    parser.add_argument('--kotlin', metavar='PATH',
                        help='the path to the Kotlin runner', default=default_runner)
    parser.add_argument('--ktflags', metavar='FLAGS',
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
    parser.add_argument('--enable-exceptions', action='store_true')
    parser.add_argument('--enable-multi-memory', action='store_true')
    parser.add_argument('--disable-bulk-memory', action='store_true')
    parser.add_argument('--disable-reference-types', action='store_true')
    options = parser.parse_args(args)

    with utils.TempDirectory(options.out_dir, 'run-spec-wasm2kotlin-') as out_dir:
        # Parse JSON file and generate main .kt file with calls to test functions.
        wast2json = utils.Executable(
            find_exe.GetWast2JsonExecutable(options.bindir),
            error_cmdline=options.error_cmdline)
        wast2json.verbose = options.print_cmd
        wast2json.AppendOptionalArgs({
            '-v': options.verbose,
            '--enable-exceptions': options.enable_exceptions,
            '--enable-multi-memory': options.enable_multi_memory,
            '--disable-bulk-memory': options.disable_bulk_memory,
            '--disable-reference-types': options.disable_reference_types})

        json_file_path = utils.ChangeDir(
            utils.ChangeExt(options.file, '.json'), out_dir)
        wast2json.RunWithArgs(options.file, '-o', json_file_path)

        wasm2kotlin = utils.Executable(
            find_exe.GetWasm2KotlinExecutable(options.bindir),
            error_cmdline=options.error_cmdline)
        wasm2kotlin.verbose = options.print_cmd
        wasm2kotlin.AppendOptionalArgs({
            '--enable-exceptions': options.enable_exceptions,
            '--enable-multi-memory': options.enable_multi_memory})

        kotlinc = utils.Executable(options.kotlinc, *options.ktflags,
                                   forward_stderr=True, forward_stdout=True)
        kotlinc.verbose = options.print_cmd

        kotlin = utils.Executable(options.kotlin)

        with open(json_file_path, encoding='utf-8') as json_file:
            spec_json = json.load(json_file)

        prefix = ''
        if options.prefix:
            with open(options.prefix, encoding='utf-8') as prefix_file:
                prefix = prefix_file.read() + '\n'

        output = io.StringIO()
        cwriter = CWriter(spec_json, prefix, output, out_dir)
        cwriter.Write()

        main_filename = utils.ChangeExt(json_file_path, '_main.kt')
        with open(main_filename, 'w', encoding='utf-8') as out_main_file:
            out_main_file.write(output.getvalue())

        kotlin_filenames = []

        # Compile wasm-rt-impl.
        kotlin_filenames.append(os.path.join(options.wasmrt_dir, 'wasm_rt_impl.kt'))

        for i, wasm_filename in enumerate(cwriter.GetModuleFilenames()):
            wasm_filename = os.path.join(out_dir, wasm_filename)
            kotlin_filename = utils.ChangeExt(wasm_filename, '.kt')
            prefix = cwriter.GetModulePrefix(i)
            wasm2kotlin.RunWithArgs(wasm_filename, '-p', 'wabt.spec_test', '-c', prefix, '-o', kotlin_filename)
            if options.compile:
                kotlin_filenames.append(kotlin_filename)

        if options.compile:
            main_kt = main_filename
            main_jar = Compile(kotlinc, utils.ChangeExt(main_kt, ".jar"), kotlin_filenames + [main_kt])

            if options.run:
                kotlin.RunWithArgs("-J-ea", "-classpath", main_jar, "wabt.spec_test.SpecTestMain")

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
