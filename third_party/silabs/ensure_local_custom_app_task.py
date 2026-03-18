#!/usr/bin/env python3
#
# Copyright (c) 2020 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use it except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Copies CustomAppTask.cpp and CustomAppTask.h from the template

import os
import shutil
import sys


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(
            "usage: ensure_local_custom_app_task.py <platform_silabs_dir> <root_build_dir>\n"
        )
        sys.exit(1)

    base = os.getcwd()
    platform_dir = os.path.normpath(os.path.abspath(os.path.join(base, sys.argv[1])))
    root_build_dir = os.path.normpath(os.path.abspath(os.path.join(base, sys.argv[2])))

    template_cpp = os.path.join(platform_dir, "CustomAppTask.cpp")
    template_h = os.path.join(platform_dir, "CustomAppTask.h")
    build_config_dir = os.path.join(root_build_dir, "config")
    dest_cpp = os.path.join(build_config_dir, "CustomAppTask.cpp")
    dest_h = os.path.join(build_config_dir, "CustomAppTask.h")

    for path in (template_cpp, template_h):
        if not os.path.isfile(path):
            sys.stderr.write("template not found: %s\n" % path)
            sys.exit(1)

    # Only copy when build dir does not have a copy
    if os.path.isfile(dest_cpp):
        return

    os.makedirs(build_config_dir, exist_ok=True)
    shutil.copy2(template_cpp, dest_cpp)
    shutil.copy2(template_h, dest_h)


if __name__ == "__main__":
    main()
