#
#    Copyright (c) 2023 Project CHIP Authors
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

"""Hyphenated entry point for TC-IDM-4.2 python certification test.

This mirrors the style used by TC-IDM-4.1 so automated tooling that
expects the hyphenated filename can locate the implementation while
sharing the core logic with existing infrastructure.
"""

from tc_idm_4_2_base import TC_IDM_4_2
from matter.testing.matter_testing import default_matter_test_main

__all__ = ["TC_IDM_4_2"]


if __name__ == "__main__":
    default_matter_test_main()
