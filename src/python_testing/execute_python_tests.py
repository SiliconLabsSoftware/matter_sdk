#!/usr/bin/env -S python3 -B
#
#    Copyright (c) 2024 Project CHIP Authors
#    All rights reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

import glob
import logging
import os
import subprocess
import sys

import click
import coloredlogs
import yaml

# Function to load --app argument environment variables from a file


def load_env_from_yaml(file_path):
    """
    Load environment variables from the specified YAML file.

    The YAML file contains key-value pairs that define --app environment variables
    required for the test scripts to run. These variables configurations needed during the test execution.

    This function reads the YAML file and sets the environment variables
    in the current process's environment using os.environ.

    Args:
        file_path (str): The path to the YAML file containing the environment variables.
    """
    for key, value in yaml.full_load(open(file_path, "r")).items():
        os.environ[key] = value


@click.command()
@click.option(
    "--search-directory",
    type=str,
    default="src/python_testing",
    help="Directory to search for Python scripts.",
)
@click.option(
    "--env-file",
    type=str,
    default="/tmp/test_env.yaml",
    help="Path to the environment variables file.",
)
@click.option(
    "--keep-going",
    is_flag=True,
    help="Run ALL the test, report a final status of what passed/failed.",
)
def main(search_directory, env_file, keep_going):
    # Determine the root directory of the CHIP project
    chip_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

    # Load environment variables from the specified file
    load_env_from_yaml(env_file)

    # Define the base command to run tests
    base_command = os.path.join(chip_root, "scripts/tests/run_python_test.py")

<<<<<<< HEAD
    # Define the files and patterns to exclude
    excluded_patterns = {
        "MinimalRepresentation.py",  # Code/Test not being used or not shared code for any other tests
        "TC_CNET_4_4.py",  # It has no CI execution block, is not executed in CI
        "TC_CCTRL_2_1.py",  # They rely on example applications that inter-communicate and there is no example app that works right now
        "TC_CCTRL_2_2.py",  # They rely on example applications that inter-communicate and there is no example app that works right now
        "TC_CCTRL_2_3.py",  # They rely on example applications that inter-communicate and there is no example app that works right now
        "TC_DGGEN_3_2.py",  # src/python_testing/test_testing/test_TC_DGGEN_3_2.py is the Unit test of this test
        "TC_EEVSE_Utils.py",  # Shared code for TC_EEVSE, not a standalone test
        "TC_EWATERHTRBase.py",  # Shared code for TC_EWATERHTR, not a standalone test
        "TC_EnergyReporting_Utils.py",  # Shared code for TC_EEM and TC_EPM, not a standalone test
        "TC_OpstateCommon.py",  # Shared code for TC_OPSTATE, not a standalone test
        "TC_pics_checker.py",  # Currently isn't enabled because we don't have any examples with conformant PICS
        "TC_TMP_2_1.py",  # src/python_testing/test_testing/test_TC_TMP_2_1.py is the Unit test of this test
        "TC_OCC_3_1.py",  # There are CI issues for the test cases that implements manually controlling sensor device for the occupancy state ON/OFF change
        "TC_OCC_3_2.py",  # There are CI issues for the test cases that implements manually controlling sensor device for the occupancy state ON/OFF change
        "TC_BRBINFO_4_1.py",  # This test requires a TH_ICD_SERVER application, hence not ready run with CI
        "TestCommissioningTimeSync.py",  # Code/Test not being used or not shared code for any other tests
        "TestConformanceSupport.py",  # Unit test - does not run against an app
        "TestChoiceConformanceSupport.py",  # Unit test - does not run against an app
        "TC_DEMTestBase.py",  # Shared code for TC_DEM, not a standalone test
        "TestConformanceTest.py",  # Unit test of the conformance test (TC_DeviceConformance) - does not run against an app
        "TestIdChecks.py",  # Unit test - does not run against an app
        "TestSpecParsingDeviceType.py",  # Unit test - does not run against an app
        "TestSpecParsingSelection.py",  # Unit test - does not run against an app
        "TestConformanceTest.py",  # Unit test - does not run against an app
        "TestMatterTestingSupport.py",  # Unit test - does not run against an app
        "TestSpecParsingSupport.py",  # Unit test - does not run against an app
        "TestTimeSyncTrustedTimeSource.py",  # Unit test and shared code for scripts/tests/TestTimeSyncTrustedTimeSourceRunner.py
        "drlk_2_x_common.py",  # Shared code for TC_DRLK, not a standalone test
        # Python code that runs all the python tests from src/python_testing (This code itself run via tests.yaml)
        "execute_python_tests.py",
        "hello_external_runner.py",  # Code/Test not being used or not shared code for any other tests
        "hello_test.py",  # Is a template for tests
        "test_plan_support.py",  # Shared code for TC_*, not a standalone test
        "test_plan_table_generator.py",  # Code/Test not being used or not shared code for any other tests
        "basic_composition_support.py",  # Test support/shared code script, not a standalone test
        "choice_conformance_support.py",  # Test support/shared code script, not a standalone test
        "conformance_support.py",  # Test support/shared code script, not a standalone test
        "global_attribute_ids.py",  # Test support/shared code script, not a standalone test
        "matter_asserts.py",  # Test support/shared code script, not a standalone test
        "matter_testing_support.py",  # Test support/shared code script, not a standalone test
        "pics_support.py",  # Test support/shared code script, not a standalone test
        "spec_parsing_support.py",  # Test support/shared code script, not a standalone test
        "taglist_and_topology_test_support.py"  # Test support/shared code script, not a standalone test
    }
=======
    metadata = yaml.full_load(
        open(os.path.join(chip_root, "src/python_testing/test_metadata.yaml"))
    )
    excluded_patterns = set([item["name"] for item in metadata["not_automated"]])
>>>>>>> csa/v1.4.2-branch

    # Get all .py files in the directory
    all_python_files = glob.glob(os.path.join(search_directory, "*.py"))

    # Filter out the files matching the excluded patterns
    python_files = [
        file
        for file in all_python_files
        if os.path.basename(file) not in excluded_patterns
    ]

    # Run each script with the base command
    failed_scripts = []
    for script in python_files:
        try:
            full_command = (
                f"{base_command} --load-from-env {env_file} --script {script}"
            )
            print(
                f"Running command: {full_command}", flush=True
            )  # Flush print to stdout immediately
            subprocess.run(full_command, shell=True, check=True)
        except Exception:
            if keep_going:
                failed_scripts.append(script)
            else:
                raise

    if failed_scripts:
        logging.error("FAILURES detected:")
        for s in failed_scripts:
            logging.error("   - %s", s)
        sys.exit(1)


if __name__ == "__main__":
    coloredlogs.install(level='INFO')
    main(auto_envvar_prefix="CHIP")
