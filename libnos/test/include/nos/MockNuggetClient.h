/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NOS_MOCK_NUGGET_CLIENT_H
#define NOS_MOCK_NUGGET_CLIENT_H

#include <cstdint>
#include <vector>

#include <gmock/gmock.h>

#include <nos/NuggetClient.h>

namespace nos {

struct MockNuggetClient : public NuggetClient {
    MOCK_METHOD0(open, void());
    MOCK_METHOD0(close, void());
    MOCK_METHOD0(isOpen, bool());
    MOCK_METHOD4(callApp, uint32_t(uint32_t, uint16_t,
                                   const std::vector<uint8_t>&,
                                   std::vector<uint8_t>&));
};

} // namespace nos

#endif // NOS_MOCK_NUGGET_CLIENT_H
