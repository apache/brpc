// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <stdio.h>
int main() {
#if defined(__clang__)
    const int major_v = __GNUC__;
    int minor_v = __GNUC_MINOR__;
    if (major_v == 4 && minor_v <= 8) {
        // Make version of clang >= 4.8 so that it's not rejected by config_brpc.sh
        minor_v = 8;
    }
    printf("%d\n", (major_v * 10000 + minor_v * 100));
#elif defined(__GNUC__)
    printf("%d\n", (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__));
#else
    printf("0\n");
#endif
    return 0;
}
