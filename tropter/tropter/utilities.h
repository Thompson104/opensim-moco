#ifndef TROPTER_UTILITIES_H
#define TROPTER_UTILITIES_H
// ----------------------------------------------------------------------------
// tropter: utilities.h
// ----------------------------------------------------------------------------
// Copyright (c) 2017 tropter authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain a
// copy of the License at http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------

#include <string>

namespace tropter {
/// Format a string in the style of sprintf.
std::string format(const char* format, ...);

} //namespace tropter

#endif // TROPTER_UTILITIES_H_
