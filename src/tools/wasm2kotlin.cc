/*
 * Copyright 2020 Soni L.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Based on wasm2c, under the following license notice:
 *
 * Copyright 2017 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "src/apply-names.h"
#include "src/binary-reader-ir.h"
#include "src/binary-reader.h"
#include "src/error-formatter.h"
#include "src/feature.h"
#include "src/generate-names.h"
#include "src/ir.h"
#include "src/option-parser.h"
#include "src/stream.h"
#include "src/validator.h"
#include "src/wast-lexer.h"

#include "src/kotlin-writer.h"

using namespace wabt;

static int s_verbose;
static std::string s_infile;
static std::string s_outfile;
static std::string s_package;
static std::string s_class;
static Features s_features;
static WriteKotlinOptions s_write_kotlin_options;
static bool s_read_debug_names = true;
static std::unique_ptr<FileStream> s_log_stream;

static const char s_description[] =
    R"(  Read a file in the WebAssembly binary format, and convert it to
  a Kotlin source file.

examples:
  # parse binary file test.wasm and write test.kt
  $ wasm2kotlin test.wasm -o test.kt

  # parse test.wasm, write test.kt, but ignore the debug names, if any
  $ wasm2kotlin test.wasm --no-debug-names -o test.kt
)";

static const std::string supported_features[] = {"multi-memory"};

static bool IsFeatureSupported(const std::string& feature) {
  return std::find(std::begin(supported_features), std::end(supported_features),
                   feature) != std::end(supported_features);
};

static void ParseOptions(int argc, char** argv) {
  OptionParser parser("wasm2kotlin", s_description);

  parser.AddOption('v', "verbose", "Use multiple times for more info", []() {
    s_verbose++;
    s_log_stream = FileStream::CreateStderr();
  });
  parser.AddOption(
      'o', "output", "FILENAME",
      "Output file for the generated Kotlin source file, by default use stdout",
      [](const char* argument) {
        s_outfile = argument;
        ConvertBackslashToSlash(&s_outfile);
      });
  parser.AddOption(
      'p', "package", "PACKAGE",
      "Package for the generated Kotlin source file, by default none",
      [](const char* argument) { s_package = argument; });
  parser.AddOption(
      'c', "class", "CLASS",
      "Class for the generated module, by default derived from filename.",
      [](const char* argument) { s_class = argument; });
  s_features.AddOptions(&parser);
  parser.AddOption("no-debug-names", "Ignore debug names in the binary file",
                   []() { s_read_debug_names = false; });
  parser.AddArgument("filename", OptionParser::ArgumentCount::One,
                     [](const char* argument) {
                       s_infile = argument;
                       ConvertBackslashToSlash(&s_infile);
                     });
  parser.Parse(argc, argv);

  bool any_non_supported_feature = false;
#define WABT_FEATURE(variable, flag, default_, help)   \
  any_non_supported_feature |=                         \
      (s_features.variable##_enabled() != default_) && \
      !IsFeatureSupported(flag);
#include "src/feature.def"
#undef WABT_FEATURE

  if (any_non_supported_feature) {
    fprintf(stderr,
            "wasm2kotlin currently only supports a fixed set of features.\n");
    exit(1);
  }
}

static std::string_view get_classname(std::string_view s) {
  size_t pos = s.find_last_of('/');
  std::string_view thing = s;
  if (pos < s.length()) {
    thing = s.substr(pos);
  }
  while (thing[0] == '/') {
    thing = thing.substr(1);
  }
  std::string_view ext = thing.substr(thing.find_last_of('.'));
  std::string_view result = thing;

  if (ext == ".kt")
    result.remove_suffix(ext.length());
  return result;
}

int ProgramMain(int argc, char** argv) {
  Result result;

  InitStdio();
  ParseOptions(argc, argv);

  std::vector<uint8_t> file_data;
  result = ReadFile(s_infile.c_str(), &file_data);
  if (Succeeded(result)) {
    Errors errors;
    Module module;
    const bool kStopOnFirstError = true;
    const bool kFailOnCustomSectionError = true;
    ReadBinaryOptions options(s_features, s_log_stream.get(),
                              s_read_debug_names, kStopOnFirstError,
                              kFailOnCustomSectionError);
    result = ReadBinaryIr(s_infile.c_str(), file_data.data(), file_data.size(),
                          options, &errors, &module);
    if (Succeeded(result)) {
      if (Succeeded(result)) {
        ValidateOptions options(s_features);
        result = ValidateModule(&module, &errors, options);
        result |= GenerateNames(&module);
      }

      if (Succeeded(result)) {
        /* TODO(binji): This shouldn't fail; if a name can't be applied
         * (because the index is invalid, say) it should just be skipped. */
        Result dummy_result = ApplyNames(&module);
        WABT_USE(dummy_result);
      }

      if (Succeeded(result)) {
        if (!s_outfile.empty()) {
          FileStream kotlin_stream(s_outfile.c_str());
          std::string class_name = std::move(s_class);
          if (class_name.empty()) {
            class_name = get_classname(s_outfile);
          }
          result =
              WriteKotlin(&kotlin_stream, class_name.c_str(), s_package.c_str(),
                          &module, s_write_kotlin_options);
        } else {
          FileStream stream(stdout);
          std::string class_name = std::move(s_class);
          if (class_name.empty()) {
            class_name = "Wasm";
          }
          result = WriteKotlin(&stream, class_name.c_str(), s_package.c_str(),
                               &module, s_write_kotlin_options);
        }
      }
    }
    FormatErrorsToFile(errors, Location::Type::Binary);
  }
  return result != Result::Ok;
}

int main(int argc, char** argv) {
  WABT_TRY
  return ProgramMain(argc, argv);
  WABT_CATCH_BAD_ALLOC_AND_EXIT
}
