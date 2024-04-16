/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef SRC_TRACE_REDACTION_BUILD_TIMELINE_H_
#define SRC_TRACE_REDACTION_BUILD_TIMELINE_H_

#include "src/trace_redaction/trace_redaction_framework.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {

// Creates events from process_tree, task_newtask, and sched_process_free
// packets and stores them in a timeline.
class BuildTimeline : public CollectPrimitive {
 public:
  base::Status Collect(const protos::pbzero::TracePacket::Decoder& packet,
                       Context* context) const override;
};

}  // namespace perfetto::trace_redaction

#endif  // SRC_TRACE_REDACTION_BUILD_TIMELINE_H_
